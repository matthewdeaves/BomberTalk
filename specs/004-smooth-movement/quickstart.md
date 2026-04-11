# Quickstart: 004-smooth-movement

**Branch**: `004-smooth-movement`

## What This Feature Does

Replaces grid-locked tile-snap movement with smooth pixel-by-pixel movement. Players slide between tiles, can be caught by explosions while between tiles, can walk off bombs they place, and get corner-sliding assistance. Also fixes ghost sprites when players disconnect.

## Key Files to Modify

| File | What Changes |
|------|-------------|
| `include/game.h` | Player struct (new fields), MsgPosition v3, BT_PROTOCOL_VERSION=3 |
| `src/player.c` | Core rewrite: pixel movement, AABB collision, corner sliding, interpolation |
| `src/bomb.c` | AABB explosion kills, bomb pass-through |
| `src/net.c` | MsgPosition v3 send/receive, disconnect dirty rect fix |
| `src/screen_game.c` | Multi-tile dirty rects, pixel-position drawing calls |
| `src/renderer.c` | DrawPlayer takes pixel coords |

## Build & Test

```bash
# Build all three targets (from repo root)
cd build-68k && cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make
cd ../build-ppc-ot && cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake -DPT_PLATFORM=OT -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make
cd ../build-ppc-mactcp && cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake -DPT_PLATFORM=MACTCP -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make
```

## Test Checklist

1. **Smooth movement**: Hold arrow key, verify sprite slides (not snaps) on all 3 Macs
2. **Wall collision**: Move into wall, verify player stops flush without overlap
3. **Explosion kills between tiles**: Place bomb, move partially off tile, verify death
4. **Explosion near-miss**: Place bomb, move fully off tile, verify survival
5. **Bomb walk-off**: Place bomb, walk away, try to walk back — should be blocked
6. **Network sync**: Two machines, verify smooth remote player movement
7. **Disconnect cleanup**: Quit on one machine, verify sprite disappears on others
8. **FPS check**: Verify no regression on Mac SE (~10fps), 6200 (~24fps), 6400 (~26fps)
9. **Corner sliding**: Move near a corridor opening, press perpendicular direction, verify nudge
10. **Protocol mismatch**: Connect v2 and v3 clients, verify lobby warning

## Key Design Decisions

- **Center-based grid derivation**: `gridCol = (pixelX + tileSize/2) / tileSize`
- **Fractional accumulator**: Prevents speed drift from integer truncation across tile sizes
- **Axis-separated collision**: Move X first, clamp, then Y, clamp — prevents corner-cutting
- **Interpolation for remote players**: Lerp toward target with tick-based rate (INTERP_TICKS=4)
- **Hitbox inset**: 12.5% inset (2px/16px SE, 4px/32px color) for forgiving collision
