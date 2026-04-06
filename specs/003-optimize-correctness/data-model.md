# Data Model: Performance & Correctness Optimizations

**Feature**: 003-optimize-correctness
**Date**: 2026-04-06

## New Data Structures

### Spatial Bomb Grid

Static boolean grid for O(1) bomb position lookups during collision checking.

- **Type**: `unsigned char` array, `MAX_GRID_ROWS` x `MAX_GRID_COLS` (25 x 31)
- **Size**: 775 bytes (static allocation)
- **Values**: 0 = no bomb, 1 = bomb present
- **Lifecycle**:
  - Zeroed on `Bomb_Init()`
  - Set to 1 on `Bomb_PlaceAt(row, col)`
  - Set to 0 on bomb deactivation in `ExplodeBomb()`
- **Invariant**: `gBombGrid[r][c] == 1` if and only if there exists an active bomb at grid position (c, r)

### Tilemap Cache

Static copy of the initial tilemap state for fast round resets.

- **Type**: `TileMap` struct (same as existing `gMap`)
- **Size**: ~800 bytes (2 shorts + 25x31 tile bytes + 4x2 spawn shorts + 1 short)
- **Lifecycle**:
  - Populated once after first `TileMap_Init()` completes
  - Read-only after population
  - Copied back to `gMap` by `TileMap_Reset()`
- **Contents cached**: tile grid, spawn positions, spawn count, dimensions

### Rebuild Flag

Static boolean for deferred background rebuild batching.

- **Type**: `int` (C89 boolean)
- **Size**: 4 bytes
- **Lifecycle**:
  - Set to `TRUE` by `Renderer_RequestRebuildBackground()`
  - Checked and cleared to `FALSE` at start of `Renderer_BeginFrame()`
  - When cleared, triggers one `Renderer_RebuildBackground()` call

## Modified Data Structures

No existing data structures are modified. All new state is file-scope static variables within existing modules.

## Memory Budget Impact

| Structure | Size (bytes) | Module |
|-----------|-------------|--------|
| Spatial Bomb Grid | 775 | bomb.c |
| Tilemap Cache | ~800 | tilemap.c |
| Rebuild Flag | 4 | renderer.c |
| **Total** | **~1,579** | |

Total new static memory: ~1.6 KB. Mac SE application heap has ~1 MB available after init (per FreeMem() logging). This is 0.16% of available heap.

## Network Messages

No changes to network message formats. All existing message types (MSG_BLOCK_DESTROYED, MSG_BOMB_PLACED, etc.) remain unchanged. The deferred rebuild is a rendering-side optimization that does not affect network protocol.
