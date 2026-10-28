# Fork review and provenance

This Windows fork starts at upstream commit `248f771250998e38a2d8212aa43ccc4a516f853b`.
Release numbers belong to this independent fork; they do not claim an official upstream release.
The first public release is **1.68.0**, without a feature-specific version suffix.

## Reviewed forks

The historical Debian HTTP security patches were also reviewed against this
fork. See [SECURITY-REVIEW.md](SECURITY-REVIEW.md) for the CVE mapping, independent
fixes, regression coverage and validation limits.

- [Magentron / PR #7](https://github.com/streamripper/streamripper/pull/7/commits),
  revision `710c1b6d78df61a05cccdbac5e05b384e89e736a`.
  Its functional core uses the older Cbuf2 architecture, whereas this fork retains
  and repairs upstream's Cbuf3 core. Wholesale cherry-picking is not appropriate.
- [jaakristioja / Jakub Kaprzyk fork](https://github.com/jaakristioja/streamripper),
  revision `71f6831`, version `3.7.1-qbus`.
  This fork retains the Cbuf3 lineage but has removed Windows support.

## Adapted fixes and features

- **Alex Thiessen**: regex match/substitution lifetime fixes and conversion-buffer
  cleanup reviewed from Magentron's history, including `725b924`, `fd93101`,
  `211f9fb`, `58e72eb`, and `daeb78b`. This implementation additionally
  releases compiled regexes, owns default-rule strings, and handles malformed final rules.
- **Jeroen Derks**: preference comparison fixes and bounded external metadata copies
  from `710c1b6`; the `--http10` compatibility option is based on the feature
  in `2eafe7e`. Existing URL and request-buffer fixes were compared with his fork.
- **Dmitry Mikushin**: optional MP3-to-WAV feature and stateful-decoder concept,
  reviewed through `840176e` and `54f05a0` in Magentron's fork.
  The decoder is reimplemented around libmad's low-level streaming API: partial
  frames survive input boundaries, mono/stereo sizes are correct, each file has
  independent state, and RIFF headers are finalized for completed and incomplete files.
- **Jakub Kaprzyk**: long ICY-name destination bounds and non-terminated metadata
  audit reviewed against `cf9e93c`. Winamp preference removal was checked against
  `e3a1240`. The corresponding source changes are adapted to the Windows implementation.
  CONNECT proxy flow was also reviewed against his
  [`56b6903`](https://github.com/jaakristioja/streamripper/commit/56b690331bbd473a47cb2e72644bf08b2f356ce4).
  The local implementation adds bounded response parsing, all successful 2xx
  statuses, informational-response handling, IPv6 authorities, and verified
  HTTPS-proxy TLS layering; these parts were implemented independently.

Commit messages identify original authors and source revisions where changes were adapted.
New HTTP chunk decoding, Windows thread/handle lifetime fixes, and regression tests
were implemented locally; they are not a wholesale import of either fork.

## HLS and FLAC

The whole-stream recording scope was reviewed against **Jakub Kaprzyk**'s
[jaakristioja/streamripper](https://github.com/jaakristioja/streamripper/tree/71f683145246b2bd8c1bcf3e55cc2d9d3c4878c7),
revision `71f683145246b2bd8c1bcf3e55cc2d9d3c4878c7` (`3.7.1-qbus`).
The implementation in `lib/stream_capture.c` was written independently around
this fork's verified HTTP/TLS/proxy transport and cross-platform file writers;
it is not an import of the other fork's separate HLS networking/output code.

HLS records complete MPEG-TS or packed ADTS AAC segments without demuxing or
transcoding. It supports master variants, live sequence tracking, ENDLIST and
AES-128 CBC with sequence-derived or explicit IVs. Failed transfers do not
advance the sequence or append partial bytes. Unsupported containers, encryption,
byte ranges and skipped live windows fail explicitly. Separate audio renditions
are currently rejected rather than silently recording a video-only variant.
Native FLAC retains its original bytes. Ogg FLAC is repaged with relative granules
and frame/sample numbers, corrected checksums, sample counts and EOS, preserving
compressed audio without re-encoding. These normalization/finalization changes
were implemented locally in `lib/ogg_flac_output.c`; they are not imported from
the reviewed fork. Neither FLAC path is split by ICY titles.
There is no FFmpeg or libFLAC runtime dependency.

`testing/stream_capture.py` uses local HTTP/HTTPS servers, generated transport
packets, AES fixtures and generated valid constant-sample FLAC/Ogg FLAC.
It checks variant selection, relative redirects, live overlap, AES IVs,
partial-segment retry, chunked bodies, Unicode paths, ICY stripping and lock
removal. Ogg tests include absolute live counters, frames spanning pages,
interruption inside pages/packets, continuous output, console/signal shutdown,
sample-based retention and header finalization. Optional `--flac flac` validates
both FLAC containers and checks seeked PCM against the expected samples.
Transport-packet and ADTS fixtures test byte preservation/framing;
they do not claim decoded MPEG-TS/AAC playback validation.

## AAC silence detection

The FAAD2 analysis path is adapted from **Jakub Kaprzyk**'s
[AAC silence detection change](https://github.com/jaakristioja/streamripper/commit/7bbfb7d6a01095c64e304cdd7c3eb3f5efa2482a).
It keeps this fork's libmad MP3 path and native ADTS finalization. Complete AAC
frames are decoded only to locate a quiet split; recorded packets are unchanged.
This adaptation validates frame extents, handles decode failures by retaining the
metadata boundary, uses the decoded HE-AAC/SBR sample rate, combines all channel
energies without phase cancellation, and returns exclusive frame-aligned cuts.
Like the reviewed port, AAC uses the standard detector with `--xs2`.

`testing/aac_silence.py` generates real AAC-LC fixtures using a test-only FAAC
encoder: mono 44.1 kHz, stereo 48 kHz and opposite-phase stereo. It verifies
decoded quiet intervals, padding, truncated input, malformed-input fallback,
local ICY splitting, disabled detection and unchanged compressed frame payloads.
These fixtures do not establish HE-AAC/SBR coverage.
Ogg Vorbis continues to split at native logical-stream boundaries and metadata;
silence trimming is deliberately not applied.

## Deliberately not imported

- Neither fork's TLS defaults: this release verifies certificate chains and hostnames
  by default. A user must explicitly request `--insecure` to bypass authentication.
- Magentron's Cbuf2-specific locking changes and its switch from `execvp` to `execve`.
  The latter changes PATH lookup and does not by itself prevent command injection.
- Autotools-generated artifacts, bundled test frameworks, cross-build containers,
  and changes that only support removed Winamp/Windows targets in another fork.
- minimp3, AAC/Ogg-to-WAV,
  flexible duration syntax, and broader numeric-option validation. These remain
  candidates for a subsequent Windows release and require separate porting/tests.
- No claim is made that this review found every inherited defect.

## Upstream issues

- [#6: relay clients fall behind](https://github.com/streamripper/streamripper/issues/6):
  the current relay passes two simultaneous readers across 280 consecutive 16 kB
  blocks each, with audio continuity and ICY checks while the circular buffer recycles.
  This exercises the reported symptom; it does not establish the original bug's cause.
- [#11: HTTPS crash in 1.64.6](https://github.com/streamripper/streamripper/issues/11):
  native verified HTTPS replaces the unsupported legacy path. Local TLS tests cover
  successful connections and rejection/error paths; the issue's live URL is also checked.
- [#10: Arch Linux buffer-overflow report](https://github.com/streamripper/streamripper/issues/10):
  the report lacks a stream URL, backtrace, or reproducer. Its specific root cause
  cannot be established from the report. Relevant shared buffer/metadata boundaries
  are hardened and covered by malformed/long-header tests.

## Memory and resource checks

`gcs_notes.txt` is retained as historical material pending review of its remaining
open items. The first-track skipping status now retains the displayed track's
decision before the writer count is incremented, matching the zero-based writer
index used for dropping.
The old libmad Valgrind warning has not been reproduced or ruled out with a
memory-access checker on the current library. Current tests exercise MP3, WAV,
generated Ogg Vorbis, native FLAC preservation, normalized/seekable Ogg FLAC and synthetic ADTS framing.
Synthetic ADTS and HLS transport fixtures do not validate decoded AAC playback.
Windows 98, Winamp, CVS, old SDK build instructions, and sample server headers in
that file are historical and are not supported build instructions for this fork.

The Windows regression program processes 100,000 metadata changes, creates/destroys
1,000 rule sets, and starts/stops 200 failed connection managers in one process.
WAV tests create and release 100 decoders for each mono/stereo fixture.
Tests check private-byte growth and process handle counts, with allocator slack;
these are regression checks, not proof of the absence of every leak or invalid access.
No AddressSanitizer or Valgrind result is claimed for this Windows build.

## Test audio

Audio is downloaded separately and is not committed. The provider describes these
samples as having no license restrictions:

- [Samplelib MP3 samples](https://samplelib.com/sample-mp3.html):
  `https://samplelib.com/mp3/sample-5s.mp3`
  SHA-256: `b3cb356d6ab822252c7199ab2bb692620af7eef07c3ff22618e0a352d7d77d9f`
- [Samplelib WAV samples](https://samplelib.com/sample-wav.html):
  `https://samplelib.com/wav/sample-3s.wav`
  SHA-256: `b3726eac5c9612ea20e245314812575bf9df5fb6b8024b80c7cfe9033452bb2b`

The optional test-only LAME encoder creates a mono MP3 from the public WAV sample.
It is not a runtime dependency and is not packaged in `dist`.

## Runtime implementation notes

These details complement the user-facing behavior described in [README.md](README.md).

### TLS and transport

OpenSSL negotiates algorithms supported by both endpoints. The release was tested with:

- TLS 1.2: `ECDHE-ECDSA-AES128-GCM-SHA256`, X25519 key exchange, `ecdsa_secp256r1_sha256` signatures.
- TLS 1.3: `TLS_AES_256_GCM_SHA384`, X25519MLKEM768 key exchange, RSA-PSS and ECDSA signatures.

ECDSA authenticates the server; ephemeral ECDHE/key exchange provides forward
secrecy. Supporting PFS does not imply every TLS 1.2 peer will select it: that
depends on negotiation and the server's configuration. Use `--tls-info` to inspect
a particular connection.

The HTTPS integration tests use local servers and generated RSA/ECDSA
certificates. They cover certificate verification, explicit verification bypass,
custom CA files, ECDHE/ECDSA with TLS 1.2, TLS 1.3, and redirects. Proxy tests check
certificate identities independently for the proxy and origin TLS layers.
See the [Windows tests](BUILDING-WINDOWS.md#tests) or
[Linux tests](BUILDING-LINUX.md#tests) for commands and requirements.

Each redirect opens the appropriate proxy connection and, for HTTPS stations, a
new CONNECT tunnel. `--http10` affects the station GET request; CONNECT uses
HTTP/1.1. CONNECT responses are bounded and timed out, and unsuccessful proxy
authentication is reported as HTTP 407.

HTTP and HTTPS streams support both IPv4 and IPv6. Hostnames are resolved with
`getaddrinfo()`; connection attempts are staggered by 250 ms so a slow or
unreachable address does not block trying the next one. Pending TCP attempts
share the connection timeout (`--timeout`); DNS lookup is outside that
timeout. The first successful TCP connection is used, and other attempts close.
TLS verification still checks the original hostname or the literal IP address.

Stream requests use HTTP/1.1 by default, with `Host`, `Accept: */*`,
`Cache-Control: no-cache`, the configured `User-Agent`, `Icy-Metadata: 1`, and
`Connection: close`. `--http10` changes the request version to HTTP/1.0.
The relay responds with `ICY 200 OK`. The retained page-request helper uses
HTTP/1.1, a Chrome/152 browser User-Agent, `Accept-Encoding: identity`, and
`Connection: close`; it is not called by the current CLI recording path.

HTTP/1.1 chunked bodies are decoded before ICY metadata parsing. Redirects allow
relative locations and at most ten hops. GLib GUri preserves query strings,
explicit ports and percent encoding. HTTPS uses OpenSSL with SNI and separate
certificate and hostname/IP verification for each TLS endpoint.

### Console and metadata conversion

Windows console output uses the Unicode `WriteConsoleW` API, independently of
the active code page; `chcp 65001` is not required. Redirected output and Linux
console output use UTF-8. Command-line arguments use UTF-8. File operations use Windows
Unicode APIs, including directory enumeration for sequence numbering;
`--codeset-filesys` does not change those operations.

Metadata encoding is checked for each update before rule processing, allowing
UTF-8 and legacy titles to alternate within one connection. The AUTO fallback
policy is documented under [metadata encoding](README.md#metadata-encoding).

The status shows incoming MP3/AAC audio duration instead of byte size, including
excluded tracks and MP3 input recorded as WAV. It uses complete audio-frame
sample counts, not elapsed download time or a bitrate estimate. The display is
`MM:SS`, or `H:MM:SS` from one hour onward, rounded down to whole seconds.
When duration is unavailable (including native Ogg input), the display falls
back to received byte size, for example `75kB` or `14,81MB`. Units are decimal
(1 kB = 1000 bytes, 1 MB = 1000000 bytes); smaller values use `B`.
The counter resets when the displayed track changes; metadata timing and split
padding can make it differ from the final file's duration.

Interactive status lines adapt to the terminal's visible width, including after
resizing. Titles use the remaining columns and old text is cleared when they
shrink. Wide Unicode characters and combining marks are counted by display
width. Redirected output retains a fixed 50-column title limit.

### Allocation, duration and finalization

Audio outputs automatically reserve disk space in 2 MiB increments to reduce
fragmentation during recording. Windows uses allocation-size reservations; the
Linux implementation uses `fallocate(FALLOC_FL_KEEP_SIZE)` where supported,
including ext4. Reservations do not change the readable file length or add audio
padding. Unused space is released during normal finalization. If reservation
fails or is unsupported, recording continues with ordinary writes.

This applies to individual MP3/AAC/Ogg/WAV recordings and continuous audio files,
not CUE files or metadata logs. It does not guarantee contiguous allocation or
defragment existing files. Linux preallocation and recording cleanup have been
validated on ext4 under Debian 13/WSL2. On Linux, forced termination can
leave reserved blocks beyond EOF until the file is truncated to its actual size.

The duration is measured from stored audio, rather than file size, bitrate
estimates, or elapsed download time. MP3/AAC use complete frame sample counts,
WAV uses the decoded PCM length, and Ogg Vorbis uses granule positions and the
stream sample rate, including chained streams. A duration that cannot be
determined, or a failed write/finalization/promotion, prevents automatic deletion.

Individual AAC recordings retain only complete ADTS frames. Track boundaries are
aligned to verified frame headers; a partial frame received when joining or
stopping a stream is discarded. AAC stays in `.aac`, with no M4A muxer or FFmpeg
dependency.

MP3 and ADTS AAC individual recordings search decoded audio for a quiet split
near each metadata change by default. AAC analysis uses FAAD2; recorded AAC
frames are copied unchanged. `--xs-none` disables the search, while window,
silence-length, offset and padding settings tune it. AAC uses the standard
detector even with `--xs2` (the alternative detector is MP3-only). If AAC analysis
fails or has insufficient data, recording retains the ADTS-aligned metadata
boundary. This does not add AAC-to-WAV support.

Ogg Vorbis recordings use logical-stream boundaries and their native metadata,
without silence detection or trimming. A single uninterrupted logical stream
does not itself identify individual songs.

Individual MP3 recordings receive a fresh Xing/Info header with their own audio
frame and byte counts when closed. Stale source Xing/Info/VBRI headers are removed.
This supplies duration information for both CBR and VBR recordings; it does not
add a seek table or reconstruct encoder delay/padding for gapless playback.
The native parser handles MPEG-1/2/2.5 Layer III with indexed bitrates; free-format
MP3 and other MPEG audio layers are not handled by this finalizer.
Indexed MP3 audio is limited to 4 GiB per individual file, and a sample-rate or
channel-count change within a file is rejected.

Both formats retain the existing ID3v2.3 artist/title tags and add `TLEN`
(duration in milliseconds) during finalization. `--no-id3` disables ID3 tags;
`--without-id3v2` disables ID3v2 specifically. Raw AAC players may ignore ID3 tags.
Compressed audio frames are copied without re-encoding.

Finalization also covers the last open individual file left in `incomplete` on
normal shutdown. Forced process termination or a power failure can prevent it.
Continuous `--continuous-file` MP3/AAC recordings keep the original stream bytes and do not use
this individual-file finalizer. WAV sizes are finalized separately; native Ogg
recording remains page-based.

Ogg FLAC finalization also clears the source PCM MD5, which describes the
original stream rather than the recording. Source seek/cue offsets are replaced
with padding; seeking uses rebuilt relative granules and FLAC frame numbers.
HLS response limits are 1 MiB per playlist, 16 bytes per AES key and 128 MiB per
segment. AES-128 CBC uses explicit or media-sequence-derived IVs.
