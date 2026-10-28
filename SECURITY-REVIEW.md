# Security and memory review

Reviewed on 2026-10-27 against the
[Debian 1.64.6-2 changelog](https://metadata.ftp-master.debian.org/changelogs/main/s/streamripper/streamripper_1.64.6-2_changelog),
the historical Debian patches and the
[Debian security tracker](https://security-tracker.debian.org/tracker/source-package/streamripper).

## Historical CVEs

| Issue | Reviewed protection in this fork |
| --- | --- |
| [CVE-2006-3124](https://security-tracker.debian.org/tracker/CVE-2006-3124) | The HTTP temporary header buffer uses `MAX_ICY_STRING`; server-version reads are limited to 63 characters. The additional malformed-version problem described below is now guarded by checking conversion success. |
| [CVE-2007-4337](https://security-tracker.debian.org/tracker/CVE-2007-4337) | Header extraction receives each destination's actual size, copies at most size minus one and terminates the result. This covers `Location`, `Server` and the ICY/audiocast fields. |
| [CVE-2008-4829](https://security-tracker.debian.org/tracker/CVE-2008-4829) | The unsafe Zwitterion-specific scanner is absent. Supported Windows/POSIX builds parse URLs with GLib and check destination lengths. The historical URL branch retains bounded scans. PLS/M3U locations use bounded, terminating copies, and full playlist buffers are rejected before adding a terminator. |

The original patches were reviewed from the Debian source archives for
[1.61.7-1sarge1](https://archive.debian.org/debian/pool/main/s/streamripper/streamripper_1.61.7-1sarge1.diff.gz)
and
[1.61.27-1+etch1](https://archive.debian.org/debian/pool/main/s/streamripper/streamripper_1.61.27-1+etch1.diff.gz).
Their applicable protections are already present; no historical patch was imported.

Searches beyond the package changelog also found
[CVE-2020-37065](https://nvd.nist.gov/vuln/detail/CVE-2020-37065), published in 2026
for the SongPattern input in StreamRipper32 2.6's Station/Song GUI. That GUI/input
is absent from this CLI fork. The searches did not identify another applicable
Streamripper CVE. This conclusion covers application records, not a complete audit
of the independently maintained runtime dependencies.

## Additional findings fixed in this review

Independently implemented fixes supplement the historical protections:

- A missing SHOUTcast or Icecast version could leave `versionbuf` uninitialized
  after a failed `sscanf`, then pass it to `strcat`. Only successful conversions
  are now appended, CR/LF ends the version, and destination copies are bounded.
  The regression failed before the fix; the earlier Linux reproducer reported
  an out-of-bounds stack read for both server families.
- A response could fill the HTTP header buffer without a terminating zero.
  The instrumented network test reproduced an out-of-bounds read at the size
  limit. The reader now reserves the terminator and compares complete four-byte
  end markers with `memcmp`, including the historical embedded-NUL marker.
  Invalid buffer pointers/sizes are rejected before socket operations.
- MP3/Ogg receive errors detached a recording chunk before returning, losing its
  ownership. LeakSanitizer reproduced a 16,024-byte leak on Linux. Error paths now
  return the chunk to its free queue, including MP3 bitrate-detection failures.
  An Ogg writer also remains queued until track finalization succeeds, so failed
  output finalization leaves it reachable by shutdown cleanup.
- Circular-buffer pointer copying assigned the destination offset to itself,
  leaving it uninitialized. A sanitized reconnect/track-split test reproduced an
  excessive allocation from the resulting invalid distance. Copying the source
  offset fixes same-node and cross-node distance calculations.
- MP3 silence analysis added two squared 16-bit samples in a signed `int`.
  UndefinedBehaviorSanitizer reproduced overflow at full negative amplitude.
  The calculation now uses 64-bit intermediates before division and square root.

These new findings have no assigned CVE identifiers. This review does not claim that
remote code execution was demonstrated.

## Regression coverage

`testing/http_security.c` repeats header and URL cases for 100 cycles, including
missing versions, CR/LF boundaries, oversized Location/Server/ICY headers,
temporary header values, Zwitterion text, URL credentials and input lengths up
to 20,000 bytes. It verifies output guards and string termination.

`testing/http_security.py` runs the native regression executable and a local HTTP
server. It exercises the actual socket and redirect paths with 8190/8191/8192-byte
response boundaries, the embedded-NUL end marker, oversized responses, long
PLS/M3U locations and long PLS titles. Oversized playlists must fail cleanly.

`testing/stream_buffer_lifecycle.c` checks exact circular-buffer distances and
100 MP3/Ogg receive-failure cycles, verifying chunk ownership and destruction.
The existing HTTP framing and HTTPS smoke tests exercise real recordings,
reconnection and track splitting, including the full-amplitude MP3 fixture.

Validation uses Windows x64 with GCC 16.2 and Debian amd64/WSL with GCC 14.2.
The Linux instrumented build applies AddressSanitizer, UndefinedBehaviorSanitizer,
leak detection and diagnostic initialization to the whole application library,
rather than only the test translation unit.

The focused HTTP and lifecycle tests passed on both platforms. HTTP framing
and HTTPS smoke suites also passed on Windows and in the instrumented Linux
build, with no sanitizer/leak reports in the final runs. Rider inspections and
`git diff --check` were checked; existing unrelated inspection warnings remain.

Run the focused tests after building:

```bat
python testing\http_security.py build\http_security_regression.exe
build\stream_buffer_regression.exe
```

For a Linux build:

```sh
python3 testing/http_security.py build/linux-console/http_security_regression
build/linux-console/stream_buffer_regression
```

For the instrumented Linux checks, use a separate Linux-only CMake directory:

```sh
cmake -S . -B build/linux-security -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -ftrivial-auto-var-init=pattern -g" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
cmake --build build/linux-security --parallel
export ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
python3 testing/http_security.py build/linux-security/http_security_regression
build/linux-security/stream_buffer_regression
```

## Limits

These are targeted HTTP/CVE and lifecycle checks. They do not establish the
absence of all memory leaks or information disclosures, audit every audio codec,
or replace vulnerability checks and updates for external runtime libraries.
A full Windows sanitizer run and other POSIX platforms were not tested.
The historical URL branch was inspected and tested separately during the
preceding CVE audit; it is not a supported alternative to the GLib builds.
