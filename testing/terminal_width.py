"""Exercise dynamic POSIX terminal width through a real pseudo-terminal."""
from pathlib import Path
import fcntl
import http.server
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time
import unicodedata

repo = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
audio = (repo / "build/sample-5s.mp3").read_bytes()
title = "Artist - " + "A" * 55 + "Příliš-e\u0301界🎵-ENDMARK"

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("icy-metaint", "16384")
        self.end_headers()
        try:
            for i in range(1000):
                meta = ("StreamTitle='" + title + "';").encode() if i == 0 else b""
                meta += bytes(-len(meta) % 16)
                block = (audio * (16384 // len(audio) + 2))[:16384]
                self.wfile.write(block + bytes([len(meta)//16]) + meta)
                self.wfile.flush()
                time.sleep(.03)
        except OSError: pass

server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
master, slave = pty.openpty()
def resize(width):
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, width, 0, 0))

def frame_until(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        if select.select([master], [], [], .2)[0]:
            data.extend(os.read(master, 65536))
        for frame in re.findall(rb"\r(\[[^\r\n]*?)\x1b\[K\r", data):
            text = frame.decode("utf-8")
            if predicate(text):
                return text
    raise AssertionError(bytes(data)[-3000:])

def columns(text):
    return sum(0 if unicodedata.combining(c) or unicodedata.category(c) == "Cf" else
               2 if unicodedata.east_asian_width(c) in ("W", "F") else 1 for c in text)

try:
    with tempfile.TemporaryDirectory(prefix="terminal-width-") as tmp:
        args = [str(exe), f"http://127.0.0.1:{server.server_port}/", "-c", "-s",
                "-k", "0", "--xs-none", "-d", tmp]
        for destination in ("pipe", "file"):
            redirected_args = args[:-1] + [str(Path(tmp) / destination), "-l", "1"]
            with tempfile.TemporaryFile() as capture:
                result = subprocess.run(redirected_args,
                    stdout=subprocess.PIPE if destination == "pipe" else capture,
                    stderr=subprocess.PIPE, timeout=10, check=True)
                capture.seek(0)
                output = result.stdout if destination == "pipe" else capture.read()
            frames = [frame for frame in output.split(b"\r") if frame.startswith(b"[ripping ")]
            assert frames and all(title in frame.decode("utf-8") for frame in frames), output
            assert b"\x1b[K" not in output
        print("PASS: complete Unicode titles and CR status frames in pipes and files")
        resize(120)
        process = subprocess.Popen(args, stdout=slave, stderr=subprocess.PIPE)
        try:
            wide = frame_until(lambda text: "ENDMARK" in text)
            assert columns(wide) <= 119
            resize(60)
            narrow = frame_until(lambda text: "ripping" in text and "ENDMARK" not in text
                                 and columns(text) <= 59 and re.search(r"\[\s*\d{2}:\d{2}\]$", text))
            assert columns(narrow) <= 59
            resize(120)
            assert "Příliš-e\u0301界🎵-ENDMARK" in frame_until(lambda text: "ENDMARK" in text)
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=5)
            assert process.returncode == 0
            print("PASS: PTY resize 120/60/120 columns, titles beyond 50 characters, combining/wide Unicode and row erasure")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
finally:
    os.close(master)
    os.close(slave)
    server.shutdown()
    server.server_close()
