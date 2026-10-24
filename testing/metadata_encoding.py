"""Check automatic ICY decoding, explicit overrides, Unicode paths and ID3."""
from pathlib import Path
import ctypes
import http.server
import os
import subprocess
import tempfile
import threading

repo = Path(__file__).resolve().parents[1]
exe = repo / 'dist' / 'streamripper.exe'
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()
stop = threading.Event()
western = 'Mötley Crüe - Kickstart My Heart'
czech = 'Žluťoučký kůň - Příliš žluťoučký'
acp = ctypes.windll.kernel32.GetACP()
fallback = 'latin-1' if acp == 65001 else f'cp{acp}'
legacy = western.encode(fallback)
cases = {
    '/utf8': western.encode(),
    '/czech': czech.encode(),
    '/legacy': legacy,
    '/cp1250': czech.encode('cp1250'),
    '/mixed': None,
}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', 'Encoding test')
        self.send_header('icy-metaint', '16384')
        self.end_headers()
        number = 0
        try:
            while not stop.is_set():
                title = cases[self.path]
                if title is None:
                    # One connection changes encoding and title at a boundary.
                    title = western.encode() if number < 30 else legacy.replace(b'Heart', b'Heart Again')
                meta = b"StreamTitle='" + title + b"';"
                meta += bytes(-len(meta) % 16)
                block = (audio * (16384 // len(audio) + 1))[:16384]
                self.wfile.write(block + bytes([len(meta) // 16]) + meta)
                self.wfile.flush()
                number += 1
                stop.wait(0.01)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='metadata-encoding-', dir=repo / 'build') as temporary:
        root = Path(temporary)
        # Keep the user's saved station/default encodings out of this test.
        config = root / 'config'
        config.mkdir()
        environment = dict(os.environ, XDG_CONFIG_HOME=str(config),
                           APPDATA=str(config), LOCALAPPDATA=str(config))
        checks = [('/utf8', [], western), ('/czech', [], czech),
                  ('/legacy', ['--codeset-metadata=AUTO'], western),
                  ('/utf8', ['--codeset-metadata=UTF-8'], western),
                  ('/cp1250', ['--codeset-metadata=CP1250'], czech),
                  # An explicit encoding must disable the UTF-8 heuristic.
                  ('/utf8', ['--codeset-metadata=ISO-8859-1'], western.encode().decode('latin-1')),
                  ('/mixed', ['--codeset-metadata=auto'], western + ' Again')]
        for number, (path, options, expected) in enumerate(checks):
            dest = root / str(number)
            result = subprocess.run([str(exe), f'http://127.0.0.1:{server.server_port}{path}',
                    '-c', '-l', '2', '-s', '-d', str(dest), '-k', '0', '--xs-none'] + options,
                    capture_output=True, timeout=8, env=environment)
            assert result.returncode == 0, result.stdout + result.stderr
            files = list(dest.rglob('*.mp3'))
            recording = next((f for f in files if f.stem == expected), None)
            assert recording is not None, (path, options, expected, files)
            artist, title = expected.split(' - ', 1)
            data = recording.read_bytes()
            assert data.startswith(b'ID3')
            assert artist.encode('utf-16-le') in data, (path, 'artist tag')
            assert title.encode('utf-16-le') in data, (path, 'title tag')
            assert expected.encode() in result.stdout, (path, 'console text', result.stdout)
            print(f'PASS: {path}, {options or "default AUTO"}, filename/console/ID3')
finally:
    stop.set()
    server.shutdown()
    server.server_close()
