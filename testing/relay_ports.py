"""Check relay port ownership, scanning, explicit addresses, and actual routing."""
from pathlib import Path
import contextlib
import http.server
import queue
import socket
import subprocess
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
exe = repo / "dist" / "streamripper.exe"
audio = (repo / "build" / "sample-5s.mp3").read_bytes()
readers = {}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("icy-name", self.path.lstrip("/"))
        self.send_header("icy-br", "128")
        self.end_headers()
        try:
            while True:
                self.wfile.write(audio)
                self.wfile.flush()
                time.sleep(0.02)
        except OSError:
            pass


def stop(process):
    if process.poll() is None:
        process.kill()
    reader = readers.get(process)
    if reader:
        process.wait(timeout=8)
        reader.join(timeout=3)
        assert not reader.is_alive()
        process.stdout.close()
        process.stderr.close()
    else:
        process.communicate(timeout=8)


def find_ports():
    for port in range(14000, 17000, 4):
        with contextlib.ExitStack() as probes:
            try:
                for offset in range(3):
                    probe = probes.enter_context(socket.socket(socket.AF_INET6))
                    probe.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
                    if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                        probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                    probe.bind(("::", port + offset))
                return port
            except OSError:
                pass
    raise AssertionError("No free relay test ports")


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix="relay-ports-", dir=repo / "build") as temporary, contextlib.ExitStack() as children:
        root = Path(temporary)
        port = find_ports()

        def launch(name, binding, no_scan=False):
            args = [str(exe), f"http://127.0.0.1:{server.server_port}/{name}",
                    "-c", "-l", "30", "-s", "-d", str(root / name), "-A", "-a", "show",
                    "--relay=" + binding, "-R", "0", "--xs-none"]
            if no_scan:
                args.append("-z")
            process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            children.callback(stop, process)
            return process

        def summary(process):
            lines = queue.Queue()

            def read_lines():
                for line in process.stdout:
                    lines.put(line.strip())

            readers[process] = threading.Thread(target=read_lines, daemon=True)
            readers[process].start()
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                try:
                    line = lines.get(timeout=0.1)
                except queue.Empty:
                    assert process.poll() is None, process.stderr.read()
                    continue
                if line.startswith((b"relay port:", b"relay:")):
                    return line
            raise AssertionError("No relay listener summary")

        def station(host, bound_port):
            with socket.create_connection((host, bound_port), timeout=3) as client:
                client.sendall(b"GET / HTTP/1.0\r\nHost: localhost\r\n\r\n")
                with client.makefile("rb") as response:
                    assert b"200" in response.readline()
                    fields = {}
                    while True:
                        line = response.readline()
                        assert line
                        if line == b"\r\n":
                            break
                        name, value = line.split(b":", 1)
                        fields[name.lower()] = value.strip()
                    return fields[b"icy-name"]

        first = launch("First", str(port))
        assert summary(first) == f"relay port: {port}".encode()
        second = launch("Second", str(port))
        assert summary(second) == f"relay port: {port + 1}".encode()
        for host in ("127.0.0.1", "::1"):
            assert station(host, port) == b"First"
            assert station(host, port + 1) == b"Second"
        for binding in (str(port), f"127.0.0.1:{port}", f"[::1]:{port}"):
            rejected = launch("Blocked" + str(len(list(root.iterdir()))), binding, no_scan=True)
            out, err = rejected.communicate(timeout=6)
            assert b"SR_ERROR_CANT_BIND_ON_PORT" in err, (out, err)
            assert b"relay port:" not in out and b"relay:" not in out, out
        print("PASS: overlapping wildcard/IPv4/IPv6 binds rejected; scanning selects next port; both families route to the correct station")

        explicit4 = launch("Explicit4", f"127.0.0.1:{port + 2}", no_scan=True)
        assert summary(explicit4) == f"relay: 127.0.0.1:{port + 2}".encode()
        explicit6 = launch("Explicit6", f"[::1]:{port + 2}", no_scan=True)
        assert summary(explicit6) == f"relay: [::1]:{port + 2}".encode()
        assert station("127.0.0.1", port + 2) == b"Explicit4"
        assert station("::1", port + 2) == b"Explicit6"
        print("PASS: explicit bind summaries and independent IPv4/IPv6-only listeners")
finally:
    server.shutdown()
    server.server_close()
