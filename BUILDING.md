# Build guides

Use the complete instructions for your operating system:

- [Windows: toolchain setup, build, tests, portable archive and release](BUILDING-WINDOWS.md)
- [Linux: DEB installation, dependencies, source build, packaging and tests](BUILDING-LINUX.md)

Both builds use the same sources and read their version from `VERSION`. Windows
releases are portable `.7z` archives. Linux releases also provide native amd64
`.deb` packages; source builds remain available. Both packaging workflows write
artifacts and SHA-256 checksums to `build/release` for manual release uploads.
