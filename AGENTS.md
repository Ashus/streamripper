# Project working agreements

## Language and scope

- Communicate with Ashus in Czech and use informal address.
- Write repository documentation, code comments, CLI help, runtime messages, and
  test output in English. This project-specific agreement also applies to logs.
- Preserve existing comments and copyright/license notices. Use UTF-8; let Git
  handle line endings.
- Maintain the Windows CLI fork at https://github.com/Ashus/streamripper.
  Downloads belong at https://github.com/Ashus/streamripper/releases.
- Keep Windows support when reviewing Linux-oriented forks. Do not claim Linux
  compatibility based only on surviving POSIX code; compile and test it first.
- Do not introduce an FFmpeg dependency. Keep native AAC/MP3 finalization and
  metadata tags compatible with the existing CLI options.

## Versioning and releases

- Keep the version until Ashus explicitly requests a version change.
  Development work alone is not permission to increment it or publish a release.
- `VERSION` is the source of truth for executable and package versions. Use normal
  numeric fork versions, without a feature suffix such as `-https`.
- This is an independent fork; its version numbers do not imply upstream releases.
- Build/release instructions should read the version from `VERSION`, rather than
  hardcoding a particular release in tag or archive commands.
- Distribute the contents of `dist` as a portable `.7z` archive with a SHA-256
  checksum. Keep the complete required runtime DLLs and license notices.
- Do not commit binaries, `dist`, toolchains, or release archives; do not use Git
  LFS for them. Do not push, publish, or create release tags without a request.

## Commits and history

- Make logical implementation commits, including their relevant tests. Keep
  unrelated fixes separate and do not stage independent user edits.
- Keep documentation and release metadata changes above the latest release tag
  together in **one final documentation commit**, after implementation commits.
  This includes `VERSION` so intermediate commits do not toggle release numbers.
  When continuing the existing local history, insert implementation commits
  before that final commit and update the consolidated documentation commit.
- Preserve the latest release tag and commits at or below it. Before an authorized
  local history rewrite, keep a backup ref and verify that the intended final
  file contents and unrelated working-tree changes are preserved.
- Inspect current history before editing it, hashes remembered from earlier
  context are not reliable.
- If adapting substantial code from another fork, name its original author and
  source repository/commit in the implementation commit message. Preserve original
  notices and update `PORTING.md` with provenance.
- Distinguish code adapted from a fork from an independent implementation merely
  reviewed against it. Do not claim a wholesale import or copied implementation
  when only the approach was compared.

## Documentation

- Keep `CHANGELOG.md`'s unreleased section ordered by user impact and grouped into
  Features and Fixes where appropriate. Merge overlapping entries.
- Keep Windows build/setup instructions complete for a clean checkout, including
  obtaining MSYS2 and installing dependencies. Prefer repository-relative paths.
- `METADATA-RULES.md` is the comprehensive guide for `-w`/`--metadata-rules` and
  related recording options. Use generic examples such as `advert`; do not add
  personal station-specific filtering files.
- Update help, relevant guides, and test instructions when an option changes.
  Preserve short options when adding descriptive aliases.
- Keep current feature descriptions distinct from historical release limitations.
  Do not present an old limitation as applying to newly implemented behavior.

## Builds, tools, and cleanup

- The default toolchain prefix is `build\toolchain\mingw64`, relative to this
  repository. CMD scripts resolve defaults from their own location, and Python
  scripts from `__file__`; explicit relative arguments refer to the caller's CWD.
- Use `build-windows.cmd` to build and `package-windows.cmd` to regenerate `dist`
  and the `.7z` archive. Do not edit generated packaged files directly.
- Keep temporary helper scripts and audit downloads under ignored `build`, remove
  them when finished, and do not commit them. Do not delete `build` wholesale:
  it contains the local toolchain, test fixtures, and release artifacts.
- Recording must not create unsolicited debug files such as `dump.mp3` or
  `gcs1.txt`, nor overwrite existing user files with those names.
- Under WSL, an existing Windows checkout may be shared, for example at
  `/mnt/projekty/Streamripper`. Keep the Linux build directory separate from the
  Windows `build` directory. Do not reuse a CMake cache between operating systems.

## Validation and transport behavior

- During implementation, run only checks relevant to the change. Defer unrelated
  tests and the complete suite until the final validation stage; do not repeatedly
  run the entire suite after minor changes.
- Build the changed code, check IDE inspections and `git diff --check`, and report
  what was actually tested. Do not treat inspection results as a runtime test.
- Keep TLS certificate and hostname/IP verification enabled by default.
  `--insecure` is the explicit opt-out; for HTTPS proxies it applies to both proxy
  TLS and origin TLS. `--tls-ca-file` supplies trust for both endpoints, whose
  identities must still be verified independently.
- Check WSL availability before promising local Debian build/test results.
