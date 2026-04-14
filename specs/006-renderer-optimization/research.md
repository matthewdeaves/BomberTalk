# Research: Renderer Performance Optimizations

**Feature**: 006-renderer-optimization
**Date**: 2026-04-14

## R1: Port Save/Restore Cost and Batching Strategy

**Decision**: Hoist SavePort/SetPortWork/RestorePort out of individual sprite draw functions into a single bracket in Game_Draw(). Add Renderer_BeginSpriteDraw() and Renderer_EndSpriteDraw() public functions that Game_Draw() calls once around the sprite loops.

**Rationale**: Each SavePort/RestorePort pair is 2 Toolbox traps. With 4 players + 4 bombs + 8 explosions = 16 sprites in a busy frame, the fallback path (Mac SE or no PICTs) executes 32 trap calls just for port management. On 8MHz 68000, Toolbox trap dispatch is ~100+ cycles per trap. Hoisting saves ~30 traps/frame. The color Mac CopyBits path (lines 692-700) does not call SavePort/RestorePort and is unaffected. The fallback PaintRect/PaintOval path requires the port to be set to the work buffer, which the bracket provides. Source: Sex, Lies and Video Games (1996) p.2374-2392.

**Alternatives considered**:
- Pass a `portAlreadySet` flag to each draw function: Requires changing 3 function signatures and adding conditional logic in each. More invasive, same benefit. Rejected.
- Remove port save/restore entirely and require callers to manage: Too fragile; individual draw functions must remain callable standalone for non-game screens. Rejected.
- Add the bracket but keep individual save/restore as fallback: Unnecessary complexity; Game_Draw() is the only hot-path caller. Rejected.

**Implementation detail**: Renderer_BeginSpriteDraw() calls SavePort()/SetPortWork() and sets a static flag `gSpriteDrawActive`. The existing per-function SavePort/RestorePort calls check this flag and skip when true. This preserves standalone callability for non-game-loop callers while eliminating redundancy in the hot path.

## R2: BeginFrame Color State Persistence

**Decision**: Set ForeColor(blackColor)/BackColor(whiteColor) on the work buffer port once after GWorld creation in Renderer_Init(), and once after each RebuildBackground() (which changes port state). Remove the per-frame 5-trap setup from Renderer_BeginFrame().

**Rationale**: CopyBits between offscreen buffers uses the color state of the destination port. The work GWorld's color state persists across frames because:
1. No code path between EndFrame and BeginFrame modifies the work port's color state
2. The sprite draw functions that change ForeColor (fallback path) operate on the work port, but they restore ForeColor(blackColor) before returning
3. RebuildBackground() operates on the background port, not the work port, but it's the one place that does SavePort/SetPortWork on the bg port which could leave color state dirty on the work port if interrupted

Verified by code inspection: the current per-frame setup (renderer.c:646-650) does SavePort/SetPortWork/ForeColor/BackColor/RestorePort. The SetPortWork is needed because ForeColor/BackColor apply to the current port. After this executes, CopyBits from bg to work uses the work port's color state. Since no code between frames changes it, setting once at init + after rebuild is sufficient.

Source: Sex, Lies and Video Games (1996) CopyBits benchmarks show ForeColor/BackColor mismatch causes up to 2.5x penalty; the state must be correct but need not be re-set every frame.

**Alternatives considered**:
- Keep per-frame setup but remove SavePort/RestorePort (just set on work port directly): Still 2 trap calls per frame (ForeColor + BackColor). Marginal improvement. Rejected.
- Set once at init and never again: Risky if any future code path changes work port color state. Adding post-rebuild setup is defensive. Selected.

## R3: Mask Region Creation from Sprite GWorlds

**Decision**: At PICT load time, create a 1-bit mask bitmap from each sprite GWorld by scanning for non-white pixels, then call BitMapToRegion() to convert to a RgnHandle. Store alongside the GWorld. Use CopyBits(..., srcCopy, maskRgn) instead of CopyBits(..., transparent, NULL).

**Rationale**: The `transparent` transfer mode checks every pixel against the background color (white) and skips matching pixels. This is per-pixel work similar to CopyMask. Sex, Lies and Video Games (1996) p.5988-6095 benchmarks show CopyBits with mask region (111 ticks) is 3.3x faster than CopyMask (369 ticks) for 256x256 blits. The `transparent` mode has comparable overhead to CopyMask since both do per-pixel conditional logic.

For BomberTalk's small sprites (16x16 or 32x32), the absolute difference is smaller but the ratio holds. With ~16 sprite blits per frame on color Macs, the cumulative savings are meaningful.

**Mask creation process** (from Sex, Lies p.6615-6700):
1. Allocate a 1-bit deep offscreen bitmap matching sprite dimensions
2. Lock the sprite GWorld PixMap
3. Scan each pixel: if not equal to white (background color), set corresponding bit in the 1-bit mask
4. Call `BitMapToRegion(rgnHandle, &maskBitMap)` -- requires the bitmap be 1-bit deep
5. Offset the region to (0,0) origin
6. Dispose the temporary 1-bit bitmap (region is independent)
7. At draw time, offset a copy of the region to the destination position, or use OffsetRgn on the stored region (offset before CopyBits, offset back after)

**Transparent color identification**: The sprite GWorlds are created with `EraseRect(&bounds)` after `NewGWorld`, which fills with the default background color (white). PICTs are then drawn over this white background. Therefore white (all bits set in each component) is the transparent color. This is consistent with how `transparent` mode already works (it uses the port's background color, which defaults to white).

**Alternatives considered**:
- CopyMask with a pre-built 1-bit mask bitmap: 3.3x slower than region mask per Sex Lies benchmarks. Rejected.
- Keep transparent mode: Known slow; the whole point of this optimization. Rejected.
- Use OpenRgn/CloseRgn with FrameRect: Only works for simple geometric shapes, not arbitrary sprite art. Rejected.

**Fallback**: If NewRgn() or BitMapToRegion() fails, store NULL for the mask region. Draw functions check for NULL and fall back to transparent mode. No crash, just slower.

## R4: Batched Tile Drawing in RebuildBackground

**Decision**: Replace the single-pass per-tile DrawTileRect() loop with a multi-pass approach: iterate the tilemap once per tile type, setting ForeColor/RGBForeColor once before each pass.

**Rationale**: Current RebuildBackground() (renderer.c:569-624) iterates row-by-row, col-by-col, calling DrawTileRect() for each tile. DrawTileRect() sets ForeColor per tile -- on Mac SE, floor tiles do ForeColor(whiteColor) + PaintRect + ForeColor(blackColor) = 3 traps per floor tile. On a 15x11 map with ~50% floors, that's ~250 ForeColor trap calls during a rebuild.

Batched approach: 4 passes (floor/spawn, wall, block, default), each sets ForeColor once. Total ForeColor calls: 4 (Mac SE) or 8 (color Mac, PaintRect + FrameRect for wall/block). This reduces ForeColor traps from O(tiles) to O(tile_types).

The tradeoff is iterating the tilemap 3-4 times instead of once. On a 15x11 = 165 tile map, each extra pass is 165 byte comparisons -- trivially fast even on 68k compared to the trap call savings.

Source: Sex, Lies and Video Games (1996) p.5645-5653 on colorization overhead.

**Alternatives considered**:
- Sort tiles by type into arrays, then draw each array: Requires temporary storage and sorting logic. Over-engineered for a 165-element grid. Rejected.
- Cache the last ForeColor and only call when it changes: Requires tracking state and comparing. Saves fewer calls than full batching since tiles are spatially mixed. Rejected.

## R5: TileMap Inline Macro Design

**Decision**: Add `TileMap_GetDataPtr()` accessor function that returns a `const unsigned char *` to the tile data, and a `TILEMAP_GET_FAST(data, cols, col, row)` macro that does direct array indexing: `((data)[(row) * (cols) + (col)])`. Callers cache the pointer and cols value before the loop.

**Rationale**: TileMap_GetTile() (tilemap.c:168-174) has function call overhead (~20 cycles on 68k for push/JSR/RTS) plus bounds checking (4 comparisons). In the bomb raycast, it's called twice per tile per direction (lines 123 and 129 of bomb.c). With range 5, that's up to 40 function calls per explosion. The bounds checking is redundant inside the raycast because the loop already checks bounds (bomb.c lines 120-121).

The macro accesses `tiles[row][col]` via linear indexing: `data[row * cols + col]`. This matches the memory layout of `unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS]` since C stores 2D arrays in row-major order, and `MAX_GRID_COLS` is the compile-time stride. However, using the actual cols value (from TileMap_GetCols()) is correct since the logical map may be smaller than MAX_GRID_COLS.

Wait -- the 2D array `tiles[MAX_GRID_ROWS][MAX_GRID_COLS]` has stride MAX_GRID_COLS (31), not the logical cols. So the macro must use MAX_GRID_COLS as stride: `((data)[(row) * MAX_GRID_COLS + (col)])`. This is simpler and avoids passing cols.

**Revised decision**: `TILEMAP_GET_FAST(col, row)` macro that accesses `TileMap_Get()->tiles[(row)][(col)]`. Since TileMap_Get() already exists and returns `TileMap *`, this is a direct struct field access through a pointer. The compiler can optimize the pointer dereference across loop iterations. For maximum performance in the bomb raycast, callers can cache `TileMap *map = TileMap_Get()` before the loop and use `map->tiles[row][col]` directly, or the macro.

**Final decision**: Provide the macro as `TILEMAP_TILE(map, col, row)` expanding to `((map)->tiles[(row)][(col)])` where `map` is a `TileMap *` obtained from `TileMap_Get()`. This avoids exposing internal data layout beyond what TileMap_Get() already exposes, is zero-overhead (direct memory access), and is idiomatic C89.

Source: Black Art of Macintosh Game Programming (1996) on function call overhead in hot loops.

**Alternatives considered**:
- Expose raw pointer + stride: Breaks encapsulation more than necessary. The TileMap struct is already public in game.h. Rejected.
- Make TileMap_GetTile an inline function: C89 has no `inline` keyword. Rejected.
- Use a function pointer: Same overhead as function call. Rejected.
