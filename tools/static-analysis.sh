#!/usr/bin/env bash
#
# tools/static-analysis.sh -- static analysis for BomberTalk.
# Standard tools only, host/SDL build, no network.  Every stage exits non-zero
# on a finding.  Mirrors tools/build-sdl.sh's env (PEERTALK_DIR / CLOG_DIR).
#
# Usage:  PEERTALK_DIR=~/peertalk CLOG_DIR=~/clog tools/static-analysis.sh [stage...]
#         stages: cppcheck tidy scan tests retro68      (default: all)
# Env:    LLVM_BIN, OUT (default <repo>/build-static), RETRO68_TOOLCHAIN (enables retro68)
set -uo pipefail
repo="${BT_REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$repo"
pt="${PEERTALK_DIR:-$repo/../peertalk}"; clog="${CLOG_DIR:-$repo/../clog}"
out="${OUT:-$repo/build-static}"; mkdir -p "$out"
llvm="${LLVM_BIN:-}"
[ -z "$llvm" ] && ! command -v clang-tidy >/dev/null 2>&1 && llvm=/opt/homebrew/opt/llvm/bin
tool() { if [ -n "$llvm" ] && [ -x "$llvm/$1" ]; then echo "$llvm/$1"; else command -v "$1"; fi; }
[ -d "$pt/include" ] && [ -d "$clog/include" ] || { echo "need PEERTALK_DIR and CLOG_DIR" >&2; exit 1; }
command -v pkg-config >/dev/null && pkg-config --exists sdl2 || { echo "SDL2 dev package needed" >&2; exit 1; }

# Single source of truth for the file list: tools/build-sdl.sh
GAME=$(sed -n '/^GAME_SOURCES="/,/^"/p' tools/build-sdl.sh | sed 's/GAME_SOURCES="//; s/"//' | tr -s ' \n' ' ')
# shellcheck disable=SC2206
SRC=($GAME)
SDL=($(pkg-config --cflags sdl2))
INC=(-I include -I "$pt/include" -I "$pt/src/core" -isystem "$clog/include" "${SDL[@]}")
DEF=(-DBT_POSIX -DPT_PLATFORM_POSIX)
sysroot=(); [ "$(uname)" = Darwin ] && sysroot=(-isysroot "$(xcrun --show-sdk-path)")

stages=("$@"); [ ${#stages[@]} -eq 0 ] && stages=(cppcheck tidy scan tests retro68)
rc=0; fail() { echo "FAIL: $1" >&2; rc=1; }

for stage in "${stages[@]}"; do
case "$stage" in
cppcheck)
    echo "== cppcheck =="
    # unusedFunction/staticFunction: whole-program noise on a game split into
    # many TUs; variableScope: house C89 style (decls at top of block).
    cppcheck --enable=warning,performance,portability,style --inconclusive --language=c \
        --std=c89 --platform=unix64 --check-level=exhaustive --quiet \
        --suppress=unusedFunction --suppress=staticFunction --suppress=variableScope \
        --suppress=missingIncludeSystem --suppress=constVariablePointer \
        --suppress=constParameterPointer --suppress=constParameterCallback \
        --inline-suppr --error-exitcode=2 "${DEF[@]}" "${INC[@]}" "${SRC[@]}" \
        > "$out/cppcheck.txt" 2>&1 && echo ok || { cat "$out/cppcheck.txt"; fail cppcheck; } ;;
tidy)
    echo "== clang-tidy =="
    ct="$(tool clang-tidy)" || { fail "clang-tidy missing"; continue; }
    checks='-*,bugprone-*,clang-analyzer-*,cert-*,concurrency-*,performance-*,portability-*'
    checks+=',-bugprone-easily-swappable-parameters,-bugprone-signed-bitwise'
    checks+=',-bugprone-narrowing-conversions,-bugprone-branch-clone,-concurrency-mt-unsafe'
    checks+=',-bugprone-too-small-loop-variable'
    "$ct" -checks="$checks" -warnings-as-errors='*' -header-filter="^$repo/include/.*" \
        "${SRC[@]}" -- -std=gnu89 "${DEF[@]}" "${INC[@]}" "${sysroot[@]}" \
        > "$out/clang-tidy.txt" 2>&1 && echo ok || { grep -E 'warning:|error:' "$out/clang-tidy.txt" | head -40; fail clang-tidy; } ;;
scan)
    echo "== scan-build =="
    sb="$(tool scan-build)" || { fail "scan-build missing"; continue; }
    cat > "$out/sb_inner.sh" <<INNER
#!/usr/bin/env bash
set -e
cd "$repo"
for f in ${SRC[*]}; do \$CC -std=gnu89 ${DEF[*]} ${INC[*]} -c \$f -o "$out/sb_\$(basename \$f).o"; done
INNER
    rm -rf "$out/scan-build"
    en=(); for c in alpha.unix.cstring.OutOfBounds alpha.unix.cstring.BufferOverlap \
        alpha.core.TestAfterDivZero optin.taint.GenericTaint optin.taint.TaintedAlloc \
        optin.taint.TaintedDiv security.insecureAPI.strcpy \
        security.insecureAPI.DeprecatedOrUnsafeBufferHandling; do en+=(-enable-checker "$c"); done
    # alpha.deadcode.UnreachableCode is deliberately NOT enabled: its one hit
    # (bomb.c Bomb_PlaceAt loop increment) is a false positive.
    "$sb" ${llvm:+--use-analyzer="$llvm/clang"} "${en[@]}" --status-bugs -o "$out/scan-build" \
        bash "$out/sb_inner.sh" > "$out/scan-build.log" 2>&1 \
        && echo ok || { grep -E 'warning:' "$out/scan-build.log" | head; fail scan-build; } ;;
tests)
    echo "== host tests under ASan+UBSan =="
    make -C tests clean >/dev/null
    make -C tests check SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -g -fno-omit-frame-pointer" \
        > "$out/tests-san.txt" 2>&1 && tail -1 "$out/tests-san.txt" || { tail -30 "$out/tests-san.txt"; fail tests; }
    make -C tests clean >/dev/null ;;
retro68)
    echo "== Retro68 gcc -fanalyzer (classic sources) =="
    if [ -z "${RETRO68_TOOLCHAIN:-}" ]; then echo "skipped (RETRO68_TOOLCHAIN unset)"; continue; fi
    : > "$out/retro68.txt"
    for arch in m68k powerpc; do
      for f in src/*.c; do
        case "$f" in *_sdl.c|*main_posix.c|*mac_shim.c|*platform_posix.c) continue;; esac
        "$RETRO68_TOOLCHAIN/bin/$arch-apple-macos-gcc" -O2 -fanalyzer -std=gnu89 -Wall -Wextra \
            -I include -I "$pt/include" -I "$pt/src/core" -I "$clog/include" -c "$f" -o /dev/null 2>> "$out/retro68.txt"
      done
    done
    # -Wconversion/-Wpointer-sign noise from the Toolbox headers is not enabled here
    if grep -q 'Wanalyzer' "$out/retro68.txt"; then grep Wanalyzer "$out/retro68.txt" | head; fail retro68; else echo ok; fi ;;
*) echo "unknown stage $stage" >&2; rc=2 ;;
esac
done
[ $rc -eq 0 ] && echo "static-analysis: clean" || echo "static-analysis: FAILED" >&2
exit $rc
