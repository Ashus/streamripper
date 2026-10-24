# Streamripper for Windows

A fork of the original Streamripper with native HTTPS, current 64-bit libraries, working ICY track names, and Unicode filenames.

Version **1.68.0** is an independent fork release published at https://github.com/Ashus/streamripper. It records MP3, AAC, and Ogg Vorbis streams; MP3 can optionally be decoded to WAV.

**Download the portable 7z archive attached to a release.** Extract the whole archive: the executable requires its bundled DLLs. In a source checkout, building and packaging generates a local, ignored `dist` directory; binaries are not committed to Git.

After extracting the release archive:

```bat
streamripper.exe "https://strm112.1.fm/highvoltage_mobile_mp3?aw_0_req.gdpr=true" -d C:\Radio --tls-info
```

Multiple instances can record different stations simultaneously. The current track is stored in `incomplete`; completed tracks are moved to their final filenames when metadata changes.

## TLS options

| Option | Description |
| --- | --- |
| `--tls-info` | Print the negotiated TLS version, cipher, key exchange group, server signature, and verification status. |
| `--tls-ca-file=C:\certificates\ca.pem` | Add trusted certificate authorities from a PEM file. |
| `--insecure` | Explicitly disable certificate and hostname verification. Traffic remains encrypted, but the server is not authenticated. |

Certificate verification is enabled by default, using Windows root certificates and the OpenSSL trust store. The minimum protocol is TLS 1.2. ECDSA, ECDHE/PFS, and TLS 1.3 are covered by integration tests; the actual algorithms are negotiated with the server.

## Recording options

- HTTP/1.1 chunked streams are decoded automatically before ICY metadata is parsed.
- Use `--http10` for servers requiring HTTP/1.0.
- Use `--wav` to decode MP3 into 16-bit PCM WAV, for individual tracks or `-a` show files. Mono/stereo and the source sample rate are preserved. This does not improve MP3 quality; files are larger and classic WAV is limited to 4 GiB.
- AAC and Ogg Vorbis are recorded in their native format; their conversion to WAV is outside this release.

## Documentation

- [Building, testing, and preparing a release](BUILDING.md)
- [Changes and known limitations](CHANGELOG.md)
- [Fork provenance, issue review, and test scope](PORTING.md)
- [HTTPS implementation notes](README-HTTPS.md)
- [Bundled libraries, licenses, and source packages](THIRD-PARTY.md)
- [Original manual and other command-line options](README)

The release version is recorded in [`VERSION`](VERSION). Upstream: https://github.com/streamripper/streamripper, commit `248f771250998e38a2d8212aa43ccc4a516f853b`. Licensed under GPL v2 or later; see [`COPYING`](COPYING).
