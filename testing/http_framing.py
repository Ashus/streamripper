"""Byte-for-byte HTTP chunk decoding and malformed framing regression tests.
Usage: python testing/http_framing.py dist/streamripper.exe path/sample.mp3
"""
from pathlib import Path
import http.server
import subprocess
import sys
import tempfile
import threading

exe = Path(sys.argv[1]).resolve()
sample = Path(sys.argv[2]).read_bytes()
audio = (sample * (640000 // len(sample) + 1))[:640000]
body = b''.join(audio[i:i + 16000] + b'\0' for i in range(0, len(audio), 16000))

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', f'http://127.0.0.1:{self.server.server_port}/valid')
            self.send_header('Connection', 'close')
            self.end_headers()
            return
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', 'Framing')
        self.send_header('icy-metaint', '16000')
        self.send_header('icy-br', '128')
        self.send_header('tRaNsFeR-EnCoDiNg', 'chunked')
        self.send_header('Connection', 'close')
        self.end_headers()
        try:
            malformed = {
                '/bad-hex': b'z\r\n',
                '/overflow': b'10000000000000000\r\n',
                '/missing-crlf': b'1\r\nxXX',
                '/truncated': b'ff\r\nx',
                '/bad-trailer': b'0\r\n' + b'x' * 1100,
            }
            if self.path in malformed:
                self.wfile.write(malformed[self.path])
                return
            offset = 0
            sizes = (1, 2, 17, 997, 16000)
            index = 0
            while offset < len(body):
                part = body[offset:offset + sizes[index % len(sizes)]]
                self.wfile.write(f'{len(part):X};foo=bar\r\n'.encode() + part + b'\r\n')
                offset += len(part)
                index += 1
            self.wfile.write(b'0;done=yes\r\nX-Test-Trailer: yes\r\n\r\n')
        except OSError:
            pass

with tempfile.TemporaryDirectory(prefix='streamripper-framing-') as tmp:
    root = Path(tmp)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for name in ('valid', 'redirect', 'bad-hex', 'overflow', 'missing-crlf', 'truncated', 'bad-trailer'):
            dest = root / name
            result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/{name}',
                                     '-c', '-A', '-a', 'show', '-l', '5', '-d', str(dest)],
                                    capture_output=True, timeout=12)
            output = result.stdout + result.stderr
            assert result.returncode == 0, output
            if name in ('valid', 'redirect'):
                host = '127.0.0.1' if name == 'redirect' else 'localhost'
                assert f'host: {host}:{server.server_port}\n'.encode() in output.replace(b'\r\n', b'\n'), output
                path = next(dest.rglob('show.mp3'))
                assert path.read_bytes() == audio, (path.stat().st_size, len(audio), output[-1000:])
            else:
                assert b'SR_ERROR_RECV_FAILED' in output, output[-1500:]
            print('OK: HTTP framing', name, flush=True)
    finally:
        server.shutdown()
print('All HTTP framing tests passed.')
