"""Exercise packaging into a fresh directory and updating a previous package.
Usage: python testing/package_smoke.py [toolchain-prefix]
"""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]
prefix = (Path(sys.argv[1]) if len(sys.argv) > 1 else repo / 'build' / 'toolchain' / 'mingw64').resolve()
env = os.environ.copy()
env['PATH'] = str(prefix / 'bin') + os.pathsep + env.get('PATH', '')

def package(dest):
    result = subprocess.run([sys.executable, str(repo / 'package-windows.py'), str(prefix), str(dest)],
                            env=env, capture_output=True, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    manifest = dest / 'SHA256SUMS'
    assert manifest.is_file(), 'Fresh packaging did not populate the destination'
    entries = {}
    for line in manifest.read_text(encoding='utf-8').splitlines():
        digest, relative = line.split('  ', 1)
        path = (dest / relative).resolve()
        assert path.is_relative_to(dest)
        assert path.is_file(), relative
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, relative
        entries[relative] = digest
    assert 'streamripper.exe' in entries and 'libvorbis-0.dll' in entries
    assert 'licenses/libvorbis/COPYING' in entries
    for name in ('METADATA-RULES.md', 'parse_rules.txt', 'advert_rules.txt'):
        assert name in entries
        assert (dest / name).read_bytes() == (repo / name).read_bytes()
    assert (dest / 'licenses/libvorbis/COPYING').read_bytes() == (repo / 'third_party/licenses/libvorbis/COPYING').read_bytes()
    output = subprocess.check_output([str(dest / 'streamripper.exe'), '--version'], cwd=dest, text=True)
    assert output.strip() == 'Streamripper ' + (repo / 'VERSION').read_text().strip()
    return entries

with tempfile.TemporaryDirectory(prefix='packaging-test-', dir=repo / 'build') as temporary:
    dest = Path(temporary) / 'fresh-dist'
    package(dest)
    print('OK: fresh destination, runtime DLLs, licenses, checksums, and executable')
    obsolete = dest / 'obsolete-runtime.dll'
    obsolete.write_bytes(b'obsolete')
    with (dest / 'SHA256SUMS').open('a', encoding='utf-8') as manifest:
        manifest.write(hashlib.sha256(obsolete.read_bytes()).hexdigest() + '  obsolete-runtime.dll\n')
    unrelated = dest / 'user-file.txt'
    unrelated.write_bytes(b'Keep this user file.')
    entries = package(dest)
    assert not obsolete.exists()
    assert unrelated.read_bytes() == b'Keep this user file.'
    assert unrelated.name not in entries
    print('OK: obsolete packaged files removed; unrelated files preserved')
