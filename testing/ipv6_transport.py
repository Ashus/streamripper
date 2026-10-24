"""Exercise IPv6 HTTP/TLS, DNS fallback, proxy requests, and relay listeners."""
from pathlib import Path
import concurrent.futures
import http.server
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
exe = repo / 'dist' / 'streamripper.exe'
prefix = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else repo / 'build' / 'toolchain' / 'mingw64'
openssl = prefix / 'bin' / 'openssl.exe'
sample = (repo / 'build' / 'sample-5s.mp3').read_bytes()
audio = (sample * (16000 // len(sample) + 1))[:16000]
title = b"StreamTitle='IPv6 Artist - IPv6 Track';"
metadata = title + bytes(-len(title) % 16)
stop = threading.Event()
requests = []
servers = []


class Server6(http.server.ThreadingHTTPServer):
    address_family = socket.AF_INET6

    def server_bind(self):
        self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        super().server_bind()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        requests.append((self.path, self.headers.get('Host')))
        if self.path == '/relative':
            self.send_response(302)
            self.send_header('Location', '/stream?redirect=yes')
            self.end_headers()
            return
        if self.path == '/ipv6':
            self.send_response(302)
            self.send_header('Location', f'http://[::1]:{plain6.server_port}/stream')
            self.end_headers()
            return
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', 'IPv6 transport test')
        self.send_header('icy-metaint', '16000')
        self.send_header('icy-br', '128')
        self.end_headers()
        try:
            while not stop.is_set():
                self.wfile.write(audio + bytes([len(metadata) // 16]) + metadata)
                self.wfile.flush()
                stop.wait(0.02)
        except OSError:
            pass


def serve(family, context=None):
    server = (Server6 if family == socket.AF_INET6 else http.server.ThreadingHTTPServer)(
        ('::1' if family == socket.AF_INET6 else '127.0.0.1', 0), Handler)
    if context:
        server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    servers.append(server)
    return server


try:
    # IPv6 is required for this check: fail rather than silently skip coverage.
    plain6, plain4 = serve(socket.AF_INET6), serve(socket.AF_INET)
    with tempfile.TemporaryDirectory(prefix='ipv6-', dir=repo / 'build') as temporary:
        root = Path(temporary)
        cert, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run([str(openssl), 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
            '-keyout', str(key), '-out', str(cert), '-days', '1', '-subj', '/CN=localhost',
            '-addext', 'subjectAltName=DNS:localhost,IP:::1'], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        tls6 = serve(socket.AF_INET6, context)
        wrong_cert, wrong_key = root / 'wrong.pem', root / 'wrong-key.pem'
        subprocess.run([str(openssl), 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
            '-keyout', str(wrong_key), '-out', str(wrong_cert), '-days', '1',
            '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
            check=True, capture_output=True)
        wrong_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        wrong_context.load_cert_chain(wrong_cert, wrong_key)
        wrong_tls6 = serve(socket.AF_INET6, wrong_context)
        config = root / 'config'
        config.mkdir()
        environment = dict(os.environ, XDG_CONFIG_HOME=str(config),
                           APPDATA=str(config), LOCALAPPDATA=str(config))
        environment.pop('SSL_CERT_FILE', None)
        environment.pop('SSL_CERT_DIR', None)
        environment['PATH'] = str(openssl.parent) + os.pathsep + environment.get('PATH', '')
        subprocess.run([str(repo / 'build' / 'core_regression.exe'), '--ipv6-requests'],
                       check=True, env=environment)
        subprocess.run([str(repo / 'build' / 'core_regression.exe'), '--ipv6-connect-cycles',
                        str(plain4.server_port), str(plain6.server_port)],
                       check=True, env=environment, timeout=30)

        def record(name, url, extra=(), expected=True):
            dest = root / name
            result = subprocess.run([str(exe), url, '-c', '-l', '2', '-k', '0', '-s',
                    '-d', str(dest), '--xs-none', '-m', '2'] + list(extra),
                    capture_output=True, timeout=10, env=environment)
            output = result.stdout + result.stderr
            files = list(dest.rglob('*.mp3'))
            if expected:
                assert result.returncode == 0 and b'IPv6 Artist - IPv6 Track' in output, (name, output[-2000:])
                assert any(f.stem == 'IPv6 Artist - IPv6 Track' and f.stat().st_size > 16000 for f in files), (name, files)
            else:
                assert b'TLS error' in output and not files, (name, output)
            print('PASS:', name, flush=True)

        record('ipv6-literal', f'http://[::1]:{plain6.server_port}/stream?name=a%26b')
        assert ('/stream?name=a%26b', f'[::1]:{plain6.server_port}') in requests
        record('ipv6-relative-redirect', f'http://[::1]:{plain6.server_port}/relative')
        assert ('/stream?redirect=yes', f'[::1]:{plain6.server_port}') in requests
        record('ipv4-to-ipv6-redirect', f'http://127.0.0.1:{plain4.server_port}/ipv6')
        record('localhost-ipv6-only', f'http://localhost:{plain6.server_port}/stream')
        record('localhost-ipv4-fallback', f'http://localhost:{plain4.server_port}/stream')
        record('ipv6-http-proxy', 'http://[::1]:12345/proxy',
               ['--proxy', f'http://[::1]:{plain6.server_port}'])
        assert ('http://[::1]:12345/proxy', '[::1]:12345') in requests
        record('ipv6-tls-ip-san', f'https://[::1]:{tls6.server_port}/stream',
               ['--tls-ca-file=' + str(cert), '--tls-info'])
        record('ipv6-tls-dns-san', f'https://localhost:{tls6.server_port}/stream',
               ['--tls-ca-file=' + str(cert)])
        record('ipv6-tls-untrusted', f'https://[::1]:{tls6.server_port}/stream', expected=False)
        record('ipv6-tls-ip-mismatch', f'https://[::1]:{wrong_tls6.server_port}/stream',
               ['--tls-ca-file=' + str(wrong_cert)], expected=False)

        for value in ('[::1', '[]:8000', '[::1]extra', '::1:8000', '[::1]:0',
                      '[::1]:65536', '[::1]:bad', '127.0.0.1:8000:1'):
            result = subprocess.run([str(exe), 'http://localhost/', '--relay=' + value, '--help'],
                                    capture_output=True, timeout=3, env=environment)
            assert result.returncode != 0 and b'Invalid relay address' in result.stderr, value
        for value in ('[::1]', '[::1]:8000', '127.0.0.1:8000', '8000'):
            result = subprocess.run([str(exe), 'http://localhost/', '--relay=' + value, '--help'],
                                    capture_output=True, timeout=3, env=environment)
            assert result.returncode == 0, (value, result.stderr)
        print('PASS: relay address parsing', flush=True)

        def relay_client(host, port, with_metadata):
            deadline = time.monotonic() + 5
            while True:
                try:
                    sock = socket.create_connection((host, port), timeout=1)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.02)
            with sock:
                sock.settimeout(3)
                sock.sendall(f'GET / HTTP/1.0\r\nHost: localhost\r\nIcy-Metadata: {int(with_metadata)}\r\n\r\n'.encode())
                with sock.makefile('rb') as reader:
                    assert b'200' in reader.readline()
                    headers = {}
                    while True:
                        line = reader.readline()
                        if line == b'\r\n':
                            break
                        assert line, 'Relay closed while sending headers'
                        field, value = line.split(b':', 1)
                        headers[field.lower()] = value.strip()
                    assert (headers.get(b'icy-metaint') == b'16000') == with_metadata
                    for _ in range(12):
                        assert reader.read(16000) == audio, (host, 'Relay audio mismatch')
                        if with_metadata:
                            length = reader.read(1)
                            assert length
                            assert title in reader.read(length[0] * 16)

        for name, binding, hosts in [('ipv6-relay', '[::1]', ['::1']),
                                     ('ipv4-relay', '127.0.0.1', ['127.0.0.1']),
                                     ('dual-stack-relay', None, ['::1', '127.0.0.1'])]:
            with socket.socket(socket.AF_INET6 if binding != '127.0.0.1' else socket.AF_INET) as probe:
                probe.bind(('::1' if binding != '127.0.0.1' else '127.0.0.1', 0))
                port = probe.getsockname()[1]
            process = subprocess.Popen([str(exe), f'http://[::1]:{plain6.server_port}/stream',
                '-c', '-l', '6', '-A', '-a', 'show', '-s', '-d', str(root / name),
                '--relay=' + (f'{binding}:{port}' if binding else str(port)),
                '-R', '0', '-z', '--xs-none', '--quiet'],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
            try:
                with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                    futures = [pool.submit(relay_client, host, port, metadata)
                               for host in hosts for metadata in (False, True)]
                    for future in futures:
                        future.result()
                out, err = process.communicate(timeout=10)
                assert process.returncode == 0, (out[-2000:], err[-2000:])
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
            print('PASS:', name, 'audio and ICY', flush=True)
finally:
    stop.set()
    for server in servers:
        server.shutdown()
        server.server_close()
