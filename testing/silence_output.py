"""Check both silence detectors leave the working directory untouched.
Usage: python testing/silence_output.py build/core_regression.exe build/sample-5s.mp3 toolchain/bin
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
audio = Path(sys.argv[2]).resolve()
env = os.environ.copy()
env['PATH'] = str(Path(sys.argv[3]).resolve()) + os.pathsep + env['PATH']
with tempfile.TemporaryDirectory(prefix='streamripper-silence-') as temporary:
    cwd = Path(temporary)
    for existing in (False, True):
        dump = cwd / 'dump.mp3'
        if existing:
            dump.write_bytes(b'Existing user file must not be overwritten.')
            (cwd / 'gcs1.txt').write_bytes(b'Another existing user file.')
        before = {path.name: path.read_bytes() for path in cwd.iterdir()}
        result = subprocess.run([str(exe), '--silence', str(audio)], cwd=cwd, env=env,
                                capture_output=True, timeout=15)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert b'Both silence detectors completed.' in result.stdout
        after = {path.name: path.read_bytes() for path in cwd.iterdir()}
        assert after == before, (before.keys(), after.keys())
        print('OK: silence detection preserves cwd; existing dump:', existing)
