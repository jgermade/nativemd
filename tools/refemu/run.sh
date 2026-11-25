#!/usr/bin/env bash
# Run every test ROM of a test ROM directory on nativemd (nmd-oracle: the original code on
# nativemd's hardware model) and on the reference emulators, and compare their results with
# each other and with the contract's expected values (tools/refemu/results.py).
#
# Usage: tools/refemu/run.sh TEST_ROMS_DIR [FRAMES]
#   TEST_ROMS_DIR has a build.sh that assembles its ROMs into TEST_ROMS_DIR/build/*.bin.
#   Needs nmd-oracle (build/, or ORACLE=...) and fetch_cores.sh run once (same WORK).
#   Exit status 1 if any check differs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
W="${WORK:-$ROOT/work}/refemu"
TR="$(cd "$1" && pwd)"
FRAMES="${2:-10}"
ORACLE="${ORACLE:-$ROOT/build/nmd-oracle}"
"$TR/build.sh" > /dev/null
OUT="$W/results"; mkdir -p "$OUT"
fail=0
for rom in "$TR"/build/*.bin; do
    t=$(basename "$rom" .bin)
    echo "== $t"
    "$ORACLE" "$rom" --frames "$FRAMES" --state-out "$OUT/$t.nmds" 2> "$OUT/$t.oracle.log" ||
        { grep -v checksum "$OUT/$t.oracle.log" >&2; fail=1; continue; }
    "$W/refemu" "$W/gpgx/genesis_plus_gx_libretro.so" "$rom" "$FRAMES" "$OUT/$t.gpgx" > "$OUT/$t.gpgx.log" 2>&1 ||
        { cat "$OUT/$t.gpgx.log" >&2; fail=1; continue; }
    "$W/refemu" "$W/blastem/blastem_libretro.so" "$rom" "$FRAMES" "$OUT/$t.blastem" > "$OUT/$t.blastem.log" 2>&1 ||
        { cat "$OUT/$t.blastem.log" >&2; fail=1; continue; }
    python3 -I "$ROOT/tools/refemu/results.py" nativemd="$OUT/$t.nmds" gpgx="$OUT/$t.gpgx" blastem="$OUT/$t.blastem" || fail=1
done
[ $fail = 0 ] && echo "refemu: all machines agree with the contract" || { echo "refemu: DIFFERENCES" >&2; exit 1; }
