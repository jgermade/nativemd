#!/usr/bin/env bash
# Local development: play the Sonic 1 client (nativemd-internal, private) built against
# THIS nativemd checkout instead of its own submodule, so engine changes show at once.
#
# Usage: tools/up.sh [VERSION]   (make up INTERNAL=VERSION)
#   VERSION  main (default) | latest (highest vX.Y.Z tag) | vX.Y.Z | same (the branch this
#            nativemd checkout is on)
# Environment: INTERNAL_REPO (clone URL), INTERNAL_DIR (default ./internal),
#              UP_ARGS (extra s1-play options, e.g. "--scale 4 --fullscreen").
# The clone in INTERNAL_DIR is yours to edit: with local changes it is not switched or
# pulled (a warning says so); the ROM is rebuilt from the disassembly only when missing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-main}"
REPO="${INTERNAL_REPO:-https://github.com/jgermade/nativemd-internal.git}"
DIR="${INTERNAL_DIR:-$ROOT/internal}"
case "$DIR" in /*) ;; *) DIR="$ROOT/$DIR";; esac

# The ref to use.
case "$VERSION" in
    same)
        REF="$(git -C "$ROOT" symbolic-ref --quiet --short HEAD)" ||
            { echo "up: INTERNAL=same, but this nativemd checkout is not on a branch" >&2; exit 2; }
        KIND=branch;;
    main) REF=main; KIND=branch;;
    latest)
        REF="$(git ls-remote --tags --refs "$REPO" 'v*' | sed 's|.*refs/tags/||' |
               { grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' || true; } | sort -V | tail -1)"
        [ -n "$REF" ] || { echo "up: INTERNAL=latest, but $REPO has no vX.Y.Z tag yet" >&2; exit 2; }
        KIND=tag;;
    v[0-9]*.[0-9]*.[0-9]*) REF="$VERSION"; KIND=tag;;
    *) echo "up: INTERNAL must be main, latest, same or vX.Y.Z (got '$VERSION')" >&2; exit 2;;
esac

# The clone, at that ref (no submodule: this checkout is the engine).
if [ ! -d "$DIR/.git" ]; then
    echo "up: cloning $REPO into $DIR"
    git clone --quiet "$REPO" "$DIR"
fi
git -C "$DIR" fetch --quiet --tags origin
if [ -n "$(git -C "$DIR" status --porcelain --untracked-files=no)" ]; then
    echo "up: $DIR has local changes: using it as it is (not switched to $KIND $REF)" >&2
elif [ "$KIND" = tag ]; then
    git -C "$DIR" rev-parse --quiet --verify "refs/tags/$REF" > /dev/null ||
        { echo "up: no tag $REF in $REPO" >&2; exit 2; }
    git -C "$DIR" checkout --quiet --detach "refs/tags/$REF"
else
    git -C "$DIR" rev-parse --quiet --verify "refs/remotes/origin/$REF" > /dev/null ||
        { echo "up: no branch $REF in $REPO" >&2; exit 2; }
    git -C "$DIR" checkout --quiet -B "$REF" "origin/$REF"
fi
echo "up: nativemd-internal at $(git -C "$DIR" describe --always --tags --dirty) ($KIND $REF)," \
     "engine from $ROOT"

# The ROM (rebuilt bit-perfect from the disassembly), then the game against this engine.
W="$DIR/work"
if [ ! -f "$W/s1/s1built.bin" ]; then
    # Versions older than NATIVEMD_DIR in build_rom.sh take tools from their submodule.
    grep -q NATIVEMD_DIR "$DIR/sonic1/scripts/build_rom.sh" || git -C "$DIR" submodule update --init --quiet
    NATIVEMD_DIR="$ROOT" WORK="$W" "$DIR/sonic1/scripts/build_rom.sh"
fi
cmake -S "$DIR/sonic1" -B "$W/build-up" -G Ninja -DCMAKE_BUILD_TYPE=Release -DNATIVEMD_DIR="$ROOT" > /dev/null
if ! cmake --build "$W/build-up" --target s1-play; then
    if ! grep -q s1-play "$DIR/sonic1/CMakeLists.txt"; then
        echo "up: this nativemd-internal ($KIND $REF) predates s1-play; try INTERNAL=same or a newer one" >&2
    elif ! grep -q 'nmd_sdl:' <(cmake --build "$W/build-up" --target help); then
        echo "up: no desktop host: SDL2 not found (make install)" >&2
    fi
    exit 2
fi
# shellcheck disable=SC2086
exec "$W/build-up/s1-play" "$W/s1/s1built.bin" ${UP_ARGS:-}
