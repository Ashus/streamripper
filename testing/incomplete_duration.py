"""Exercise opt-in incomplete retention by media duration, without FFmpeg."""
from pathlib import Path
import http.server
import os
import re
import subprocess
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
exe = repo / 'dist' / 'streamripper.exe'
mp3 = (repo / 'build' / 'sample-5s.mp3').read_bytes()
# A synthetic AAC-LC ADTS frame: 1024 samples at 44100 Hz, 107 bytes.
aac = bytes([255, 241, 80, 128, 13, 127, 252]) + bytes(100)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'audio/aac' if self.path.startswith('/aac') else 'audio/mpeg')
        self.send_header('icy-name', 'Retention test')
        self.send_header('icy-br', '128')
        named = self.path.endswith('/named')
        if named:
            self.send_header('icy-metaint', '16384')
        self.end_headers()
        source = aac * 200 if self.path.startswith('/aac') else mp3
        offset = 0
        try:
            for i in range(1000):
                block = (source * (16384 // len(source) + 2))[offset:offset + 16384]
                offset = (offset + 16384) % len(source)
                if named:
                    meta = f"StreamTitle='Artist - Song {i // 24}';".encode()
                    meta += bytes(-len(meta) % 16)
                    block += bytes([len(meta) // 16]) + meta
                self.wfile.write(block)
                time.sleep(0.01)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()


def run(dest, kind, duration=None, named=False, extra=()):
    endpoint = 'aac' if kind == 'aac' else 'mp3'
    endpoint += '/named' if named else '/no-metadata'
    args = [str(exe), f'http://localhost:{server.server_port}/{endpoint}',
            '-c', '-l', '2', '-s', '-d', str(dest), '--xs-none']
    if kind == 'wav':
        args.append('--wav')
    if duration is not None:
        args.append('--incomplete-min-duration=' + duration)
    result = subprocess.run(args + list(extra), capture_output=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    assert b'Unable' not in result.stderr, result.stderr
    return list(dest.rglob('*.' + kind)), result


try:
    for value in ('', '-1', '+1', '1.5m', '1d', '10ms', ' 1', '1 ',
                  '18446744073709551616', '18446744073709552s', '99999999999999h'):
        result = subprocess.run([str(exe), 'http://localhost/',
                                 '--incomplete-min-duration=' + value, '-h'], capture_output=True)
        assert result.returncode != 0 and b'Invalid incomplete duration' in result.stderr, value
    for value in ('0', '30', '30s', '10m', '1h', '18446744073709551s'):
        result = subprocess.run([str(exe), 'http://localhost/',
                                 '--incomplete-min-duration=' + value, '-h'], capture_output=True)
        assert result.returncode == 0, value
    print('PASS: duration syntax, units, disabled value, and overflow rejection')

    with tempfile.TemporaryDirectory(prefix='incomplete-duration-', dir=repo / 'build') as temporary:
        root = Path(temporary)
        source = root / 'exact.aac'
        source.write_bytes((bytes([255, 241, 76, 128, 13, 127, 252]) + bytes(100)) * 3000)
        env = os.environ.copy()
        env['PATH'] = str(repo / 'dist') + os.pathsep + env.get('PATH', '')
        duration = subprocess.check_output([str(repo / 'build' / 'core_regression.exe'),
                    '--frame-duration', 'aac', str(source), str(root / 'exact-out.aac'), '997'], env=env)
        assert duration.strip() == b'64000', duration
        print('PASS: exact sample-based duration without accumulated per-frame rounding')
        for kind in ('mp3', 'aac', 'wav'):
            for setting in (None, '30s', '1h', '0'):
                files, result = run(root / (kind + str(setting)), kind, setting)
                if setting == '1h':
                    assert not files, files
                    assert b'Discarded incomplete recording' in result.stderr
                else:
                    assert len(files) == 1 and 'incomplete' in files[0].parts, files
                    assert b'Discarded' not in result.stderr
            print('PASS:', kind, 'no-metadata recording: default/disabled preserved, long retained, short removed')

        dest = root / 'named-žluťoučký'
        incomplete = dest / 'incomplete'
        incomplete.mkdir(parents=True)
        old = b'Existing user recording; preserve byte for byte.'
        (incomplete / 'Artist - Song 0.mp3').write_bytes(old)
        (incomplete / 'old-unrelated.mp3').write_bytes(old)
        files, result = run(dest, 'mp3', '15m', named=True,
                            extra=('-t', '-q', '1', '--metadata-log', '--timeout=8',
                                   '--user-agent=WinampMPEG/5.0',
                                   '-D', str(dest / '%q - %A - %T')))
        completed = [path for path in files if path.parent == dest]
        assert len(completed) >= 2, files
        leftovers = [path for path in files if path.parent == incomplete]
        assert len(leftovers) == 2 and all(path.read_bytes() == old for path in leftovers), leftovers
        assert result.stderr.count(b'Discarded incomplete recording') >= 2
        statuses = re.findall(rb'\[\s*(skipping|ripping)\.\.\.\s*\] Artist - Song (\d+)', result.stdout)
        assert (b'skipping', b'0') in statuses, result.stdout
        assert (b'ripping', b'0') not in statuses, statuses
        assert (b'ripping', b'1') in statuses, statuses
        assert (b'skipping', b'1') not in statuses, statuses
        assert any(path.name.endswith('Artist - Song 1.mp3') for path in completed), completed
        assert not any(b'Discarded incomplete recording' in line and b'Song 1.mp3' in line
                       for line in result.stderr.splitlines()), result.stderr
        print('PASS: first skipped and final tracks removed; completed tracks, -t backup, and older files preserved')
        print('PASS: default first-track skipping matches the displayed title; complete second track survives the 15-minute filter')

        dest = root / 'duplicates'
        dest.mkdir()
        (dest / 'Artist - Song 1.mp3').write_bytes(old)
        files, result = run(dest, 'mp3', '1h', named=True, extra=('-k', '0', '-o', 'never'))
        assert (dest / 'Artist - Song 1.mp3').read_bytes() == old
        assert not list((dest / 'incomplete').glob('*.mp3'))
        print('PASS: -o never leaves existing complete recording intact and cleans the new incomplete duplicate')

        dest = root / 'truncated'
        dest.mkdir()
        (dest / 'Artist - Song 1.mp3').write_bytes(old)
        files, result = run(dest, 'mp3', '1s', named=True, extra=('-k', '0', '-o', 'never', '-T'))
        assert (dest / 'Artist - Song 1.mp3').read_bytes() == old
        assert not (dest / 'incomplete' / 'Artist - Song 1.mp3').exists()
        print('PASS: -T truncated duplicate counts as zero retained audio duration')

        dest = root / 'show'
        _, result = run(dest, 'mp3', '1h', extra=('-a', 'continuous', '-A'))
        assert list(dest.glob('continuous*.mp3'))
        assert b'Discarded' not in result.stderr
        print('PASS: continuous -a recording unaffected')
finally:
    server.shutdown()
    server.server_close()
