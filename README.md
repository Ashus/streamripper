# Streamripper for Windows and Linux

A fork of the original Streamripper with native HTTPS, IPv6 support, current 64-bit libraries, working ICY track names, and Unicode filenames.

This independent fork is maintained at [Ashus/streamripper](https://github.com/Ashus/streamripper). It records MP3, AAC, Ogg Vorbis, FLAC and supported HLS streams; MP3 can optionally be decoded to WAV.

**[Download Windows portable archives and Linux amd64 DEB packages from Releases](https://github.com/Ashus/streamripper/releases).** On Windows, extract the whole 7z archive: the executable requires its bundled DLLs. Binaries and generated packages are not committed to Git.

The portable release is for Windows. Linux amd64 users can install the `.deb`
package on systems satisfying its Debian runtime dependencies; it is built and
tested on Debian 13. Installation, optional source builds and DEB packaging are
covered by the complete [Linux guide](BUILDING-LINUX.md).
Windows toolchain setup, builds and releases have a separate
[Windows build guide](BUILDING-WINDOWS.md).

## Format support

| Input | Saved format | Individual tracks | Silence-based splitting | WAV conversion | Relay |
| --- | --- | --- | --- | --- | --- |
| MP3 | `.mp3` | Yes, from ICY titles | Yes | Yes, 16-bit PCM | Yes |
| ADTS AAC | `.aac` | Yes, from ICY titles | Yes, with FAAD2 | No | Yes |
| Ogg Vorbis | `.ogg` | Native logical-stream boundaries and comments | No | No | Yes |
| Native FLAC | `.flac` | No, whole stream | No | No | No |
| Ogg FLAC | `.oga` | No, whole stream | No | No | No |
| HLS with MPEG-TS segments | `.ts` | No, whole stream | No | No | No |
| HLS with packed ADTS AAC | `.aac` | No, whole stream | No | No | No |

MP3/AAC metadata rules can clean names and exclude individual tracks. They do
not filter continuous recordings or relay audio. Native Ogg comments use a
separate path and do not run through those rules. Ogg song boundaries depend
on the broadcaster creating separate logical streams; the container cannot
infer songs inside one uninterrupted logical stream.

`--metadata-log` logs available ICY titles for MP3, AAC and FLAC. It does not
extract native Ogg comments or HLS segment metadata. FLAC ICY titles update
the display and log without splitting the recording.

Compressed audio remains in its original codec; `--wav` decodes MP3 to PCM.
HLS does not demux MPEG-TS into separate audio files. Ogg FLAC headers and
timing are normalized without re-encoding. HLS/FLAC ignore `--relay` with a
warning so shared command lines can still record them.
Opus, DASH, fragmented MP4 HLS and AAC in LATM/LOAS are not supported.

## Quick start

After extracting the release archive:

```bat
streamripper.exe "https://strm112.1.fm/highvoltage_mobile_mp3?aw_0_req.gdpr=true" -d C:\Radio --tls-info
```

Multiple instances can record different stations simultaneously. The current track is stored in `incomplete`; completed tracks are moved to their final filenames when metadata changes.

Each recording directory is protected by `incomplete/.lock`, containing the
owner's PID. Another process targeting the same directory exits with an error
before opening recording files. The operating-system lock remains held across
reconnects and is released at shutdown or process termination. Orderly shutdown
removes `.lock` after recording cleanup. A crash can leave the file behind;
a stale PID does not block a new recording. Do not delete
it while a recording is active. Separate directories can be recorded concurrently.
Continuous recordings with a separate show directory lock that directory too.

The console connection summary shows the station name, the final stream hostname
and port after redirects (`host: hostname:port`), and the server software name.
IPv6 addresses use brackets; a proxy does not replace the displayed stream host.

## Configuration

The CLI uses built-in defaults and command-line options. It does not automatically
read or write `streamripper.ini` in AppData, the home directory, or the current
directory. Old implicit configuration files are ignored and can be removed.

To explicitly load an existing UTF-8 INI file, use `--config FILE` or
`--config=FILE` after the stream URL. The file is read-only; command-line options
override its values regardless of option order. A missing/unreadable or malformed
file is an error. Relative paths are resolved from the current working directory.

```bat
streamripper.exe "https://example.com/radio" --config "C:\Radio\settings.ini" --codeset-metadata=AUTO
```

The existing `[stream defaults]` and station sections are supported. For example:

```ini
[stream defaults]
codeset_metadata=AUTO
output_dir=C:/Radio
```

## Reconnection

Automatic reconnect is enabled after the first successful connection. Temporary
network/DNS failures, dropped connections, timeouts and HTTP 408/429/5xx responses
retry after five seconds, or a longer `Retry-After` delay. Each attempt resolves
DNS again. Invalid URLs, certificate/trust errors, authentication failures and
file-output errors stop recording. `--no-reconnect` disables retries; the initial
connection failure still exits. Retry waits can be interrupted normally.

## TLS options

| Option                                 | Description                                                                                                               |
|----------------------------------------|---------------------------------------------------------------------------------------------------------------------------|
| `--tls-info`                           | Print the negotiated TLS version, cipher, key exchange group, server signature, and verification status.                  |
| `--tls-ca-file=C:\certificates\ca.pem` | Add trusted certificate authorities from a PEM file.                                                                      |
| `--insecure`                           | Explicitly disable certificate and hostname verification. Traffic remains encrypted, but the server is not authenticated. |

Native HTTPS uses OpenSSL with SNI and certificate/hostname/IP verification.
With `--tls-info`, the station TLS summary and, when applicable, the proxy TLS
summary are each printed once per recording run, without repetition on reconnect.
Verification is enabled by default, using the OpenSSL system trust store and,
on Windows, Windows root certificates. The minimum protocol is TLS 1.2. TLS option overrides apply
only to the current invocation and are not saved in station preferences.

### Cryptography and verification

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

## HTTP and HTTPS proxies

`-p URL` / `--proxy URL` accepts an HTTP or HTTPS forward proxy. The proxy URL's
scheme controls the connection to the proxy; the stream URL independently
controls the connection to the station.

| Stream | Proxy | Transport                                                                              |
|--------|-------|----------------------------------------------------------------------------------------|
| HTTP   | HTTP  | Absolute-form HTTP GET through the proxy.                                              |
| HTTPS  | HTTP  | HTTP CONNECT, then verified TLS to the station through the tunnel.                     |
| HTTP   | HTTPS | Verified TLS to the proxy, then an absolute-form HTTP GET.                             |
| HTTPS  | HTTPS | Verified TLS to the proxy, CONNECT, then a second verified TLS session to the station. |

```bat
streamripper.exe "https://radio.example/stream" --proxy "http://proxy.example:8080"
streamripper.exe "https://radio.example/stream" --proxy "https://user:password@proxy.example:8443" --tls-info
```

These hostnames are examples. Proxy credentials in the URL use preemptive Basic
authentication. Percent-encode reserved characters in the username/password.
`Proxy-Authorization` is sent to the proxy, while station credentials use
`Authorization` inside the origin request. Digest, NTLM, and automatic system
proxy discovery are not implemented. With an HTTP proxy, CONNECT destinations
and proxy credentials are unencrypted on that hop; an HTTPS proxy encrypts it.

The proxy and station certificates are verified separately against their own
hostnames or IP addresses. Both use the configured trust settings, including
`--tls-ca-file`; a custom bundle can contain the CAs needed for both endpoints.
`--insecure` disables verification for both TLS layers. `--tls-info` labels the
outer connection `TLS proxy:` and the station connection `TLS:`.

Each redirect opens the appropriate proxy connection and, for HTTPS stations, a
new CONNECT tunnel. `--http10` affects the station GET request; CONNECT uses
HTTP/1.1. CONNECT responses are bounded and timed out, and unsuccessful proxy
authentication is reported as HTTP 407.

## IPv4 and IPv6

HTTP and HTTPS streams support both IPv4 and IPv6. Hostnames are resolved with
`getaddrinfo()`; connection attempts are staggered by 250 ms so a slow or
unreachable address does not block trying the next one. Pending TCP attempts
share the connection timeout (`-m` / `--timeout`); DNS lookup is outside that
timeout. The first successful TCP connection is used, and other attempts close.
TLS verification still checks the original hostname or the literal IP address.

Use `-4` / `--ipv4` to restrict outgoing sockets to IPv4, or `-6` / `--ipv6`
to restrict them to IPv6. These flags are mutually exclusive and take no values.
Without either flag, both families remain available. The selection applies to
every redirected connection and to the HTTP proxy endpoint when a proxy is used;
the proxy is responsible for resolving and connecting to the origin server.
It applies only to this invocation and is not saved in station preferences.
The relay listener is independently controlled by its bind address.

Enclose IPv6 literals in brackets in URLs:

```bat
streamripper.exe "https://[2001:db8::1]:8443/radio"
```

The address above is a documentation example, not a public test station.
Bracketed authorities are also used in HTTP Host headers and HTTP proxy requests.
CONNECT destinations use bracketed IPv6 authorities as well.

Without an explicit bind address, the relay prefers a dual-stack listener on all
interfaces, accepting IPv4 and IPv6 clients; it falls back to IPv4 if a dual-stack
listener is unavailable. An explicit IPv4 bind stays IPv4-only, and an explicit
IPv6 bind is IPv6-only. For a local IPv6 relay:

```bat
streamripper.exe "https://example.com/radio" --relay=[::1]:8000
```

Use `--relay=127.0.0.1:8000` for an IPv4 loopback listener. Connect to the IPv6
example at `http://[::1]:8000/`. `[::1]` without a port keeps the configured relay
port (8000 by default). Other explicit forms remain `HOST:PORT` and `PORT`.

An explicit bind address is included in the console summary, for example
`relay: [::1]:8000`; without one, it shows the selected `relay port`.
Windows relay listeners reserve their addresses exclusively, including both
families of a dual-stack socket. If the requested port is occupied, automatic
port scanning selects the next available port; `-z` / `--no-port-scan` disables
that search and reports a bind error instead.

## Recording options

- HTTP/1.1 chunked streams are decoded automatically before ICY metadata is parsed.
- Use `--http10` for servers requiring HTTP/1.0.
- HTTP and HTTPS redirects support relative locations and are limited to 10 hops.
- URLs are parsed with GLib GUri, preserving query strings, explicit ports, and percent encoding.
- Use `--wav` to decode MP3 into 16-bit PCM WAV, for individual tracks or `-a` show files. Mono/stereo and the source sample rate are preserved. This does not improve MP3 quality; files are larger and classic WAV is limited to 4 GiB.
- AAC and Ogg Vorbis are recorded in their native format; their conversion to WAV is outside this release.

## Command-line options

Short and descriptive long options are supported. Run `streamripper --help` for
the complete option list, grouped by purpose. Options with values accept
`--name VALUE` or `--name=VALUE`; existing short forms remain available.

## Metadata parsing

ICY `StreamTitle` is a single string, typically `Artist - Title`, rather than
separate artist and title fields. Streamripper extracts those fields using rules.
Without `--metadata-rules`, it uses built-in rules. A custom rules file replaces
the built-in rules completely, even if it contains only substitutions or filters.

To retain ordinary artist/title extraction, put this rule at the end of your file,
after substitutions and filters:

```text
m/^\s*(.*?[^-\s])\s+-\s+(.*)\s*$/A1T2
```

The first matching extraction rule ends processing, so rules below it do not run.
Without a matching extraction rule, the entire remaining string becomes the title
and the artist is empty. Incomplete filenames composed as `Artist - Title` then
have a leading ` - `, for example ` - Artist - Song.mp3`.
See [metadata rules](METADATA-RULES.md#metadata-input) for details and examples.

## Metadata encoding

Use `--metadata-log` to append timestamped ICY title changes to
`incomplete/metadata.log`. This captures the decoded titles before metadata rules
change or exclude them, independently of recorded filenames. See
[metadata logging](METADATA-RULES.md#log-the-titles-used-by-rules) for format and scope.

Windows console output uses the Unicode `WriteConsoleW` API, independently of
the active code page; `chcp 65001` is not required. Redirected output and Linux
console output use UTF-8. Command-line arguments use UTF-8. File operations use Windows
Unicode APIs, including directory enumeration for sequence numbering;
`--codeset-filesys` does not change those operations.

The default `--codeset-metadata=AUTO` checks each incoming metadata string for
valid UTF-8. Valid UTF-8 is decoded directly. For MP3/AAC streams identified by
an Icecast server header, other bytes use ISO-8859-1, independent of the system
locale. For other servers, legacy bytes use the system's legacy
encoding (the Windows ANSI code page, such as CP1250). With a UTF-8 system locale,
the legacy fallback is ISO-8859-1. This policy also applies to ICY station names.
Detection is repeated for every update, so a
station can switch between UTF-8 and legacy titles within one connection.

This prevents UTF-8 names such as `Mötley Crüe` from becoming `MĂ¶tley CrĂĽe` in
the console, filenames, and ID3 tags. Decoding happens before metadata rules run.
It does not guess reliably between arbitrary legacy encodings, or repair text
that was already corrupted before the station sent it.

An explicit encoding, such as `--codeset-metadata=CP1252` or
`--codeset-metadata=UTF-8`, disables automatic detection. Encoding settings in an
explicit `--config` file are respected; `--codeset-metadata=AUTO` overrides them.
Old implicit INI files are ignored. Output encoding options and the relay's
encoding are separate settings.

## Incomplete recordings

The console shows `filtered` for tracks excluded by metadata rules, `incomplete`
for initial tracks kept in `incomplete` by `-k` (one by default), and `ripping`
for ordinary recording. `filtered` takes precedence if both decisions apply.
An interruption can still leave a `ripping` track incomplete. Each reconnect
starts a new initial-skip quota. The duration filter does not delete completed tracks.

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

To discard incomplete recordings shorter than ten minutes:

```bat
streamripper.exe "https://example.com/radio" -d C:\Radio --keep-incomplete --incomplete-min-duration=10m
```

`--incomplete-min-duration=N[s|m|h]` accepts non-negative integer seconds (the
default unit), minutes, or hours: `600`, `600s`, `10m`, and `1h` are valid values.
Use the `=` form shown above. The option is disabled by default; `0` explicitly
disables it. It applies only to the current invocation and is not saved as a
station preference.

After closing and finalizing an individual recording, Streamripper removes it
only if it remains in `incomplete` and its recorded audio duration is strictly
below the limit. A file exactly at the minimum is kept. This covers the first
tracks skipped by `-k`, the last open track on normal shutdown or reconnection,
and new incomplete duplicates left by an overwrite policy. Completed tracks are
kept regardless of length. Long recordings without song metadata remain in
`incomplete`; the option does not promote or rename them.

Closing the Windows console with its Close button, Ctrl+C, or Ctrl+Break requests
the same finalization and duration check. The console-close handler waits for
cleanup instead of returning immediately. Windows limits console-close cleanup
time (normally about five seconds); forced termination can still interrupt it.

The duration is measured from stored audio, rather than file size, bitrate
estimates, or elapsed download time. MP3/AAC use complete frame sample counts,
WAV uses the decoded PCM length, and Ogg Vorbis uses granule positions and the
stream sample rate, including chained streams. A duration that cannot be
determined, or a failed write/finalization/promotion, prevents automatic deletion.

Only the individual files opened by this recording are considered. No directory
scan deletes older files, and backups preserved by `-t` remain untouched.
`--keep-incomplete` (`-t`) protects existing recordings from overwriting; it does
not disable the duration filter for newly created incomplete files.
`-T` can truncate an incomplete duplicate to zero duration; a positive duration
limit then removes that empty file. Continuous `-a` recordings and the relay are
unaffected. Deletions and removal failures are reported on stderr. Forced process
termination or a power failure can prevent cleanup, just as they can prevent
header finalization.

## Recorded file compatibility

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
(duration in milliseconds) during finalization. `-i` / `--no-id3` disables ID3 tags;
`--without-id3v2` disables ID3v2 specifically. Raw AAC players may ignore ID3 tags.
Compressed audio frames are copied without re-encoding.

Finalization also covers the last open individual file left in `incomplete` on
normal shutdown. Forced process termination or a power failure can prevent it.
Continuous `-a` MP3/AAC recordings keep the original stream bytes and do not use
this individual-file finalizer. WAV sizes are finalized separately; native Ogg
recording remains page-based.

The default User-Agent is `streamripper/<version>`. If a station rejects it,
retry with `-u "WinampMPEG/5.0"` for that station. This changes the HTTP
identification string; TLS certificate and hostname verification remain enabled
unless `--insecure` is explicitly specified.

Stream requests use HTTP/1.1 by default, with `Host`, `Accept: */*`,
`Cache-Control: no-cache`, the configured `User-Agent`, `Icy-Metadata: 1`, and
`Connection: close`. `--http10` changes the request version to HTTP/1.0.
The relay responds with `ICY 200 OK`. The retained page-request helper uses
HTTP/1.1, a Chrome/152 browser User-Agent, `Accept-Encoding: identity`, and
`Connection: close`; it is not called by the current CLI recording path.

## HLS and FLAC recording

HLS URLs ending in `.m3u8` (including query strings), or responses with an HLS
content type, are recorded as one continuous file. Master playlists select the
highest bandwidth by default; `--hls-variant=worst` chooses the lowest and
`--hls-variant=128000` chooses the closest advertised bandwidth in bits/s.
Live playlists are refreshed and duplicate media sequence numbers are skipped.
The recorder retries interrupted segment downloads using the normal reconnect
policy, without writing partial segments. If the live window moves past a
missing segment, recording stops with an error instead of silently losing it.

MPEG-TS segments retain their container in `.ts`; packed ADTS AAC segments with
`.aac` URLs are concatenated into `.aac`, preserving their ID3 timestamp tags.
AES-128 CBC is supported with explicit or sequence-derived IVs. Keys, playlists
and segments use the same verified HTTPS, proxy and timeout settings as other
streams. Each response is bounded to 1 MiB for playlists, 16 bytes for keys and
128 MiB for segments. There is no demuxing, transcoding, adaptive switching or
FFmpeg dependency. Separate audio renditions, fMP4, byte ranges and DRM are
rejected explicitly.

Native FLAC (`audio/flac`, `audio/x-flac` or a `.flac` URL) and Ogg FLAC
(`application/ogg`/`audio/ogg`) are preserved as `.flac` and `.oga`. ICY metadata
is removed from the audio transport and can be saved with `--metadata-log`;
it never splits a FLAC stream or adds MP3 ID3 tags.
Ogg FLAC is finalized with relative page/sample/frame counters, a corrected
STREAMINFO sample count and complete end-of-stream pages. This fixes duration
and seeking when a live server supplies pages from a long-running encoder.
Incomplete final pages/frames are discarded; compressed audio is not re-encoded.
The source PCM MD5 is cleared because it does not describe the recorded subset.
Source seek/cue offsets are replaced with padding; seeking uses the rebuilt Ogg
granules and relative FLAC frame numbers.

HLS and FLAC use the usual output directory, naming, locking, overwrite policy
and allocation settings. They record whole streams, so metadata rules,
external metadata commands, relay output and `--wav` are unavailable.
If `--relay` is present, recording continues with a warning and no relay listener
or relay playlist is created. Relay client/port settings have no effect for these
formats; metadata rules and external metadata remain explicit errors.
Track splitting, initial-track skipping and MP3 ID3 options do not apply. A finite
FLAC response or HLS ENDLIST completes the file; interrupted/live captures stay
in `incomplete`. HLS progress and incomplete cleanup use accumulated EXTINF
durations. Ogg FLAC uses its actual recorded sample count, including for
minimum-duration cleanup. Native FLAC duration is currently unknown while
recording, so progress shows file size and cleanup preserves interrupted native FLAC files.
Continuous `-a` output is also supported; it retains the same native container.
The console reports `stream: Unknown station` when no station name is supplied. HLS/FLAC
progress shows `-` until a track title is known, rather than repeating the
station name. FLAC ICY titles update the display without renaming or splitting
the recording; artist/title tags inside Ogg/FLAC and HLS segments are not used
for the track display.
Missing station names use `Unknown station` both in the console and in output
paths. For MP3/AAC, an empty artist and title also display as `-`; filename
patterns retain their literal separators.
The startup summary reports the input format, including ADTS or Ogg framing
and HLS transport where applicable. MPEG-TS HLS reports its container without
guessing the codec carried inside it. With `--wav`, the input remains MP3.

## Limitations

- DASH, fragmented MP4 HLS, byte-range HLS and SAMPLE-AES/DRM are not supported.
- Some legacy size limits remain, notably 254 bytes for paths and track metadata.

## Documentation

- [Metadata rules, regex filtering, and track numbering](METADATA-RULES.md)
- [Windows: building, testing, and preparing a portable release](BUILDING-WINDOWS.md)
- [Linux: DEB installation, source builds, packaging, and testing](BUILDING-LINUX.md)
- [Release history](CHANGELOG.md)
- [Fork provenance, issue review, and test scope](PORTING.md)
- [Bundled libraries, licenses, and source packages](THIRD-PARTY.md)
- [Original manual and other command-line options](README)

The release version is recorded in [`VERSION`](VERSION). Upstream: https://github.com/streamripper/streamripper, commit `248f771250998e38a2d8212aa43ccc4a516f853b`. Licensed under GPL v2 or later; see [`COPYING`](COPYING).

Development of this fork was assisted by GPT-6.1 Sol.
