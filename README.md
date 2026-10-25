# Streamripper for Windows and Linux

A fork of the original Streamripper with native HTTPS, IPv6 support, current 64-bit libraries, working ICY track names, and Unicode filenames.

This independent fork is maintained at [Ashus/streamripper](https://github.com/Ashus/streamripper). It records MP3, AAC, Ogg Vorbis, FLAC and supported HLS streams; MP3 can optionally be decoded to WAV.

Download Windows portable archives and Linux amd64 DEB packages from **[releases](https://github.com/Ashus/streamripper/releases).** On Windows, extract the whole 7z archive: the executable requires its bundled DLLs.

The portable release is for Windows. Linux amd64 users can install the `.deb`
package on systems satisfying its Debian runtime dependencies; it is built and
tested on Debian 13 Trixie. Installation, optional source builds and DEB packaging are
covered by the complete [Linux guide](BUILDING-LINUX.md).
Windows toolchain setup, builds and releases have a separate
[Windows build guide](BUILDING-WINDOWS.md).

## Format support

| Input | Saved format | Individual tracks | Silence-based splitting | WAV conversion | Relay |
| --- | --- | --- | --- | --- | --- |
| MP3 | `.mp3` | Yes, from ICY titles | Yes | Yes, 16-bit PCM | Yes |
| ADTS AAC | `.aac` | Yes, from ICY titles | Yes | No | Yes |
| Ogg Vorbis | `.ogg` | Native logical-stream boundaries and comments | No | No | Yes |
| Native FLAC | `.flac` | No, whole stream | No | No | No |
| Ogg FLAC | `.oga` | No, whole stream | No | No | No |
| HLS with MPEG-TS segments | `.ts` | No, whole stream | No | No | No |
| HLS with packed ADTS AAC | `.aac` | No, whole stream | No | No | No |

MP3/AAC metadata rules can clean names and exclude individual tracks. They do
not filter continuous recordings or relay audio. Ogg comments are not processed
by metadata rules. Ogg song boundaries depend on the broadcaster creating
separate logical streams; the container cannot
infer songs inside one uninterrupted logical stream.

`--metadata-log` logs available ICY titles for MP3, AAC and FLAC. It does not
extract native Ogg comments or HLS segment metadata. FLAC ICY titles update
the display and log without splitting the recording.

Compressed audio remains in its original codec; `--wav` decodes MP3 to PCM.
HLS saves MPEG-TS as a single transport-stream file. HLS/FLAC ignore `--relay` with a
warning so shared command lines can still record them.
Opus, DASH, fragmented MP4 HLS and AAC in LATM/LOAS are not supported.

## Quick start

After extracting the release archive:

```bat
streamripper.exe "https://strm112.1.fm/highvoltage_mobile_mp3?aw_0_req.gdpr=true" --output-directory C:\Radio --tls-info
```

Multiple instances can record different stations simultaneously. The current track is stored in `incomplete`; completed tracks are moved to their final filenames when metadata changes.

Each recording directory is protected by `incomplete/.lock`. Another process
targeting the same directory exits with an error. The lock is released when the
process exits; a file left behind after a crash does not block a new recording.
Do not delete it while a recording is active.

The connection summary shows the station name, stream hostname and port,
server software, and input format.

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

Example configuration:

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

HTTPS verifies certificates and server identities by default, using the system
trust store (including Windows root certificates). TLS 1.2 and 1.3 are supported.
`--tls-info` prints connection details once, without repetition on reconnect.

## HTTP and HTTPS proxies

`--proxy URL` accepts an HTTP or HTTPS proxy for either HTTP or HTTPS streams:

```bat
streamripper.exe "https://radio.example/stream" --proxy "http://proxy.example:8080"
streamripper.exe "https://radio.example/stream" --proxy "https://user:password@proxy.example:8443" --tls-info
```

Proxy credentials use Basic authentication. Percent-encode reserved characters
in the username/password. An HTTP proxy does not encrypt its own connection,
including proxy credentials; use an HTTPS proxy to protect that connection.
Digest, NTLM, and automatic system proxy discovery are not supported.

For HTTPS proxies and streams, both certificates are verified.
`--tls-ca-file` supplies trust for both endpoints, and `--insecure` disables
verification for both. `--tls-info` labels the proxy connection `TLS proxy:`
and the stream connection `TLS:`.

## IPv4 and IPv6

HTTP and HTTPS streams support both IPv4 and IPv6. Connection attempts try
available addresses without waiting for an unreachable address to time out.
`--timeout` controls the connection timeout; DNS lookup is outside that limit.

Use `--ipv4` to restrict outgoing sockets to IPv4, or `--ipv6`
to restrict them to IPv6. These flags are mutually exclusive and take no values.
Without either flag, both families remain available. The selection applies to
every redirected connection and to the HTTP proxy endpoint when a proxy is used;
the proxy is responsible for resolving and connecting to the origin server.
The relay listener is independently controlled by its bind address.

Enclose IPv6 literals in brackets in URLs:

```bat
streamripper.exe "https://[2001:db8::1]:8443/radio"
```

The address above is a documentation example, not a public test station.

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
If the requested port is occupied, automatic port scanning selects the next
available port; `--no-port-scan` disables
that search and reports a bind error instead.

## Recording options

Use `--wav` to save MP3 as 16-bit PCM WAV, for individual tracks or
`--continuous-file` recordings. Mono/stereo and the source sample rate are
preserved. WAV does not improve source quality, requires more disk space, and
has a 4 GiB file limit. AAC and Ogg Vorbis cannot be converted to WAV.

Use `--http10` for servers requiring HTTP/1.0. If a station rejects the default
User-Agent, try `--user-agent "WinampMPEG/5.0"`.

## Command-line options

Run `streamripper --help` for the complete option list, grouped by purpose.
Options with values accept `--name VALUE` or `--name=VALUE`.

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

Use `--metadata-log` to append timestamped ICY title changes to
`incomplete/metadata.log`. This captures the titles before metadata rules
change or exclude them, helping you fine-tune the rules. See
[metadata logging](METADATA-RULES.md#log-the-titles-used-by-rules) for format and scope.


## Metadata encoding

Windows console output supports Unicode independently of the active code page;
`chcp 65001` is not required. Redirected output and Linux console output use
UTF-8. Windows filenames support Unicode; `--codeset-filesys` does not change
their encoding.

The default `--codeset-metadata=AUTO` checks each incoming metadata string for
valid UTF-8. Valid UTF-8 is decoded directly. For MP3/AAC streams identified by
an Icecast server header, other bytes use ISO-8859-1, independent of the system
locale. For other servers, legacy bytes use the system's legacy
encoding (the Windows ANSI code page, such as CP1250). With a UTF-8 system locale,
the legacy fallback is ISO-8859-1. This policy also applies to ICY station names.
Automatic detection cannot reliably distinguish all legacy encodings or repair
text already corrupted by the station.

An explicit encoding, such as `--codeset-metadata=CP1252` or
`--codeset-metadata=UTF-8`, disables automatic detection. Encoding settings in an
explicit `--config` file are respected; `--codeset-metadata=AUTO` overrides them.
Old implicit INI files are ignored. Output encoding options and the relay's
encoding are separate settings.

## Incomplete recordings

The console shows `filtered` for tracks excluded by metadata rules, `incomplete`
for initial tracks kept in `incomplete` by `--skip-initial-tracks` (one by default), and `ripping`
for ordinary recording. `filtered` takes precedence if both decisions apply.
An interruption can still leave a `ripping` track incomplete. Each reconnect
starts a new initial-skip quota. The duration filter does not delete completed tracks.

Progress shows the current track's audio duration as `MM:SS` or `H:MM:SS`,
or its received size when duration is unavailable. The counter resets on a title
change and can differ slightly from the final file's duration.
Track names adapt to the terminal width, including after resizing.

To discard incomplete recordings shorter than ten minutes:

```bat
streamripper.exe "https://example.com/radio" --output-directory C:\Radio --keep-incomplete --incomplete-min-duration=10m
```

`--incomplete-min-duration=N[s|m|h]` accepts non-negative integer seconds (the
default unit), minutes, or hours: `600`, `600s`, `10m`, and `1h` are valid values.
Use the `=` form shown above. The option is disabled by default; `0` explicitly
disables it.

After closing and finalizing an individual recording, Streamripper removes it
only if it remains in `incomplete` and its recorded audio duration is strictly
below the limit. A file exactly at the minimum is kept. This covers the first
tracks skipped by `--skip-initial-tracks`, the last open track on normal shutdown or reconnection,
and new incomplete duplicates left by an overwrite policy. Completed tracks are
kept regardless of length. Long recordings without song metadata remain in
`incomplete`; the option does not promote or rename them.

The limit uses recorded audio duration. If duration cannot be determined or a
file cannot be finalized, it is preserved. Only files created by this recording
are checked; older files and backups remain untouched.

`--keep-incomplete` preserves existing incomplete files rather than overwriting
them; it does not exempt new files from the duration filter.
`--truncate-duplicates` can leave an empty duplicate, which a positive duration
limit removes. Continuous recordings and relay audio are unaffected.

Normal shutdown also checks the last open recording. Forced process termination
or a power failure can prevent cleanup and file finalization.

## Recorded file compatibility

Recordings automatically reserve disk space in 2 MiB increments to reduce
fragmentation where supported. Unused space is released when files are closed
normally. If reservation is unavailable, recording continues normally.

MP3 and AAC tracks search for a quiet split near each metadata change by default.
`--xs-none` disables this; timing and padding options are described in
[metadata rules](METADATA-RULES.md#boundaries-and-audio-timing).
Ogg Vorbis uses the station's native track boundaries instead.

MP3 and AAC files are finalized with duration information and artist/title tags.
`--no-id3` disables ID3 tags; `--without-id3v2` disables ID3v2 specifically.
Some raw AAC players ignore these tags. Compressed audio is not re-encoded.
Continuous MP3/AAC recordings preserve the source stream without individual-track
finalization. Forced process termination can leave files unfinished.

## HLS and FLAC recording

HLS URLs (`.m3u8`) record the whole stream. The highest-bandwidth variant is
selected by default; `--hls-variant=worst` chooses the lowest, and
`--hls-variant=128000` chooses the closest advertised bandwidth in bits/s.
AES-128 encrypted streams are supported. Interrupted segments are retried
without saving partial data. If a missing segment is no longer available,
recording stops with an error.

Native FLAC and Ogg FLAC are saved as `.flac` and `.oga`, without re-encoding.
Ogg FLAC recordings are finalized to provide correct duration and seeking.
ICY titles can update the display and `--metadata-log`, but do not split or
rename FLAC recordings.

HLS/FLAC use the usual output directory, filename pattern, locking and overwrite
settings; `--continuous-file` is supported. Metadata rules, external metadata
commands and `--wav` are unavailable. `--relay` is ignored with a warning.
Track splitting, initial-track skipping and MP3 ID3 options do not apply.

Finished streams complete the recording; interrupted or live captures remain
in `incomplete`. Minimum-duration cleanup works for HLS and Ogg FLAC.
Native FLAC duration is unknown while recording, so its progress shows size and
interrupted files are preserved.

Missing station names display as `Unknown station` and use that name in output
paths. Missing track titles display as `-`. Embedded Ogg/FLAC tags and HLS
segment metadata are not used for the HLS/FLAC track display.

## Limitations

- DASH, fragmented MP4 HLS, byte-range HLS and SAMPLE-AES/DRM are not supported.
- Some legacy size limits remain, notably 254 bytes for paths and track metadata.
- Individual MP3 files with duration indexing are limited to 4 GiB. Free-format
  MP3 and other MPEG audio layers are not supported by the MP3 finalizer.

## Documentation

- [Metadata rules, regex filtering, and track numbering](METADATA-RULES.md)
- [Windows: building, testing, and preparing a portable release](BUILDING-WINDOWS.md)
- [Linux: DEB installation, source builds, packaging, and testing](BUILDING-LINUX.md)
- [Release history](CHANGELOG.md)
- [Fork provenance, implementation notes, and test scope](PORTING.md)
- [Bundled libraries, licenses, and source packages](THIRD-PARTY.md)
- [Original manual and other command-line options](README)

The release version is recorded in [`VERSION`](VERSION). Licensed under GPL v2 or later; see [`COPYING`](COPYING).

Development of this fork was assisted by GPT-6.1 Sol.
