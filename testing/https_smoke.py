"""Integration tests for HTTPS, ICY names, redirects, and certificate verification.
Usage: python testing/https_smoke.py dist/streamripper.exe path/sample.mp3 openssl.exe
"""
from pathlib import Path
import concurrent.futures
import http.server
import os
import ssl
import socket
import subprocess
import sys
import tempfile
import threading
import time

exe = Path(sys.argv[1]).resolve()
audio = Path(sys.argv[2]).read_bytes()
openssl = sys.argv[3]
assert len(audio) >= 16000

with tempfile.TemporaryDirectory(prefix='streamripper-https-') as tmp:
    root = Path(tmp)
    key, cert = root / 'key.pem', root / 'cert.pem'
    subprocess.run([openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(key), '-out', str(cert), '-days', '1', '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'], check=True, capture_output=True)

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path.startswith('/http10'):
                assert self.request_version == 'HTTP/1.0', self.request_version
            if self.path.startswith('/redirect'):
                self.send_response(302)
                self.send_header('Location', '/stream?test=1')
                self.end_headers()
                return
            if self.path.startswith('/upgrade'):
                self.send_response(302)
                self.send_header('Location', f'https://localhost:{tls.server_port}/stream')
                self.end_headers()
                return
            if self.path.startswith('/loop'):
                self.send_response(302)
                self.send_header('Location', '/loop')
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-Type', 'audio/mpeg')
            self.send_header('icy-name', 'X' * 3500 if self.path.startswith('/long-headers') else 'Test HTTPS')
            if self.path.startswith('/long-headers'):
                self.send_header('Server', 'Y' * 500)
            self.send_header('icy-metaint', '16000')
            self.send_header('icy-br', '256')
            if self.path.startswith('/chunked'):
                self.send_header('Transfer-Encoding', 'chunked')
            self.end_headers()
            title = "StreamTitle='Žluťoučký - Příliš žluťoučký kůň';".encode('utf-8')
            title += b' ' * (-len(title) % 16) if self.path.startswith('/long-headers') else bytes(-len(title) % 16)
            offset = 0
            try:
                for index in range(1000):
                    if index == 64:
                        title = "StreamTitle='Druhá kapela - Další píseň';".encode('utf-8')
                        title += bytes(-len(title) % 16)
                    block = (audio * 2)[offset:offset + 16000]
                    offset = (offset + 16000) % (len(audio) - 16000)
                    metadata = bytes([len(title) // 16]) + title if index % 8 == 0 else b'\0'
                    payload = block + metadata
                    # Split the metadata length byte and text across TLS records.
                    for start in range(0, len(payload), 997):
                        part = payload[start:start + 997]
                        if self.path.startswith('/chunked'):
                            self.wfile.write(f'{len(part):x};test=fragmented\r\n'.encode() + part + b'\r\n')
                        else:
                            self.wfile.write(part)
                    self.wfile.flush()
                    time.sleep(0.01)
            except (OSError, ssl.SSLError):
                pass

    tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    ec_key, ec_cert = root / 'ec-key.pem', root / 'ec-cert.pem'
    subprocess.run([openssl, 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes', '-keyout', str(ec_key), '-out', str(ec_cert), '-days', '1', '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'], check=True, capture_output=True)
    ec_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ec_context.minimum_version = ssl.TLSVersion.TLSv1_2
    ec_context.maximum_version = ssl.TLSVersion.TLSv1_2
    ec_context.set_ciphers('ECDHE-ECDSA-AES128-GCM-SHA256')
    ec_context.load_cert_chain(ec_cert, ec_key)
    ec_tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    ec_tls.socket = ec_context.wrap_socket(ec_tls.socket, server_side=True)
    plain = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    for server in (tls, plain, ec_tls):
        threading.Thread(target=server.serve_forever, daemon=True).start()

    def record(name, url, trust=True, expected=True, extra=(), trusted_cert=None, tls_expected=None):
        env = os.environ.copy()
        if trust:
            env['SSL_CERT_FILE'] = str(trusted_cert or cert)
        else:
            env.pop('SSL_CERT_FILE', None)
        dest = root / name
        cmd = [str(exe), url, '-c', '-l', '2', '-k', '0', '--xs-none', '--codeset-metadata=UTF-8', '-D', str(dest / '%A - %T')]
        cmd.extend(extra)
        result = subprocess.run(cmd, capture_output=True, timeout=15, env=env)
        output = result.stdout.decode('utf-8', errors='replace') + result.stderr.decode('utf-8', errors='replace')
        files = list(dest.rglob('*.mp3')) if dest.exists() else []
        if expected:
            assert result.returncode == 0 and '[ripping...' in output and 'Příliš žluťoučký kůň' in output, (name, output[-3000:])
            assert any('Příliš žluťoučký kůň' in path.name and path.stat().st_size > 16000 for path in files), (name, files, output[-1000:])
            assert any('Další píseň' in path.name and path.stat().st_size > 16000 for path in files), (name, files)
            assert any('Příliš žluťoučký kůň' in path.name and path.parent.name != 'incomplete' for path in files), (name, files)
        else:
            assert not files and ('TLS error' in output or 'SR_ERROR_INVALID_URL' in output or 'unsupported HTTP chunked' in output), (name, output)
        if tls_expected:
            assert tls_expected in output, (name, output[:2000])
            print(next(line for line in output.splitlines() if line.startswith('TLS:')), flush=True)
        print('OK:', name, flush=True)

    def relay_test():
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        env = os.environ.copy()
        env['SSL_CERT_FILE'] = str(cert)
        process = subprocess.Popen([str(exe), f'https://localhost:{tls.server_port}/stream', '-c', '-l', '3', '-k', '0', '--xs-none', '--codeset-metadata=UTF-8', '--codeset-relay=UTF-8', '-r', str(port), '-D', str(root / 'relay' / '%A - %T')], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    client = socket.create_connection(('127.0.0.1', port), timeout=2)
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.02)
            with client:
                client.sendall(b'GET / HTTP/1.0\r\nHost: localhost\r\nIcy-Metadata: 1\r\n\r\n')
                response = client.makefile('rb')
                status = response.readline()
                assert b'200' in status, status
                interval = None
                while True:
                    line = response.readline()
                    if line == b'\r\n':
                        break
                    if line.lower().startswith(b'icy-metaint:'):
                        interval = int(line.split(b':', 1)[1])
                assert interval == 16000, interval
                found = False
                for _ in range(16):
                    assert len(response.read(interval)) == interval
                    length = response.read(1)
                    assert length
                    metadata = response.read(length[0] * 16)
                    if b'StreamTitle=' in metadata:
                        title = metadata.rstrip(b'\0').decode('utf-8')
                        assert 'Příliš' in title or 'Další píseň' in title, title
                        found = True
                        break
                assert found, 'No ICY track name received from relay'
                response.close()
            out, err = process.communicate(timeout=10)
            assert process.returncode == 0, (out[-1000:], err)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        print('OK: relay-metadata-and-shutdown', flush=True)

    try:
        record('https', f'https://localhost:{tls.server_port}/stream?x=1:2&y=@')
        record('redirect', f'https://localhost:{tls.server_port}/redirect')
        record('http', f'http://localhost:{plain.server_port}/stream')
        record('http-to-https', f'http://localhost:{plain.server_port}/upgrade')
        record('untrusted-certificate', f'https://localhost:{tls.server_port}/stream', trust=False, expected=False)
        record('hostname-mismatch', f'https://127.0.0.1:{tls.server_port}/stream', expected=False)
        record('insecure', f'https://127.0.0.1:{tls.server_port}/stream', trust=False, extra=['--insecure', '--tls-info'], tls_expected='verification: disabled')
        record('custom-ca', f'https://localhost:{tls.server_port}/stream', trust=False, extra=[f'--tls-ca-file={cert}', '--tls-info'], tls_expected='verification: enabled')
        record('tls13', f'https://localhost:{tls.server_port}/stream', extra=['--tls-info'], tls_expected='TLSv1.3')
        record('ecdsa-pfs-tls12', f'https://localhost:{ec_tls.server_port}/stream', trusted_cert=ec_cert, extra=['--tls-info'], tls_expected='ECDHE-ECDSA-AES128-GCM-SHA256')
        ec_context.maximum_version = ssl.TLSVersion.TLSv1_3
        record('ecdsa-tls13', f'https://localhost:{ec_tls.server_port}/stream', trusted_cert=ec_cert, extra=['--tls-info'], tls_expected='ecdsa_secp256r1_sha256')
        record('chunked-https', f'https://localhost:{tls.server_port}/chunked')
        record('chunked-http', f'http://localhost:{plain.server_port}/chunked')
        record('http10', f'http://localhost:{plain.server_port}/http10', extra=['--http10'])
        record('long-headers-nonterminated-metadata', f'https://localhost:{tls.server_port}/long-headers', extra=['-s'])
        relay_test()
        record('redirect-loop', f'https://localhost:{tls.server_port}/loop', expected=False)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(lambda index: record(f'concurrent-{index}', f'https://localhost:{tls.server_port}/stream'), range(2)))
    finally:
        tls.shutdown()
        plain.shutdown()
        ec_tls.shutdown()
print('All tests passed.')
