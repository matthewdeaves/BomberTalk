#!/bin/bash
#
# build-all.sh -- Rebuild clog, PeerTalk, and BomberTalk for all targets
#
# Ensures the full dependency chain is always fresh:
#   clog -> PeerTalk -> BomberTalk
#
# Usage: ./build-all.sh [68k|ppc-mactcp|ppc-ot|all]
#        Default: all

set -euo pipefail

RETRO68_TOOLCHAIN="${RETRO68_TOOLCHAIN:-$HOME/Retro68-build/toolchain}"
CLOG_DIR="${CLOG_DIR:-$HOME/clog}"
PEERTALK_DIR="${PEERTALK_DIR:-$HOME/peertalk}"
BOMBERTALK_DIR="$(cd "$(dirname "$0")" && pwd)"
JOBS="$(nproc 2>/dev/null || echo 4)"

TARGET="${1:-all}"

die() { echo "FATAL: $*" >&2; exit 1; }

verify_file() {
    [ -f "$1" ] || die "$1 was not produced"
    echo "    -> $1 ($(stat -c%s "$1") bytes)"
}

build_clog() {
    local dir="$1" tc="$2"
    echo "  clog ($dir)..."
    mkdir -p "$CLOG_DIR/$dir"
    cd "$CLOG_DIR/$dir"
    cmake .. -DCMAKE_TOOLCHAIN_FILE="$tc" || die "clog cmake failed"
    make -j"$JOBS" clog || die "clog make failed"
    verify_file "$CLOG_DIR/$dir/libclog.a"
}

build_peertalk() {
    local dir="$1" tc="$2"
    shift 2
    echo "  peertalk ($dir)..."
    mkdir -p "$PEERTALK_DIR/$dir"
    cd "$PEERTALK_DIR/$dir"
    cmake .. -DCMAKE_TOOLCHAIN_FILE="$tc" -DCLOG_DIR="$CLOG_DIR" "$@" || die "peertalk cmake failed"
    make -j"$JOBS" peertalk || die "peertalk make failed"
    verify_file "$PEERTALK_DIR/$dir/libpeertalk.a"
}

build_bombertalk() {
    local dir="$1" tc="$2" bin_name="$3"
    shift 3
    echo "  bombertalk ($dir)..."
    mkdir -p "$BOMBERTALK_DIR/$dir"
    cd "$BOMBERTALK_DIR/$dir"
    cmake .. -DCMAKE_TOOLCHAIN_FILE="$tc" \
        -DPEERTALK_DIR="$PEERTALK_DIR" -DCLOG_DIR="$CLOG_DIR" "$@" || die "bombertalk cmake failed"
    # Force relink to pick up new .a files
    rm -f "$bin_name" BomberTalk.bin
    make -j"$JOBS" || die "bombertalk make failed"
    verify_file "$BOMBERTALK_DIR/$dir/BomberTalk.bin"
}

build_68k() {
    echo "=== Building 68k (Mac SE) ==="
    local TC="$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake"
    build_clog      "build-m68k"       "$TC"
    build_peertalk  "build-68k"        "$TC"
    build_bombertalk "build-68k"       "$TC" "BomberTalk.code.bin"
}

build_ppc_mactcp() {
    echo "=== Building PPC MacTCP (6200) ==="
    local TC="$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
    build_clog      "build-ppc"        "$TC"
    build_peertalk  "build-ppc-mactcp" "$TC" -DPT_PLATFORM=MACTCP
    build_bombertalk "build-ppc-mactcp" "$TC" "BomberTalk.xcoff" -DPT_PLATFORM=MACTCP
}

build_ppc_ot() {
    echo "=== Building PPC OT (6400) ==="
    local TC="$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
    build_clog      "build-ppc"        "$TC"
    build_peertalk  "build-ppc-ot"     "$TC" -DPT_PLATFORM=OT
    build_bombertalk "build-ppc-ot"    "$TC" "BomberTalk.xcoff" -DPT_PLATFORM=OT
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
