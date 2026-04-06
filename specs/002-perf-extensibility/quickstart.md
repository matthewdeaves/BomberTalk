# Quickstart: Performance & Extensibility Upgrade

**Feature**: `002-perf-extensibility`
**Date**: 2026-04-06

## Prerequisites

Same as v1.0-alpha:
- Retro68 toolchain built at `~/Retro68-build/toolchain`
- PeerTalk SDK built for all targets at `~/peertalk`
- clog built for all targets at `~/clog`

## Build Commands

All three targets use the same commands as v1.0-alpha. The only build system change is the addition of `-std=c89` to compiler flags (automatic via CMakeLists.txt).

```bash
# 68k MacTCP (Mac SE, System 6)
mkdir -p build-68k && cd build-68k && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC Open Transport (Performa 6400, System 7.6.1)
mkdir -p build-ppc-ot && cd build-ppc-ot && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC MacTCP (Performa 6200, System 7.5.3)
mkdir -p build-ppc-mactcp && cd build-ppc-mactcp && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make
```

## Verifying C89 Enforcement

After building, confirm the `-std=c89` flag is active:

```bash
# Should see -std=c89 in the compile commands
grep -r "std=c89" build-68k/CMakeFiles/BomberTalk.dir/flags.make
```

To verify it catches C99 code, temporarily add a `//` comment to any .c file and rebuild — it should produce a warning.

## Testing Dirty Rectangle System

The dirty rect optimization is transparent — the game should look and play identically. To verify it's working:

1. **Visual test**: Play a game. No visual artifacts, tearing, or missing tiles.
2. **Performance test**: On Mac SE, observe frame rate during 4-player gameplay with bombs. Should be smoother than v1.0-alpha.
3. **Full-dirty fallback**: Place a bomb that destroys multiple blocks. After explosion, the full background rebuild should trigger a full-screen dirty (all tiles marked). Next frame should recover to partial updates.

## Testing Protocol Version

1. Build two copies: one with `BT_PROTOCOL_VERSION 2` (current) and one with the value changed to `99`.
2. Run both on the same LAN. Discover each other in the lobby.
3. Press Start on either client. The game should NOT start. The lobby should indicate a version mismatch.
4. Build both copies with the same version. Press Start. Game should start normally.

## Testing Custom Maps (TMAP Resource)

To test resource-based map loading:

1. Create a custom map using ResEdit or Rez:
   ```
   /* In a .r file */
   read 'TMAP' (128) "custom_map.bin";
   ```
   Where `custom_map.bin` is a binary file: 2-byte cols + 2-byte rows + tile data.

2. Embed in the application's resource fork during build (add to bombertalk.r).
3. Launch the game. The tilemap should reflect the custom layout.
4. Remove the TMAP resource. Launch again. Should fall back to default 15x13 level.

## Testing Player Stats

Player stats are invisible to the user — they produce identical gameplay to v1.0-alpha. Verify:

1. Bomb range is still 1 tile (default `stats.bombRange = 1`).
2. Only 1 bomb can be placed at a time (default `stats.bombsMax = 1`).
3. Movement cooldown feels the same (~0.2s between moves, `stats.speedTicks = 12`).

## Remote Log Monitoring

Same as v1.0-alpha:

```bash
socat UDP-RECV:7355 -
```

New log messages to watch for:
- `"Version mismatch: got %d, expected %d"` — protocol version rejection
- `"TMAP resource loaded: %dx%d"` — custom map loaded
- `"TMAP not found, using default level"` — fallback to built-in map
- `"Found %d spawn points in map"` — spawn scan results
