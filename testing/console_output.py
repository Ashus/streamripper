"""Check native Windows console output and redirected UTF-8 independently."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env["PATH"] = str(root / "build/toolchain/mingw64/bin") + os.pathsep + env["PATH"]
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
result = subprocess.run([str(root / "build/console_output_regression.exe")],
    env=env, startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE,
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
expected = "Mirar - När Solen Går Ned\r\n".encode("utf-8")
title = "0123456789" * 6 + "-Příliš-界-e\u0301-🎵-ENDMARK"
expected += f"[{'ripping':<13}] {title} [{'00:01':>8}]\r".encode("utf-8")
assert result.stdout == expected, result.stdout
assert result.stderr == expected, result.stderr
print("Windows console code pages 1250/65001 and redirected UTF-8: passed")
