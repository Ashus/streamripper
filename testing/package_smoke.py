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
    assert 'libfaad-2.dll' in entries
    assert 'licenses/faad2/COPYING' in entries and 'licenses/faad2/NOTICE' in entries
    assert 'licenses/libvorbis/COPYING' in entries
    for name in ('crt/COPYING.MinGW-w64-runtime.txt', 'libgcc/COPYING3',
                 'libgcc/COPYING.RUNTIME', 'pcre2/LICENCE.md',
                 'libiconv/COPYING.LIB', 'gettext-runtime/intl/COPYING.LIB'):
        assert 'licenses/' + name in entries
        assert (dest / 'licenses' / name).read_bytes() == (prefix / 'share/licenses' / name).read_bytes()
    for name in ('headers/', 'gettext-runtime/libasprintf/', 'libiconv/libcharset/'):
        assert not any(path.startswith('licenses/' + name) for path in entries)
    assert 'licenses/pcre2/COPYING' not in entries
    assert 'licenses/crt/COPYING.MinGW-w64.txt' not in entries
    assert 'parse_rules.txt' not in entries
    for name in ('METADATA-RULES.md', 'BUILDING-WINDOWS.md', 'BUILDING-LINUX.md',
                 'rules-examples.txt'):
        assert name in entries
        assert (dest / name).read_bytes() == (repo / name).read_bytes()
    assert (dest / 'licenses/libvorbis/COPYING').read_bytes() == (repo / 'third_party/licenses/libvorbis/COPYING').read_bytes()
    output = subprocess.check_output([str(dest / 'streamripper.exe'), '--version'], cwd=dest, text=True)
    assert output.split()[:2] == ['Streamripper', (repo / 'VERSION').read_text().strip()]
    assert output.splitlines()[1] == 'https://github.com/Ashus/streamripper'
    return entries

with tempfile.TemporaryDirectory(prefix='packaging-test-', dir=repo / 'build') as temporary:
    dest = Path(temporary) / 'fresh-dist'
    package(dest)
    print('OK: fresh destination, runtime DLLs, licenses, checksums, and executable')
    obsolete = dest / 'obsolete-runtime.dll'
    obsolete.write_bytes(b'obsolete')
    old_rules = dest / 'parse_rules.txt'
    old_rules.write_bytes(b'old example')
    old_notice = dest / 'licenses/headers/ddk-readme.txt'
    old_notice.parent.mkdir(parents=True)
    old_notice.write_bytes(b'old DDK readme')
    with (dest / 'SHA256SUMS').open('a', encoding='utf-8') as manifest:
        manifest.write(hashlib.sha256(obsolete.read_bytes()).hexdigest() + '  obsolete-runtime.dll\n')
        for old in (old_rules, old_notice):
            manifest.write(hashlib.sha256(old.read_bytes()).hexdigest() + '  ' + old.relative_to(dest).as_posix() + '\n')
    unrelated = dest / 'user-file.txt'
    unrelated.write_bytes(b'Keep this user file.')
    entries = package(dest)
    assert not obsolete.exists()
    assert not old_rules.exists() and not old_notice.exists()
    assert unrelated.read_bytes() == b'Keep this user file.'
    assert unrelated.name not in entries
    print('OK: obsolete packaged files removed; unrelated files preserved')
