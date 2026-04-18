# Data Model: Hot-Path Performance & Memory Optimizations (008)

**Phase**: 1 (Design & Contracts)
**Date**: 2026-04-18

## Summary

**No data-model changes.** This feature is a local-optimization and cleanup pass. No new structs, no field additions, no field removals, no invariant changes.

## Affected shared state (read-only enumeration for context)

The change set touches code that reads or writes these existing data structures, but does not redefine any of them:

| Structure | Location | Effect |
|---|---|---|
| `GameState gGame` | `include/game.h` | No change. FR-007 reads `gGame.heapCheckTimer` unchanged; FR-001/002/003 operate on `gGame.players[]`, `gGame.bombs[]` unchanged. |
| `TileMap gMap` (static) | `src/tilemap.c` | No change. FR-005 removes an accessor function `TileMap_IsSolid` but does not modify the struct. |
| `unsigned char gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS]` | `src/bomb.c` (private) | **Visibility change**: promoted from file-static to a module-public `extern` declaration in `bomb.h`, to enable the `BOMB_GRID_CELL(col,row)` macro in player.c. Storage shape and semantics unchanged. |
| `short gDirtyListCol[]`, `short gDirtyListRow[]` | `src/renderer.c` (static) | **Optional FR-008 only**: may become a single `short gDirtyList[]` with packed `(col << 8) | row`. Storage halves from 2 × 775 × 2 = 3100 B to 775 × 2 = 1550 B. Semantics unchanged. |
| `GWorldPtr gTitleSprite` | `src/renderer.c` (static) | **Optional FR-009 only**: lifecycle extends to "disposed on first exit from SCREEN_MENU". NULL-guarded by existing draw paths. |
| `MsgPosition` | `include/game.h` (network wire format) | No change. FR-N2 forbids any protocol change. |

## Removed data-model elements

- **`int TileMap_IsSolid(short col, short row)`** — function removed from `include/tilemap.h` (line 16) and `src/tilemap.c` (lines 183-191). No callers in current codebase (cppcheck-confirmed). This is not a *data* change strictly, but captured here for completeness since the header is a contract surface.

## Added data-model elements

- **`#define BOMB_GRID_CELL(col, row) (gBombGrid[(row)][(col)])`** — new macro in `include/bomb.h`. Parallels `TILEMAP_TILE(map, col, row)` in `include/tilemap.h:29`. Consumes the "at most one inline helper" budget allowed by spec FR-N3.
- **`extern unsigned char gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS];`** — declaration added to `include/bomb.h`. Definition stays in `src/bomb.c`. Array dimensions and initialization unchanged.

## Invariants preserved

1. **Bomb grid mirror invariant**: `gBombGrid[r][c] == 1` iff there is an active bomb whose `gridCol == c && gridRow == r`. Unchanged — FR-001 only reads, never writes, via the new macro.
2. **Map bounds invariant**: `0 <= col < TileMap_GetCols() && 0 <= row < TileMap_GetRows()` holds for every `gBombGrid[r][c]` and `TILEMAP_TILE` access in hot paths. FR-001 relies on this being established by caller clamping (player.c:201-205); the invariant is unchanged by this feature.
3. **Rectangle half-open convention**: all inlined rect initializations under FR-002 use `right = left + size; bottom = top + size` to match QuickDraw's exclusive right/bottom convention. Unchanged.
4. **Colour state on exit of `Renderer_BeginSpriteDraw` bracket**: ForeColor=black, BackColor=white restored by `Renderer_EndSpriteDraw`'s port restore. FR-003 relies on this but does not alter it.

## State transitions

None. All FRs are synchronous, straight-line edits inside existing control flow. No new state machines, no new lifecycle hooks. FR-009 (optional) adds exactly one new transition effect: on first entry to `Lobby_Init`, dispose the title GWorld and NULL its pointer. No state variable is introduced for this (a simple once-per-session check via `if (gTitleSprite != NULL)` suffices).
