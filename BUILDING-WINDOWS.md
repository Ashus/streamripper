# Building and releasing on Windows

## Requirements

- 64-bit Windows, Git, and Python 3.10 or newer.
- MSYS2's **MINGW64** environment. Use one architecture/toolchain for every dependency.
  The obsolete bundled Windows SDKs and Winamp targets have been removed.

7-Zip is required for release archives. The packaging command defaults to
`C:\Program Files\7-Zip\7z.exe`; pass a different executable as its second argument.

## Prepare a local toolchain

Start from a checkout of this repository. Run the Windows commands below in
`cmd.exe` from its root. This is a one-time setup for a fresh checkout; if
`build\toolchain` already contains MSYS2, open its terminal and update packages
instead of extracting another copy over it.

1. Create the download directory:

   ```bat
   mkdir build
   ```

2. Download the x86_64 self-extracting base archive (`msys2-base-x86_64-*.sfx.exe`)
   from the [official MSYS2 releases](https://github.com/msys2/msys2-installer/releases/latest).
   Save it as `build\msys2-base.sfx.exe`. Choose the base archive, rather than
   the GUI installer. See the [official archive/setup instructions](https://www.msys2.org/docs/installer/).

3. Extract it and rename the extracted `msys64` directory to `toolchain`:

   ```bat
   build\msys2-base.sfx.exe -y -obuild
   move build\msys64 build\toolchain
   ```

4. Open the local MINGW64 terminal:

   ```bat
   build\toolchain\msys2_shell.cmd -mingw64
   ```

The first shell launch initializes MSYS2. The resulting compiler prefix will be
`build\toolchain\mingw64`, which the project scripts use by default. No toolchain
files are committed or included in the portable Streamripper package. After
successful extraction, the downloaded `build\msys2-base.sfx.exe` can be removed.
Use a short ASCII checkout path without spaces for this local MSYS2 setup;
otherwise use a separate MSYS2 directory and pass its `mingw64` path to the scripts.

Install dependencies in that MSYS2 **MINGW64** terminal:

```sh
pacman -Syu
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-glib2 mingw-w64-x86_64-libmad mingw-w64-x86_64-faad2 mingw-w64-x86_64-libogg mingw-w64-x86_64-libvorbis mingw-w64-x86_64-libiconv mingw-w64-x86_64-openssl
```

For the optional WAV/AAC fixture tests, also install
`mingw-w64-x86_64-lame` and `mingw-w64-x86_64-faac`.
FAAC only generates test audio; its encoder DLL is not packaged.
If an update asks you to close MSYS2, reopen it and finish `pacman -Syu`.
See the official [package management guide](https://www.msys2.org/docs/package-management/).

Close the MSYS2 terminal and return to `cmd.exe` for the build commands below.
An existing external MSYS2 installation can also be used: install the same
packages in its MINGW64 terminal, then pass its compiler prefix explicitly.

## Build from cmd.exe

Run from the repository root:

```bat
build-windows.cmd
package-windows.cmd
```

These commands use `build\toolchain\mingw64`. To select another toolchain:

```bat
build-windows.cmd C:\msys64\mingw64
package-windows.cmd C:\msys64\mingw64
```

CMake generates `build`; Ninja compiles the CLI and regression executable.
After linking, CMake copies the required toolchain DLLs beside `streamripper.exe`,
including their transitive dependencies. This also applies to the Rider presets
in `build/rider-release` and `build/rider-debug`, so the executable can run without
adding the toolchain to the runtime `PATH`. Use the packaging command below for
distribution with the complete license notices and documentation.
`package-windows.cmd` prepares `dist`, calls 7-Zip, tests the archive, and writes
its SHA-256 checksum. The Python helper resolves runtime DLLs recursively from PE import tables and copies
their licenses, linked runtime notices, documentation, and metadata. It checks that
the executable version matches `VERSION` and removes obsolete previously packaged
files using the old checksum manifest. Unrelated user files are preserved.

The output is the portable `dist` directory and a 7z archive under `build\release`.
`dist`, build intermediates, fixtures, and logs are ignored. Commit source and
packaging scripts; distribute the 7z archive as a GitHub Release asset.

The default toolchain prefix is `build\toolchain\mingw64`, relative to the
repository root. Build, packaging, and test scripts use it when no argument is
supplied, regardless of the current working directory. An explicit toolchain
argument can be an absolute path or a path relative to the caller's directory.
The toolchain is local and ignored by Git.
Runtime versions are recorded in `dist\build-info.json` and `THIRD-PARTY.md`.

Manual configuration:

```bat
set "PATH=%CD%\build\toolchain\mingw64\bin;%PATH%"
set "PKG_CONFIG_PATH=%CD%\build\toolchain\mingw64\lib\pkgconfig"
cmake -S . -B build -G Ninja -DSR_MODERN_WINDOWS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%CD%\build\toolchain\mingw64"
cmake --build build
```

This supported Windows build requires OpenSSL 3.x and GLib 2.66 or newer.
`VERSION` controls the program and package version. `-DBUILD_TESTING=OFF` omits
the regression executables. Linux has a separate [complete build guide](BUILDING-LINUX.md).

## Tests

Download the small public test fixtures once; hashes are checked against
[PORTING.md](PORTING.md). The audio files are not committed.

```bat
python testing\fetch-samples.py
testing\run-windows.cmd
```

Tests then run locally without accessing the internet. Build and package first.
`audio_headers.py` checks ADTS framing, per-file MP3 Xing/Info counts, ID3v2/TLEN,
and writer allocation lifecycles without FFmpeg. AAC fixtures are synthetic
framing tests; actual MP3 output is also decoded with libmad.
`aac_silence.py` separately generates real AAC-LC tone/silence fixtures using
FAAC and checks FAAD2 analysis and lossless ICY splits.
For individual checks, set the toolchain's `bin` on PATH and run:

```bat
ctest --test-dir build --output-on-failure
python testing\cli_options.py
python testing\package_smoke.py
python testing\sequence_number.py
python testing\recording_lock.py
python testing\metadata_filter.py
python testing\live_updates.py dist\streamripper.exe
python testing\stream_capture.py dist\streamripper.exe build\toolchain\mingw64\bin\openssl.exe
python testing\metadata_encoding.py
python testing\console_output.py
python testing\http_security.py build\http_security_regression.exe
python testing\console_close.py
python testing\audio_headers.py
python testing\aac_silence.py dist\streamripper.exe build\aac_silence_regression.exe --faac build\toolchain\mingw64\bin\faac.exe
python testing\incomplete_duration.py
python testing\silence_output.py build\core_regression.exe build\sample-5s.mp3 build\toolchain\mingw64\bin
python testing\https_smoke.py dist\streamripper.exe build\sample-5s.mp3 build\toolchain\mingw64\bin\openssl.exe
python testing\http_framing.py dist\streamripper.exe build\sample-5s.mp3
python testing\ipv6_transport.py
python testing\proxy_connect.py
python testing\relay_stress.py dist\streamripper.exe build\sample-5s.mp3
python testing\relay_ports.py
python testing\wav_smoke.py dist\streamripper.exe build\core_regression.exe build\toolchain\mingw64\bin\lame.exe build\sample-3s.wav build\sample-5s.mp3
```

Coverage:

- Historical HTTP CVE inputs, malformed server versions, exact header size
  boundaries and oversized playlist entries. See [SECURITY-REVIEW.md](SECURITY-REVIEW.md)
  for the instrumented Linux checks and audit limits.
- Five-second rule reloads, atomic replacement, invalid/missing-file retention,
  current-title reparsing, filter changes and metadata-log deduplication.
- DNS/network and HTTP 503/429 recovery, Retry-After, fatal/initial failures,
  disabled reconnects and cancellation during retry waits.
- Console width changes, long Unicode titles and clearing stale row contents.
- HLS variants/live playlists, AES-128, relative redirects, partial-segment
  retries and rejected unsupported formats; native/ICY FLAC byte preservation,
  normalized Ogg FLAC timestamps, frame numbers, complete pages and sample counts,
  continuous output and interrupted-recording cleanup. See PORTING.md for the
  fixture and decoder coverage limits.
- Windows relay port ownership, automatic port scanning, explicit bind summaries,
  and independent IPv4/IPv6 routing between concurrent stations.
- Recording-directory locks: PID diagnostics, simultaneous starts, reconnects,
  Unicode/alias paths, separate show directories, and normal/crash recovery.
- Actual 2 MiB disk reservations without extending EOF, allocation-boundary
  growth, independent files, header rewrites, reservation release, and fallback.
- Fresh-directory packaging, runtime DLL/license inclusion, file checksums,
  obsolete-file removal, and preservation of unrelated user files.
- TLS 1.2 ECDHE/ECDSA and TLS 1.3, certificate/hostname rejection, explicit
  `--insecure`, custom CAs, redirects/loop limits, and simultaneous recordings.
- Track splitting, Unicode names, long station/server headers, and ICY blocks
  without a terminating NUL byte.
- Automatic UTF-8/legacy metadata decoding, per-update encoding changes,
  explicit encoding overrides, Unicode filenames, console text, and ID3 tags.
- Optional ICY title logging before rules, timestamps, duplicate suppression,
  empty/excluded titles, UTF-8 text, and preservation of existing log contents.
- HTTP/HTTPS chunk framing across metadata boundaries; byte-for-byte audio
  preservation, extensions/trailers, malformed/truncated/overflowing framing.
- Two relay clients, with and without ICY, across 280 consecutive 16 kB blocks
  each while the circular buffer repeatedly recycles; clean shutdown.
- HTTP/HTTPS forward proxies, CONNECT, nested TLS, Basic authentication isolation,
  IPv6 proxy/origin endpoints, redirects, chunked ICY streams, and independent
  certificate checks for both TLS layers. Fragmented, malformed, oversized,
  truncated, informational, stalled, and unsuccessful CONNECT responses are
  covered, along with repeated TLS/socket cleanup. Use `--successes-only` or
  `--errors-only` with `proxy_connect.py` to run one focused group.
- IPv6 loopback HTTP/HTTPS, IP and DNS certificate identities, trust/IP mismatch
  rejection, redirects, bracketed proxy requests, IPv4 fallback, repeated socket
  handle cleanup, and IPv4/IPv6 relay clients with and without ICY. This targeted
  check requires IPv6 loopback and fails rather than silently skipping it.
  Use `python testing\ipv6_transport.py --families-only` for focused `-4`/`-6`
  alias, conflict, HTTP/TLS,
  redirect, proxy, and automatic-mode checks without repeating the relay suite.
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

The maintained repository is [Ashus/streamripper](https://github.com/Ashus/streamripper).
Downloads are published on its [Releases page](https://github.com/Ashus/streamripper/releases).
For a fresh checkout:

```bat
git clone https://github.com/Ashus/streamripper.git
cd streamripper
git remote add upstream https://github.com/streamripper/streamripper.git
```

Update `VERSION` and [CHANGELOG.md](CHANGELOG.md), build, package, run the tests,
and commit the source changes. `SHA256SUMS` covers every packaged file except itself.
The 7z archive contains the executable, runtime DLLs, documentation, and licenses.
Neither binaries nor `.7z` archives are committed to Git; Git LFS is not required.

When ready to publish:

```bat
set /p SR_RELEASE_VERSION=<VERSION
git tag -a v%SR_RELEASE_VERSION% -m "Streamripper %SR_RELEASE_VERSION%"
git push origin v%SR_RELEASE_VERSION%
```

Attach `build\release\streamripper-<version>-win64.7z` to the matching GitHub release,
using the version from `VERSION`.
Also attach its `.7z.sha256` checksum file. The 7z archive contains the contents of `dist`
directly, so extracting it provides the executable and DLLs in one directory.

For the Linux release asset, run `package-linux.py` on Debian amd64/WSL using
the [Linux packaging guide](BUILDING-LINUX.md#build-the-amd64-deb). It writes
`streamripper-ng_<version>-1_amd64.deb` and its `.deb.sha256` into the same
`build/release` directory. Attach both files manually to the same release;
the Windows packaging command preserves those Linux artifacts.

Verify the finished archive, every packaged file, and the extracted executable:

```bat
set /p SR_RELEASE_VERSION=<VERSION
python testing\verify-release.py build\release\streamripper-%SR_RELEASE_VERSION%-win64.7z
```

Only generated, checksummed files are included via a 7-Zip list file; unrelated
files in `dist` are preserved but excluded from the archive.
Nothing is published by the build/package scripts. See [THIRD-PARTY.md](THIRD-PARTY.md)
for library licenses and corresponding source packages.
