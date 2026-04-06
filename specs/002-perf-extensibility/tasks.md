# Tasks: Performance & Extensibility Upgrade

**Input**: Design documents from `/specs/002-perf-extensibility/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/network-protocol.md

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)

---

## Phase 1: Setup

**Purpose**: Build system and data structure changes that all stories depend on

- [X] T001 Add `-std=c89` to CMAKE_C_FLAGS in CMakeLists.txt (change line 37: append `-std=c89` to the existing `-Wall -Wextra` flags)
- [X] T002 Add `BT_PROTOCOL_VERSION`, `MAX_GRID_COLS`, `MAX_GRID_ROWS`, and `PlayerStats` struct to include/game.h — define `#define BT_PROTOCOL_VERSION 2`, `#define MAX_GRID_COLS 31`, `#define MAX_GRID_ROWS 25`, and the `PlayerStats` typedef with fields `bombsMax`, `bombRange`, `speedTicks` (all `short`)
- [X] T003 Modify the `Player` struct in include/game.h — add `PlayerStats stats;` field, remove standalone `bombRange` field (keep `bombsAvailable` as runtime counter)
- [X] T004 Modify the `TileMap` struct in include/game.h — add `short cols`, `short rows`, `short spawnCols[MAX_PLAYERS]`, `short spawnRows[MAX_PLAYERS]`, `short spawnCount` fields; resize `tiles` array to `unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS]`
- [X] T005 Modify `MsgGameStart` struct in include/game.h — rename `pad` field to `version`

**Checkpoint**: Data structures updated. All three targets must compile clean (verify with a build).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure changes that multiple user stories depend on

- [X] T006 [P] Update include/tilemap.h — add declarations for `TileMap_LoadFromResource(void)`, `TileMap_ScanSpawns(void)`, `TileMap_GetCols(void)`, `TileMap_GetRows(void)`, `TileMap_GetSpawnCol(short index)`, `TileMap_GetSpawnRow(short index)`, `TileMap_GetSpawnCount(void)`
- [X] T007 [P] Update include/renderer.h — add declarations for `Renderer_MarkDirty(short col, short row)`, `Renderer_MarkAllDirty(void)`, `Renderer_ClearDirty(void)`
- [X] T008 [P] Update include/net.h — add declaration for `int Net_HasVersionMismatch(void);`
- [X] T009 Implement TileMap_LoadFromResource in src/tilemap.c — use `GetResource('TMAP', 128)`, parse 2-byte cols + 2-byte rows + tile data. Validate resource size matches expected (4 + cols*rows bytes) — if mismatch, fall back to default. Validate dimensions (clamp to 7-31 cols, 7-25 rows), sanitize unknown tile values (>TILE_SPAWN) to TILE_FLOOR, fall back to static level1.h data if resource not found or invalid. Update TileMap_Init to call this and store cols/rows in the TileMap struct. Update tilemap.c internal functions to use stored cols/rows instead of compile-time GRID_COLS/GRID_ROWS.
- [X] T010 Implement TileMap_ScanSpawns in src/tilemap.c — scan tiles top-left to bottom-right (row-major) for TILE_SPAWN, store up to MAX_PLAYERS spawn positions in spawnCols/spawnRows arrays, set spawnCount. Handle partial spawns: if map has 1-3 TILE_SPAWN markers but fewer than MAX_PLAYERS, fill remaining slots with default corners (1,1), (13,1), (1,11), (13,11) skipping any defaults that duplicate an already-found spawn. If zero spawns found, use all four default corners. Call from TileMap_Init after loading.
- [X] T011 Initialize PlayerStats defaults in src/player.c — in Player_Init, set `p->stats.bombsMax = 1`, `p->stats.bombRange = 1`, `p->stats.speedTicks = 12`. Set `p->bombsAvailable = p->stats.bombsMax`.

**Checkpoint**: Foundation ready — tilemap loads from resource with fallback, player stats initialized, headers updated. All three targets compile clean.

---

## Phase 3: User Story 1 — Smoother Gameplay on Mac SE (Priority: P1)

**Goal**: Implement dirty rectangle tracking, LockPixels hoisting, static colors, PixMap caching, and 32-bit alignment to dramatically reduce per-frame rendering work on Mac SE.

**Independent Test**: Build 68k target, run 4-player game on Mac SE. Measure frame rate via TickCount() delta. Verify no visual artifacts. Confirm frames with minimal movement only update changed tiles.

### Implementation for User Story 1

- [X] T012 [US1] Add static const RGBColor constants at file scope in src/renderer.c — move player colors array out of Renderer_DrawPlayer into `static const RGBColor kPlayerWhite`, `kPlayerRed`, `kPlayerBlue`, `kPlayerYellow` and a lookup array `static const RGBColor *kPlayerColors[4]`. Add `static const RGBColor kExplosionOrange = {0xFFFF, 0x6600, 0x0000}`. Update Renderer_DrawPlayer (line ~563) to use `kPlayerColors[playerID & 3]` instead of stack array. Update Renderer_DrawExplosion (line ~633) to use `kExplosionOrange`.
- [X] T013 [US1] Add cached PixMap pointer statics and sprite lock/unlock helpers in src/renderer.c — add `static BitMap *gCachedPlayerPM[MAX_PLAYERS]`, `gCachedBombPM`, `gCachedExplosionPM` at file scope. Create static helper `LockAllSprites(void)` that locks each non-NULL sprite GWorld and caches the dereferenced PixMap pointer (skip on isMacSE). Create `UnlockAllSprites(void)` that unlocks each and NULLs the cached pointers.
- [X] T014 [US1] Integrate sprite lock batching into frame lifecycle in src/renderer.c — call `LockAllSprites()` at the end of `Renderer_BeginFrame()` (after bg→work copy). Call `UnlockAllSprites()` at the start of `Renderer_EndFrame()` (before work→window blit). Update `Renderer_DrawPlayer` to use `gCachedPlayerPM[playerID]` instead of calling `GetGWorldPixMap(gPlayerSprites[playerID])` and `LockPixels`/`UnlockPixels`. Apply same change to `Renderer_DrawBomb` (use `gCachedBombPM`) and `Renderer_DrawExplosion` (use `gCachedExplosionPM`). Keep NULL checks — if cached pointer is NULL, fall through to colored rectangle fallback.
- [X] T015 [US1] Implement dirty rectangle grid in src/renderer.c — add `static unsigned char gDirtyGrid[MAX_GRID_ROWS][MAX_GRID_COLS]`, `static short gDirtyCount`, `static short gDirtyTotal` (set to cols*rows on init). Implement `Renderer_MarkDirty(col, row)`: bounds-check, skip if already dirty, set grid cell to 1, increment gDirtyCount. Implement `Renderer_MarkAllDirty()`: memset grid to 1, set gDirtyCount = gDirtyTotal. Implement `Renderer_ClearDirty()`: memset grid to 0, set gDirtyCount = 0. Call `Renderer_MarkAllDirty()` from `Renderer_RebuildBackground()` at the end. Initialize gDirtyTotal when renderer is initialized (after tilemap cols/rows are known).
- [X] T016 [P] [US1] Add 32-bit alignment helper in src/renderer.c — create static helper `AlignRect32(Rect *r)` that aligns left edge down and right edge up to 32-bit pixel boundaries. For Mac SE (1-bit): `r->left &= ~31; r->right = (r->right + 31) & ~31;`. For color (8-bit): `r->left &= ~3; r->right = (r->right + 3) & ~3;`. Clamp right edge to playWidth.
- [X] T017 [US1] Integrate dirty rect into BeginFrame bg→work copy in src/renderer.c — in `Renderer_BeginFrame()`, replace the full-screen `CopyBits(bg, work)` with: if `gDirtyCount >= gDirtyTotal` or `gDirtyCount > gDirtyTotal / 2`, do full-screen CopyBits (existing behavior). Otherwise, iterate dirty grid: for each dirty cell, compute tile rect, call `AlignRect32`, CopyBits that rect from bg to work.
- [X] T018 [US1] Integrate dirty rect into EndFrame work→window blit in src/renderer.c — in `Renderer_BlitToWindow()`, apply same dirty-or-full logic: if all dirty or >50% dirty, full-screen CopyBits to window. Otherwise iterate dirty grid and CopyBits each dirty tile rect (aligned). Call `Renderer_ClearDirty()` after blit.
- [X] T019 [US1] Mark tiles dirty on sprite movement in src/screen_game.c — before Player_Update, record each active player's current gridCol/gridRow. After Player_Update, if position changed, call `Renderer_MarkDirty(oldCol, oldRow)` and `Renderer_MarkDirty(newCol, newRow)` for that player. Also mark dirty for all active player positions each frame (they are drawn into work buffer).
- [X] T020 [US1] Mark tiles dirty on bomb/explosion events in src/bomb.c — in `Bomb_PlaceAt()`, call `Renderer_MarkDirty(col, row)`. In `ExplodeBomb()`, call `Renderer_MarkDirty` for the bomb center and each explosion tile. In explosion timer cleanup (when explosions expire), call `Renderer_MarkDirty` for each removed explosion tile.
- [X] T021 [US1] Mark tiles dirty for network-received events in src/net.c — in `on_position` callback, call `Renderer_MarkDirty` for the player's old and new positions. In `on_bomb_placed` callback, mark the bomb tile dirty. In `on_bomb_explode` and `on_block_destroyed` callbacks, mark affected tiles dirty.

**Checkpoint**: Dirty rect system fully integrated. Game renders identically but copies fewer pixels on low-change frames. All three targets compile and run correctly.

---

## Phase 4: User Story 2 — Stable Cross-Version Multiplayer (Priority: P2)

**Goal**: Add protocol versioning to MSG_GAME_START, bounds-check MSG_GAME_OVER winnerID, display version mismatch in lobby.

**Independent Test**: Build two copies with different BT_PROTOCOL_VERSION values. Verify game start is rejected on mismatch. Verify invalid winnerID doesn't crash.

### Implementation for User Story 2

- [X] T022 [P] [US2] Add version to Net_SendGameStart in src/net.c — in the function that broadcasts MSG_GAME_START, set `msg.version = BT_PROTOCOL_VERSION` (instead of the old `msg.pad = 0`). Add `static int gVersionMismatch = FALSE;` at file scope. Implement `Net_HasVersionMismatch(void)` returning gVersionMismatch. Reset gVersionMismatch to FALSE when entering the lobby screen.
- [X] T023 [P] [US2] Add version check to on_game_start callback in src/net.c — after size validation, check `msg->version != BT_PROTOCOL_VERSION`. If mismatch: log `CLOG_WARN("Version mismatch: got %d, expected %d", msg->version, BT_PROTOCOL_VERSION)`, set `gVersionMismatch = TRUE`, return without setting `gGame.gameStartReceived`. If match: existing behavior.
- [X] T024 [P] [US2] Add winnerID bounds check to on_game_over callback in src/net.c — before any use of `msg->winnerID` as an array index, add guard: `if (msg->winnerID < MAX_PLAYERS)`. Log the winnerID value regardless. If winnerID >= MAX_PLAYERS (including 0xFF), treat as draw/no winner.
- [X] T025 [US2] Display version mismatch indicator in src/screen_lobby.c — in Lobby_Draw, check `Net_HasVersionMismatch()`. If true, draw a warning string (e.g., "Version mismatch!") in the lobby screen below the peer list. Use a pre-built Pascal string and cached StringWidth like other lobby text.

**Checkpoint**: Protocol versioning active. Mismatched clients cannot start a game. Invalid winner IDs are safely handled.

---

## Phase 5: User Story 3 — Loading Custom Maps (Priority: P3)

**Goal**: Wire up resource-based map loading into the game flow, externalize spawn points, and adapt renderer to dynamic grid dimensions.

**Independent Test**: Add a 'TMAP' resource with non-standard dimensions to the resource fork. Verify the game loads it, spawns players at TILE_SPAWN positions, and renders correctly. Remove the resource and verify default level fallback.

### Implementation for User Story 3

- [X] T026 [US3] Update Game_Init in src/screen_game.c to use map-derived spawns — replace hardcoded `kSpawnCols`/`kSpawnRows` arrays with calls to `TileMap_GetSpawnCol(i)` and `TileMap_GetSpawnRow(i)` for each player. Use `TileMap_GetSpawnCount()` to determine available spawn positions; fall back to defaults for excess players (already handled by TileMap_ScanSpawns).
- [X] T027 [US3] Update renderer to use dynamic grid dimensions — in src/renderer.c, replace all uses of compile-time `GRID_COLS`/`GRID_ROWS` with `TileMap_GetCols()`/`TileMap_GetRows()`. This affects: `Renderer_RebuildBackground` loop bounds, `Renderer_MarkAllDirty` (set gDirtyTotal), dirty grid iteration in BeginFrame/EndFrame, and any bounds checks. Update `gGame.playWidth`/`playHeight` calculation in main.c or renderer init to use `TileMap_GetCols() * gGame.tileSize` / `TileMap_GetRows() * gGame.tileSize`.
- [X] T028 [US3] Update gameplay collision checks for dynamic grid — in src/tilemap.c, update `TileMap_IsSolid`, `TileMap_GetTile`, `TileMap_SetTile` to use the stored `cols`/`rows` from the TileMap struct for bounds checking (instead of compile-time GRID_COLS/GRID_ROWS). Out-of-bounds returns TILE_WALL (existing behavior).
- [X] T029 [US3] Update bomb explosion for dynamic grid — in src/bomb.c, replace any use of GRID_COLS/GRID_ROWS in explosion range clamping with `TileMap_GetCols()`/`TileMap_GetRows()`.
- [X] T030 [US3] Update GWorld buffer allocation for dynamic dimensions — in src/renderer.c, `Renderer_Init` must compute offscreen buffer size from `TileMap_GetCols() * tileSize` and `TileMap_GetRows() * tileSize` instead of the fixed PLAY_WIDTH/PLAY_HEIGHT constants. Ensure buffers are allocated AFTER tilemap is loaded.

**Checkpoint**: Custom maps load from resource fork. Default level works as before. Spawn points come from map data. Renderer adapts to map dimensions.

---

## Phase 6: User Story 4 — Player Stats Groundwork (Priority: P4)

**Goal**: Wire gameplay logic to read from PlayerStats struct instead of standalone fields/constants.

**Independent Test**: Play a game. Verify bomb range, bomb count, and movement cooldown are identical to v1.0-alpha behavior.

### Implementation for User Story 4

- [X] T031 [P] [US4] Update movement cooldown in src/player.c — replace `MOVE_COOLDOWN_TICKS` constant usage with `gGame.players[playerID].stats.speedTicks` in Player_Update. Remove or keep MOVE_COOLDOWN_TICKS as the default initializer value.
- [X] T032 [P] [US4] Update bomb range reading in src/bomb.c — in Bomb_PlaceAt, use the player's `stats.bombRange` when setting the bomb's range field. The standalone `bombRange` field was removed from Player in T003, so any remaining `->bombRange` references will be compilation errors — fix them all to `->stats.bombRange`.
- [X] T033 [P] [US4] Update bomb availability in src/screen_game.c — where `local->bombsAvailable` is checked/decremented for bomb placement, ensure it resets to `local->stats.bombsMax` on round start (in Game_Init). Verify the decrement-on-place / increment-on-explode cycle still works with stats.bombsMax as the cap.
- [X] T034 [US4] Update any remaining references to removed bombRange field — search all .c and .h files for `->bombRange` or `.bombRange` that is NOT `stats.bombRange` and update. Ensure net.c on_bomb_placed callback reads range from the message, not from a player field.

**Checkpoint**: All gameplay logic reads from PlayerStats. Behavior is identical to v1.0-alpha. No standalone bombRange field remains.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final validation and cleanup across all stories

- [X] T035 [P] Verify all three build targets compile clean with zero warnings — run cmake + make for build-68k, build-ppc-ot, build-ppc-mactcp. Fix any -std=c89 -Wall -Wextra warnings.
- [X] T036 [P] Verify GRID_COLS/GRID_ROWS constants are no longer used in gameplay logic — grep the codebase for GRID_COLS and GRID_ROWS. They should only appear in game.h as defaults and in TileMap_Init for fallback initialization — not in renderer, bomb, player, or screen code.
- [X] T037 Review dirty rect marking completeness — trace all code paths that modify visible game state (player move, bomb place, bomb explode, block destroy, player death flash, network position update) and verify each calls Renderer_MarkDirty for affected tiles.
- [X] T038 Verify memory budget on Mac SE — add a FreeMem() log call after all initialization (in main.c after Game_Init or renderer init). Confirm remaining heap is >0 and total usage is under 1 MB. Remove or guard behind showFPS flag after validation.
- [X] T039 Update CLAUDE.md — add dirty rect documentation to Architecture section, document protocol version in Network Layer section, add TMAP resource to Asset Pipeline section, document PlayerStats in Global State section.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 (struct changes must be in place)
- **Phase 3 (US1 - Renderer)**: Depends on Phase 2 (needs dirty rect API in headers, tilemap cols/rows for grid sizing)
- **Phase 4 (US2 - Protocol)**: Depends on Phase 1 only (MsgGameStart struct change). Can run in parallel with Phase 3.
- **Phase 5 (US3 - Maps)**: Depends on Phase 2 (needs TileMap resource loading and spawn scan)
- **Phase 6 (US4 - Stats)**: Depends on Phase 2 (needs PlayerStats in Player struct). Can run in parallel with Phase 3, 4, 5.
- **Phase 7 (Polish)**: Depends on all previous phases

### User Story Dependencies

- **US1 (Renderer perf)**: Depends on Phase 2 foundational. No dependency on other stories.
- **US2 (Protocol version)**: Depends on Phase 1 setup only. Independent of all other stories.
- **US3 (Custom maps)**: Depends on Phase 2 foundational. No dependency on other stories.
- **US4 (Player stats)**: Depends on Phase 2 foundational. No dependency on other stories.

### Parallel Opportunities

After Phase 2 completes, US1, US3, and US4 can all proceed in parallel (they touch different files):
- US1 primarily modifies renderer.c
- US2 primarily modifies net.c and screen_lobby.c
- US3 primarily modifies screen_game.c and tilemap.c
- US4 primarily modifies player.c and bomb.c

Note: Some files are touched by multiple stories (bomb.c by US1 and US4, screen_game.c by US1 and US3). Within those files, the changes are to different functions and should not conflict.

### Within Each User Story

- Models/data changes before logic changes
- Core implementation before integration points
- Each story independently testable at its checkpoint

---

## Parallel Example: After Phase 2

```
# These can all run in parallel after Phase 2 completes:

Story 1 (Renderer): T012 [P] + T013 → T014 → T015 → T016 → T017 + T018 → T019 + T020 + T021
Story 2 (Protocol): T022 [P] + T023 [P] + T024 [P] → T025
Story 4 (Stats):    T031 [P] + T032 [P] + T033 [P] → T034

# Story 3 (Maps) can also run in parallel but shares some files with Story 1:
Story 3 (Maps):     T026 → T027 → T028 + T029 → T030
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T005)
2. Complete Phase 2: Foundational (T006-T011)
3. Complete Phase 3: User Story 1 — Renderer optimizations (T012-T021)
4. **STOP and VALIDATE**: Build all three targets, test on Mac SE, measure frame rate
5. If performance target met (10+ fps), proceed to remaining stories

### Incremental Delivery

1. Setup + Foundational → Struct changes in place
2. US1 (Renderer) → Measurable perf improvement → Build & test
3. US2 (Protocol) → Version safety → Build & test
4. US3 (Maps) → Extensibility groundwork → Build & test
5. US4 (Stats) → Final struct cleanup → Build & test
6. Polish → Full validation across all targets

---

## Notes

- All changes must be C89 clean — `-std=c89` is enforced from T001 onward
- No new source files — all changes are to existing files
- No malloc during gameplay — dirty grid and cached pointers are file-scope statics
- The `GRID_COLS`/`GRID_ROWS` defines remain in game.h as default values but gameplay code reads from TileMap struct
- Commit after each phase checkpoint
