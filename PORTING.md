# Fork review and provenance

This Windows fork starts at upstream commit `248f771250998e38a2d8212aa43ccc4a516f853b`.
Release numbers belong to this independent fork; they do not claim an official upstream release.
The first public release is **1.68.0**, without a feature-specific version suffix.

## Reviewed forks

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
