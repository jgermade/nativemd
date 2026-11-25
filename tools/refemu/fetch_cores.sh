#!/usr/bin/env bash
# Fetch and build the reference emulators (libretro cores) and the refemu runner into
# $WORK/refemu (default: work/refemu of this repository). Nothing of them is committed or
# linked into nativemd: they are run as separate programs (docs/harness.md, "Reference
# emulators"). Licences: Genesis Plus GX (non-commercial licence), BlastEm (GPL-3.0).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="${WORK:-$ROOT/work}/refemu"
GPGX_COMMIT=51348f65738b0c75f4b8b4488fbc67645e3769af
BLASTEM_COMMIT=1935a96634f89ff43ee43bcdbeb1d7257c70afdb
mkdir -p "$W"
fetch() {  # DIR URL COMMIT
    [ -d "$W/$1/.git" ] || git init -q "$W/$1"
    if [ "$(git -C "$W/$1" rev-parse HEAD 2>/dev/null)" != "$3" ]; then
        git -C "$W/$1" fetch -q --depth 1 "$2" "$3"
        git -C "$W/$1" checkout -q FETCH_HEAD
    fi
}
fetch gpgx https://github.com/libretro/Genesis-Plus-GX "$GPGX_COMMIT"
fetch blastem https://github.com/libretro/blastem "$BLASTEM_COMMIT"
make -C "$W/gpgx" -f Makefile.libretro -j"$(nproc)" > "$W/gpgx.log" 2>&1 || { tail -20 "$W/gpgx.log"; exit 1; }
make -C "$W/blastem" -f Makefile.libretro -j"$(nproc)" > "$W/blastem.log" 2>&1 || { tail -20 "$W/blastem.log"; exit 1; }
cc -O2 -Wall -Wextra -I "$W/gpgx/libretro/libretro-common/include" "$ROOT/tools/refemu/refemu.c" -ldl -o "$W/refemu"
echo "$W: genesis_plus_gx_libretro.so, blastem_libretro.so, refemu"
