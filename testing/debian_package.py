"""Verify a generated amd64 DEB and isolated install/removal without host changes.
Usage: python3 testing/debian_package.py build/release/streamripper-ng_VERSION-1_amd64.deb
Requires dpkg-dev and fakeroot. --apt-transition also requires Docker and network
access for a disposable Debian 13 container. No sudo or host installation is used.
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
    parser.add_argument('--apt-transition', action='store_true',
                        help='Test replacement of Debian streamripper with apt in a disposable container.')
    args = parser.parse_args()
    package = args.package.resolve()
    assert shutil.which('fakeroot'), 'Install fakeroot for the isolated dpkg test.'
    version = (repo / 'VERSION').read_text().strip()
    digest, filename = package.with_suffix('.deb.sha256').read_text().strip().split('  ', 1)
    assert filename == package.name and hashlib.sha256(package.read_bytes()).hexdigest() == digest
    control = run('dpkg-deb', '--field', str(package))
    fields = dict(line.split(': ', 1) for line in control.splitlines() if ': ' in line and not line.startswith(' '))
    assert fields['Package'] == 'streamripper-ng'
    assert fields['Conflicts'] == fields['Replaces'] == 'streamripper'
    assert 'Provides' not in fields
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
                assert member.name.startswith('./usr/bin/') or member.name.startswith('./usr/share/doc/streamripper-ng/'), member.name
            else:
                assert member.isdir(), member.name
        expected_docs = ('README', 'README.md', 'METADATA-RULES.md', 'CHANGELOG.md',
                         'PORTING.md', 'THIRD-PARTY.md', 'COPYING', 'VERSION', 'copyright')
        assert 'usr/bin/streamripper' in files
        for filename in expected_docs:
            assert 'usr/share/doc/streamripper-ng/' + filename in files
        assert 'usr/share/doc/streamripper-ng/examples/rules-examples.txt' in files
        for name in files:
            assert not Path(name).name.startswith('BUILDING'), name
            assert not name.endswith(('.a', '.so', '.h', '.dll', '.exe')), name
    print('PASS: DEB metadata, dynamic runtime dependencies, payload, ownership and checksum')

    # Copy the dependency status database into a disposable root. dpkg validates
    # dependencies normally, but writes package state/files only inside this root.
    with tempfile.TemporaryDirectory(prefix='streamripper-deb-', dir=repo / 'build') as temporary:
        root = Path(temporary)
        admin = root / 'var/lib/dpkg'
        admin.mkdir(parents=True)
        host_status = Path('/var/lib/dpkg/status').read_bytes()
        # This is the clean-install test. Do not inherit host application entries
        # or a GUI's old dependency; the real APT migration is tested separately.
        excluded = {'streamripper', 'streamripper-ng', 'streamripper-gui'}
        entries = host_status.decode().split('\n\n')
        entries = [entry for entry in entries if not any(
            line.startswith('Package: ') and line[9:] in excluded for line in entry.splitlines())]
        (admin / 'status').write_text('\n\n'.join(entries))
        options = ('fakeroot', 'dpkg', '--root=' + str(root), '--log=' + str(root / 'dpkg.log'))
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
                     '--showformat=$' + '{Status}', 'streamripper-ng')
        assert status == 'install ok installed', status
        for filename in expected_docs:
            if filename != 'copyright':
                assert (root / 'usr/share/doc/streamripper-ng' / filename).read_bytes() == (repo / filename).read_bytes()
        assert (root / 'usr/share/doc/streamripper-ng/examples/rules-examples.txt').read_bytes() == (repo / 'rules-examples.txt').read_bytes()
        run(*options, '--remove', 'streamripper-ng')
        assert not binary.exists()
        assert not (root / 'usr/share/doc/streamripper-ng').exists()
        assert Path('/var/lib/dpkg/status').read_bytes() == host_status
    print('PASS: isolated dpkg install, executable/help, packaged documentation and removal')

    if args.apt_transition:
        assert shutil.which('docker'), 'Install Docker for the isolated APT transition test.'
        host_status = Path('/var/lib/dpkg/status').read_bytes()
        script = r'''
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y streamripper
test "$(dpkg-query -W -f='${Status}' streamripper)" = 'install ok installed'
dpkg-query -W -f='Original package: ${Package} ${Version}\n' streamripper
apt install -y "./$1"
test "$(dpkg-query -W -f='${Status}' streamripper-ng)" = 'install ok installed'
test "$(dpkg-query -W -f='${Version}' streamripper-ng)" = "$2-1"
if dpkg-query -W -f='${Status}' streamripper 2>/dev/null | grep -q '^install ok installed$'; then
    echo 'Original streamripper remains installed' >&2
    exit 1
fi
test "$(dpkg-query -S /usr/bin/streamripper)" = 'streamripper-ng: /usr/bin/streamripper'
case "$(streamripper --version)" in
    "Streamripper $2 (built: "*) ;;
    *) echo 'Unexpected application version' >&2; exit 1 ;;
esac
# Slim images may exclude documentation through dpkg policy; the separate
# isolated install above verifies the complete documentation payload.
apt remove -y streamripper-ng
test ! -e /usr/bin/streamripper
'''
        output = run('docker', 'run', '--rm', '--mount',
                     'type=bind,src=' + str(package.parent) + ',dst=/packages,readonly',
                     '--workdir', '/packages', 'debian:trixie-slim',
                     'sh', '-ec', script, 'debian-package-test', package.name, version)
        print(output)
        assert Path('/var/lib/dpkg/status').read_bytes() == host_status
        print('PASS: real Debian package replaced with apt install ./streamripper-ng DEB in a disposable container')


if __name__ == '__main__':
    main()
