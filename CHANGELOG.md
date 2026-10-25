# Changelog

## 1.68.2

### Features

- Record HLS master/live playlists with variant selection, AES-128 decryption
  and verified HTTPS, preserving MPEG-TS or packed ADTS AAC.
- Record native and Ogg FLAC streams without transcoding, with ICY metadata
  stripping and optional title logging.
- Provide Linux amd64 DEB packages with automatic runtime dependencies, guides
  and SHA-256 checksums alongside the Windows portable release.
- Find quiet ADTS AAC track boundaries with FAAD2, honoring silence windows,
  offsets, padding and `--xs-none` while preserving compressed audio unchanged.
- Add opt-in `--reload-rules` to apply validated metadata-rule changes every five
  seconds without reconnecting. Re-evaluate the current title and retain the
  previous rules if an update is invalid or unavailable.
- Expand recording status to the terminal width, handle resizing and preserve
  Unicode characters. Show the input format and container in the startup summary.
- Include the version, build timestamp and project URL in help and version output.

### Fixes

- Recover from temporary network and DNS failures after a successful connection,
  with interruptible five-second retries and support for longer HTTP
  `Retry-After` delays. Permanent configuration, authentication, certificate
  and output errors still stop recording.
- Fix Ogg FLAC duration and seeking for live encoders with absolute counters.
  Finalize sample counts and EOS pages, discard partial final frames/pages, and
  use recorded sample counts for progress and incomplete-file retention.
- Retry complete HLS segments without appending partial transfers or silently
  skipping unavailable audio. Continue HLS/FLAC recording when shared command
  lines include `--relay`, warning once and reporting no active relay.
- Decode ICY station names consistently in console output and paths. In AUTO
  mode, use Latin-1 for non-UTF-8 Icecast MP3/AAC metadata while preserving valid
  UTF-8 and explicit encoding overrides.
- Use `Unknown station` consistently for missing station names and `-` for
  missing track titles; show available FLAC ICY titles separately.

### Documentation

- Add a format capability table and consolidate metadata-rule guidance in
  METADATA-RULES.md with one concise rules-examples.txt example.

## 1.68.1

### Breaking changes

- User INI files are no longer loaded or saved automatically. Existing settings
  apply only when explicitly loaded with `--config FILE`. Configuration files
  are read-only; command-line options override their values.

### Features

- Detect UTF-8 ICY metadata automatically, with a legacy-encoding fallback.
  Explicit encoding options and explicitly loaded configuration remain supported.
- Add `--metadata-log` to save timestamped ICY title changes before filtering or
  rewriting in `incomplete/metadata.log`, independently of recorded filenames.
- Add `--incomplete-min-duration=N[s|m|h]` to remove short incomplete recordings
  by actual audio duration. Completed tracks, older files and continuous
  recordings are preserved; unknown durations and failed outputs are kept.
- Show track duration as `MM:SS` or `H:MM:SS`, falling back to readable byte
  sizes when unknown. Distinguish `incomplete`, `filtered` and `ripping` states,
  and show the final stream hostname/port and relay bind address.
- Reserve audio disk space in 2 MiB increments to reduce fragmentation on Windows
  and Linux ext4. Unused reservations are released during normal finalization;
  unsupported filesystems and allocation failures fall back to ordinary writes.
- Prevent concurrent recordings into the same directory with `incomplete/.lock`.
  Ownership survives reconnects, normal shutdown removes the lock file, and
  stale files after a crash do not block a new recording.
- Add accurate per-file MP3 duration headers and AAC/MP3 duration tags without
  re-encoding or FFmpeg, including the final incomplete recording.
- Add IPv6 HTTP/HTTPS and relay support, with IPv4 fallback and `-4` / `--ipv4`
  and `-6` / `--ipv6` selection. Relay and outgoing address families are independent.
- Support HTTPS stations through HTTP CONNECT proxies and HTTPS proxies,
  including authentication and independent certificate verification for each
  TLS connection. `--tls-info` reports origin and proxy details once per run.
- Add descriptive long aliases for existing CLI options while retaining short
  forms. Accept spaced and equals values and reorganize command-line help.
- Restore Linux source builds and validate recording, HTTPS, ext4 allocation,
  locks and signal cleanup on Debian 13/WSL2. Provide separate complete Windows
  and Linux build guides; Linux users build from source without `.deb` packages.
- Include a complete metadata-rule guide and generic filtering examples.
  Resolve the default Windows toolchain relative to the repository.

### Fixes

- Restore metadata-rule exclusion, including WAV output, without gaps in saved
  track numbering. Support Unicode rule paths and detect save/exclude changes
  even when artist and title stay the same.
- Fix automatic `%q` numbering at the start of Windows filenames and after restart.
- Preserve Unicode in Windows consoles regardless of code page, keep redirected
  output in UTF-8, and avoid cutting multi-byte characters when shortening titles.
- Finalize recordings and check incomplete retention when closing the Windows
  console, as well as during normal shutdown.
- Prevent overlapping Windows relay listeners from sharing a port; scan for the
  next available port instead of reporting a listener that conflicts with another.
- Align AAC cuts to complete frames, preserve the final byte of MP3 split frames,
  and replace stale source duration headers.
- Keep incomplete cleanup tied to each writer, report failed file promotions,
  and avoid accumulated rounding errors in AAC/MP3 duration calculations.
- Remove unwanted `dump.mp3` and `gcs1.txt` writes. Recover stalled alternative
  silence detection without terminating the recorder.
- Fix packaging into a fresh directory and consistently reject invalid timeout
  values or values supplied to flags that take no argument.

## 1.68.0

First public release of this independent Windows CLI fork, based on upstream
`1.65.0-alpha` at `248f771250998e38a2d8212aa43ccc4a516f853b`.

### Features

- Native HTTPS with OpenSSL 3.x: TLS 1.2/1.3, SNI, certificate/hostname/IP
  verification, Windows root certificates, and custom CA files.
- `--tls-info`, `--tls-ca-file=...`, and explicit `--insecure`.
- HTTP/1.1 chunk decoding before ICY processing, including extensions and trailers.
  `--http10` provides compatibility with older servers.
- Optional `--wav` decodes MP3 recordings into 16-bit PCM WAV, preserving sample
  rate and mono/stereo channels. Works for tracks and single-file recordings;
  completed and incomplete files receive valid RIFF headers.
- Modern 64-bit CMake/Ninja/MinGW-w64 CLI build, local portable `dist` output,
  release 7z archive, SHA-256 sums, dependency versions, and runtime licenses.
- UTF-8 console/arguments and native Unicode Windows file operations.

### Fixes

- Restore MP3 relay audio and ICY metadata, correct buffer recycling, avoid
  empty-buffer busy loops, disconnect the correct client, and stop relay threads
  before releasing shared buffers.
- Correct Windows socket timeout units, pending TLS reads, zero-byte writes,
  semaphore behavior, and thread handles used for joining workers.
- Release parser regexes/matches/substitutions, conversion buffers, pending writers,
  WAV decoder state, pipe/process/thread/socket handles, and configuration objects.
- Clean up after failed connections and repeated stop calls; balance Winsock startup.
- Bound station/header strings to their actual destination sizes, terminate received
  metadata explicitly, validate HTTP status parsing, and build bounded relay responses.
- Parse URLs with GLib GUri, preserve query delimiters/encoding, resolve relative
  redirects, and limit redirect chains to 10 hops.
- Correct preference comparisons, case-insensitive metadata rules, and malformed
  final rule handling. Keep CUE files as text rather than inserting binary ID3 strings.
- Handle partial file writes and propagate output failures.
- Modernize 64-bit list offsets and compiler declarations; repair the Ogg cleanup status.

### Removed

- Winamp plugin/GUI and preferences, bundled obsolete Windows SDKs/DLLs/projects,
  unused Cbuf2/curses code, ad-hoc test programs, and obsolete distribution scripts.
- License notices for unrelated build tools from the portable runtime package.
