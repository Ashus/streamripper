"""Close a real Windows console and verify final incomplete retention."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import http.server
import os
import subprocess
import sys
import tempfile
import threading
import time

if len(sys.argv) == 3 and sys.argv[1] == "--close":
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    user = ctypes.WinDLL("user32", use_last_error=True)
    kernel.GetConsoleWindow.restype = wintypes.HWND
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    kernel.FreeConsole()
    assert kernel.AttachConsole(int(sys.argv[2])), ctypes.get_last_error()
    window = kernel.GetConsoleWindow()
    assert window
    kernel.FreeConsole()
    assert user.PostMessageW(window, 0x0010, 0, 0), ctypes.get_last_error()
    sys.exit(0)

root = Path(__file__).resolve().parents[1]
audio = (root / "build/sample-5s.mp3").read_bytes()
aac = bytes([255, 241, 80, 128, 13, 127, 252]) + bytes(100)

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "audio/aac" if self.path == "/aac" else "audio/mpeg")
        self.send_header("icy-metaint", "16384")
        self.end_headers()
        source = aac * 200 if self.path == "/aac" else audio
        offset = 0
        meta = b"StreamTitle='Artist - Closing test';"
        meta += bytes(-len(meta) % 16)
        try:
            for _ in range(1000):
                block = (source * (16384 // len(source) + 2))[offset:offset + 16384]
                offset = (offset + 16384) % len(source)
                self.wfile.write(block + bytes([len(meta) // 16]) + meta)
                self.wfile.flush()
                time.sleep(.02)
        except OSError:
            pass

server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
env = os.environ.copy()
env["PATH"] = str(root / "build/toolchain/mingw64/bin") + os.pathsep + env["PATH"]
try:
    with tempfile.TemporaryDirectory(prefix="console-close-", dir=root / "build") as tmp:
        for kind, minimum in [("mp3", "15m"), ("aac", "15m"), ("wav", "15m"), ("mp3", "0")]:
            dest = Path(tmp) / (kind + minimum)
            incomplete = dest / "incomplete"
            incomplete.mkdir(parents=True)
            old = incomplete / "existing.mp3"
            old.write_bytes(b"Existing user file")
            args = [str(root / "build/streamripper.exe"),
                f"http://127.0.0.1:{server.server_port}/" + ("aac" if kind == "aac" else "mp3"),
                "-c", "-s", "-d", str(dest), "-k", "0", "--xs-none", "--timeout=8",
                "--metadata-log", "--incomplete-min-duration=" + minimum]
            if kind == "wav":
                args.append("--wav")
            proc = subprocess.Popen(args, env=env, startupinfo=startup,
                creationflags=subprocess.CREATE_NEW_CONSOLE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 8
                while True:
                    files = [p for p in incomplete.glob("*." + kind) if p != old]
                    if files and files[0].stat().st_size >= 65536:
                        break
                    assert proc.poll() is None, proc.communicate()
                    assert time.monotonic() < deadline, "Recording did not start"
                    time.sleep(.02)
                started = time.monotonic()
                closed = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--close", str(proc.pid)],
                    creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True, timeout=3)
                assert closed.returncode == 0, closed.stderr
                stdout, stderr = proc.communicate(timeout=5)
                assert time.monotonic() - started < 5, "Shutdown exceeded console close timeout"
                files = [p for p in incomplete.glob("*." + kind) if p != old]
                assert old.read_bytes() == b"Existing user file"
                assert (incomplete / "metadata.log").is_file()
                assert not (incomplete / ".lock").exists()
                if minimum == "0":
                    assert len(files) == 1, files
                    assert files[0].read_bytes().startswith(b"ID3")
                else:
                    assert not files, (files, stdout, stderr)
                    assert b"Discarded incomplete recording" in stderr, stderr
                print("PASS: console close:", kind, "retained" if minimum == "0" else "short incomplete removed")
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.communicate()
finally:
    server.shutdown()
    server.server_close()
