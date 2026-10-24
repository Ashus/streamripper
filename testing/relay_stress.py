"""Exercise relay clients across hundreds of recycled circular-buffer nodes.
Usage: python testing/relay_stress.py dist/streamripper.exe path/sample.mp3
"""
from pathlib import Path
import concurrent.futures
import http.server
import socket
import subprocess
import sys
import tempfile
import threading
import time

exe = Path(sys.argv[1]).resolve()
sample = Path(sys.argv[2]).read_bytes()
tail = (sample * (16000 // len(sample) + 1))[:15992]

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_GET(self):
        self.send_response(200)
        for key, value in {'Content-Type': 'audio/mpeg', 'icy-name': 'Relay stress',
                           'icy-metaint': '16000', 'icy-br': '128'}.items():
            self.send_header(key, value)
        self.end_headers()
        try:
            for index in range(1000):
                title = f"StreamTitle='Artist - Track {index // 32}';".encode()
                title += bytes(-len(title) % 16)
                self.wfile.write(index.to_bytes(8, 'big') + tail + bytes([len(title) // 16]) + title)
                self.wfile.flush()
                time.sleep(0.025)
        except OSError:
            pass

with tempfile.TemporaryDirectory(prefix='streamripper-relay-') as tmp:
    root = Path(tmp)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen([str(exe), f'http://localhost:{server.server_port}/stream',
                                '-c', '-l', '12', '-A', '-a', 'show', '-d', str(root),
                                '-r', f'127.0.0.1:{port}', '-R', '0', '-z', '--xs-none', '--quiet'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    def client(name, metadata):
        deadline = time.monotonic() + 5
        while True:
            try:
                sock = socket.create_connection(('127.0.0.1', port), timeout=2)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.02)
        with sock:
            sock.settimeout(3)
            sock.sendall(f'GET / HTTP/1.0\r\nHost: localhost\r\nIcy-Metadata: {int(metadata)}\r\n\r\n'.encode())
            reader = sock.makefile('rb')
            assert b'200' in reader.readline()
            headers = {}
            while True:
                line = reader.readline()
                if line == b'\r\n':
                    break
                key, value = line.decode().split(':', 1)
                headers[key.lower()] = value.strip()
            if metadata:
                assert headers['icy-metaint'] == '16000'
            previous = None
            for _ in range(280):
                block = reader.read(16000)
                assert len(block) == 16000, (name, len(block), previous)
                index = int.from_bytes(block[:8], 'big')
                assert block[8:] == tail, (name, index, 'audio corruption')
                assert previous is None or index == previous + 1, (name, previous, index)
                previous = index
                if metadata:
                    length = reader.read(1)
                    assert length
                    title = reader.read(length[0] * 16)
                    assert b'StreamTitle=' in title, (name, title)
            reader.close()
        print('OK:', name, '280 consecutive blocks across buffer recycling', flush=True)
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(lambda item: client(*item), [('relay-with-icy', True), ('relay-audio-only', False)]))
        out, err = process.communicate(timeout=10)
        assert process.returncode == 0, (out[-1000:], err[-1000:])
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
        server.shutdown()
print('Relay stress test passed.')
