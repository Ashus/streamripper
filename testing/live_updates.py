"""Cross-platform metadata rules reload integration checks.
Usage: python testing/live_updates.py /path/to/streamripper
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
exe = Path(sys.argv[1]).resolve()
audio = (repo / "build/sample-5s.mp3").read_bytes()
raw = "Artist - Original"
split = r"m/^(.+?) - (.*)$/A1T2" + "\n"

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
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
    def __init__(self, path, dest, options=(), duration=32):
        self.process = subprocess.Popen([str(exe), f"http://127.0.0.1:{server.server_port}{path}",
            "-s", "-d", str(dest), "-k", "0", "--xs-none", "--timeout=2",
            "-l", str(duration), *options], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        for reader in self.readers:
            reader.join(timeout=2)

try:
    with tempfile.TemporaryDirectory(prefix="live-updates-", dir=repo / "build" if os.name == "nt" else None) as tmp:
        root = Path(tmp)
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

finally:
    server.shutdown()
    server.server_close()
