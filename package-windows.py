"""Package runtime binaries, their licenses, documentation, and SHA-256 sums."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parent
if sys.argv[1:2] == ['--checksum']:
    archive_path = Path(sys.argv[2]).resolve()
    archive_path.with_suffix(archive_path.suffix + '.sha256').write_text(
        hashlib.sha256(archive_path.read_bytes()).hexdigest() + '  ' + archive_path.name + '\n',
        encoding='utf-8', newline='\n')
    print('Release archive:', archive_path)
    sys.exit(0)
prefix = (Path(sys.argv[1]) if len(sys.argv) > 1 else Path('C:/Download/streamripper-toolchain/mingw64')).resolve()
dest = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else repo / 'dist'
if dest == repo or dest == prefix or dest in repo.parents or dest in prefix.parents:
    raise ValueError('The package directory must not be a source or toolchain root.')
release = repo / 'build' / 'release'
release.mkdir(parents=True, exist_ok=True)
modules = {'glib2': 'glib-2.0', 'libmad': 'mad', 'libogg': 'ogg', 'libvorbis': 'vorbis',
           'openssl': 'openssl', 'pcre2': 'libpcre2-8', 'gettext-runtime': 'libintl', 'libiconv': 'iconv'}
env = os.environ.copy()
env['PATH'] = str(prefix / 'bin') + os.pathsep + env.get('PATH', '')
env['PKG_CONFIG_PATH'] = str(prefix / 'lib' / 'pkgconfig')
version = (repo / 'VERSION').read_text().strip()
if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', version):
    raise ValueError('VERSION must contain a valid numeric release version.')
reported = subprocess.check_output([str(repo / 'build' / 'streamripper.exe'), '--version'], env=env, text=True).strip()
if reported != 'Streamripper ' + version:
    raise RuntimeError('Rebuild first: executable version does not match VERSION.')

with tempfile.TemporaryDirectory(prefix='package-', dir=release) as temporary:
    stage = Path(temporary)
    queue = [repo / 'build' / 'streamripper.exe']
    seen = set()
    while queue:
        binary = queue.pop()
        if binary.name.lower() in seen:
            continue
        seen.add(binary.name.lower())
        shutil.copy2(binary, stage / binary.name)
        output = subprocess.check_output([str(prefix / 'bin' / 'objdump.exe'), '-p', str(binary)], text=True)
        for dll in re.findall(r'DLL Name:\s*(\S+)', output):
            path = prefix / 'bin' / dll
            if path.is_file():
                queue.append(path)
    for name in modules.keys() | {'libgcc', 'crt', 'headers'}:
        source = prefix / 'share' / 'licenses' / name
        if source.is_dir():
            shutil.copytree(source, stage / 'licenses' / name)
        elif name not in ('libmad', 'libvorbis'):
            raise FileNotFoundError(f'Missing license directory: {source}')
    shutil.copytree(repo / 'third_party' / 'licenses', stage / 'licenses', dirs_exist_ok=True)
    (stage / 'licenses' / 'libmad').mkdir(exist_ok=True)
    shutil.copy2(repo / 'COPYING', stage / 'licenses' / 'libmad' / 'COPYING')
    for name in ('COPYING', 'README', 'README.md', 'README-HTTPS.md', 'BUILDING.md', 'CHANGELOG.md',
                 'PORTING.md', 'VERSION', 'THIRD-PARTY.md', 'METADATA-RULES.md',
                 'parse_rules.txt', 'advert_rules.txt'):
        shutil.copy2(repo / name, stage / name)
    versions = {}
    for name, module in modules.items():
        result = subprocess.run([str(prefix / 'bin' / 'pkg-config.exe'), '--modversion', module],
                                capture_output=True, text=True, env=env)
        if result.returncode == 0:
            versions[name] = result.stdout.strip()
    manifest = {'version': version, 'platform': 'windows-x86_64',
                'upstream': 'https://github.com/streamripper/streamripper',
                'upstream_commit': '248f771250998e38a2d8212aa43ccc4a516f853b',
                'libraries': versions, 'binaries': sorted(seen)}
    (stage / 'build-info.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    checksums = []
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(stage).as_posix())
    (stage / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n', encoding='utf-8', newline='\n')

    # Remove only obsolete files listed in the previous generated manifest.
    # Preserve unrelated user files and never recursively remove the destination.
    current = {path.relative_to(stage).as_posix() for path in stage.rglob('*') if path.is_file()}
    previous = dest / 'SHA256SUMS'
    if previous.is_file():
        for line in previous.read_text(encoding='utf-8').splitlines():
            _, relative = line.split('  ', 1)
            old = (dest / relative).resolve()
            if not old.is_relative_to(dest):
                raise ValueError('Invalid path in previous SHA256SUMS.')
            if relative not in current and old.is_file():
                try:
                    old.unlink()
                except OSError as exc:
                    print(f'[DELETE ERROR] Failed to delete file: {old}', file=sys.stderr)
                    print(f'Error: {exc}', file=sys.stderr)
                    print(f'WinError: {getattr(exc, "winerror", None)}', file=sys.stderr)
                    raise
    dest.mkdir(parents=True, exist_ok=True)
    for path in stage.rglob('*'):
        if path.is_file():
            target = dest / path.relative_to(stage)
            target.parent.mkdir(parents=True, exist_ok=True)

            print(f'[COPY] {path} -> {target}', flush=True)

            try:
                shutil.copy2(path, target)
            except OSError as exc:
                print('\n' + '=' * 70, file=sys.stderr)
                print('[ERROR] Failed to copy file!', file=sys.stderr)
                print(f'Source:   {path}', file=sys.stderr)
                print(f'Target:   {target}', file=sys.stderr)
                print(f'Error:    {exc}', file=sys.stderr)
                print(f'WinError: {getattr(exc, "winerror", None)}', file=sys.stderr)

                if getattr(exc, 'winerror', None) in (32, 33):
                    print(
                        '\n[LOCKED] The file is likely open or locked '
                        'by another process!',
                        file=sys.stderr
                    )
                    print(f'Check source: {path}', file=sys.stderr)
                    print(f'Check target: {target}', file=sys.stderr)

                print('=' * 70, file=sys.stderr)
                raise
    for directory in sorted((path for path in dest.rglob('*') if path.is_dir()), reverse=True):
        try:
            directory.rmdir()
        except OSError:
            pass
    file_list = release / ('streamripper-' + version + '-win64-files.txt')
    file_list.write_text('\n'.join(sorted(current)) + '\n', encoding='utf-8', newline='\n')
print('Prepared:', dest, ', binaries:', len(seen))
print('Run package-windows.cmd to create the 7z release archive.')
