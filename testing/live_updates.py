"""Cross-platform rules reload and transient reconnect integration checks.
Usage: python testing/live_updates.py /path/to/streamripper [--rules-only|--reconnect-only]
"""
from pathlib import Path
import http.server
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
audio = (repo / "build/sample-5s.mp3").read_bytes()
requests = {}
raw = "Artist - Original"
split = r"m/^(.+?) - (.*)$/A1T2" + "\n"

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        seen = requests.setdefault(self.path, [])
        seen.append(time.monotonic())
        number = len(seen)
        if self.path == "/initial" or (self.path == "/recover" and number == 3):
            self.send_response(503)
            self.end_headers()
            return
        if self.path == "/recover" and number == 2:
            self.send_response(302)
            self.send_header("Location", "http://missing-streamripper-test.invalid/radio")
            self.end_headers()
            return
        if (self.path == "/recover" and number == 4) or (self.path == "/cancel" and number > 1):
            self.send_response(429)
            self.send_header("Retry-After", "7" if self.path == "/recover" else "20")
            self.end_headers()
            return
        if self.path == "/fatal" and number > 1:
            self.send_response(401)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("icy-metaint", "16384")
        self.end_headers()
        try:
            for i in range(10000):
                block = (audio * (16384 // len(audio) + 2))[:16384]
                meta = ("StreamTitle='" + raw + "';").encode() if i == 0 else b""
                meta += bytes(-len(meta) % 16)
                self.wfile.write(block + bytes([len(meta) // 16]) + meta)
                self.wfile.flush()
                if self.path != "/rules" and number == 1 and i >= 30:
                    return
                time.sleep(.03)
        except OSError:
            pass

server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
server.daemon_threads = True
threading.Thread(target=server.serve_forever, daemon=True).start()

def wait_for(predicate, seconds=9):
    deadline = time.monotonic() + seconds
    while not predicate():
        assert time.monotonic() < deadline, "Timed out waiting for expected state"
        time.sleep(.05)

class Recording:
    def __init__(self, path, dest, options=(), duration=32, console=False):
        kwargs = {}
        if console and os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            kwargs = dict(startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.process = subprocess.Popen([str(exe), f"http://127.0.0.1:{server.server_port}{path}",
            "-s", "-d", str(dest), "-k", "0", "--xs-none", "--timeout=2",
            "-l", str(duration), *options], stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
        self.out, self.err = bytearray(), bytearray()
        self.readers = []
        for stream, target in [(self.process.stdout, self.out), (self.process.stderr, self.err)]:
            def read(pipe=stream, result=target):
                while chunk := pipe.read(1):
                    result.extend(chunk)
            reader = threading.Thread(target=read, daemon=True)
            reader.start()
            self.readers.append(reader)

    def finish(self, timeout=10):
        self.process.wait(timeout=timeout)
        for reader in self.readers:
            reader.join(timeout=2)
        assert self.process.returncode == 0, bytes(self.err)

    def close(self):
        if self.process.poll() is None:
            if os.name == "nt":
                result = subprocess.run([sys.executable, str(repo / "testing/console_close.py"),
                    "--close", str(self.process.pid)], creationflags=subprocess.CREATE_NO_WINDOW,
                    capture_output=True, timeout=4)
                assert result.returncode == 0, result.stderr
            else:
                self.process.send_signal(signal.SIGTERM)
            self.finish(4)

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        for reader in self.readers:
            reader.join(timeout=2)

try:
    with tempfile.TemporaryDirectory(prefix="live-updates-", dir=repo / "build" if os.name == "nt" else None) as tmp:
        root = Path(tmp)
        if "--reconnect-only" not in sys.argv:
            rulefile = root / "rules.txt"
            def change(text):
                pending = rulefile.with_suffix(".new")
                pending.write_text(text, encoding="utf-8")
                pending.replace(rulefile)
            change("s/Original/First/\n" + split)
            rec = Recording("/rules", root / "rules", ["-c", "-w", str(rulefile), "--reload-rules", "--metadata-log"])
            try:
                wait_for(lambda: b"Artist - First" in rec.out)
                change("s/Original/Second/\n" + split)
                wait_for(lambda: b"Artist - Second" in rec.out)
                change("s/Original/Bad/\nm/[/x\n" + split)
                wait_for(lambda: b"Invalid metadata rules" in rec.err)
                assert b"Artist - Bad" not in rec.out
                rulefile.unlink()
                wait_for(lambda: b"Metadata rules unavailable" in rec.err)
                change("m/Original/x\ns/Original/Excluded/\n" + split)
                wait_for(lambda: b"filtered...   ] Artist - Excluded" in rec.out)
                change("s/Original/Third/\n" + split)
                wait_for(lambda: b"Artist - Third" in rec.out)
                rec.finish(12)
                entries = (root / "rules/incomplete/metadata.log").read_text(encoding="utf-8").splitlines()
                assert len(entries) == 1 and entries[0].endswith("\t" + raw), entries
                assert rec.err.count(b"Metadata rules reloaded.") == 3, rec.err
                assert not (root / "rules/incomplete/.lock").exists()
                print("PASS: 5-second atomic reload, invalid/missing retention, unchanged ICY reparsing, filter transitions and log deduplication", flush=True)
            finally:
                rec.kill()
            bad = subprocess.run([str(exe), "http://localhost/", "--reload-rules"],
                                 capture_output=True, timeout=5)
            assert bad.returncode != 0
        if "--rules-only" not in sys.argv:
            rec = Recording("/recover", root / "recover", duration=28)
            try:
                wait_for(lambda: len(requests.get("/recover", [])) >= 5, 27)
                rec.finish(10)
                times = requests["/recover"]
                assert all(times[i + 1] - times[i] >= 4.8 for i in range(3)), times
                assert times[4] - times[3] >= 6.8, times
                assert b"SR_ERROR_CANT_RESOLVE_HOSTNAME" in rec.err, rec.err
                assert b"Retrying in 7 s" in rec.err, rec.err
                assert not (root / "recover/incomplete/.lock").exists()
                print("PASS: dropped stream, DNS failure, HTTP 503/429, Retry-After and recovery", flush=True)
            finally:
                rec.kill()
            for path, options, count in [("/initial", [], 1), ("/disabled", ["-c"], 1), ("/fatal", [], 2)]:
                rec = Recording(path, root / path[1:], options, duration=12)
                try:
                    rec.finish(10)
                    assert len(requests[path]) == count, (path, requests[path], rec.err)
                    print("PASS:", path, "stops without inappropriate retries", flush=True)
                finally:
                    rec.kill()
            rec = Recording("/cancel", root / "cancel", duration=40, console=True)
            try:
                wait_for(lambda: b"Retrying in 20 s" in rec.err, 10)
                started = time.monotonic()
                rec.close()
                assert time.monotonic() - started < 3
                assert not (root / "cancel/incomplete/.lock").exists()
                print("PASS: interruptible retry wait and lock cleanup", flush=True)
            finally:
                rec.kill()
finally:
    server.shutdown()
    server.server_close()
