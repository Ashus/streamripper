"""Linux recordings, ext4 allocation, retention and signal shutdown.
Usage: python3 testing/linux_smoke.py /path/to/streamripper
Requires the pinned MP3 fixture and oggenc (vorbis-tools); no FFmpeg.
"""
from pathlib import Path
import http.server
import math
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave

repo = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]).resolve()
mp3 = (repo / "build/sample-5s.mp3").read_bytes()
aac = bytes([255, 241, 80, 128, 13, 127, 252]) + bytes(100)

with tempfile.TemporaryDirectory(prefix="streamripper-linux-") as tmp:
    root = Path(tmp)
    wav = root / "fixture.wav"
    with wave.open(str(wav), "wb") as out:
        out.setparams((1, 2, 44100, 0, "NONE", "not compressed"))
        out.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * n / 44100)))
                                 for n in range(44100 * 4)))
    ogg = root / "fixture.ogg"
    subprocess.run(["oggenc", "-Q", "-o", str(ogg), str(wav)], check=True)
    ogg_data = ogg.read_bytes()

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", {"/aac": "audio/aac", "/ogg": "application/ogg"}.get(
                self.path, "audio/mpeg"))
            self.send_header("icy-name", "Linux test")
            self.end_headers()
            source = aac * 200 if self.path == "/aac" else mp3
            try:
                if self.path == "/ogg":
                    self.wfile.write(ogg_data)
                    self.wfile.flush()
                    time.sleep(4)
                    return
                offset = 0
                for _ in range(2000):
                    block = (source * (16384 // len(source) + 2))[offset:offset + 16384]
                    offset = (offset + 16384) % len(source)
                    self.wfile.write(block)
                    self.wfile.flush()
                    time.sleep(.02)
            except OSError:
                pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for kind in ("mp3", "aac", "wav", "ogg"):
            dest = root / kind
            args = [str(exe), f"http://127.0.0.1:{server.server_port}/" + ("mp3" if kind == "wav" else kind),
                    "-c", "-l", "2", "-s", "-d", str(dest), "-k", "0", "--xs-none"]
            if kind == "wav":
                args.append("--wav")
            result = subprocess.run(args, capture_output=True, timeout=8)
            assert result.returncode == 0, (kind, result.stdout, result.stderr)
            files = list(dest.rglob("*." + kind))
            assert len(files) == 1, (kind, files, result.stderr)
            data = files[0].read_bytes()
            assert len(data) > 1000
            if kind == "wav":
                with wave.open(str(files[0]), "rb") as recording:
                    assert recording.getnframes() > 1000
            elif kind == "ogg":
                assert data.startswith(b"OggS")
            assert not (dest / "incomplete/.lock").exists()
            assert files[0].stat().st_blocks * 512 < len(data) + 65536, "Unused reservation remains"
            print("PASS:", kind, "recording and final allocation release", flush=True)

        for sig in (signal.SIGINT, signal.SIGTERM):
            dest = root / sig.name
            args = [str(exe), f"http://127.0.0.1:{server.server_port}/mp3", "-c", "-s",
                    "-d", str(dest), "--xs-none", "--incomplete-min-duration=15m"]
            process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 5
                while True:
                    files = list(dest.rglob("*.mp3"))
                    if files and files[0].stat().st_size > 65536:
                        stat = files[0].stat()
                        assert stat.st_blocks * 512 >= 2 * 1024 * 1024, "No 2 MiB reservation on test filesystem"
                        break
                    assert process.poll() is None, process.communicate()
                    assert time.monotonic() < deadline, "Recording did not start"
                    time.sleep(.02)
                process.send_signal(sig)
                stdout, stderr = process.communicate(timeout=5)
                assert process.returncode == 0, (stdout, stderr)
                assert not list(dest.rglob("*.mp3")), stderr
                assert b"Discarded incomplete recording" in stderr, stderr
                assert not (dest / "incomplete/.lock").exists()
                print("PASS:", sig.name, "cleanup, duration retention and lock removal", flush=True)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
    finally:
        server.shutdown()
        server.server_close()
