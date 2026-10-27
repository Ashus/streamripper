"""Exercise DEB generation, repeated builds and staging cleanup after failures.
Usage: python3 testing/debian_packaging.py [--build-dir Linux-build-directory]
"""
from pathlib import Path
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path,
                        default=Path.home() / '.cache/streamripper-build')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    version = (repo / 'VERSION').read_text().strip()
    with tempfile.TemporaryDirectory(prefix='deb-workflow-') as temporary:
        root = Path(temporary)
        output = root / 'release with spaces'
        command = [sys.executable, str(repo / 'package-linux.py'),
                   '--build-dir', str(build), '--output-dir', str(output)]
        environment = dict(os.environ)
        result = subprocess.run(command, cwd=root, capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        package = output / ('streamripper-ng_' + version + '-1_amd64.deb')
        checksum = package.with_suffix('.deb.sha256')
        assert {file.name for file in output.iterdir()} == {package.name, checksum.name}
        assert not list(build.glob('package-deb-*'))
        unrelated = output / 'keep-user-file.txt'
        unrelated.write_text('Preserve this file.')
        result = subprocess.run(command, cwd=root, capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert unrelated.read_text() == 'Preserve this file.'
        before = {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in output.iterdir()}
        fake_tools = root / 'fake-tools'
        fake_tools.mkdir()
        fake_cpack = fake_tools / 'cpack'
        fake_cpack.write_text(
            '#!/usr/bin/env python3\n'
            'from pathlib import Path\n'
            'import sys\n'
            'stage = Path(sys.argv[sys.argv.index("-B") + 1])\n'
            '(stage / "_CPack_Packages").mkdir()\n'
            '(stage / "_CPack_Packages" / "partial").write_text("incomplete package")\n'
            'sys.exit(42)\n')
        fake_cpack.chmod(0o755)
        environment['PATH'] = str(fake_tools) + os.pathsep + environment.get('PATH', '')
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, env=environment)
        assert result.returncode != 0, result.stdout + result.stderr
        assert not list(build.glob('package-deb-*'))
        assert not list(output.glob('.deb-copy-*'))
        assert before == {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in output.iterdir()}
        print('PASS: build from another cwd, paths with spaces, repeated packaging, failure cleanup and old-file preservation')

        bad = subprocess.run([sys.executable, str(repo / 'package-linux.py'),
                              '--build-dir', str(repo / 'build')],
                             capture_output=True, text=True)
        assert bad.returncode != 0 and 'Windows build directory' in bad.stderr, bad
        fake_dpkg = fake_tools / 'dpkg'
        fake_dpkg.write_text('#!/usr/bin/env python3\nprint("arm64")\n')
        fake_dpkg.chmod(0o755)
        bad = subprocess.run(command, cwd=root, capture_output=True, text=True, env=environment)
        assert bad.returncode != 0 and 'Only native amd64' in bad.stderr, bad
        print('PASS: Windows CMake cache isolation and unsupported-architecture rejection')


if __name__ == '__main__':
    main()
