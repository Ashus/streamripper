# Building and testing on Linux

This guide builds the CLI from source. Linux does not use the Windows toolchain,
DLL bundle, packaging scripts or `dist` directory. No `.deb` packages are produced.
The Windows workflow is documented independently in [BUILDING-WINDOWS.md](BUILDING-WINDOWS.md).

## Requirements and dependencies

The commands below were validated on Debian 13 x86_64 under WSL2, using GCC 14.2,
GLib 2.84.4 and OpenSSL 3.5.7. Other distributions need equivalent packages;
they have not been validated by these tests.

Install Git, a C/C++ build toolchain, CMake 3.16 or newer, Ninja, pkg-config,
GLib 2.66 or newer, OpenSSL 3.x, libmad, libogg and libvorbis development headers.
Iconv is provided by glibc on Debian. CA certificates provide the normal system
trust store; certificate and hostname/IP verification remain enabled by default.

On Debian or Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build pkg-config libglib2.0-dev libmad0-dev libogg-dev libvorbis-dev libssl-dev ca-certificates
```

Python 3.10 or newer, the OpenSSL CLI and vorbis-tools are needed for the optional
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
commands. `-DBUILD_TESTING=OFF` omits the allocation regression executable.
Re-run configuration after changing dependencies, options or `VERSION`.

## Run or install

Running directly from the build tree requires no installation:

```sh
"$HOME/.cache/streamripper-build/streamripper" "https://example.com/radio" -d "$HOME/Radio" --tls-info
```

Replace the example URL with a real MP3, AAC or Ogg Vorbis stream. Ctrl+C requests
normal finalization and incomplete cleanup; SIGTERM follows the same path.
SIGKILL and power loss cannot perform cleanup.

Optional installation for the current user:

```sh
cmake --install "$HOME/.cache/streamripper-build" --prefix "$HOME/.local"
"$HOME/.local/bin/streamripper" --version
```

Add `$HOME/.local/bin` to PATH if needed. For a system installation, choose the
prefix explicitly and run the install command with appropriate privileges.
Installation copies the CLI and static library; it does not create a package or
a service. Keep [METADATA-RULES.md](METADATA-RULES.md) and the example rules
available if you use custom metadata filtering.

## Tests

Install the optional test tools:

```sh
sudo apt-get install -y python3 openssl vorbis-tools
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
python3 testing/recording_lock.py "$HOME/.cache/streamripper-build/streamripper"
python3 testing/https_smoke.py "$HOME/.cache/streamripper-build/streamripper" build/sample-5s.mp3 /usr/bin/openssl
```

The allocation test writes into its build directory. Python recording tests use
the normal temporary directory (usually `/tmp`); set `TMPDIR` to another
existing directory if needed. Use ext4 for allocation validation. In WSL, check
with `findmnt -T "$HOME"` and `findmnt -T /tmp`; a Windows-mounted directory
usually reports `9p`/DrvFS and does not establish ext4 allocation behavior.

Validated coverage:

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
