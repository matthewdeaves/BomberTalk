# Data Model: Renderer Performance Optimizations

**Feature**: 006-renderer-optimization
**Date**: 2026-04-14

## New Static Data

### Sprite Mask Regions (renderer.c)

Per-sprite RgnHandle stored alongside existing sprite GWorlds. Created at PICT load time, disposed at shutdown. Color Macs only.

```
static RgnHandle gPlayerMaskRgn[MAX_PLAYERS]   -- one per player sprite GWorld
static RgnHandle gBombMaskRgn                   -- bomb sprite mask
static RgnHandle gExplosionMaskRgn              -- explosion sprite mask
static RgnHandle gTitleMaskRgn                  -- title sprite mask (if needed)
```

**Lifecycle**:
- Created: During LoadPICTResources(), after each sprite GWorld is created and PICT drawn
- Used: Every frame in Renderer_DrawPlayer/DrawBomb/DrawExplosion (color Mac path only)
- Disposed: During Renderer_Shutdown(), before DisposeGWorld for each sprite

**Memory cost**: Region data is compact (run-length encoded scan lines). For 32x32 sprites, each region is ~100-200 bytes. Total: ~1.4KB for 7 regions. Negligible.

**Failure mode**: If NewRgn() returns NULL or BitMapToRegion() returns error, the mask region is stored as NULL. Draw functions check for NULL and fall back to `transparent` mode.

### Sprite Draw State Flag (renderer.c)

```
static int gSpriteDrawActive   -- TRUE between BeginSpriteDraw/EndSpriteDraw
```

**Lifecycle**:
- Set TRUE by Renderer_BeginSpriteDraw()
- Set FALSE by Renderer_EndSpriteDraw()
- Checked by DrawPlayer/DrawBomb/DrawExplosion fallback paths to skip redundant SavePort/RestorePort

### Temporary Mask Construction Bitmap

Allocated and disposed within the mask creation helper function (not persistent):

```
1-bit BitMap:
  .baseAddr = NewPtr(rowBytes * height)    -- temporary allocation
  .rowBytes = ((width + 15) / 16) * 2     -- word-aligned
  .bounds   = {0, 0, height, width}
```

Created once per sprite during LoadPICTResources(), disposed immediately after BitMapToRegion() copies the data into the region. No persistent memory cost.

## Modified Data

### TileMap Macro (tilemap.h)

New preprocessor macro added to the existing public header:

```
TILEMAP_TILE(map, col, row) -- expands to ((map)->tiles[(row)][(col)])
```

Depends on: TileMap struct definition in game.h (already public), TileMap_Get() accessor in tilemap.h (already exists).

No new data fields. No struct changes. The macro is purely a compile-time access pattern.

## Unchanged Data

The following existing data structures are NOT modified:

- `GameState gGame` -- no new fields
- `TileMap` struct in game.h -- no field changes
- `Bomb` / `Explosion` structs -- no changes
- `Player` struct -- no changes
- Dirty rectangle arrays -- no changes
- GWorld pointers (gBackground, gWorkBuffer, sprite GWorlds) -- unchanged
- Cached PixMap pointers (gCachedPlayerPM, etc.) -- unchanged

## State Diagram: Sprite Draw Lifecycle

```
Renderer_Init()
  └─> LoadPICTResources()
       ├─> LoadPICTToGWorld() -- existing, creates sprite GWorld
       └─> CreateMaskRegion() -- NEW, creates RgnHandle from GWorld
            ├─> Success: gPlayerMaskRgn[i] = valid RgnHandle
            └─> Failure: gPlayerMaskRgn[i] = NULL (fallback to transparent)

Per Frame (Game_Draw):
  Renderer_BeginFrame()          -- dirty rect CopyBits bg->work (unchanged)
  Renderer_BeginSpriteDraw()     -- NEW: SavePort/SetPortWork once
  ├─> DrawBomb() x N             -- uses maskRgn if available, else transparent/fallback
  ├─> DrawExplosion() x N        -- uses maskRgn if available, else transparent/fallback
  └─> DrawPlayer() x N           -- uses maskRgn if available, else transparent/fallback
  Renderer_EndSpriteDraw()       -- NEW: RestorePort once
  Renderer_EndFrame(window)      -- dirty rect CopyBits work->window (unchanged)

Renderer_Shutdown()
  ├─> DisposeRgn() for each non-NULL mask region -- NEW
  └─> DisposeGWorld() for each sprite GWorld     -- existing
```
