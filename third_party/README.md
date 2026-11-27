# Third-party files

Used only by the optional desktop host (`host/sdl`), for the HUD's text.

| Path | What | Licence | Source |
|---|---|---|---|
| `stb/stb_truetype.h` | TrueType rasteriser, v1.26 | public domain or MIT (in the file) | github.com/nothings/stb, commit f58f558c120e9b32c217290b80bad1a0729fbb2c |
| `inter/Inter-Medium.ttf` | Inter 4.1, Medium (unmodified) | SIL Open Font License 1.1 (`inter/LICENSE.txt`) | github.com/rsms/inter, release v4.1 (`extras/ttf/`), SHA-256 97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872 |

The font is embedded into the executables at build time (`cmake/embed.cmake`).
