#!/usr/bin/env bash
#
# build-sdl.sh -- Build BomberTalk as a native SDL2 app (011-macosx-sdl2).
#
# Targets modern desktops (Linux dev box today; Apple Silicon macOS is the
# shipping target). Graphics/input/main-loop use SDL2 (renderer_sdl.c,
# input_sdl.c, main_posix.c) with a tiny QuickDraw text shim (mac_shim.c) so
# the shared screens are unchanged. Networking is PeerTalk's POSIX/BSD backend
# (net.c unchanged). The shared game core is byte-order-correct on little-endian
# via net_wire.c, so this build interoperates with the big-endian classic Macs.
#
# env:
#   PEERTALK_DIR  peertalk checkout (default: ../peertalk)
#   CLOG_DIR      clog checkout     (default: ../clog)
#   CC            compiler          (default: cc)
#   OUT           output dir        (default: build-sdl)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PEERTALK_DIR="${PEERTALK_DIR:-$REPO_ROOT/../peertalk}"
CLOG_DIR="${CLOG_DIR:-$REPO_ROOT/../clog}"
CC="${CC:-cc}"
OUT="${OUT:-$REPO_ROOT/build-sdl}"

[ -d "$PEERTALK_DIR/include" ] || { echo "[sdl] peertalk not at $PEERTALK_DIR" >&2; exit 1; }
[ -d "$CLOG_DIR/include" ]     || { echo "[sdl] clog not at $CLOG_DIR" >&2; exit 1; }
command -v pkg-config >/dev/null 2>&1 || { echo "[sdl] pkg-config required" >&2; exit 1; }
pkg-config --exists sdl2        || { echo "[sdl] SDL2 dev package not found (pkg-config sdl2)" >&2; exit 1; }

SDL_CFLAGS="$(pkg-config --cflags sdl2)"
SDL_LIBS="$(pkg-config --libs sdl2)"

# Shared core stays strict C89; SDL/platform glue uses gnu89 for SDL headers.
# clog's #pragma GCC diagnostic + variadic macros need -isystem and no -pedantic.
COMMON="-O2 -std=gnu89"
WARN="-Wall -Wextra"
# BT_POSIX: SDL2/POSIX build (no Toolbox; mac_shim.h supplies the value types).
# PT_PLATFORM_POSIX: PeerTalk BSD-sockets backend.
DEFS="-DBT_POSIX -DPT_PLATFORM_POSIX"
INCS="-I$REPO_ROOT/include -I$PEERTALK_DIR/include -I$PEERTALK_DIR/src/core \
      -isystem $CLOG_DIR/include $SDL_CFLAGS"

# Game sources: shared core + SDL backends (renderer_sdl/input_sdl/main_posix/
# mac_shim/platform_posix). The classic backends (renderer.c/input.c/main.c)
# are excluded.
GAME_SOURCES="
  src/screens.c src/screen_loading.c src/screen_menu.c src/screen_lobby.c
  src/screen_game.c src/tilemap.c src/tilemap_parse.c src/player.c
  src/collision.c src/bomb.c src/raycast.c src/movement.c src/wincond.c
  src/net.c src/netcoord.c src/net_wire.c
  src/renderer_sdl.c src/input_sdl.c src/main_posix.c
  src/mac_shim.c src/platform_posix.c
"
SDK_SOURCES="
  $PEERTALK_DIR/src/core/pt_core.c
  $PEERTALK_DIR/src/core/pt_memory.c
  $PEERTALK_DIR/src/core/pt_discovery.c
  $PEERTALK_DIR/src/core/pt_messaging.c
  $PEERTALK_DIR/src/platform/posix/pt_posix.c
  $CLOG_DIR/src/clog_posix.c
"

mkdir -p "$OUT/obj"
echo "[sdl] host: $(uname -srm)  CC: $($CC -dumpversion 2>/dev/null || echo '?')"
echo "[sdl] SDL2: $(pkg-config --modversion sdl2)"

OBJS=""
FAILED=0
for src in $GAME_SOURCES $SDK_SOURCES; do
  obj="$OUT/obj/$(basename "${src%.c}").o"
  # shellcheck disable=SC2086  # intentional word-splitting of flag groups
  if "$CC" $COMMON $WARN $INCS $DEFS -c "$src" -o "$obj" 2> "$obj.err"; then
    :
  else
    echo "[sdl] COMPILE FAILED: $src"; cat "$obj.err"; FAILED=1
  fi
  # surface warnings (clog's implicit fsync is the one documented exception)
  if [ -s "$obj.err" ] && grep -qvE 'fsync' "$obj.err"; then
    grep -vE 'fsync' "$obj.err" | sed "s|^|[warn $(basename "$src")] |" || true
  fi
  OBJS="$OBJS $obj"
done

[ "$FAILED" = 0 ] || { echo "[sdl] one or more sources failed to compile"; exit 1; }

echo "[sdl] link BomberTalk"
# shellcheck disable=SC2086
"$CC" $COMMON $OBJS $SDL_LIBS -lpthread -lm -o "$OUT/BomberTalk"

echo "[sdl] done -- $OUT/BomberTalk"
