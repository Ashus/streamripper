# Metadata rules and track filtering

`-w FILE` (or `--metadata-rules FILE`) loads regular-expression rules for parsing and filtering incoming
metadata. Use it to discard individually recorded tracks, clean up titles, or
extract artist, title, album, year, and track-number fields.

The examples describe this Windows fork, version 1.68.1. See [README.md](README.md)
for installation and [the original manual](README) for other recording options.

See [command-line aliases](README.md#command-line-aliases) for descriptive long
forms of all supported single-letter options used below.

## Quick start

Copy [advert_rules.txt](advert_rules.txt), customize it, then run:

```bat
streamripper.exe "https://example.com/radio" -w "C:\Radio\advert_rules.txt" -d C:\Radio -q
```

The example excludes titles containing the standalone word `advert`, ignoring
case, and retains ordinary `Artist - Title` parsing. It does not recognize
advertisements by their audio content. Customize the expression for each station;
a legitimate song title containing that word is also excluded.

For a custom filename pattern with automatic numbering:

```bat
streamripper.exe "https://example.com/radio" -w advert_rules.txt -D "C:/Radio/Station/%q - %A - %T"
```

In a Windows `.cmd` or `.bat` file, double the filename pattern's percent signs:
`%%q - %%A - %%T`. Percent signs in an ordinary rules file are not doubled.

## Input and scope

### Log the titles used by rules

Add `--metadata-log` to append timestamped ICY title changes to
`incomplete/metadata.log` under the recording's output directory:

```bat
streamripper.exe "https://example.com/radio" -w advert_rules.txt -d C:\Radio --metadata-log
```

The UTF-8 log records the decoded `StreamTitle` before substitutions, filtering,
or artist/title extraction, after the ICY parser trims leading/trailing whitespace.
It includes excluded and ignored titles, independently
of the filename pattern and `--quiet`. Each line contains a local timestamp with
UTC offset, a tab, and the title. For example:

```text
2026-10-24T15:30:00+0200	Artist - Title
```

Repeated identical titles and zero-length metadata blocks do not add lines.
Explicit empty titles do. The first title after connecting or reconnecting is
logged even if it matches the previous session. Existing logs are appended to;
each entry is closed after writing. Backslashes and control characters are escaped
as `\\`, `\t`, `\r`, `\n`, or `\xHH` to keep one update per line.

Logging is disabled by default and the option is not saved in preferences.
It applies to the MP3/AAC ICY path, including `--wav` and continuous recordings;
native Ogg comments and external metadata (`-E`) bypass this path. The
`incomplete` directory is created for logging even with `-A`. Write failures are
reported on stderr without stopping audio recording. The log is not rotated.

### Metadata input

For ICY streams, rules receive the decoded `StreamTitle` text, without the
`StreamTitle='...';` wrapper. They do not receive the output path, filename
sequence, or ID3 tags. Substitutions change the text seen by subsequent rules.

The default metadata encoding is `AUTO`: valid UTF-8 takes priority, otherwise
the system's legacy encoding is used (ISO-8859-1 when the locale is UTF-8).
Explicit `--codeset-metadata=NAME` and encoding settings loaded with `--config`
override this heuristic. Use `--codeset-metadata=AUTO` to override a configured
encoding. Implicit INI files are neither loaded nor saved; see
[configuration](README.md#configuration).
Rules receive the decoded UTF-8 text; the rule file itself remains UTF-8.
See [metadata encoding](README.md#metadata-encoding) for limitations.

ICY `StreamTitle` is one string, typically `Artist - Title`; the station does not
supply separate artist and title fields through it. Streamripper's built-in rules
extract those fields when no custom rules file is supplied.

**A custom rules file replaces all built-in rules; it is not appended to them.**
This also applies to files containing only substitutions or filters. To retain
ordinary artist/title extraction, include this rule at the end, after cleanup
substitutions and filtering rules:

```text
m/^\s*(.*?[^-\s])\s+-\s+(.*)\s*$/A1T2
```

`A1` assigns capture group 1 to the artist and `T2` assigns group 2 to the title.
The first matching extraction rule ends processing; rules below it do not run.
Without a matching extraction rule, the remaining text becomes the title and the
artist is empty. For example, a substitution may correctly clean metadata to
`Artist - Song`, but without extraction that entire string remains in the title.
Incomplete filenames composed as `Artist - Title` then become
` - Artist - Song.mp3`. Adding the extraction rule produces `Artist - Song.mp3`
and separate artist/title fields for tags. See [parse_rules.txt](parse_rules.txt)
for additional parsing variants, including metadata without spaces around `-`.

Track exclusion applies to individual recordings in the MP3/AAC ICY recording
path, including MP3 decoded with `--wav`. It does not remove audio from continuous
`-a` recordings or the relay. Do not assume these rules filter native Ogg Vorbis
comments: Ogg follows a separate metadata path. The native AAC framing checks
use synthetic ADTS frames and do not constitute an AAC decoder test.

## File format

- Save as UTF-8 without a BOM. Both LF and CRLF line endings work; choose either.
  The final newline is optional. A bare CR is not a supported line separator.
- Use one rule per line. Keep each line below 2048 bytes, including its newline.
- Blank lines and lines beginning with `#`, optionally indented, are ignored.
- Leading indentation is allowed. Do not put spaces between the command and `/`.
- Put comments on separate lines. Trailing comments and spaces after flags are
  not supported.
- The delimiter is always `/`. Escape a literal slash inside an expression as
  `\/`; other delimiter characters are not supported.

Two commands are available:

```text
m/EXPRESSION/FLAGS
s/EXPRESSION/LITERAL_REPLACEMENT/FLAGS
```

An empty expression matches every title, including an empty one: `m//x`.
An empty replacement removes the matched text: `s/ suffix$//`.

The Windows build uses GLib's regex engine, backed by PCRE2. Expressions support
anchors (`^`, `$`), groups, alternatives (`foo|bar`), character classes,
quantifiers, word boundaries (`\b`), and lookarounds. Matching is case-sensitive
unless `i` is specified. A match can occur anywhere unless you anchor it.

## Flags

| Flag | Command  | Meaning                                                                                      |
|------|----------|----------------------------------------------------------------------------------------------|
| `i`  | `m`, `s` | Case-insensitive matching.                                                                   |
| `x`  | `m`      | Exclude the individual track while preserving its metadata boundary.                         |
| `s`  | `m`      | Select tracks to save; see allowlist behavior below.                                         |
| `e`  | `m`      | Ignore this metadata update and stop processing it.                                          |
| `A1` | `m`      | Copy capture group 1 to the artist field.                                                    |
| `T2` | `m`      | Copy capture group 2 to the title field.                                                     |
| `C3` | `m`      | Copy capture group 3 to the album field.                                                     |
| `N4` | `m`      | Copy capture group 4 to the track-number field.                                              |
| `Y5` | `m`      | Copy capture group 5 to the year field.                                                      |
| `g`  | `s`      | Accepted, but global substitution is not implemented; only the first occurrence is replaced. |

Capture indices can be 1 through 24. Choose the index corresponding to your
expression; the numbers above are examples. Flags can be combined, for example
`ix` or `iA1T2`. Use separate rules for save/exclude/drop actions and field
extraction: action flags take precedence over capture flags. Do not combine
`e`, `s`, and `x` on the same rule.

Substitution replacements are literal text. Neither `\1`, `$1`, nor named
backreferences are expanded, and escape sequences such as `\n` do not produce
newlines. Use capture flags to extract fields rather than trying to rearrange
them with substitution backreferences.

## Processing order

Rules run from top to bottom for each metadata update:

1. Start with saving enabled.
2. A substitution modifies the working title and processing continues.
3. Matching `x` and `s` rules choose whether to save, but processing continues
   so the fields can still be parsed. The first matching save/exclude action wins.
4. An `s` rule that does not match disables saving until an eligible `s` rule
   matches. Thus one or more `s` rules can implement an allowlist.
5. A matching `e` rule immediately ignores the update, even after a save/exclude
   decision. It does not create a new track boundary.
6. A matching ordinary `m` rule extracts fields and ends processing. This also
   happens with a plain `m/EXPRESSION/` without capture flags; it is not a
   continue-on-match assertion.
7. If no extraction rule matches, the working text becomes the title.

Put filtering and cleanup before the broad artist/title extraction rules.
An ordinary extraction rule above your filter can make that filter unreachable.

## Examples

### Exclude advertisements

Put this before the extraction rules:

```text
m/\badvert\b/ix
```

For several labels, use alternatives:

```text
m/\b(advert|commercial|station promo)\b/ix
```

Use `x` for advertisements. Using `e` instead would ignore the new title and
could append the advertisement's audio to the preceding accepted track.

### Keep only selected titles

```text
m/^(Artist One|Artist Two)\s+-/is
```

Follow this with normal extraction rules. Only titles matching the allowlist are
saved. To save an exception before excluding everything else:

```text
m/^Special Artist\s+-/is
m//x
```

### Clean up and extract fields

```text
s/\s*-?\s*mp3pro$//i
m/^\s*(.*?)\s+-\s+(.*)$/A1T2
```

For metadata formatted as `Artist | Album | Title | Year | Track`:

```text
m/^([^|]+) \| ([^|]+) \| ([^|]+) \| ([0-9]{4}) \| ([0-9]+)$/A1C2T3Y4N5
```

Captured fields can feed filenames and supported ID3 fields. The metadata track
number (`N`) is distinct from the output filename sequence (`%q`).

### Empty titles and hyphens

Optional rule, before extraction:

```text
m/^[\s-]*$/x
```

It excludes explicit empty titles, whitespace-only titles, and titles made only
of spaces and hyphens, including ` - `. The supplied example leaves it disabled
because such metadata can also accompany music.

A zero-length ICY metadata block means **no update**, rather than an empty title.
The previous track and its save/exclude decision continue. An explicit empty
`StreamTitle` can be matched by the rule above. If a station provides no metadata
at all, the filter cannot identify advertisements or infer song boundaries.

## Related command-line options

| Option                                              | Relationship to metadata rules                                                                                                                                                                                                                                                          |
|-----------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `-w FILE`                                           | Load this rules file at recording startup. Restart after editing it; there is no live reload. Quote paths containing spaces.                                                                                                                                                            |
| `-d DIR`                                            | Set the output directory; this path is not regex input.                                                                                                                                                                                                                                 |
| `-D PATTERN`                                        | Set the output filename/directory pattern. `%A`, `%T`, and `%C` use parsed artist, title, and album. `%S` is the stream name.                                                                                                                                                           |
| `-q [START]`                                        | Add numbering to the default filename pattern. Without `START`, detect the next number from existing files; `-q 1` starts at 1 for this run.                                                                                                                                            |
| `%q` in `-D`                                        | Automatically scan for the next number. An empty directory starts at `0000`; a custom `-D` controls its own numbering independently of the default-pattern `-q` setting.                                                                                                                |
| `%1q` in `-D`                                       | Start an explicit per-run counter at 1; the digits before `q` are the starting value, not the width. Numbers have a minimum width of four digits.                                                                                                                                       |
| `-s`                                                | Disable the default per-stream output subdirectory; unrelated to the rule flag `s`.                                                                                                                                                                                                     |
| `-k COUNT`                                          | Leave the first `COUNT` encountered tracks in `incomplete` instead of promoting them as complete. This is startup handling, not a regex filter; `-k 0` disables it.                                                                                                                     |
| `--incomplete-min-duration=N[s\|m\|h]`              | After finalization, remove current incomplete files shorter than the audio-duration limit; default off. This also covers tracks left by `-k` and overwrite policies. Completed tracks and older recordings are preserved. See [Incomplete recordings](README.md#incomplete-recordings). |
| `-t`, `--keep-incomplete`                           | Preserve existing incomplete tracks when names collide; it does not exclude tracks.                                                                                                                                                                                                     |
| `-o always\|never\|larger\|version`                 | Choose how existing complete filenames are handled. This is separate from regex selection.                                                                                                                                                                                              |
| `-T`                                                | Truncate duplicate incomplete tracks when applicable; it is not an advertisement filter.                                                                                                                                                                                                |
| `-a [FILE]`                                         | Also record the continuous stream and a CUE file; exclusion does not remove audio from this recording.                                                                                                                                                                                  |
| `-A`                                                | Disable individual recordings. Together with `-a`, there are no individual files for `x` to suppress.                                                                                                                                                                                   |
| `-r [PORT]`, `-R COUNT`                             | Enable/configure the relay; excluded audio still plays through the relay.                                                                                                                                                                                                               |
| `--wav`                                             | Decode MP3 recordings to PCM WAV; accepted individual tracks follow the same exclusion decision.                                                                                                                                                                                        |
| `--codeset-metadata=NAME`                           | Choose the incoming metadata character encoding when the station's text is decoded incorrectly. The rules file remains UTF-8.                                                                                                                                                           |
| `--codeset-id3=NAME`, `--codeset-relay=NAME`        | Control downstream tag/relay encoding, rather than the rules file's encoding.                                                                                                                                                                                                           |
| `-i`, `--no-id3`, `--with-id3v1`, `--without-id3v2` | Control ID3 output; disabling tags does not disable metadata parsing, splitting, or filtering.                                                                                                                                                                                          |
| `-E COMMAND`                                        | Obtain metadata from an external process. This path supplies fields directly and bypasses the ICY regex parser; do not assume `-w` filters its output.                                                                                                                                  |
| `-f`                                                | Obsolete drop-string option; rejected by the program. Use `-w` instead.                                                                                                                                                                                                                 |

Excluded tracks are not opened as individual files and do not consume an output
sequence number. For example, `Song A`, `advert`, `Song B` produces consecutive
saved numbers for A and B. This differs from the internal count of encountered
tracks used by `-k`, which includes metadata transitions for excluded tracks.
Existing gaps or pre-existing files are not renumbered. Avoid simultaneous
recorders sharing the same output directory/prefix: the recording-directory lock
rejects a second active writer before files are opened. Sequence scanning itself
does not reserve numbers against unrelated applications or older Streamripper builds.

## Boundaries and audio timing

Filtering follows metadata boundaries. Late or inaccurate station metadata can
leave part of an advertisement in an accepted recording. MP3 split options tune
the relationship between metadata and the audio split:

| Option                            | Meaning                                                                                                       |
|-----------------------------------|---------------------------------------------------------------------------------------------------------------|
| `--xs-none`                       | Disable silence searching.                                                                                    |
| `--xs-offset=MSEC`                | Shift the expected split relative to the metadata update.                                                     |
| `--xs-padding=PREV:NEXT`          | Set padding for the previous and next recordings in milliseconds; padding can retain audio across a boundary. |
| `--xs-search-window=BEFORE:AFTER` | Set the silence search window around the expected split, in milliseconds.                                     |
| `--xs-silence-length=MSEC`        | Set the expected silence duration in milliseconds.                                                            |
| `--xs2`                           | Select the alternative silence detector.                                                                      |

These options adjust split timing; they do not recognize advertisements.

## Troubleshooting and limitations

- A missing or unreadable `-w` file silently falls back to the built-in rules.
  Check the actual path and permissions before relying on exclusion.
- Malformed rules print warnings and are skipped. An empty or comment-only file
  supplies no custom actions: the whole title is kept as the title field.
- Check the console for malformed-command/regex warnings at startup. Use a short
  recording in a temporary output directory to validate a rule before leaving it
  running unattended. A console title alone does not prove a track was saved.
- Patterns operate on the current title after preceding substitutions. Test
  against exactly what the station sends, including separators and suffixes.
- Inherited metadata and filename byte limits still apply. Regexes cannot recover
  text that was already truncated by those limits.
- A completed file is promoted only after the next detected boundary. The final
  accepted track normally remains in `incomplete` when recording stops.
