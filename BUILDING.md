# Building and releasing on Windows

## Requirements

- 64-bit Windows, Git, and Python 3.10 or newer. Tests were verified with Python 3.14.
- MSYS2's **MINGW64** environment. Use one architecture/toolchain for every dependency.
  The obsolete bundled Windows SDKs and Winamp targets have been removed.

Install dependencies in the MSYS2 MINGW64 terminal:

```sh
pacman -Syu
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-glib2 mingw-w64-x86_64-libmad mingw-w64-x86_64-libogg mingw-w64-x86_64-libvorbis mingw-w64-x86_64-libiconv mingw-w64-x86_64-openssl
```

For the optional WAV fixture tests, also install `mingw-w64-x86_64-lame`.
If an update asks you to close MSYS2, reopen it and finish `pacman -Syu`.
See the official [package management guide](https://www.msys2.org/docs/package-management/).

7-Zip is required for release archives. The packaging command defaults to
`C:\Program Files\7-Zip\7z.exe`; pass a different executable as its second argument.

## Build from cmd.exe

Run from the repository root:

```bat
build-windows.cmd C:\msys64\mingw64
package-windows.cmd C:\msys64\mingw64
```

CMake generates `build`; Ninja compiles the CLI and regression executable.
`package-windows.cmd` prepares `dist`, calls 7-Zip, tests the archive, and writes
its SHA-256 checksum. The Python helper resolves runtime DLLs recursively from PE import tables and copies
their licenses, linked runtime notices, documentation, and metadata. It checks that
the executable version matches `VERSION` and removes obsolete previously packaged
files using the old checksum manifest. Unrelated user files are preserved.

The output is the portable `dist` directory and a 7z archive under `build\release`.
`dist`, build intermediates, fixtures, and logs are ignored. Commit source and
packaging scripts; distribute the 7z archive as a GitHub Release asset.

The release toolchain in this workspace is
`C:\Download\streamripper-toolchain\mingw64`; both scripts use it when no argument
is supplied. Verified with GCC 16.2.0, GLib 2.90.1, and OpenSSL 3.6.5.
Runtime versions are recorded in `dist\build-info.json` and `THIRD-PARTY.md`.

Manual configuration:

```bat
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "PKG_CONFIG_PATH=C:\msys64\mingw64\lib\pkgconfig"
cmake -S . -B build -G Ninja -DSR_MODERN_WINDOWS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\msys64\mingw64
cmake --build build
```

This supported Windows build requires OpenSSL 3.x and GLib 2.66 or newer.
`VERSION` controls the program and package version. `-DBUILD_TESTING=OFF` omits
the regression executable. The inherited Unix build is not validated by this release.

## Tests

Download the small public test fixtures once; hashes are checked against
[PORTING.md](PORTING.md). The audio files are not committed.

```bat
python testing\fetch-samples.py
testing\run-windows.cmd C:\msys64\mingw64
```

Tests then run locally without accessing the internet. Build and package first.
For individual checks, set the toolchain's `bin` on PATH and run:

```bat
ctest --test-dir build --output-on-failure
python testing\https_smoke.py dist\streamripper.exe build\sample-5s.mp3 C:\msys64\mingw64\bin\openssl.exe
python testing\http_framing.py dist\streamripper.exe build\sample-5s.mp3
python testing\relay_stress.py dist\streamripper.exe build\sample-5s.mp3
python testing\wav_smoke.py dist\streamripper.exe build\core_regression.exe C:\msys64\mingw64\bin\lame.exe build\sample-3s.wav build\sample-5s.mp3
```

Coverage:

- TLS 1.2 ECDHE/ECDSA and TLS 1.3, certificate/hostname rejection, explicit
  `--insecure`, custom CAs, redirects/loop limits, and simultaneous recordings.
- Track splitting, Unicode names, long station/server headers, and ICY blocks
  without a terminating NUL byte.
- HTTP/HTTPS chunk framing across metadata boundaries; byte-for-byte audio
  preservation, extensions/trailers, malformed/truncated/overflowing framing.
- Two relay clients, with and without ICY, across 280 consecutive 16 kB blocks
  each while the circular buffer repeatedly recycles; clean shutdown.
- Mono/stereo WAV, identical PCM for 1/997/16384/65536-byte input writes,
  completed/incomplete/show files, CUE output, and malformed MP3 rejection.
- 100,000 metadata changes, 1,000 rule-set lifecycles, 200 failed-connection
  managers, and 100 WAV decoder lifecycles per fixture, checking process memory
  growth and handle counts.

These targeted checks do not establish that every inherited path is free of
memory bugs. No Windows AddressSanitizer/Valgrind run is claimed.

## Fork and release

Upstream is https://github.com/streamripper/streamripper at
`248f771250998e38a2d8212aa43ccc4a516f853b`. See [PORTING.md](PORTING.md)
for adapted fork changes and author attribution.

After creating your fork, retain the original remote as `upstream`:

```bat
git remote rename origin upstream
git remote add origin https://github.com/YOUR_ACCOUNT/streamripper.git
git push -u origin master
```

Update `VERSION` and [CHANGELOG.md](CHANGELOG.md), build, package, run the tests,
and commit the source changes. `SHA256SUMS` covers every packaged file except itself.
The 7z archive contains the executable, runtime DLLs, documentation, and licenses.
Neither binaries nor `.7z` archives are committed to Git; Git LFS is not required.

When ready to publish:

```bat
git tag -a v1.68.0 -m "Streamripper 1.68.0"
git push origin v1.68.0
```

Attach `build\release\streamripper-1.68.0-win64.7z` to the matching GitHub release.
Also attach its `.7z.sha256` checksum file. The 7z archive contains the contents of `dist`
directly, so extracting it provides the executable and DLLs in one directory.

Verify the finished archive, every packaged file, and the extracted executable:

```bat
python testing\verify-release.py build\release\streamripper-1.68.0-win64.7z
```

Only generated, checksummed files are included via a 7-Zip list file; unrelated
files in `dist` are preserved but excluded from the archive.
Nothing is published by the build/package scripts. See [THIRD-PARTY.md](THIRD-PARTY.md)
for library licenses and corresponding source packages.
