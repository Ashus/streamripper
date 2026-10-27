"""Verify a generated 7z release, checksums, bundled DLLs, and executable version.
Usage: python testing/verify-release.py build/release/streamripper-1.68.0-win64.7z
       [path/to/7z.exe]
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

archive = Path(sys.argv[1]).resolve()
seven_zip = Path(sys.argv[2]) if len(sys.argv) > 2 else Path('C:/Program Files/7-Zip/7z.exe')
expected = archive.with_suffix('.7z.sha256').read_text().split('  ', 1)[0]
assert hashlib.sha256(archive.read_bytes()).hexdigest() == expected
with tempfile.TemporaryDirectory(prefix='verify-release-', dir=archive.parent) as temporary:
    dest = Path(temporary)
    subprocess.run([str(seven_zip), 'x', '-y', '-o' + str(dest), str(archive)],
                   check=True, capture_output=True)
    listed = set()
    for line in (dest / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        digest, relative = line.split('  ', 1)
        path = (dest / relative).resolve()
        assert path.is_relative_to(dest), relative
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, relative
        listed.add(relative)
    actual = {path.relative_to(dest).as_posix() for path in dest.rglob('*') if path.is_file()}
    assert not any(Path(name).name.startswith('BUILDING') for name in actual), actual
    assert actual == listed | {'SHA256SUMS'}, actual ^ (listed | {'SHA256SUMS'})
    version = (dest / 'VERSION').read_text().strip()
    assert json.loads((dest / 'build-info.json').read_text())['version'] == version
    output = subprocess.check_output([str(dest / 'streamripper.exe'), '--version'], text=True).strip()
    assert output.split()[:2] == ['Streamripper', version], output
    assert output.splitlines()[1] == 'https://github.com/Ashus/streamripper', output
    print('Verified:', archive.name, 'files:', len(actual), 'version:', version)
print('Archive and all packaged file checksums passed.')
