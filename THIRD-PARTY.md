# Bundled libraries

The binaries in `dist` use official MSYS2 mingw64 packages. The table records the exact package versions used for this release and their corresponding source packages, including original source code, PKGBUILD recipes, and MSYS2 patches. License notices are included in `dist/licenses`, with the GPL text in `dist/COPYING`. The modified Streamripper source is included in this repository.

| Library | Package version | License | Corresponding source |
| --- | --- | --- | --- |
| glib2 | 2.90.1-1 | spdx:LGPL-2.1-or-later | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-glib2-2.90.1-1.src.tar.zst) |
| openssl | 3.6.5-1 | spdx:Apache-2.0 | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-openssl-3.6.5-1.src.tar.zst) |
| libiconv | 1.19-1 | spdx:LGPL-2.1-or-later, documentation:spdx:GPL-3.0-or-later | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-libiconv-1.19-1.src.tar.zst) |
| gettext-runtime | 1.0-1 | spdx:GPL-3.0-or-later AND LGPL-2.1-or-later | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-gettext-1.0-1.src.tar.zst) |
| libmad | 0.15.1b-5 | GPL | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-libmad-0.15.1b-5.src.tar.zst) |
| libogg | 1.3.6-1 | spdx:BSD-3-Clause | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-libogg-1.3.6-1.src.tar.zst) |
| libvorbis | 1.3.7-3 | custom | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-libvorbis-1.3.7-3.src.tar.zst) |
| pcre2 | 10.49-1 | spdx:BSD-3-Clause | [Source package, including MSYS2 patches](https://mirror.msys2.org/mingw/sources/mingw-w64-pcre2-10.49-1.src.tar.zst) |

Packaging recursively copies only DLLs imported by the executable and its dependencies. Windows system DLLs are not redistributed. License notices cover the shipped libraries and linked compiler/Windows runtime components; unrelated build tools are excluded. Versions reported by pkg-config are also recorded in `dist/build-info.json`. LAME is optional for tests only and is not packaged.

Update this table against the actual packages used for each new release, especially after changing toolchains.
