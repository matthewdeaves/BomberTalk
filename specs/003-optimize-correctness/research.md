# Research: Performance & Correctness Optimizations

**Feature**: 003-optimize-correctness
**Date**: 2026-04-06

## R1: CopyBits ForeColor/BackColor Performance Impact

**Decision**: Normalize ForeColor(blackColor) and BackColor(whiteColor) before ALL srcCopy CopyBits calls, on all platforms.

**Rationale**: "Sex, Lies and Video Games" (1996) p.5370-5790 benchmarks CopyBits with various transfer modes and color states. Key finding: with foreground != black or background != white, CopyBits performs colorization which costs 241 ticks vs 95 ticks baseline — a 2.5x slowdown. Currently, BomberTalk only normalizes colors on Mac SE paths (renderer.c lines 529-530, 573-574). Color Mac paths (GWorld CopyBits for bg-to-work, work-to-window) skip normalization entirely.

**Affected call sites** (all in renderer.c):
1. `DrawTileFromSheet()` — tile sheet to background buffer (called during RebuildBackground)
2. `Renderer_BeginFrame()` — background to work buffer (full-screen and per-dirty-tile paths)
3. `Renderer_BlitToWindow()` — work buffer to window (full-screen and per-dirty-tile paths)

Call sites using `transparent` transfer mode (DrawPlayer, DrawBomb, DrawExplosion) are lower risk since the book's benchmark specifically measures srcCopy. No change needed for transparent-mode calls.

**Alternatives considered**:
- Set colors once at init: Rejected. Other code paths (DrawFPS, screen draws) change ForeColor/BackColor during the frame, so state cannot be guaranteed between frames.
- Set colors only in BeginFrame: Rejected. BlitToWindow runs after EndFrame unlocks sprites, and DrawFPS may have changed colors between EndFrame and BlitToWindow.

## R2: Background Rebuild Batching Strategy

**Decision**: Add a static flag `gNeedRebuildBg` checked once per frame in `Renderer_BeginFrame()`.

**Rationale**: A single range-3 explosion can destroy up to 4 blocks. Each remote MSG_BLOCK_DESTROYED triggers `Renderer_RebuildBackground()` which redraws every tile in the background buffer. With 4 messages arriving in one `Net_Poll()` call, the background is rebuilt 4 times before the next frame draws. Chain explosions (multiple bombs) make this worse.

The deferred flag pattern: callers set `gNeedRebuildBg = TRUE` instead of calling rebuild directly. At the start of `Renderer_BeginFrame()`, if the flag is set, rebuild runs once and the flag clears. This guarantees at most one rebuild per frame regardless of how many blocks were destroyed.

**Direct calls preserved**: `Renderer_Init()` and `Game_Init()` call `Renderer_RebuildBackground()` directly since they run outside the frame loop and need immediate results for initialization.

**Alternatives considered**:
- Accumulate dirty tiles instead of full rebuild: Rejected. RebuildBackground already uses the dirty rect system. The issue is N full rebuilds, not per-tile overhead.
- Timer-based debounce: Rejected. Adds complexity. Frame-based batching is simpler and sufficient.

## R3: TileMap Cache and Reset Strategy

**Decision**: Add a static `TileMap gInitialMap` cache populated after first load. `TileMap_Reset()` copies from cache.

**Rationale**: `TileMap_Init()` is called twice: once in `main()` for window sizing, once in `Game_Init()` for each round start. The second call re-executes `GetResource('TMAP', 128)` and `ReleaseResource()` unnecessarily. On Mac SE, Resource Manager calls are relatively expensive trap calls.

The cache approach: after the first `TileMap_Init()` completes (loading from TMAP resource or static fallback, scanning spawns), copy `gMap` to `gInitialMap`. `TileMap_Reset()` copies `gInitialMap` back to `gMap`. Cost: one additional `TileMap` struct (~800 bytes) of static memory.

**Alternatives considered**:
- Only call TileMap_Init once and track block destruction separately: Rejected. Would require tracking all destroyed blocks and restoring them individually. The cache copy is simpler and more reliable.
- Re-scan spawns in Reset: Not needed. Spawn positions don't change between rounds.

## R4: Spatial Bomb Grid Design

**Decision**: Add `static unsigned char gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS]` for O(1) bomb position lookups.

**Rationale**: `Bomb_ExistsAt()` currently iterates all `MAX_BOMBS` (16) slots checking `active`, `gridCol`, and `gridRow`. Called once per frame when the local player attempts to move (gated by movement cooldown). At ~15fps on Mac SE, this is 15 scans/second of 16 slots = 240 comparisons/second. While acceptable, the spatial grid reduces this to 15 array lookups/second.

Grid lifecycle:
- `Bomb_Init()`: `memset(gBombGrid, 0, sizeof(gBombGrid))`
- `Bomb_PlaceAt()`: Set `gBombGrid[row][col] = 1` after placing. Also replaces the inline duplicate-check linear scan.
- `ExplodeBomb()`: Clear `gBombGrid[b->gridRow][b->gridCol] = 0` when deactivating the bomb.
- `Bomb_ExistsAt()`: `return gBombGrid[row][col]`

Memory cost: `MAX_GRID_ROWS * MAX_GRID_COLS` = 25 * 31 = 775 bytes. Well within Mac SE budget.

**Alternatives considered**:
- Hash map: Overkill for 16 bombs on a 775-cell grid. Array is simpler and faster.
- Bitfield: Would save memory (97 bytes vs 775) but adds bit manipulation complexity. Not worth it given the budget.

## R5: Peer Pointer Cleanup

**Decision**: Add `gGame.players[i].peer = NULL` in the `on_disconnected()` callback after marking the player inactive.

**Rationale**: When a peer disconnects, `on_disconnected()` finds the matching player by comparing peer pointers and marks them inactive. The peer pointer itself is left pointing to the now-invalid PeerTalk peer struct. While the pointer is never dereferenced (only compared), clearing it to NULL is defensive programming that prevents future bugs if the code evolves to dereference peer pointers.

**Alternatives considered**:
- Leave as-is: Technically safe today but creates a maintenance hazard.
