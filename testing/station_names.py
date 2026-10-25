"""Check ICY station-name decoding in console output and output directories.
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
    ('legacy', b'R\xe1dio Helax', 'AUTO', 'Rádio Helax'),
    ('utf8', 'Rádio Žluťoučký'.encode('utf-8'), 'AUTO', 'Rádio Žluťoučký'),
    ('explicit', 'Žluťoučký'.encode('cp1250'), 'CP1250', 'Žluťoučký'),
]


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        raw = cases[int(self.path[1:])][1]
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', raw.decode('latin1'))
        self.end_headers()
        try:
            while True:
                self.wfile.write(audio)
                self.wfile.flush()
                time.sleep(.02)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='station-names-', dir=repo / 'build' if os.name == 'nt' else None) as tmp:
        for index, (label, raw, encoding, expected) in enumerate(cases):
            output = Path(tmp) / label
            result = subprocess.run(
                [str(exe), f'http://127.0.0.1:{server.server_port}/{index}',
                 '-c', '-l', '1', '-d', str(output), '--keep-incomplete',
                 '--codeset-metadata=' + encoding],
                capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            log = result.stdout.decode('utf-8')
            assert 'stream: ' + expected in log.splitlines(), log
            assert (output / expected).is_dir(), list(output.iterdir())
            assert list((output / expected).rglob('*.mp3')), list(output.rglob('*'))
            assert not list(output.rglob('.lock')), list(output.rglob('*'))
            print('PASS:', label, 'station name in UTF-8 console output and directory')
finally:
    server.shutdown()
    server.server_close()
