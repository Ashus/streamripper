# Bundled source code

These inherited helpers are kept separate from Streamripper's application code
in `console` and `lib`. They are the copies carried by this fork, including
historical adaptations; they are not pristine imports of current upstream code.
Preserve their original comments and copyright/license notices, and exclude them
from application-wide formatting.

| Directory | Contents and provenance |
| --- | --- |
| `libiberty` | `argv.c`: argument-vector helpers from GNU libiberty, written by Fred Fish; GNU Library General Public License, version 2 or later, as stated in the file. |
| Root | `charset.c`, `charset.h` and `iconvert.c` name Edmund Grimley Evans; `utf8.c` names Peter Harris and Edmund Grimley Evans. These source files state GPL version 2 or later. `charset.c` says it was first written for Ogg Vorbis; no organizational origin is assigned to this group. `utf8.h` contains the conversion API, and `charmaps.h` contains generated character-mapping tables that should not be edited by hand. |
| `linux` | `list.h`: linked-list helpers adapted from the Linux kernel, as recorded in the existing source comment. |
| `dirent` | `uce_dirent.h`: Toni Ronkko's directory compatibility implementation, with its original permissive license notice. |
| `licenses` | License notices used when packaging runtime libraries; see [THIRD-PARTY.md](../THIRD-PARTY.md). |

`lib/CMakeLists.txt` includes these files in the existing `streamripper1` target.
`libiberty/argv.c` remains conditional on the absence of system libiberty.
Application includes name the relative `third_party` path explicitly; includes
between files in the same third-party directory remain local.
