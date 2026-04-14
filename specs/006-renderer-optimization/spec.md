# Feature Specification: Renderer Performance Optimizations

**Feature Branch**: `006-renderer-optimization`  
**Created**: 2026-04-14  
**Status**: Draft  
**Input**: Five renderer performance optimizations for Classic Mac framerate improvement, derived from cross-referencing the 6 game programming books in `books/` against the BomberTalk codebase.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Smoother Gameplay on Mac SE (Priority: P1)

A player on the Mac SE (8MHz 68000) experiences improved framerate during gameplay with multiple bombs, explosions, and players on screen. The game feels more responsive because fewer Toolbox trap calls are wasted on redundant port save/restore operations and color state setup each frame.

**Why this priority**: The Mac SE is the performance floor (Constitution V). Every trap call saved at 8MHz directly translates to higher FPS. The current ~10-19fps gameplay can improve measurably by eliminating ~35 redundant trap calls per frame (optimizations 1 and 2).

**Independent Test**: Build 68k target and run a 4-player game with active bombs and explosions. Measure FPS before and after. The frame time budget freed by removing ~35 trap calls should be observable as higher FPS or more headroom per frame.

**Acceptance Scenarios**:

1. **Given** a 4-player game on Mac SE with 4 active bombs and 8 explosion tiles, **When** Game_Draw() renders a frame, **Then** SavePort/RestorePort is called exactly once (not once per sprite).
2. **Given** normal gameplay on Mac SE, **When** Renderer_BeginFrame() executes, **Then** no ForeColor/BackColor/SavePort/RestorePort trap calls occur (color state set once at init).
3. **Given** any gameplay scenario on all three targets, **When** the optimizations are active, **Then** rendering output is pixel-identical to the pre-optimization build.

---

### User Story 2 - Faster Sprite Blitting on Color Macs (Priority: P2)

A player on a PPC Mac (Performa 6200 or 6400) experiences faster sprite rendering because player, bomb, and explosion sprites use srcCopy with pre-computed mask regions instead of the slow `transparent` transfer mode. This yields 2-3x faster per-sprite CopyBits calls.

**Why this priority**: Color Macs are already at ~24fps, but headroom matters for future features and for the 6200 which is slower. The `transparent` CopyBits mode is documented as significantly slower than srcCopy with mask region (Sex, Lies and Video Games, 1996). This optimization only applies to color Macs with loaded PICTs, so it has no Mac SE risk.

**Independent Test**: Build PPC OT target. Run a 4-player game. Compare frame times with transparent mode vs srcCopy+maskRgn. The per-sprite blit should be measurably faster.

**Acceptance Scenarios**:

1. **Given** a color Mac with loaded PICT sprites, **When** Renderer_DrawPlayer() blits a sprite, **Then** CopyBits uses srcCopy with a pre-computed mask region (not transparent mode).
2. **Given** a color Mac with loaded PICT sprites, **When** Renderer_DrawBomb() or Renderer_DrawExplosion() blits, **Then** CopyBits uses srcCopy with a pre-computed mask region.
3. **Given** sprite PICTs fail to load (fallback path), **When** drawing sprites, **Then** the fallback PaintRect/PaintOval path is unchanged and still works correctly.
4. **Given** a Mac SE (no color, no PICTs), **When** drawing sprites, **Then** the monochrome fallback path is completely unaffected.

---

### User Story 3 - Reduced Hitch During Background Rebuild (Priority: P3)

When multiple blocks are destroyed by an explosion chain, the deferred background rebuild executes with fewer ForeColor trap calls because tiles are drawn in batches by type rather than individually with per-tile color switching. This reduces the visible hitch on Mac SE after large explosions.

**Why this priority**: Background rebuilds are infrequent (only on block destruction) but cause the most noticeable frame hitches, especially on Mac SE. Batching by tile type reduces ForeColor calls from O(tiles) to O(tile_types) during rebuilds.

**Independent Test**: Build 68k target. Place a bomb that destroys 3+ blocks. The rebuild should complete faster (fewer ForeColor trap calls). Visual output must be identical.

**Acceptance Scenarios**:

1. **Given** a background rebuild triggered by block destruction, **When** RebuildBackground() draws all tiles, **Then** ForeColor/RGBForeColor is called once per tile type (not once per tile).
2. **Given** a 15x11 map with mixed tile types, **When** RebuildBackground() completes, **Then** the rendered background is pixel-identical to the current per-tile approach.
3. **Given** a Mac SE rebuild, **When** tiles are batched, **Then** ForeColor is called at most 4 times total (floor, wall, block, default) instead of up to 165 times.

---

### User Story 4 - Faster Bomb Raycast and Player Collision (Priority: P4)

Bomb explosion raycasting and player collision checks execute faster because tilemap lookups in validated-bounds loops use a direct macro instead of a function call with redundant bounds checking. This saves ~20 cycles per lookup on 68k.

**Why this priority**: Bomb raycasts (4 directions x range per bomb) and per-frame collision checks are hot paths. The function call overhead is small per call but accumulates with multiple bombs and players. Low risk, small effort.

**Independent Test**: Build 68k target. Gameplay with multiple simultaneous explosions at max range. Verify identical behavior (same tiles destroyed, same kills) with the macro path.

**Acceptance Scenarios**:

1. **Given** a bomb explosion with range 5, **When** the raycast iterates 4 directions, **Then** tilemap lookups within the validated loop bounds use direct array access (no function call overhead).
2. **Given** any tilemap lookup from outside a validated loop, **When** TileMap_GetTile() is called, **Then** it still performs bounds checking (safe function preserved).
3. **Given** any gameplay scenario, **When** using the inline macro, **Then** game behavior is identical to the function-based path.

---

### Edge Cases

- What happens when all sprites are in fallback mode (no PICTs loaded) on a color Mac? The batched port save/restore must still work correctly for the PaintRect/PaintOval fallback path.
- What happens when the tilemap has only one tile type (all floors)? Batched rebuild must handle single-type maps without skipping or double-drawing.
- What happens when mask region creation fails (out of memory)? Must fall back to transparent mode gracefully.
- What happens when the tilemap inline macro is called with boundary coordinates by a future caller? The safe function must remain the public API; the macro is internal-only.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Renderer MUST call SavePort()/SetPortWork() at most once before all sprite drawing in a frame, and RestorePort() at most once after, instead of per-sprite.
- **FR-002**: Renderer MUST set ForeColor(blackColor)/BackColor(whiteColor) on the work buffer port once during initialization and after each RebuildBackground(), not every frame in BeginFrame(). Implementation must verify that no code path between EndFrame and the next BeginFrame modifies the work port's color state.
- **FR-003**: Renderer MUST create pre-computed mask Regions for each sprite GWorld at PICT load time on color Macs.
- **FR-004**: Renderer MUST use CopyBits with srcCopy transfer mode and mask Region (not transparent mode) for all sprite blits on color Macs when PICTs are loaded.
- **FR-005**: Renderer MUST fall back to transparent mode if mask Region creation fails (e.g., NewRgn() returns NULL).
- **FR-006**: RebuildBackground() MUST batch tile drawing by tile type, setting ForeColor/RGBForeColor once per type instead of once per tile.
- **FR-007**: Tilemap module MUST provide an inline macro for direct array access (no bounds checking) for use in performance-critical loops with pre-validated bounds.
- **FR-008**: The safe TileMap_GetTile() function with bounds checking MUST be preserved as the public API for all external and boundary callers.
- **FR-009**: All optimizations MUST produce pixel-identical rendering output compared to the pre-optimization build on all three targets.
- **FR-010**: All code MUST be C89/C90 compliant (no // comments, no mixed declarations, no VLAs).
- **FR-011**: All optimizations MUST build cleanly on all three targets: 68k MacTCP, PPC OT, PPC MacTCP.
- **FR-012**: Mac SE monochrome rendering path MUST remain fully functional and unaffected by color-Mac-only optimizations (mask regions).

### Key Entities

- **Sprite Mask Region**: A QuickDraw Region handle (RgnHandle) computed at sprite load time by scanning the sprite GWorld for transparent pixels. One per sprite GWorld (4 players + 1 bomb + 1 explosion + 1 title = 7 total). Disposed at Renderer_Shutdown().
- **Tile Type Batch**: A logical grouping of tiles by type (TILE_FLOOR/TILE_SPAWN, TILE_WALL, TILE_BLOCK) for batched drawing during RebuildBackground(). Not a persistent data structure; just a loop reorganization.
- **TileMap Fast Macro**: A preprocessor macro providing direct array access to the tilemap data without function call overhead or bounds checking.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Mac SE gameplay frame renders execute fewer than 10 Toolbox port/color trap calls per frame (down from ~40+), measurable by code inspection of the hot path.
- **SC-002**: Color Mac sprite blits use srcCopy transfer mode (not transparent), verifiable by code inspection and visual confirmation that sprites render correctly with no artifacts.
- **SC-003**: RebuildBackground() executes at most 8 ForeColor/RGBForeColor calls for a standard 15x11 map (4 types x 2 calls max each), down from up to 165, verifiable by code inspection.
- **SC-004**: All three build targets (68k MacTCP, PPC OT, PPC MacTCP) compile without warnings or errors after all optimizations.
- **SC-005**: Gameplay on all three target Macs produces visually identical output to the pre-optimization build (no rendering artifacts, no missing sprites, no color changes).
- **SC-006**: No new memory allocations occur during gameplay frames; all mask regions are allocated at init time and disposed at shutdown.

## Assumptions

- The work GWorld's ForeColor/BackColor state persists across frames because no code path between EndFrame and the next BeginFrame modifies the work port's color state. This will be verified during implementation.
- The `transparent` CopyBits transfer mode on Classic Mac OS behaves like a per-pixel color-key check (skipping white/background-colored pixels), which is slower than srcCopy with a pre-computed mask region as documented in Sex, Lies and Video Games (1996).
- Sprite GWorlds use a consistent background/transparent color (white) that can be reliably detected during mask region creation.
- The tilemap data array is accessible from the header via an extern declaration or accessor macro without breaking encapsulation in a way that causes maintenance issues.
- Batching tile drawing by type (multiple passes over the tilemap) is net faster than single-pass per-tile color switching because ForeColor trap calls are more expensive than simple loop iteration on 68k.

## Research

- **Sex, Lies and Video Games (1996)**: p.2374-2392 (port switching overhead), p.5645-5653 (colorization overhead), p.5662-5735 (transfer mode benchmarks: srcCopy=95 ticks, srcXor=217, transparent similarly slow), p.5988-6095 (CopyBits with mask region 3x faster than CopyMask; transparent mode is comparable to CopyMask overhead).
- **Black Art of Macintosh Game Programming (1996)**: Function call overhead on 68k (~20 cycles per call for push/JSR/RTS), recommends macros for hot-path inner loops.
- **Tricks of the Mac Game Programming Gurus (1995)**: p.10332-10539 (dirty rectangle system design), p.10145-10327 (encoded sprites -- not pursued this iteration but noted as future optimization path).
- **Macintosh Game Programming Techniques (1996)**: Ch.7 LockPixels batching, which BomberTalk already implements.
