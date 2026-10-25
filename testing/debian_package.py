"""Verify a generated amd64 DEB and isolated install/removal without host changes.
Usage: python3 testing/debian_package.py build/release/streamripper_VERSION-1_amd64.deb
Requires dpkg-dev and fakeroot. No sudo is used.
"""
from pathlib import Path
import argparse
import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile

repo = Path(__file__).resolve().parents[1]


def run(*command):
    environment = dict(os.environ)
    environment['PATH'] = environment.get('PATH', '') + ':/usr/sbin:/sbin'
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=environment)
    assert result.returncode == 0, result.stdout
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    args = parser.parse_args()
    package = args.package.resolve()
    assert shutil.which('fakeroot'), 'Install fakeroot for the isolated dpkg test.'
    version = (repo / 'VERSION').read_text().strip()
    digest, filename = package.with_suffix('.deb.sha256').read_text().strip().split('  ', 1)
    assert filename == package.name and hashlib.sha256(package.read_bytes()).hexdigest() == digest
    control = run('dpkg-deb', '--field', str(package))
    fields = dict(line.split(': ', 1) for line in control.splitlines() if ': ' in line and not line.startswith(' '))
    assert fields['Package'] == 'streamripper'
    assert fields['Version'] == version + '-1'
    assert fields['Architecture'] == 'amd64'
    assert fields['Homepage'] == 'https://github.com/Ashus/streamripper'
    dependencies = fields['Depends']
    for library in ('ca-certificates', 'libc6', 'libfaad', 'libglib2.0', 'libmad', 'libogg', 'libssl', 'libvorbis'):
        assert library in dependencies, (library, dependencies)
    for unwanted in ('ffmpeg', 'cmake', 'ninja', '-dev'):
        assert unwanted not in dependencies, dependencies
    archive = subprocess.check_output(['dpkg-deb', '--fsys-tarfile', str(package)])
    with tarfile.open(fileobj=io.BytesIO(archive)) as contents:
        files = {}
        for member in contents:
            assert member.uid == member.gid == 0, member
            assert member.mode & 0o022 == 0, (member.name, oct(member.mode))
            assert not member.name.startswith('/') and '..' not in Path(member.name).parts, member.name
            if member.isfile():
                files[member.name.removeprefix('./')] = member
                assert member.name.startswith('./usr/bin/') or member.name.startswith('./usr/share/doc/streamripper/'), member.name
            else:
                assert member.isdir(), member.name
        expected_docs = ('README', 'README.md', 'METADATA-RULES.md', 'BUILDING.md',
                         'BUILDING-LINUX.md', 'BUILDING-WINDOWS.md', 'CHANGELOG.md',
                         'PORTING.md', 'THIRD-PARTY.md', 'COPYING', 'VERSION', 'copyright')
        assert 'usr/bin/streamripper' in files
        for filename in expected_docs:
            assert 'usr/share/doc/streamripper/' + filename in files
        assert 'usr/share/doc/streamripper/examples/parse_rules.txt' in files
        for name in files:
            assert not name.endswith(('.a', '.so', '.h', '.dll', '.exe')), name
    print('PASS: DEB metadata, dynamic runtime dependencies, payload, ownership and checksum')

    # Copy the dependency status database into a disposable root. dpkg validates
    # dependencies normally, but writes package state/files only inside this root.
    with tempfile.TemporaryDirectory(prefix='streamripper-deb-') as temporary:
        root = Path(temporary)
        admin = root / 'var/lib/dpkg'
        admin.mkdir(parents=True)
        shutil.copyfile('/var/lib/dpkg/status', admin / 'status')
        host_status = Path('/var/lib/dpkg/status').read_bytes()
        options = ('fakeroot', 'dpkg', '--root=' + str(root))
        run(*options, '--install', str(package))
        binary = root / 'usr/bin/streamripper'
        elf = binary.read_bytes()[:20]
        assert elf[:6] == b'\x7fELF\x02\x01' and elf[18:20] == b'\x3e\x00', elf
        output = run(str(binary), '--version')
        assert output.split()[:2] == ['Streamripper', version], output
        assert output.splitlines()[1] == 'https://github.com/Ashus/streamripper', output
        assert binary.stat().st_mode & 0o111
        assert 'Usage: streamripper URL [OPTIONS]' in run(str(binary), '--help')
        status = run('dpkg-query', '--admindir=' + str(admin), '--show',
                     '--showformat=$' + '{Status}', 'streamripper')
        assert status == 'install ok installed', status
        for filename in expected_docs:
            if filename != 'copyright':
                assert (root / 'usr/share/doc/streamripper' / filename).read_bytes() == (repo / filename).read_bytes()
        assert (root / 'usr/share/doc/streamripper/examples/parse_rules.txt').read_bytes() == (repo / 'parse_rules.txt').read_bytes()
        run(*options, '--remove', 'streamripper')
        assert not binary.exists()
        assert not (root / 'usr/share/doc/streamripper').exists()
        assert Path('/var/lib/dpkg/status').read_bytes() == host_status
    print('PASS: isolated dpkg install, executable/help, packaged documentation and removal')


if __name__ == '__main__':
    main()
