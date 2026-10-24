# Native HTTPS support for Windows

Based on https://github.com/streamripper/streamripper, upstream commit `248f771250998e38a2d8212aa43ccc4a516f853b`. These changes extend the original CLI; the modern build does not compile the Winamp plugin.

## Implementation

- Native HTTPS through OpenSSL, requiring TLS 1.2 or newer, SNI, and hostname/IP verification.
- Certificate chains are checked against Windows root certificates and the standard OpenSSL trust store. Verification is enabled by default.
- `--insecure` explicitly disables certificate and hostname checks. `--tls-ca-file=...` adds custom trust anchors. `--tls-info` prints the actual protocol, cipher, key exchange group, and server signature. These overrides are not persisted.
- HTTP and HTTPS redirects support relative locations and are limited to 10 hops.
- HTTP/1.1 chunk framing is decoded before ICY metadata processing. Use `--http10` for older servers.
- GLib GUri parses URLs while preserving query strings, explicit ports, and percent encoding.
- Command-line arguments and console output use UTF-8. File operations use Windows Unicode APIs, including directory enumeration for sequence numbering.
- The 64-bit build uses current GLib, libiconv, libmad, Ogg/Vorbis, and OpenSSL libraries.

ICY metadata remains interleaved with audio. The existing track splitting, ID3 tags, and Streamripper HTTP relay are retained.

## Build and verification

See [BUILDING.md](BUILDING.md) for dependency installation, complete build commands, tests, and release packaging. Run `build-windows.cmd` and `package-windows.cmd` with the root of your MinGW-w64 installation. The portable result is written to `dist` and a `.7z` archive is created in `build/release`.

The integration suite uses local servers and generated RSA/ECDSA certificates. It verifies that tracks are shown correctly, written with Unicode filenames, and moved out of `incomplete` after a metadata change. It also tests certificate verification, explicit verification bypass, custom CA files, ECDHE/ECDSA with TLS 1.2, TLS 1.3, redirects, and concurrent recordings.

## Cryptography

OpenSSL negotiates algorithms supported by both endpoints. The release was tested with:

- TLS 1.2: `ECDHE-ECDSA-AES128-GCM-SHA256`, X25519 key exchange, `ecdsa_secp256r1_sha256` signatures.
- TLS 1.3: `TLS_AES_256_GCM_SHA384`, X25519MLKEM768 key exchange, RSA-PSS and ECDSA signatures.

ECDSA authenticates the server; ephemeral ECDHE/key exchange provides forward secrecy. Supporting PFS does not imply every TLS 1.2 peer will select it: that depends on negotiation and the server's configuration. Use `--tls-info` to inspect a particular connection.

## Limitations

- HTTPS through the HTTP proxy option `-p` is explicitly rejected because CONNECT tunneling is not implemented.
- The inherited transport remains IPv4-only. HLS and DASH are not supported.
- Some legacy size limits remain, notably 254 bytes for paths and track metadata.
- The first partially received track normally remains in `incomplete`; this is existing Streamripper behavior, not a missing metadata indication.
- Modern Windows builds use UTF-8 for console output and Unicode file operations; `--codeset-filesys` does not change those operations. Specify `--codeset-metadata=UTF-8` when appropriate for a station's ICY metadata encoding.
