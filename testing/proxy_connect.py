"""Local HTTP/HTTPS proxies, CONNECT framing, authentication, and nested TLS.
Usage: python testing/proxy_connect.py [toolchain-prefix] [--errors-only|--successes-only]
"""
from pathlib import Path
import base64
import concurrent.futures
import http.server
import os
import select
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

repo = Path(__file__).resolve().parents[1]
arguments = [arg for arg in sys.argv[1:] if arg not in ('--errors-only', '--successes-only')]
prefix = Path(arguments[0]).resolve() if arguments else repo / 'build' / 'toolchain' / 'mingw64'
exe = repo / 'dist' / 'streamripper.exe'
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()
audio = (audio * (16000 // len(audio) + 1))[:16000]
stop = threading.Event()
servers, origin_requests = [], []
proxy_auth = 'Basic ' + base64.b64encode(b'proxyuser:proxysecret').decode()
origin_auth = 'Basic ' + base64.b64encode(b'station:music').decode()


class Server6(http.server.ThreadingHTTPServer):
    address_family = socket.AF_INET6

    def server_bind(self):
        self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        super().server_bind()


def serve(handler, context=None, ipv6=False, **options):
    server = (Server6 if ipv6 else http.server.ThreadingHTTPServer)(('::1' if ipv6 else '127.0.0.1', 0), handler)
    server.daemon_threads = True
    for name, value in options.items():
        setattr(server, name, value)
    if context:
        server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    servers.append(server)
    return server


class Origin(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        origin_requests.append((self.path, dict(self.headers)))
        if self.path.startswith('/upgrade') or self.path.startswith('/downgrade') or self.path.startswith('/relative'):
            self.send_response(302)
            location = ('/stream' if self.path.startswith('/relative') else
                        f'https://radio.test.invalid:{tls_origin.server_port}/stream' if self.path.startswith('/upgrade') else
                        f'http://radio.test.invalid:{plain_origin.server_port}/stream')
            self.send_header('Location', location)
            self.send_header('Content-Length', '0')
            self.send_header('Connection', 'close')
            self.end_headers()
            return
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', 'Proxy transport test')
        self.send_header('icy-metaint', '16000')
        self.send_header('icy-br', '128')
        chunked = self.path.startswith('/chunked')
        if chunked:
            self.send_header('Transfer-Encoding', 'chunked')
        self.end_headers()
        try:
            index = 0
            while not stop.is_set():
                title = f"StreamTitle='Proxy Artist - Příliš {index // 24}';".encode()
                title += bytes(-len(title) % 16)
                payload = audio + bytes([len(title) // 16]) + title
                # Fragment both TLS layers and HTTP chunk boundaries independently.
                for offset in range(0, len(payload), 997):
                    part = payload[offset:offset + 997]
                    self.wfile.write(f'{len(part):x}\r\n'.encode() + part + b'\r\n' if chunked else part)
                self.wfile.flush()
                index += 1
                stop.wait(0.02)
        except OSError:
            pass


class Proxy(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def pump(self, upstream):
        self.connection.settimeout(3)
        upstream.settimeout(3)
        try:
            while not stop.is_set():
                ready, _, _ = select.select([self.connection, upstream], [], [], 0.1)
                if isinstance(self.connection, ssl.SSLSocket) and self.connection.pending() and self.connection not in ready:
                    ready.append(self.connection)
                for source in ready:
                    data = source.recv(32768)
                    if not data:
                        return
                    (upstream if source is self.connection else self.connection).sendall(data)
        except OSError:
            pass

    def connect_origin(self, authority):
        parsed = urllib.parse.urlsplit('//' + authority)
        host = '127.0.0.1' if parsed.hostname == 'radio.test.invalid' else parsed.hostname
        return socket.create_connection((host, parsed.port), timeout=3)

    def authenticated(self):
        if self.server.auth and self.headers.get('Proxy-Authorization') != proxy_auth:
            self.connection.sendall(b'HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n')
            return False
        return True

    def do_CONNECT(self):
        self.close_connection = True
        self.server.events.append(('CONNECT', self.path, dict(self.headers)))
        if not self.authenticated():
            return
        mode = self.server.mode
        faults = {
            '403': b'HTTP/1.1 403 Forbidden\r\n\r\n',
            '502': b'HTTP/1.1 502 Bad Gateway\r\n\r\n',
            'bad-status': b'HTTP/1.1 2000 invalid\r\n\r\n',
            'bad-status-cr': b'HTTP/1.1 200\rX-invalid\r\n\r\n',
            'truncated': b'HTTP/1.1 200 OK\r\nX: unfinished',
            'oversized': b'HTTP/1.1 200 OK\r\nX: ' + b'x' * 9000 + b'\r\n\r\n',
            'informational-loop': b'HTTP/1.1 100 Continue\r\n\r\n' * 9,
            'nul': b'HTTP/1.1 200 OK\r\nX: \0bad\r\n\r\n',
        }
        if mode == 'stall':
            stop.wait(3)
            return
        if mode in faults:
            try:
                self.connection.sendall(faults[mode])
            except OSError:
                pass
            return
        try:
            with self.connect_origin(self.path) as upstream:
                header = (b'HTTP/1.1 103 Early Hints\r\n\r\n' if mode == 'interim' else b'')
                header += (b'HTTP/1.0 201 Connected\r\n' if mode == '201' else b'HTTP/1.1 200 Connection established\r\n')
                if mode == 'body-headers':
                    header += b'Content-Length: 999999\r\nTransfer-Encoding: chunked\r\n'
                header += b'\r\n'
                for offset in range(0, len(header), 3):
                    self.connection.sendall(header[offset:offset + 3])
                self.pump(upstream)
        except OSError:
            pass

    def do_GET(self):
        self.close_connection = True
        self.server.events.append(('GET', self.path, dict(self.headers)))
        if not self.authenticated():
            return
        parsed = urllib.parse.urlsplit(self.path)
        try:
            with self.connect_origin(parsed.netloc) as upstream:
                path = parsed.path or '/'
                if parsed.query:
                    path += '?' + parsed.query
                request = f'GET {path} {self.request_version}\r\n'
                request += ''.join(f'{key}: {value}\r\n' for key, value in self.headers.items()
                                   if key.lower() not in ('proxy-authorization', 'proxy-connection', 'connection'))
                upstream.sendall((request + 'Connection: close\r\n\r\n').encode())
                self.pump(upstream)
        except OSError:
            pass


try:
    with tempfile.TemporaryDirectory(prefix='proxy-connect-', dir=repo / 'build') as temporary:
        root = Path(temporary)

        def certificate(name, san):
            cert, key = root / (name + '.pem'), root / (name + '-key.pem')
            subprocess.run([str(prefix / 'bin' / 'openssl.exe'), 'req', '-x509', '-newkey', 'rsa:2048',
                '-nodes', '-keyout', str(key), '-out', str(cert), '-days', '1', '-subj', '/CN=' + name,
                '-addext', 'subjectAltName=' + san], check=True, capture_output=True)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            return cert, context

        origin_cert, origin_context = certificate('origin', 'DNS:radio.test.invalid,DNS:localhost,IP:::1')
        proxy_cert, proxy_context = certificate('proxy', 'DNS:localhost,IP:::1')
        bundle = root / 'trusted.pem'
        bundle.write_bytes(origin_cert.read_bytes() + proxy_cert.read_bytes())
        tls_origin = serve(Origin, origin_context)
        plain_origin = serve(Origin)
        tls_origin6 = serve(Origin, origin_context, ipv6=True)

        def proxy(context=None, ipv6=False, auth=False, mode='normal'):
            return serve(Proxy, context, ipv6=ipv6, auth=auth, mode=mode, events=[])

        http_proxy = proxy()
        https_proxy = proxy(proxy_context)
        auth_proxy = proxy(auth=True)
        auth_https_proxy = proxy(proxy_context, auth=True)
        proxy6 = proxy(proxy_context, ipv6=True)
        config = root / 'config'
        config.mkdir()
        environment = dict(os.environ, XDG_CONFIG_HOME=str(config), APPDATA=str(config), LOCALAPPDATA=str(config))
        environment.pop('SSL_CERT_FILE', None)
        environment.pop('SSL_CERT_DIR', None)

        def record(name, server=http_proxy, secure=False, path='/stream', scheme='https',
                   host='radio.test.invalid', port=None, extra=(), expected=None, ca=bundle, auth=False,
                   origin_credentials=False, proxy_host_override=None):
            if port is None:
                port = tls_origin.server_port if scheme == 'https' else plain_origin.server_port
            authority = f'[{host}]' if ':' in host else host
            dest = root / name
            proxy_host = '[::1]' if server.address_family == socket.AF_INET6 else 'localhost'
            if proxy_host_override:
                proxy_host = proxy_host_override
            proxy_url = ('https' if secure else 'http') + '://'
            proxy_url += ('proxyuser:proxysecret@' if auth else '') + f'{proxy_host}:{server.server_port}'
            url = f'{scheme}://' + ('station:music@' if origin_credentials else '') + f'{authority}:{port}{path}'
            result = subprocess.run([str(exe), url, '-c', '-l', '2', '-m', '2', '-k', '0', '-s',
                '-d', str(dest), '--xs-none', '--tls-ca-file=' + str(ca), '--tls-info', '-p', proxy_url] + list(extra),
                capture_output=True, timeout=10, env=environment)
            output = result.stdout + result.stderr
            files = list(dest.rglob('*.mp3'))
            if expected is None:
                assert result.returncode == 0 and b'Proxy Artist - P\xc5\x99\xc3\xadli\xc5\xa1' in output, (name, output[-2200:])
                assert any(f.stat().st_size > 16000 and 'Příliš' in f.name for f in files), (name, files)
                if secure:
                    assert b'TLS proxy:' in output, (name, output)
                if scheme == 'https' and path != '/downgrade':
                    assert b'TLS:' in output, (name, output)
            else:
                assert expected in output and not files, (name, output[-2200:], files)
            print('PASS:', name, flush=True)

        if '--errors-only' not in sys.argv:
            record('https-via-http')
            record('https-via-https', https_proxy, secure=True)
            record('http-via-http', scheme='http')
            record('http-via-https', https_proxy, secure=True, scheme='http')
            record('auth-connect', auth_proxy, auth=True)
            record('auth-nested-connect', auth_https_proxy, secure=True, auth=True)
            record('separate-origin-auth', auth_https_proxy, secure=True, auth=True, origin_credentials=True)
            assert any(headers.get('Authorization') == origin_auth for _, headers in origin_requests)
            record('insecure-both', https_proxy, secure=True, host='127.0.0.1',
                   proxy_host_override='127.0.0.1', extra=['--insecure'])
            record('nested-chunked', https_proxy, secure=True, path='/chunked', extra=['-4'])
            record('nested-http10', https_proxy, secure=True, extra=['--http10'])
            record('proxy-ipv6', proxy6, secure=True, extra=['-6'])
            record('origin-ipv6', host='::1', port=tls_origin6.server_port, extra=['-4'])
            record('relative-redirect', https_proxy, secure=True, path='/relative')
            record('upgrade-redirect', https_proxy, secure=True, scheme='http', path='/upgrade')
            record('downgrade-redirect', https_proxy, secure=True, path='/downgrade')
            for mode in ('interim', '201', 'body-headers'):
                record('connect-' + mode, proxy(mode=mode))
            native_environment = dict(environment)
            native_environment['PATH'] = str(prefix / 'bin') + os.pathsep + environment.get('PATH', '')
            for endpoint in (f'http://localhost:{http_proxy.server_port}', f'https://localhost:{https_proxy.server_port}'):
                subprocess.run([str(repo / 'build' / 'core_regression.exe'), '--proxy-connect-cycles',
                    f'https://radio.test.invalid:{tls_origin.server_port}/stream', endpoint, str(bundle)],
                    env=native_environment, check=True, timeout=30)

        if '--successes-only' not in sys.argv:
            record('missing-proxy-auth', auth_proxy, expected=b'HTTP:407')
            for mode in ('403', '502', 'bad-status', 'bad-status-cr', 'truncated', 'oversized', 'informational-loop', 'nul', 'stall'):
                record('connect-error-' + mode, proxy(mode=mode), expected=b'error')
            record('untrusted-proxy', https_proxy, secure=True, ca=origin_cert, expected=b'TLS proxy error')
            record('untrusted-origin', https_proxy, secure=True, ca=proxy_cert, expected=b'TLS error')
            record('proxy-ip-mismatch', https_proxy, secure=True, proxy_host_override='127.0.0.1', expected=b'TLS proxy error')
            record('origin-ip-mismatch', host='127.0.0.1', expected=b'TLS error')
        assert all('Proxy-Authorization' not in headers for _, headers in origin_requests)
        assert all(event[2].get('Authorization') is None for server in servers if hasattr(server, 'events')
                   for event in server.events if event[0] == 'CONNECT')
        assert all(event[1] == event[2]['Host'] for server in servers if hasattr(server, 'events')
                   for event in server.events if event[0] == 'CONNECT')
        print('PASS: proxy credentials stay at the proxy; CONNECT authority/Host match', flush=True)
finally:
    stop.set()
    for server in servers:
        server.shutdown()
        server.server_close()
