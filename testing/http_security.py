"""Exercise historical HTTP CVE inputs through the real playlist/socket path."""
from pathlib import Path
import http.server
import subprocess
import sys
import threading

exe = Path(sys.argv[1]).resolve()
subprocess.run([str(exe)], check=True)
payload = b""
redirects = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path.startswith(("/header/", "/embedded/")):
            size = int(self.path.rsplit("/", 1)[1])
            prefix = b"HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\nX-Long: "
            end = b"\n\0\r\n" if self.path.startswith("/embedded/") else b"\r\n\r\n"
            response = prefix + b"A" * (size - len(prefix) - len(end)) + end
            assert len(response) == size
            self.close_connection = True
            try:
                self.wfile.write(response)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if self.path.startswith("/sink/"):
            redirects.append(self.path)
            content = b""
            mime = "audio/mpeg"
        else:
            content = payload
            mime = "audio/x-scpls" if self.path.endswith(".pls") else "audio/x-mpegurl"
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(content)
        except (BrokenPipeError, ConnectionResetError):
            pass


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
base = f"http://127.0.0.1:{server.server_port}"
try:
    for terminator in ("header", "embedded"):
        for size in (128, 8190, 8191, 8192, 20000):
            result = subprocess.run([str(exe), "--connect", f"{base}/{terminator}/{size}"],
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == (0 if size < 8192 else 1), (size, result)
            assert "HTTP result:" in result.stdout, (size, result)
            assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr, result.stderr
    print("PASS: exact network header limits, embedded-NUL terminator and oversized response rejection")
    for kind in ("pls", "m3u"):
        for size in (0, 510, 511, 512, 1023, 4096, 8200, 20000):
            target = base + "/sink/" + "A" * size
            text = (f"[playlist]\nFile1={target}\nTitle1=(#1 - 1/100) Test\n"
                    if kind == "pls" else f"#EXTM3U\n{target}\n")
            payload = text.encode("ascii")
            redirects.clear()
            result = subprocess.run([str(exe), "--connect", f"{base}/case.{kind}"],
                                    capture_output=True, text=True, timeout=10)
            accepted = len(payload) < 8192
            assert result.returncode == (0 if accepted else 1), (kind, size, result)
            assert "HTTP result:" in result.stdout, (kind, size, result)
            assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr, result.stderr
            if accepted:
                assert len(redirects) == 1, (kind, size, redirects, result)
                assert len(base + redirects[0]) <= 511, (kind, size, redirects)
            else:
                assert not redirects, (kind, size, redirects)
    for size in (512, 4096, 20000):
        payload = f"[playlist]\nFile1={base}/sink/\nTitle1={('A' * size)}\n".encode("ascii")
        redirects.clear()
        result = subprocess.run([str(exe), "--connect", f"{base}/case.pls"],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == (0 if len(payload) < 8192 else 1), (size, result)
        assert "HTTP result:" in result.stdout, (size, result)
        assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr, result.stderr
    print("PASS: PLS/M3U long entries/titles, bounded redirects and rejection of oversized playlists")
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)
