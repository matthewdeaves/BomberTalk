# Contract: TileMap API Changes

**Feature**: 003-optimize-correctness
**Date**: 2026-04-06

## New Function: TileMap_Reset

**Purpose**: Restore the tilemap to its initial state (all blocks restored) without re-loading resources.

**Declaration** (tilemap.h):
```c
void TileMap_Reset(void);
```

**Behavior**:
- Copies the cached initial map state back to `gMap`
- Does NOT call `GetResource()` or any Resource Manager functions
- Does NOT re-scan spawns (spawn positions don't change between rounds)
- Safe to call multiple times
- If called before `TileMap_Init()` has run, behavior is undefined (caller's responsibility)

**Caller**:
- `screen_game.c` `Game_Init()` — replaces `TileMap_Init()` call for round restarts

## Modified Behavior: TileMap_Init

**Change**: After loading the tilemap (from TMAP resource or static fallback) and scanning spawns, copy the fully-initialized `gMap` to a static `gInitialMap` cache.

**Ordering**: Cache copy MUST happen after `TileMap_ScanSpawns()` completes, so spawn positions are included in the cache.

**Existing callers unchanged**: `main.c` `main()` continues to call `TileMap_Init()` once at startup.
