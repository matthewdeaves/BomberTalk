# Quickstart: Performance & Correctness Optimizations

**Feature**: 003-optimize-correctness
**Date**: 2026-04-06

## What This Feature Does

Five targeted changes to improve rendering performance and code correctness:

1. **ForeColor/BackColor fix** — Ensures QuickDraw CopyBits runs at full speed on color Macs (potential 2.5x speedup on srcCopy blits)
2. **Deferred background rebuild** — Batches multiple block-destroy events into one background redraw per frame
3. **TileMap cache/reset** — Avoids redundant Resource Manager calls on round restart
4. **Spatial bomb grid** — O(1) bomb collision lookup replacing O(16) linear scan
5. **Peer pointer cleanup** — NULLs stale peer pointer on player disconnect

## Files Changed

| File | Change |
|------|--------|
| `src/renderer.c` | ForeColor/BackColor normalization on all platforms; deferred rebuild flag |
| `include/renderer.h` | New: `Renderer_RequestRebuildBackground()` |
| `src/bomb.c` | Spatial bomb grid; use deferred rebuild |
| `src/tilemap.c` | Cache initial map; add `TileMap_Reset()` |
| `include/tilemap.h` | New: `TileMap_Reset()` |
| `src/net.c` | Use deferred rebuild; NULL peer on disconnect |
| `src/screen_game.c` | Call `TileMap_Reset()` instead of `TileMap_Init()` |

## How to Test

### Build all three targets
```bash
export RETRO68_TOOLCHAIN=~/Retro68-build/toolchain
export PEERTALK_DIR=~/peertalk
export CLOG_DIR=~/clog

# 68k
cd build-68k && cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC OT
cd ../build-ppc-ot && cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake -DPT_PLATFORM=OT -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC MacTCP
cd ../build-ppc-mactcp && cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake -DPT_PLATFORM=MACTCP -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make
```

### Verify on hardware
1. Deploy to all three Macs
2. Press F to show FPS counter — compare with previous build
3. Play a networked game with bomb explosions near breakable blocks
4. Watch socat logs for "RebuildBackground" calls — should see at most 1 per frame during explosions
5. Play multiple rounds — verify blocks restore correctly
6. Disconnect a player — verify no crashes on reconnection
