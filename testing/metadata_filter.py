"""Exercise the bundled generic rules against a local ICY stream."""
from pathlib import Path
import http.server
import subprocess
import tempfile
import threading
import time
import wave

repo = Path(__file__).resolve().parents[1]
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()
titles = ['ADVERT - Opening', 'Artist - One', 'advert - Break',
          'ADVERT - Another break', 'Artist - Two', ' - ',
          'Artist - Three', '', 'advert - Closing']


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        for key, value in {'Content-Type': 'audio/mpeg', 'icy-name': 'Test stream',
                           'icy-metaint': '16000', 'icy-br': '128'}.items():
            self.send_header(key, value)
        self.end_headers()
        try:
            for i in range(1000):
                title = titles[min(i // 24, len(titles) - 1)]
                # Zero-length blocks must retain the previous track decision.
                meta = ("StreamTitle='" + title + "';").encode() if i % 24 == 0 else b''
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
    with tempfile.TemporaryDirectory(prefix='metadata-filter-', dir=repo / 'build') as tmp:
        root = Path(tmp)
        rules = root / 'pravidla-žluťoučký.txt'
        example = (repo / 'advert_rules.txt').read_text(encoding='utf-8')
        rules.write_text(example.replace('# m/^[[:space:]-]*$/x',
                                         'm/^[[:space:]-]*$/x'), encoding='utf-8')
        for wav in (False, True):
            dest = root / ('wav' if wav else 'mp3')
            args = [str(repo / 'dist' / 'streamripper.exe'),
                    f'http://localhost:{server.server_port}/stream', '-c', '-l', '4',
                    '-k', '0', '-s', '-d', str(dest), '-q', '1', '--xs-none',
                    '-w', str(rules)]
            if wav:
                args.append('--wav')
            result = subprocess.run(args, capture_output=True, timeout=15)
            assert result.returncode == 0, result.stdout + result.stderr
            extension = '.wav' if wav else '.mp3'
            files = sorted(dest.rglob('*' + extension))
            expected = [f'{n:04d}_Artist - {title}{extension}'
                        for n, title in enumerate(('One', 'Two', 'Three'), 1)]
            assert [p.name for p in files] == expected, [p.relative_to(dest) for p in files]
            if wav:
                for path in files:
                    with wave.open(str(path), 'rb') as recorded:
                        assert recorded.getnframes() > 0
            print('PASS:', extension, 'exclusion, Unicode rules path, empty/hyphen titles, consecutive numbering')
finally:
    server.shutdown()
    server.server_close()
