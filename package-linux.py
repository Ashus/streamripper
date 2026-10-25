"""Build a native Debian amd64 runtime package and SHA-256 checksum."""
from pathlib import Path
import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path,
                        default=Path.home() / '.cache/streamripper-build')
    parser.add_argument('--output-dir', type=Path, default=repo / 'build/release')
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('Run this command on Linux, including inside WSL.')
    for tool in ('cmake', 'cpack', 'ninja', 'dpkg', 'dpkg-deb', 'dpkg-shlibdeps'):
        if not shutil.which(tool):
            parser.error('Missing packaging tool: ' + tool + '; install cmake ninja-build dpkg-dev.')
    architecture = subprocess.check_output(['dpkg', '--print-architecture'], text=True).strip()
    if architecture != 'amd64':
        parser.error('Only native amd64 DEB packages are supported.')
    version = (repo / 'VERSION').read_text(encoding='utf-8').strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        parser.error('VERSION must contain a numeric release version.')
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    if build == repo or build in repo.parents or build == repo / 'build':
        parser.error('Use a separate Linux build directory, not the source or Windows build directory.')
    cache = build / 'CMakeCache.txt'
    if cache.is_file():
        contents = cache.read_text(encoding='utf-8')
        if re.search(r'^SR_MODERN_WINDOWS:BOOL=(ON|TRUE|1)$', contents, re.MULTILINE):
            parser.error('The selected CMake cache belongs to the Windows build.')
    subprocess.run(['cmake', '-S', str(repo), '-B', str(build), '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', '-DSR_MODERN_WINDOWS=OFF'], check=True)
    config = build / 'CPackConfig.cmake'
    if not config.is_file():
        parser.error('DEB packaging is unavailable for this build; check architecture and dpkg-dev.')
    subprocess.run(['cmake', '--build', str(build), '--parallel'], check=True)
    binary = (build / 'streamripper').read_bytes()[:20]
    if binary[:6] != b'\x7fELF\x02\x01' or binary[18:20] != b'\x3e\x00':
        raise RuntimeError('The built executable is not a little-endian amd64 ELF binary.')
    output.mkdir(parents=True, exist_ok=True)
    filename = 'streamripper_' + version + '-1_amd64.deb'
    package = output / filename
    checksum = package.with_suffix('.deb.sha256')
    # CPack leaves a sizeable _CPack_Packages tree, including on failure.
    # Keep it on the Linux filesystem and remove it with the temporary stage.
    with tempfile.TemporaryDirectory(prefix='package-deb-', dir=build) as temporary:
        stage = Path(temporary)
        subprocess.run(['cpack', '--config', str(config), '-G', 'DEB', '-B', str(stage)],
                       cwd=build, check=True)
        staged_package = stage / filename
        if not staged_package.is_file():
            raise RuntimeError('CPack did not produce the expected package: ' + str(staged_package))
        metadata = subprocess.check_output(
            ['dpkg-deb', '--field', str(staged_package), 'Package', 'Version', 'Architecture'],
            text=True)
        expected = {'Package': 'streamripper', 'Version': version + '-1', 'Architecture': 'amd64'}
        fields = dict(line.split(': ', 1) for line in metadata.splitlines())
        if fields != expected:
            raise RuntimeError('Unexpected DEB metadata: ' + metadata)
        digest = hashlib.sha256(staged_package.read_bytes()).hexdigest()
        # Copy completely before replacing an earlier release artifact.
        with tempfile.TemporaryDirectory(prefix='.deb-copy-', dir=output) as transfer:
            copied = Path(transfer) / filename
            shutil.copyfile(staged_package, copied)
            copied.replace(package)
            staged_checksum = Path(transfer) / checksum.name
            staged_checksum.write_text(digest + '  ' + filename + '\n', encoding='utf-8')
            staged_checksum.replace(checksum)
    # Remove only the legacy staging tree created by the initial workflow.
    legacy = output / '_CPack_Packages'
    if output == (repo / 'build/release').resolve() and legacy.exists():
        if legacy.is_symlink() or legacy.resolve().parent != output:
            raise RuntimeError('Refusing to remove an unexpected staging path: ' + str(legacy))
        shutil.rmtree(legacy)
    print('Package:', package)
    print('Checksum:', checksum)


if __name__ == '__main__':
    main()
