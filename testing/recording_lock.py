"""Check recording directory ownership, races, reconnects and stale lock files."""
from pathlib import Path
import concurrent.futures
import contextlib
import http.server
import os
import subprocess
import tempfile
import threading
import time

repo = Path(__file__).resolve().parents[1]
exe = repo / "dist" / "streamripper.exe"
audio = (repo / "build" / "sample-5s.mp3").read_bytes()
requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        requests.append(self.path)
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("icy-name", "Lock test")
        self.send_header("icy-br", "128")
        self.end_headers()
        try:
            for index in range(96 if self.path == "/reconnect" else 3000):
                offset = (index * 16000) % len(audio)
                self.wfile.write((audio * 2)[offset:offset + 16000])
                self.wfile.flush()
                time.sleep(0.01)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
processes = []


def start(dest, extra=(), endpoint="/stream", seconds=20):
    args = [str(exe), f"http://localhost:{server.server_port}{endpoint}",
            "-l", str(seconds), "-s", "-d", str(dest), "--xs-none", "--quiet"]
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
               APPDATA=str(root / "config"), LOCALAPPDATA=str(root / "config"))
    process = subprocess.Popen(args + list(extra), stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    processes.append(process)
    children.callback(stop_process, process)
    return process


def stop_process(process):
    if process.poll() is None:
        process.kill()
        process.communicate(timeout=6)


def await_lock(dest, process):
    lock = dest / "incomplete" / ".lock"
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        if lock.is_file() and lock.read_bytes().strip() == str(process.pid).encode():
            assert process.poll() is None
            return lock
        assert process.poll() is None, process.communicate()
        time.sleep(0.02)
    raise AssertionError(f"No lock for PID {process.pid}: {lock}")


def rejected(process, owner, require_pid=True):
    out, err = process.communicate(timeout=6)
    assert process.returncode != 0, (out, err)
    assert b"Output directory is already in use" in err, err
    if require_pid:
        assert f"PID {owner.pid}".encode() in err, err
    assert owner.poll() is None


try:
    with tempfile.TemporaryDirectory(prefix="recording-lock-", dir=repo / "build") as temporary, contextlib.ExitStack() as children:
        root = Path(temporary)
        dest = root / "žluťoučký"
        owner = start(dest, endpoint="/reconnect")
        lock = await_lock(dest, owner)
        # Reconnect must retain ownership even while the TCP connection is replaced.
        deadline = time.monotonic() + 6
        while requests.count("/reconnect") < 2 and time.monotonic() < deadline:
            time.sleep(0.02)
        assert requests.count("/reconnect") >= 2, requests
        original = lock.read_bytes()
        sentinel = dest / "incomplete" / "existing.mp3"
        sentinel.write_bytes(b"Existing user recording")
        rejected(start(dest / ".." / dest.name), owner)
        assert lock.read_bytes() == original and sentinel.read_bytes() == b"Existing user recording"
        other = start(root / "independent", seconds=2)
        other.communicate(timeout=6)
        assert other.returncode == 0
        owner.kill()
        owner.communicate(timeout=6)
        assert lock.exists()  # A stale PID alone must not block recovery.
        recovered = start(dest, seconds=2)
        await_lock(dest, recovered)
        recovered.communicate(timeout=6)
        assert recovered.returncode == 0
        restarted = start(dest, seconds=2)
        restarted.communicate(timeout=6)
        assert restarted.returncode == 0
        print("PASS: Unicode/alias paths, PID diagnostics, reconnect ownership, independent directories, crash and normal restart")

        race = root / "race"
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            pair = list(pool.map(lambda _: start(race), range(2)))
        deadline = time.monotonic() + 6
        while all(process.poll() is None for process in pair) and time.monotonic() < deadline:
            time.sleep(0.02)
        winners = [process for process in pair if process.poll() is None]
        assert len(winners) == 1, [process.poll() for process in pair]
        winner = winners[0]
        loser = next(process for process in pair if process is not winner)
        rejected(loser, winner, require_pid=False)
        winner.kill()
        winner.communicate(timeout=6)
        print("PASS: simultaneous startup admits exactly one writer")

        shared = root / "shared-show"
        owner = start(root / "one", ["-A", "-a", str(shared / "show")])
        await_lock(shared, owner)
        rejected(start(root / "two", ["-A", "-a", str(shared / "show")]), owner)
        assert not list(shared.glob("show (*).mp3"))
        owner.kill()
        owner.communicate(timeout=6)
        print("PASS: show-only recordings protect their separate output directory before renaming files")
finally:
    for process in processes:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=6)
    server.shutdown()
    server.server_close()
