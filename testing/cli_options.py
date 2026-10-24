"""Check descriptive option aliases and LF/CRLF metadata rule files."""
from pathlib import Path
import http.server
import subprocess
import tempfile
import threading

repo = Path(__file__).resolve().parents[1]
exe = repo / 'dist' / 'streamripper.exe'
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()
stop = threading.Event()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', 'CLI test')
        self.send_header('icy-metaint', '16384')
        self.send_header('icy-br', '128')
        self.end_headers()
        if self.path == '/stall':
            stop.wait(20)
            return
        meta = b"StreamTitle='Artist - Song';"
        meta += bytes(-len(meta) % 16)
        offset = 0
        try:
            while not stop.is_set():
                block = (audio * (16384 // len(audio) + 2))[offset:offset + 16384]
                offset = (offset + 16384) % len(audio)
                self.wfile.write(block + bytes([len(meta) // 16]) + meta)
                stop.wait(0.01)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='cli-options-', dir=repo / 'build') as temporary:
        root = Path(temporary)
        for number, options in enumerate((['-m', '1'], ['--timeout', '1'], ['--timeout=1'])):
            # If the alias were ignored, this server would stall beyond the test deadline.
            result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/stall',
                                     '-c', '-d', str(root / str(number))] + options,
                                    capture_output=True, timeout=5)
            assert b'error' in result.stderr.lower(), result.stdout + result.stderr
        for options in (['-m', 'invalid'], ['--timeout', 'invalid'], ['--timeout=invalid'],
                        ['--timeout=2147483648'], ['--timeout=-1']):
            result = subprocess.run([str(exe), 'http://localhost/'] + options + ['-h'],
                                    capture_output=True)
            assert result.returncode != 0 and b'Invalid timeout' in result.stderr, options
        print('PASS: short/spaced/equals timeout forms enforce the connection timeout and reject invalid values')

        for number, options in enumerate((['-t', '-i'], ['--keep-incomplete', '--no-id3'])):
            dest = root / ('tags' + str(number))
            incomplete = dest / 'incomplete'
            incomplete.mkdir(parents=True)
            old = b'Preserve the existing incomplete recording.'
            (incomplete / 'Artist - Song.mp3').write_bytes(old)
            result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/audio',
                                     '-c', '-l', '2', '-s', '-d', str(dest), '--xs-none',
                                     '--with-id3v1'] + options, capture_output=True, timeout=8)
            assert result.returncode == 0, result.stdout + result.stderr
            files = list(incomplete.glob('*.mp3'))
            assert len(files) == 2, files
            bodies = [path.read_bytes() for path in files]
            assert old in bodies
            recorded = next(data for data in bodies if data != old)
            assert not recorded.startswith(b'ID3') and recorded[-128:-125] != b'TAG'
        print('PASS: short and long keep-incomplete/no-id3 forms preserve the backup and disable both tag versions')

        for number, newline in enumerate((b'\n', b'\r\n')):
            rules = root / ('rules' + str(number) + '.txt')
            rules.write_bytes(newline.join((b'# Filter before extracting fields',
                              b'm/^Artist - Song$/x', b'm/^(.*?) - (.*)$/A1T2', b'')))
            dest = root / ('rules-out' + str(number))
            result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/audio',
                                     '-c', '-l', '2', '-s', '-d', str(dest), '--xs-none',
                                     '-w', str(rules)], capture_output=True, timeout=8)
            assert result.returncode == 0, result.stdout + result.stderr
            assert b'malformed' not in result.stdout.lower() + result.stderr.lower()
            assert not list(dest.rglob('*.mp3'))
        print('PASS: LF and CRLF metadata rule files apply the same exclusion')
finally:
    stop.set()
    server.shutdown()
    server.server_close()
