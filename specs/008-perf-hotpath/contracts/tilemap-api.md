# Contract: `include/tilemap.h` API change

**Phase**: 1 (Design & Contracts)
**Date**: 2026-04-18
**Change type**: Removal (no replacement function — macro pattern already in place).

## Change

Remove the declaration:

```c
int TileMap_IsSolid(short col, short row);
```

from `include/tilemap.h` (currently at line 16). Correspondingly remove the definition from `src/tilemap.c` (currently at lines 183-191).

## Rationale

- `cppcheck 2.21 --enable=unusedFunction --std=c89 -I include src/` reports `TileMap_IsSolid` as the only unused function in the codebase (2026-04-18 run).
- No caller exists in `src/`: `grep -rn 'TileMap_IsSolid' src/` returns zero hits. Historical callers (pre-002 solid-check code path) migrated to the `TILEMAP_TILE(map, col, row)` macro + explicit `== TILE_WALL || == TILE_BLOCK` tests during the 002-perf-extensibility change.
- Public API surface is constrained by Constitution Principle V (Mac SE Is the Floor — every byte of text segment counts on a 4 MB machine) and spec FR-N3 (no new public header APIs beyond at most one inline helper). Shrinking the API aligns with both.

## Caller-migration guide

Any future code that needs a "solid tile" test should use:

```c
unsigned char t = TILEMAP_TILE(map, col, row);
if (t == TILE_WALL || t == TILE_BLOCK) { /* solid */ }
```

The project's collision hot path (`CheckTileSolid` in `src/player.c`) already follows this pattern and additionally handles bomb occupancy via `gBombGrid` — new solid-tile callers should follow the same shape, NOT reintroduce `TileMap_IsSolid`.

## Verification

After the change:

1. `grep -rn 'TileMap_IsSolid' include/ src/` returns zero hits.
2. `cppcheck --std=c89 --enable=unusedFunction -I include src/` reports no unused functions.
3. All three targets build clean.
4. `specs/*/tasks.md` references (historical log, not source) remain as-is — they describe past work and do not affect build correctness.

## Related public-header additions under this feature

The only *addition* to any public header under 008-perf-hotpath is the FR-001 macro helper:

```c
/* include/bomb.h */
extern unsigned char gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS];
#define BOMB_GRID_CELL(col, row) (gBombGrid[(row)][(col)])
```

This consumes the FR-N3 "at most one inline helper" budget. No other public header gains a symbol in this feature.
