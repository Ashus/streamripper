"""Check automatic %q numbering at the start of a filename and after restart."""
from pathlib import Path
import http.server
import subprocess
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        for key, value in {'Content-Type': 'audio/mpeg', 'icy-name': 'Sequence test',
                           'icy-metaint': '16000', 'icy-br': '128'}.items():
            self.send_header(key, value)
        self.end_headers()
        try:
            for i in range(1000):
                title = 'Band - Song ' + str(min(i // 48, 3))
                meta = ("StreamTitle='" + title + "';").encode()
                meta += bytes(-len(meta) % 16)
                offset = (i * 16000) % len(audio)
                self.wfile.write((audio * 2)[offset:offset + 16000]
                                 + bytes([len(meta) // 16]) + meta)
                time.sleep(0.01)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='sequence-test-', dir=repo / 'build') as tmp:
        for separator, prefix in [('/', ''), ('\\', ''), ('/', 'recording_')]:
            dest = Path(tmp) / ('case' + str(len(list(Path(tmp).iterdir()))))
            dest.mkdir()
            pattern = str(dest).replace('\\', separator) + separator + prefix + '%q - %A - %T'
            for run in range(2):
                result = subprocess.run([
                    str(repo / 'dist' / 'streamripper.exe'),
                    f'http://localhost:{server.server_port}/stream', '-c', '-l', '3',
                    '-k', '0', '-s', '-D', pattern, '--xs-none',
                ], capture_output=True, timeout=15)
                assert result.returncode == 0, result.stdout + result.stderr
                files = sorted(dest.glob('*.mp3'))
                assert len(files) == (run + 1) * 3, [p.name for p in files]
                numbers = sorted(int(p.name[len(prefix):].split(' - ')[0]) for p in files)
                assert numbers == list(range(len(files))), [p.name for p in files]
            print(f'PASS: {separator!r} separator, prefix {prefix!r}, restart continues 0003..0005')
finally:
    server.shutdown()
    server.server_close()
