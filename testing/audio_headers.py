"""Check native ADTS framing, MP3 duration headers, and recording ID3 tags.

No FFmpeg dependency. Synthetic frames test framing, not AAC decoding; MP3
recording additionally uses the pinned real-audio fixture from fetch-samples.py.
"""
from pathlib import Path
import http.server
import os
import struct
import subprocess
import tempfile
import threading
import time
import wave

repo = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env['PATH'] = str(repo / 'dist') + os.pathsep + env.get('PATH', '')


def mp3_header(data, offset):
    a, b, c, d = data[offset:offset + 4]
    assert a == 255 and b & 224 == 224 and b & 6 == 2
    version = (b >> 3) & 3
    assert version != 1 and c >> 4 not in (0, 15)
    rate = (44100, 48000, 32000)[(c >> 2) & 3] // ({3: 1, 2: 2, 0: 4}[version])
    rates = ((0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320)
             if version == 3 else (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160))
    size = (144 if version == 3 else 72) * rates[c >> 4] * 1000 // rate + ((c >> 1) & 1)
    side = (17 if d >> 6 == 3 else 32) if version == 3 else (9 if d >> 6 == 3 else 17)
    return size, rate, 1152 if version == 3 else 576, 4 + side


def mp3_frames(data):
    result = []
    pos = 0
    while pos + 4 <= len(data):
        try:
            size, _, _, tag = mp3_header(data, pos)
        except (AssertionError, IndexError):
            pos += 1
            continue
        if pos + size > len(data):
            break
        frame = data[pos:pos + size]
        if frame[tag:tag + 4] not in (b'Xing', b'Info') and frame[36:40] != b'VBRI':
            result.append(frame)
        pos += size
    return result


def adts_frame(payload, crc=False, rate_index=4, blocks=0):
    size = len(payload) + (9 if crc else 7)
    return bytes([255, 240 if crc else 241, 64 | rate_index << 2,
                  128 | (size >> 11), (size >> 3) & 255,
                  ((size & 7) << 5) | 31, 252 | blocks]) + (b'\0\0' if crc else b'') + payload


def read_tags(data):
    tags = {}
    if not data.startswith(b'ID3'):
        return tags, data
    assert data[3:6] == b'\x03\0\0'
    size = sum(value << (7 * (3 - i)) for i, value in enumerate(data[6:10]))
    pos = 10
    while pos + 10 <= size + 10 and data[pos] != 0:
        name = data[pos:pos + 4].decode('ascii')
        count = int.from_bytes(data[pos + 4:pos + 8], 'big')
        assert count and pos + 10 + count <= size + 10
        tags[name] = data[pos + 10:pos + 10 + count]
        pos += count + 10
    return tags, data[size + 10:]


def check_mp3(data):
    size, rate, samples, tag = mp3_header(data, 0)
    assert data[tag:tag + 4] in (b'Xing', b'Info')
    flags, count, byte_count = struct.unpack('>III', data[tag + 4:tag + 16])
    assert flags == 3 and byte_count == len(data)
    frames = []
    pos = size
    while pos < len(data):
        frame_size, actual_rate, actual_samples, _ = mp3_header(data, pos)
        assert actual_rate == rate and actual_samples == samples
        assert pos + frame_size <= len(data)
        frames.append(data[pos:pos + frame_size])
        pos += frame_size
    assert len(frames) == count and count
    return frames, count * samples * 1000 // rate


def check_adts(data):
    frames = []
    duration = 0.0
    rates = (96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
             16000, 12000, 11025, 8000, 7350)
    while data:
        assert len(data) >= 7 and data[0] == 255 and data[1] & 246 == 240
        size = ((data[3] & 3) << 11) | (data[4] << 3) | (data[5] >> 5)
        assert size > (7 if data[1] & 1 else 9) and size <= len(data)
        duration += 1024 * ((data[6] & 3) + 1) * 1000 / rates[(data[2] >> 2) & 15]
        frames.append(data[:size])
        data = data[size:]
    assert frames
    return frames, int(duration)


real_mp3 = mp3_frames((repo / 'build' / 'sample-5s.mp3').read_bytes())
assert len(real_mp3) > 50
aac = [adts_frame(bytes([i % 200]) * (70 + i % 53), crc=i % 3 == 0,
                  blocks=i % 4) for i in range(240)]


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        is_aac = self.path.startswith('/aac')
        source = b''.join(aac if is_aac else real_mp3)
        # Start mid-frame and place all metadata boundaries inside audio frames.
        offset = 31
        self.send_response(200)
        for key, value in {'Content-Type': 'audio/aac' if is_aac else 'audio/mpeg',
                           'icy-name': 'Header test', 'icy-metaint': '16001',
                           'icy-br': '128'}.items():
            self.send_header(key, value)
        self.end_headers()
        try:
            for i in range(1000):
                block = (source * (16001 // len(source) + 2))[offset:offset + 16001]
                offset = (offset + 16001) % len(source)
                meta = f"StreamTitle='Artist - Song {i // 32}';".encode()
                meta += bytes(-len(meta) % 16)
                self.wfile.write(block + bytes([len(meta) // 16]) + meta)
                time.sleep(0.01)
        except OSError:
            pass


with tempfile.TemporaryDirectory(prefix='audio-headers-', dir=repo / 'build') as temporary:
    root = Path(temporary)
    # Direct writer: arbitrary write fragmentation, CRC, MPEG versions and VBR.
    mp3_cases = [real_mp3, [bytes.fromhex('fffb9000') + bytes(413)] * 20,
                 [bytes.fromhex('fff380c0') + bytes(204)] * 20,
                 [bytes.fromhex('ffe38000') + bytes(413)] * 20,
                 [bytes.fromhex('fffa9000') + bytes(413)] * 20,
                 [bytes.fromhex('fffb9000') + bytes(413),
                  bytes.fromhex('fffbb000') + bytes(622)] * 12]
    for kind, cases in [('aac', [aac]), ('mp3', mp3_cases)]:
        for n, frames in enumerate(cases):
            source = root / 'input.bin'
            source.write_bytes(b'junk' + frames[0][17:] + b''.join(frames[1:-1]) + frames[-1][:19])
            for block in (1, 7, 997, 16384):
                output = root / 'output.bin'
                subprocess.run([str(repo / 'build' / 'core_regression.exe'), '--frame-copy',
                                kind, str(source), str(output), str(block)], env=env, check=True)
                actual, _ = (check_adts if kind == 'aac' else check_mp3)(output.read_bytes())
                assert actual == frames[1:-1], (kind, n, block)
    print('PASS: complete frames, partial edges, fragmented writes, CRC, MPEG-1/2/2.5, CBR/VBR')

    # Replace a source recording's duration header instead of keeping its counts.
    old = output.read_bytes()
    old_size, _, _, _ = mp3_header(old, 0)
    source.write_bytes(old[:old_size] + b''.join(real_mp3))
    subprocess.run([str(repo / 'build' / 'core_regression.exe'), '--frame-copy',
                    'mp3', str(source), str(output), '997'], env=env, check=True)
    assert check_mp3(output.read_bytes())[0] == real_mp3
    print('PASS: stale source Xing/Info replaced with per-file counts')

    for kind, frames in [('aac', aac), ('mp3', real_mp3)]:
        source.write_bytes(b''.join(frames))
        subprocess.run([str(repo / 'build' / 'core_regression.exe'), '--stress-frames',
                        kind, str(source), str(output)], env=env, check=True)

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for kind in ('aac', 'mp3'):
            for tags_enabled in (True, False):
                dest = root / (kind + str(tags_enabled))
                args = [str(repo / 'dist' / 'streamripper.exe'),
                        f'http://localhost:{server.server_port}/{kind}', '-c', '-l', '3',
                        '-k', '0', '-s', '-d', str(dest), '--xs-none',
                        '--xs-padding=0:0', '--xs-offset=0', '-q', '1']
                args += ['--with-id3v1'] if tags_enabled else ['-i']
                result = subprocess.run(args, capture_output=True, timeout=15)
                assert result.returncode == 0, result.stdout + result.stderr
                assert b'finalize' not in result.stderr, result.stderr
                files = list(dest.rglob('*.' + kind))
                assert len(files) >= 3, files
                assert any('incomplete' in path.parts for path in files)
                ordered_frames = []
                files.sort(key=lambda path: ('incomplete' in path.parts, path.name))
                for path in files:
                    tags, data = read_tags(path.read_bytes())
                    if tags_enabled and 'incomplete' not in path.parts:
                        assert data[-128:-125] == b'TAG'
                    if data[-128:-125] == b'TAG':
                        data = data[:-128]
                    frames, duration = (check_adts if kind == 'aac' else check_mp3)(data)
                    ordered_frames.extend(frames)
                    if tags_enabled:
                        assert tags['TPE1'] and tags['TIT2'] and tags['TLEN'][0] == 0
                        assert abs(int(tags['TLEN'][1:]) - duration) <= 1
                    else:
                        assert not tags
                if kind == 'aac':
                    indices = {frame: n for n, frame in enumerate(aac)}
                    order = [indices[frame] for frame in ordered_frames]
                    assert all(b == (a + 1) % len(aac) for a, b in zip(order, order[1:])), order
                else:
                    recorded = b''.join(ordered_frames)
                    source = b''.join(real_mp3)
                    expected = (source * (len(recorded) // len(source) + 2))[len(real_mp3[0]):]
                    assert recorded == expected[:len(recorded)], 'MP3 split lost or duplicated an audio frame'
                    decoded = root / 'decoded.wav'
                    subprocess.run([str(repo / 'build' / 'core_regression.exe'), '--decode',
                                    str(files[0]), str(decoded), '997'], env=env, check=True)
                    with wave.open(str(decoded), 'rb') as pcm:
                        assert pcm.getnframes() > 0
                print('PASS:', kind, 'ICY splits, completed/incomplete files, ID3v2/TLEN' if tags_enabled else 'tag-free output')
    finally:
        server.shutdown()
        server.server_close()
