"""Decode real AAC fixtures and verify silence-based, lossless ICY splits.
Usage: python testing/aac_silence.py executable driver [--faac encoder]
FAAC is test-only. Without --faac, use fixtures generated previously in build.
"""
from pathlib import Path
import argparse
import array
import bisect
import hashlib
import http.server
import math
import os
import subprocess
import sys
import tempfile
import threading
import time
import wave

repo = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('driver', type=Path)
parser.add_argument('--faac', type=Path)
args = parser.parse_args()
exe, driver = args.executable.resolve(), args.driver.resolve()
env = dict(os.environ)
if os.name == 'nt':
    env['PATH'] = str(repo / 'build/toolchain/mingw64/bin') + os.pathsep + env.get('PATH', '')
fixtures = repo / 'build/aac-silence-fixtures'
fixtures.mkdir(parents=True, exist_ok=True)
specifications = [('mono', 44100, 1, False), ('stereo', 48000, 2, False),
                  ('antiphase', 44100, 2, True)]
if args.faac:
    for label, rate, channels, antiphase in specifications:
        with tempfile.TemporaryDirectory(dir=fixtures) as tmp:
            source = Path(tmp) / 'tone.wav'
            pcm = array.array('h')
            for sample in range(rate * 24):
                value = 0 if rate * 8 <= sample < rate * 10 else int(14000 * math.sin(2 * math.pi * 440 * sample / rate))
                pcm.append(value)
                if channels == 2:
                    pcm.append(-value if antiphase else value)
            if sys.byteorder != 'little':
                pcm.byteswap()
            with wave.open(str(source), 'wb') as output:
                output.setparams((channels, 2, rate, 0, 'NONE', 'not compressed'))
                output.writeframes(pcm.tobytes())
            destination = fixtures / (label + '.aac')
            subprocess.run([str(args.faac.resolve()), '-b', '128', '--overwrite',
                            '-o', str(destination), str(source)],
                           check=True, capture_output=True, env=env)
            (fixtures / (label + '.sha256')).write_text(hashlib.sha256(destination.read_bytes()).hexdigest())
            print('Generated:', label, flush=True)


def frames(data):
    position = 0
    result = []
    while position + 7 <= len(data):
        assert data[position] == 255 and data[position + 1] & 246 == 240, position
        length = ((data[position + 3] & 3) << 11) | (data[position + 4] << 3) | (data[position + 5] >> 5)
        assert length >= 7 and position + length <= len(data), position
        result.append((position, data[position:position + length]))
        position += length
    assert position == len(data), (position, len(data))
    return result


def analyze(path, start=4000, length=10000, silence=500, before=0, after=0):
    result = subprocess.run([str(driver), str(path), str(start), str(length),
                             str(silence), str(before), str(after)], env=env,
                            capture_output=True, timeout=10)
    assert result.returncode == 0, result.stderr
    return tuple(map(int, result.stdout.split()))


state = {}
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        data, change, bitrate = state['data'], state['change'], state['bitrate']
        self.send_response(200)
        self.send_header('Content-Type', 'audio/aac')
        self.send_header('icy-name', 'AAC silence test')
        self.send_header('icy-metaint', '4096')
        self.send_header('icy-br', str(bitrate))
        self.end_headers()
        try:
            for position in range(0, len(data) - 4096, 4096):
                title = 'Artist - Before' if position < change else 'Artist - After'
                metadata = ("StreamTitle='" + title + "';").encode()
                metadata += bytes(-len(metadata) % 16)
                self.wfile.write(data[position:position + 4096] + bytes([len(metadata) // 16]) + metadata)
                self.wfile.flush()
                time.sleep(.005)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    for label, rate, channels, antiphase in specifications:
        path = fixtures / (label + '.aac')
        assert path.is_file(), 'Generate fixtures with --faac first: ' + str(path)
        data = path.read_bytes()
        assert hashlib.sha256(data).hexdigest() == path.with_suffix('.sha256').read_text()
        packets = frames(data)
        offsets = [offset for offset, _ in packets]
        def at(offset):
            assert offset in offsets, offset
            return bisect.bisect_left(offsets, offset) * 1024 / rate
        rc, end, start = analyze(path)
        assert rc == 0 and end == start and 8 <= at(end) <= 10, (label, rc, end, start, at(end))
        rc, padded_end, padded_start = analyze(path, before=250, after=250)
        assert rc == 0 and .20 <= at(padded_end) - at(end) <= .30
        assert .20 <= at(start) - at(padded_start) <= .30
        rc, outside_end, outside_start = analyze(path, start=12000, length=6000)
        assert rc == 0 and outside_end == outside_start and 12 <= at(outside_end) <= 18
        with tempfile.TemporaryDirectory(prefix='aac-silence-', dir=repo / 'build' if os.name == 'nt' else None) as tmp:
            root = Path(tmp)
            broken = root / 'broken.aac'
            broken.write_bytes(b'not an ADTS frame')
            failed = analyze(broken)
            assert failed[0] != 0 and failed[1:] == (123, 456), failed
            # A valid ADTS header with a broken payload must preserve fallback.
            damaged = bytearray(data)
            frame_offset, frame = packets[10]
            damaged[frame_offset + 7:frame_offset + len(frame)] = bytes([255]) * (len(frame) - 7)
            broken.write_bytes(damaged)
            failed = analyze(broken)
            assert failed[0] != 0 and failed[1:] == (123, 456), failed
            truncated = root / 'truncated.aac'
            truncated.write_bytes(data[:-5])
            assert analyze(truncated) == (0, end, start)
            state.update(data=data, change=offsets[int(7 * rate / 1024)],
                         bitrate=round(len(data) * 8 / 24 / 1000))
            outputs = {}
            for option in ('default', '--xs2', '--xs-none'):
                destination = root / option
                command = [str(exe), f'http://127.0.0.1:{server.server_port}/audio',
                           '-c', '-s', '-d', str(destination), '-k', '0', '-i']
                if option != 'default':
                    command.append(option)
                result = subprocess.run(command, capture_output=True, env=env, timeout=10)
                assert result.returncode == 0, result.stdout + result.stderr
                complete = destination / 'Artist - Before.aac'
                assert complete.is_file(), (option, result.stdout + result.stderr, list(destination.rglob('*')))
                recorded = complete.read_bytes()
                recorded_packets = frames(recorded)
                # No re-encoding: every recorded frame equals the original prefix.
                assert recorded == b''.join(frame for _, frame in packets[:len(recorded_packets)])
                outputs[option] = len(recorded_packets) * 1024 / rate
                assert not list(destination.rglob('.lock'))
            assert 8 <= outputs['default'] <= 10, (label, outputs)
            assert outputs['--xs2'] == outputs['default'], (label, outputs)
            assert outputs['--xs-none'] < 8, (label, outputs)
        print('PASS:', label, 'decoded silence, ADTS boundaries, padding, partial input, default/xs2/disabled, lossless output', flush=True)
finally:
    server.shutdown()
    server.server_close()
