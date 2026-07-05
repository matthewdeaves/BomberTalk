#!/usr/bin/env bash
#
# build-macosx.sh -- Build BomberTalk as a native Mac OS X Carbon app.
#
# Produces a fat (universal) Mach-O with a PPC slice and an i386 slice so one
# binary runs on OS X 10.4-10.7, PowerPC or Intel. Graphics use Carbon +
# Color QuickDraw (renderer.c, unchanged); networking uses PeerTalk's POSIX/
# BSD backend (Darwin is BSD). Adapted from ~/peertalk/tools/build-macosx-fat.sh.
#
# RUN THIS ON THE OS X BUILD HOST (mini-intel: 10.7, gcc-4.0, 10.4u SDK).
# From the Linux dev box: rsync the repo + ~/peertalk + ~/clog over, then
# run this over ssh. See specs/011-macosx-sdl2/quickstart.md.
#
# env:
#   PEERTALK_DIR  peertalk checkout (default: ../peertalk)
#   CLOG_DIR      clog checkout     (default: ../clog)
#   SDK           sysroot   (default: 10.4u)
#   MIN           deployment target (default: 10.4)
#   CC            compiler  (default: gcc-4.0)
#   ARCHS         -arch list (default: "ppc i386")
#   OUT           output dir (default: build-macosx)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PEERTALK_DIR="${PEERTALK_DIR:-$REPO_ROOT/../peertalk}"
CLOG_DIR="${CLOG_DIR:-$REPO_ROOT/../clog}"
CC="${CC:-/usr/bin/gcc-4.0}"
MIN="${MIN:-10.4}"
ARCHS="${ARCHS:-ppc i386}"
OUT="${OUT:-$REPO_ROOT/build-macosx}"

if [ -z "${SDK:-}" ]; then
  for cand in \
    /Developer/SDKs/MacOSX10.4u.sdk \
    /Developer/SDKs/MacOSX10.5.sdk \
    /Developer/SDKs/MacOSX10.3.9.sdk; do
    if [ -d "$cand" ]; then SDK="$cand"; break; fi
  done
fi
: "${SDK:?no Mac OS X SDK found -- set SDK=/Developer/SDKs/MacOSX10.4u.sdk}"
[ -d "$PEERTALK_DIR/include" ] || { echo "[osx] peertalk not at $PEERTALK_DIR" >&2; exit 1; }
[ -d "$CLOG_DIR/include" ]     || { echo "[osx] clog not at $CLOG_DIR" >&2; exit 1; }
[ -x "$CC" ]                   || { echo "[osx] compiler $CC not found" >&2; exit 1; }

ARCHFLAGS=""
for a in $ARCHS; do ARCHFLAGS="$ARCHFLAGS -arch $a"; done

# -fpascal-strings: "\pFoo" Pascal string literals (gcc-4.0 needs this flag;
#                   Retro68 has it built in).
# -Wno-deprecated-declarations: QuickDraw is deprecated-but-present on OS X.
#                   Using it is the deliberate strategy (Carbon graphics),
#                   so we suppress the deprecation noise. All other warnings
#                   stay on.
COMMON="$ARCHFLAGS -isysroot $SDK -mmacosx-version-min=$MIN -O2 -std=gnu89 -fpascal-strings"
WARN="-Wall -Wextra -Wno-deprecated-declarations -Wno-unused-parameter"
# clog header uses #pragma GCC diagnostic (unknown to gcc-4.0); -isystem silences it.
INCS="-I$REPO_ROOT/include -I$PEERTALK_DIR/include -I$PEERTALK_DIR/src/core -isystem $CLOG_DIR/include"
# BT_CARBON: BomberTalk OS X Carbon build (guards Carbon init + compiles out
#            the Mac SE monochrome fallback path, which never runs on OS X).
# PT_PLATFORM_POSIX: PeerTalk BSD-sockets backend.
DEFS="-DBT_CARBON -DPT_PLATFORM_POSIX"
FRAMEWORKS="-framework Carbon"

GAME_SOURCES="
  src/main.c src/screens.c src/screen_loading.c src/screen_menu.c
  src/screen_lobby.c src/screen_game.c src/tilemap.c src/tilemap_parse.c src/player.c
  src/bomb.c src/renderer.c src/pixfmt.c src/movement.c src/input.c src/net.c src/netcoord.c
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
echo "[osx] host: $(sw_vers -productVersion 2>/dev/null)  CC: $($CC -dumpversion)"
echo "[osx] SDK: $SDK  archs:$ARCHFLAGS  min: $MIN"

OBJS=""
FAILED=0
for src in $GAME_SOURCES $SDK_SOURCES; do
  obj="$OUT/obj/$(basename "${src%.c}").o"
  # shellcheck disable=SC2086  # intentional word-splitting of flag groups
  if "$CC" $COMMON $WARN $INCS $DEFS -c "$src" -o "$obj" 2> "$obj.err"; then
    :
  else
    echo "[osx] COMPILE FAILED: $src"; cat "$obj.err"; FAILED=1
  fi
  # surface non-deprecation warnings even on success
  if [ -s "$obj.err" ] && grep -qvE 'deprecated|mlong-branch' "$obj.err"; then
    grep -vE 'deprecated' "$obj.err" | sed "s|^|[warn $(basename "$src")] |" || true
  fi
  OBJS="$OBJS $obj"
done

[ "$FAILED" = 0 ] || { echo "[osx] one or more sources failed to compile"; exit 1; }

# Assemble a proper .app bundle so the app owns a menu bar, a Dock icon, and
# a resource file. Layout:
#   BomberTalk.app/Contents/MacOS/BomberTalk        (the fat Mach-O)
#   BomberTalk.app/Contents/Resources/BomberTalk.rsrc  (PICTs, via Rez)
#   BomberTalk.app/Contents/Info.plist
#   BomberTalk.app/Contents/PkgInfo
APP="$OUT/BomberTalk.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

echo "[osx] link BomberTalk"
# shellcheck disable=SC2086
"$CC" $COMMON $WARN $INCS $DEFS $OBJS $FRAMEWORKS -o "$APP/Contents/MacOS/BomberTalk"
lipo -info "$APP/Contents/MacOS/BomberTalk"

# Compile the classic PICT resources into a data-fork resource file. The .r
# is self-contained inline hex (no type includes needed). -useDF writes the
# data fork so the file survives copying across non-HFS filesystems; the app
# opens it via FSOpenResourceFile(data fork) at startup.
if command -v Rez >/dev/null 2>&1; then
  echo "[osx] Rez resources -> BomberTalk.rsrc"
  Rez -useDF -o "$APP/Contents/Resources/BomberTalk.rsrc" resources/bombertalk_gfx.r
else
  echo "[osx] WARNING: Rez not found; app will use fallback graphics"
fi

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>              <string>BomberTalk</string>
    <key>CFBundleDisplayName</key>       <string>BomberTalk</string>
    <key>CFBundleExecutable</key>        <string>BomberTalk</string>
    <key>CFBundleIdentifier</key>        <string>com.matthewdeaves.bombertalk</string>
    <key>CFBundlePackageType</key>       <string>APPL</string>
    <key>CFBundleSignature</key>         <string>BTLK</string>
    <key>CFBundleVersion</key>           <string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>LSMinimumSystemVersion</key>    <string>10.3.9</string>
    <key>NSHighResolutionCapable</key>   <true/>
</dict>
</plist>
PLIST

printf 'APPLBTLK' > "$APP/Contents/PkgInfo"

echo "[osx] done -- app bundle at $APP"
