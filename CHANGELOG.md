# Changelog

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

### Known limitations

- `--wav` accepts MP3 input only. It is not support for recording arbitrary WAV
  source streams, does not recover quality lost during MP3 encoding, and stops at
  the classic RIFF 4 GiB limit or a sample-rate/channel change.
- HTTPS through HTTP proxy `-p` needs CONNECT and is not implemented.
- IPv4 transport; HLS/DASH and FLAC are not supported by this release.
- Inherited path/track limits remain, notably 254 bytes; Unicode may reach them sooner.
- See [PORTING.md](PORTING.md) for fork provenance, issue review, test scope, and
  functions intentionally left for a later release.
