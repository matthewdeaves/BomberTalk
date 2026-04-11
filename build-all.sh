#!/bin/bash
#
# build-all.sh -- Build BomberTalk for all Classic Mac targets
#
# Dependencies (clog, peertalk) are built automatically via CMake
# FetchContent. To use local checkouts instead of fetching from GitHub,
# set CLOG_DIR and/or PEERTALK_DIR environment variables.
#
# Usage: ./build-all.sh [68k|ppc-mactcp|ppc-ot|all]
#        Default: all

set -euo pipefail

RETRO68_TOOLCHAIN="${RETRO68_TOOLCHAIN:-$HOME/Retro68-build/toolchain}"
BOMBERTALK_DIR="$(cd "$(dirname "$0")" && pwd)"
JOBS="$(nproc 2>/dev/null || echo 4)"

TARGET="${1:-all}"

# Pass through local dependency overrides if set
CMAKE_EXTRA_ARGS=()
[ -n "${CLOG_DIR:-}" ] && CMAKE_EXTRA_ARGS+=("-DCLOG_DIR=$CLOG_DIR")
[ -n "${PEERTALK_DIR:-}" ] && CMAKE_EXTRA_ARGS+=("-DPEERTALK_DIR=$PEERTALK_DIR")

die() { echo "FATAL: $*" >&2; exit 1; }

verify_file() {
    [ -f "$1" ] || die "$1 was not produced"
    echo "    -> $1 ($(stat -c%s "$1") bytes)"
}

build_target() {
    local dir="$1" tc="$2"
    shift 2
    echo "  bombertalk ($dir)..."
    mkdir -p "$BOMBERTALK_DIR/$dir"
    cd "$BOMBERTALK_DIR/$dir"
    cmake .. -DCMAKE_TOOLCHAIN_FILE="$tc" "${CMAKE_EXTRA_ARGS[@]}" "$@" \
        || die "cmake failed"
    make -j"$JOBS" || die "make failed"
    verify_file "$BOMBERTALK_DIR/$dir/BomberTalk.bin"
}

build_68k() {
    echo "=== Building 68k (Mac SE) ==="
    local TC="$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake"
    build_target "build-68k" "$TC"
}

build_ppc_mactcp() {
    echo "=== Building PPC MacTCP (6200) ==="
    local TC="$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
    build_target "build-ppc-mactcp" "$TC" -DPT_PLATFORM=MACTCP
}

build_ppc_ot() {
    echo "=== Building PPC OT (6400) ==="
    local TC="$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
    build_target "build-ppc-ot" "$TC" -DPT_PLATFORM=OT
}

case "$TARGET" in
    68k)         build_68k ;;
    ppc-mactcp)  build_ppc_mactcp ;;
    ppc-ot)      build_ppc_ot ;;
    all)
        build_68k
        build_ppc_mactcp
        build_ppc_ot
        echo ""
        echo "All 3 targets built successfully."
        ;;
    *)
        echo "Usage: $0 [68k|ppc-mactcp|ppc-ot|all]"
        exit 1
        ;;
esac
