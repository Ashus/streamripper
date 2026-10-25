"""Check ICY station/title decoding, Icecast defaults and explicit overrides.
Usage: python testing/station_names.py executable
"""
from pathlib import Path
import http.server
import os
import subprocess
import sys
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()
exe = Path(sys.argv[1]).resolve()
cases = [
    ('legacy', b'R\xe1dio Helax', 'AUTO', 'Rádio Helax', 'Test server'),
    ('utf8', 'Rádio Žluťoučký'.encode('utf-8'), 'AUTO', 'Rádio Žluťoučký', 'Test server'),
    ('explicit', 'Žluťoučký'.encode('cp1250'), 'CP1250', 'Žluťoučký', 'Icecast 2.4.4'),
    ('icecast-latin1', 'Rádio Þór'.encode('latin1'), 'AUTO', 'Rádio Þór', 'Icecast 2.4.4'),
    ('icecast-utf8', 'Rádio Žluťoučký'.encode('utf8'), 'AUTO', 'Rádio Žluťoučký', 'iCeCaSt/2.4.4'),
]


class Handler(http.server.BaseHTTPRequestHandler):
    def version_string(self):
        return cases[int(self.path[1:])][4]

    def log_message(self, *args):
        pass

    def do_GET(self):
        raw = cases[int(self.path[1:])][1]
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', raw.decode('latin1'))
        self.send_header('icy-metaint', '16000')
        self.end_headers()
        metadata = b"StreamTitle='Artist - " + raw + b"';"
        metadata += bytes(-len(metadata) % 16)
        block = (audio * (16000 // len(audio) + 1))[:16000]
        try:
            while True:
                self.wfile.write(block + bytes([len(metadata) // 16]) + metadata)
                self.wfile.flush()
                time.sleep(.02)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='station-names-', dir=repo / 'build' if os.name == 'nt' else None) as tmp:
        for index, (label, raw, encoding, expected, server_name) in enumerate(cases):
            output = Path(tmp) / label
            result = subprocess.run(
                [str(exe), f'http://127.0.0.1:{server.server_port}/{index}',
                 '-c', '-l', '1', '-d', str(output), '--keep-incomplete',
                 '--xs-none', '-k', '0',
                 '--codeset-metadata=' + encoding],
                capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            log = result.stdout.decode('utf-8')
            assert 'stream: ' + expected in log.splitlines(), log
            assert (output / expected).is_dir(), list(output.iterdir())
            files = list((output / expected).rglob('*.mp3'))
            track = 'Artist - ' + expected
            assert any(file.stem == track for file in files), files
            assert track in log, log
            assert not list(output.rglob('.lock')), list(output.rglob('*'))
            print('PASS:', label, 'station/title console output, directory and track filename')
finally:
    server.shutdown()
    server.server_close()
