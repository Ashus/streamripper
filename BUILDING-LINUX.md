# Installing, building and packaging on Linux

Linux amd64 releases include a native Debian package. This guide covers package
installation, optional source builds, DEB packaging and tests. Linux does not use
the Windows toolchain, DLL bundle or `dist` directory.
The Windows workflow is documented independently in [BUILDING-WINDOWS.md](BUILDING-WINDOWS.md).

## Install the release DEB

Download the `streamripper_<version>-1_amd64.deb` asset from
[Releases](https://github.com/Ashus/streamripper/releases), replacing
`<version>` below with the downloaded version:

```sh
sudo apt install "./streamripper_<version>-1_amd64.deb"
streamripper --version
```

APT installs the required shared libraries and CA certificates. Release packages
are built and tested on Debian 13 amd64. They require the library versions and
package names listed in their `Depends` field; older Debian/Ubuntu releases or
other architectures may need a source build. No ARM or i386 DEB is produced.
The package installs `/usr/bin/streamripper`, guides under
`/usr/share/doc/streamripper`, and the sample rules in its `examples` subdirectory.
It uses the package/command name `streamripper`, replacing an older installed
package of that name when APT upgrades it. It creates no service or recording
directory. Remove it with `sudo apt remove streamripper`.

## Requirements and dependencies

The commands below were validated on Debian 13 x86_64 under WSL2, using GCC 14.2,
GLib 2.84.4 and OpenSSL 3.5.7. Other distributions need equivalent packages;
they have not been validated by these tests.

Install Git, a C/C++ build toolchain, CMake 3.16 or newer, Ninja, pkg-config,
GLib 2.66 or newer, OpenSSL 3.x, libmad, FAAD2, libogg and libvorbis development headers.
Iconv is provided by glibc on Debian. CA certificates provide the normal system
trust store; certificate and hostname/IP verification remain enabled by default.

On Debian or Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build pkg-config libglib2.0-dev libmad0-dev libfaad-dev libogg-dev libvorbis-dev libssl-dev ca-certificates
```

Python 3.10 or newer, the OpenSSL CLI, vorbis-tools and the FLAC reference decoder are needed for the optional
integration tests below. They are not runtime dependencies of the recorder.
Streamripper does not require FFmpeg.

## Obtain the sources

```sh
git clone https://github.com/Ashus/streamripper.git
cd streamripper
```

Use the desired release tag or development checkout. `VERSION` is the source of
the executable version; configuration reads it automatically.

## Configure and build

Run from the repository root:

```sh
cmake -S . -B "$HOME/.cache/streamripper-build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$HOME/.cache/streamripper-build" --parallel
"$HOME/.cache/streamripper-build/streamripper" --version
```

The executable is `$HOME/.cache/streamripper-build/streamripper`. It uses shared
system libraries. Keep their runtime packages installed; copying the executable
alone to another machine does not bundle those libraries.

A different Linux-only build directory is fine. Use the same path in subsequent
commands. `-DBUILD_TESTING=OFF` omits the regression executables.
Re-run configuration after changing dependencies, options or `VERSION`.

## Run or install

Running directly from the build tree requires no installation:

```sh
"$HOME/.cache/streamripper-build/streamripper" "https://example.com/radio" -d "$HOME/Radio" --tls-info
```

Replace the example URL with a real MP3, AAC, Ogg Vorbis, FLAC or supported HLS stream. Ctrl+C requests
normal finalization and incomplete cleanup; SIGTERM follows the same path.
SIGKILL and power loss cannot perform cleanup.

Optional installation for the current user:

```sh
cmake --install "$HOME/.cache/streamripper-build" --prefix "$HOME/.local"
"$HOME/.local/bin/streamripper" --version
```

Add `$HOME/.local/bin` to PATH if needed. For a system installation, choose the
prefix explicitly and run the install command with appropriate privileges.
Direct CMake installation copies the CLI and static library; it does not create
a service. DEB packaging below includes only the runtime and documentation.
Keep [METADATA-RULES.md](METADATA-RULES.md) and the example rules
available if you use custom metadata filtering.

## Build the amd64 DEB

On native Debian amd64 (including WSL), install the build dependencies above and
the additional packaging tools:

```sh
sudo apt-get install -y python3 dpkg-dev
python3 package-linux.py
```

The helper configures and builds in `$HOME/.cache/streamripper-build`, then runs
CPack DEB with automatic shared-library dependencies from `dpkg-shlibdeps`.
CPack staging stays in a temporary directory under that Linux build directory
and is removed on success or failure. The release directory receives only the
completed DEB and checksum, preserving an earlier artifact if CPack fails.
It reads the release version from `VERSION` and creates:

- `build/release/streamripper_<version>-1_amd64.deb`
- `build/release/streamripper_<version>-1_amd64.deb.sha256`

These share the Windows archive's output directory. The `-1` is the Debian
packaging revision; the application version still comes from `VERSION`.
The package includes the executable, documentation, license and sample rules,
without static libraries, headers or bundled runtime libraries.
Neither packaging command publishes anything.

Defaults are relative to the script's repository, so it can be called from any
working directory. Explicit relative arguments use the caller's directory:

```sh
python3 package-linux.py --build-dir "$HOME/.cache/streamripper-build" --output-dir ./build/release
```

Keep the Linux CMake cache separate from the Windows `build` directory. The
helper rejects that Windows directory and verifies the resulting amd64 ELF.
The Linux CMake build also exposes a `package` target when `dpkg-dev` is
available on amd64; use the Python helper for version checks and SHA-256 output.
The raw CMake `package` target writes to the Linux build's `packages` directory;
the helper copies validated artifacts into the shared release directory.

### Validate and upload manually

```sh
sudo apt-get install -y fakeroot
python3 testing/debian_package.py "build/release/streamripper_$(cat VERSION)-1_amd64.deb"
python3 testing/debian_packaging.py
```

The test checks metadata, generated runtime dependencies, payload, root ownership,
permissions and checksums. It installs and removes the package with `fakeroot`
in a disposable root, checks the executable and bundled guides, and leaves the
host package database unchanged. It does not need `sudo`.

After both OS workflows pass, manually attach the Windows `.7z`, Linux amd64
`.deb`, and both SHA-256 files from `build/release` to the same release. The
Debian package is not a portable bundle for arbitrary Linux distributions.

## Tests

Install the optional test tools:

```sh
sudo apt-get install -y python3 openssl vorbis-tools flac
python3 testing/fetch-samples.py
```

The fixture downloader verifies pinned hashes and writes audio fixtures under
the checkout's ignored `build` directory. Fixtures are not committed. Download
once; subsequent tests use local servers and do not contact radio stations.

Run from the repository root:

```sh
ctest --test-dir "$HOME/.cache/streamripper-build" --output-on-failure
python3 testing/linux_smoke.py "$HOME/.cache/streamripper-build/streamripper"
python3 testing/metadata_filter.py "$HOME/.cache/streamripper-build/streamripper"
python3 testing/aac_silence.py "$HOME/.cache/streamripper-build/streamripper" "$HOME/.cache/streamripper-build/aac_silence_regression"
python3 testing/recording_lock.py "$HOME/.cache/streamripper-build/streamripper"
python3 testing/live_updates.py "$HOME/.cache/streamripper-build/streamripper"
python3 testing/terminal_width.py "$HOME/.cache/streamripper-build/streamripper"
python3 testing/stream_capture.py "$HOME/.cache/streamripper-build/streamripper" /usr/bin/openssl --flac flac
python3 testing/https_smoke.py "$HOME/.cache/streamripper-build/streamripper" build/sample-5s.mp3 /usr/bin/openssl
```

The allocation test writes into its build directory. Python recording tests use
the normal temporary directory. The AAC silence test requires real AAC fixtures:
generate them once with its `--faac /path/to/faac` option (FAAC is test-only).
A WSL checkout can reuse `build/aac-silence-fixtures` generated by the Windows
test command above. FAAC is not available in every distribution's default
repositories; build it from [its source](https://github.com/knik0/faac) if needed.
The AAC tests cover mono/stereo decoded silence, padding, disabled detection,
malformed/truncated input and preservation of compressed frames.

Other Python recording tests use
the normal temporary directory (usually `/tmp`); set `TMPDIR` to another
existing directory if needed. Use ext4 for allocation validation. In WSL, check
with `findmnt -T "$HOME"` and `findmnt -T /tmp`; a Windows-mounted directory
usually reports `9p`/DrvFS and does not establish ext4 allocation behavior.

Validated coverage:

- Five-second atomic rule reloads, invalid/missing-file retention, current-title
  reparsing, filter transitions and metadata-log deduplication.
- Recovery after DNS, dropped connections and HTTP 503/429, Retry-After waits,
  fatal/initial failures, disabled retries and interruption during backoff.
- Real PTY resizing with long titles, combining/wide Unicode and stale-row erasure.
- HLS variants, relative redirects, live overlap/gaps, AES-128, partial-segment
  retries, unsupported formats and continuous output; native/ICY FLAC byte
  preservation and normalized Ogg FLAC duration/seek/shutdown with reference-decoder
  validation and final reservation/lock cleanup.
- Real 2 MiB allocation reservations without extending logical EOF, boundary
  growth, release at finalization and safe fallback on allocation failure.
- Actual MP3, synthetic ADTS AAC, decoded WAV and Ogg Vorbis recordings, plus
  reservation release after closing files. Synthetic AAC tests framing, not decoding.
- SIGINT/SIGTERM shutdown: audio finalization, short incomplete removal and
  `.lock` removal.
- Metadata filtering, Unicode paths, numbering, continuous MP3/WAV recordings,
  and timestamped metadata logging before rules.
- Directory ownership with `flock`, concurrent starts, reconnects, Unicode and
  alias paths, separate continuous-output directories, normal restart and
  recovery after forced termination.
- HTTP/HTTPS, redirects, chunked transport, Unicode ICY titles, relay shutdown,
  TLS 1.2/1.3, RSA/ECDSA, untrusted certificates, hostname mismatch, custom CA
  files and explicit `--insecure`.

This is targeted Debian/WSL validation, not a claim that every inherited feature
or every Linux filesystem/distribution has been tested. Windows-specific console
and DLL packaging tests belong to the Windows guide.

## Allocation and lock behavior

On Linux, audio outputs use `fallocate(FALLOC_FL_KEEP_SIZE)` in 2 MiB increments
where supported. Normal finalization releases reservations beyond EOF with
`ftruncate` at the current file length. Unsupported filesystems or allocation
failure fall back to ordinary recording. Preallocation can reduce fragmentation,
but does not guarantee a contiguous file or defragment existing data.

The `incomplete/.lock` file contains the owner's PID and is protected by an OS
lock across reconnects. Normal shutdown removes it after recording cleanup.
A crash can leave the file behind; a stale PID does not block the next process.
Do not manually remove a live recorder's lock.
