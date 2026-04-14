# Tasks: Renderer Performance Optimizations

**Input**: Design documents from `/specs/006-renderer-optimization/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/renderer-api.md

**Tests**: No test tasks -- testing is manual hardware verification (visual comparison + FPS observation on Mac SE, Performa 6200, Performa 6400).

**Organization**: Tasks grouped by user story (optimization). Each optimization is independently implementable and testable.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, US4)

---

## Phase 1: Setup

**Purpose**: No setup tasks required. All changes are modifications to existing files in an established project.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Add the TILEMAP_TILE macro and sprite draw bracket API that multiple user stories depend on.

- [x] T001 Add TILEMAP_TILE(map, col, row) macro to `include/tilemap.h`. The macro expands to `((map)->tiles[(row)][(col)])`. Add a comment noting it performs no bounds checking and callers must validate bounds before use. Place after the TileMap_Get() declaration.
- [x] T002 [P] Add `Renderer_BeginSpriteDraw(void)` and `Renderer_EndSpriteDraw(void)` declarations to `include/renderer.h`. Place after the Renderer_BeginFrame declaration block, before the dirty rectangle section. Add comment: "Bracket all per-frame sprite drawing to batch port save/restore."
- [x] T003 Implement Renderer_BeginSpriteDraw() and Renderer_EndSpriteDraw() in `src/renderer.c`. Add `static int gSpriteDrawActive = FALSE;` near the other static state variables (around line 52). BeginSpriteDraw: calls SavePort(), SetPortWork(), sets ForeColor(blackColor)/BackColor(whiteColor), sets gSpriteDrawActive = TRUE. EndSpriteDraw: calls RestorePort(), sets gSpriteDrawActive = FALSE.

**Checkpoint**: Foundation ready. New API compiles but is not yet called. All three targets must build clean.

---

## Phase 3: User Story 1 - Smoother Gameplay on Mac SE (Priority: P1) MVP

**Goal**: Eliminate ~35 redundant Toolbox trap calls per gameplay frame by hoisting port save/restore out of sprite draw loops and removing per-frame color state setup from BeginFrame.

**Independent Test**: Build 68k target. Run 4-player game. Verify Mac SE gameplay renders correctly. Code inspection: SavePort/RestorePort called once per frame in Game_Draw, not per-sprite. BeginFrame has no ForeColor/BackColor/SavePort calls.

### Implementation for User Story 1

- [x] T004 [US1] Modify Game_Draw() in `src/screen_game.c` to call Renderer_BeginSpriteDraw() before the bomb draw loop (after Renderer_BeginFrame at line 308) and Renderer_EndSpriteDraw() after the player draw loop (before Renderer_EndFrame at line 341).
- [x] T005 [US1] Modify Renderer_DrawPlayer() fallback path in `src/renderer.c` (lines 702-737). Wrap the existing SavePort()/SetPortWork() call at line 702 with `if (!gSpriteDrawActive) {` and the RestorePort() at line 737 with `if (!gSpriteDrawActive)`. Preserve identical drawing logic between the conditionals.
- [x] T006 [P] [US1] Modify Renderer_DrawBomb() fallback path in `src/renderer.c` (lines 757-764). Same pattern as T005: guard SavePort/SetPortWork and RestorePort with `if (!gSpriteDrawActive)`.
- [x] T007 [P] [US1] Modify Renderer_DrawExplosion() fallback path in `src/renderer.c` (lines 784-794). Same pattern as T005: guard SavePort/SetPortWork and RestorePort with `if (!gSpriteDrawActive)`.
- [x] T008 [US1] Move the per-frame color state setup from Renderer_BeginFrame() in `src/renderer.c` (lines 646-650: SavePort/SetPortWork/ForeColor/BackColor/RestorePort) into Renderer_Init(), after GWorld creation for color Macs (after line 396) and after AllocOffscreenBitMap for Mac SE (after line 380). Set the color state on the work buffer port once. Also add the same color state setup at the end of Renderer_RebuildBackground() (before the MarkAllDirty call at line 624) since RebuildBackground changes the active port. Remove the 5-line block from BeginFrame entirely. Verify per FR-002: audit all code paths between EndFrame and BeginFrame to confirm nothing modifies the work port's color state (check Renderer_EndFrame, Renderer_BeginScreenDraw/EndScreenDraw, and any screen Update functions).
- [x] T009 [US1] Build all three targets (68k, PPC OT, PPC MacTCP) and verify clean compilation with no warnings.

**Checkpoint**: Mac SE saves ~35 trap calls per gameplay frame. Rendering output identical. All three targets build clean.

---

## Phase 4: User Story 2 - Faster Sprite Blitting on Color Macs (Priority: P2)

**Goal**: Replace `transparent` CopyBits transfer mode with srcCopy + pre-computed mask regions for 2-3x faster sprite blits on color Macs.

**Independent Test**: Build PPC OT target. Run 4-player game on Performa 6400. Verify sprites render correctly with no visual artifacts (no white rectangles around sprites, no missing pixels). Fallback to transparent mode must work if mask creation fails.

### Implementation for User Story 2

- [x] T010 [US2] Add mask region static variables to `src/renderer.c` near the existing sprite GWorld declarations (around line 35): `static RgnHandle gPlayerMaskRgn[MAX_PLAYERS];`, `static RgnHandle gBombMaskRgn = NULL;`, `static RgnHandle gExplosionMaskRgn = NULL;`, `static RgnHandle gTitleMaskRgn = NULL;`. Initialize all to NULL.
- [x] T011 [US2] Implement static helper `CreateMaskFromGWorld(GWorldPtr gw, short width, short height)` in `src/renderer.c` that returns RgnHandle. Steps: (1) Allocate a 1-bit BitMap with rowBytes = ((width + 15) / 16) * 2, baseAddr = NewPtrClear(rowBytes * height), bounds = {0, 0, height, width}. (2) Lock sprite GWorld PixMap via GetGWorldPixMap/LockPixels. (3) Get PixMap baseAddr and rowBytes. (4) Determine the white/background pixel index by reading the GWorld's color table: get CTabHandle from PixMap, scan ctTable entries for the one whose rgb matches {0xFFFF,0xFFFF,0xFFFF}, use its index as the transparent value. Do NOT hardcode 0xFF. (5) Scan each pixel row: for 8-bit indexed mode, compare each pixel byte against the determined background index. For each non-background pixel, set the corresponding bit in the 1-bit mask. (6) Call rgnHandle = NewRgn(); if NULL, clean up and return NULL. (7) Call BitMapToRegion(rgnHandle, &maskBitMap). If error, DisposeRgn and return NULL. (8) UnlockPixels, DisposePtr(maskBitMap.baseAddr). (9) Return rgnHandle.
- [x] T012 [US2] Call CreateMaskFromGWorld() for each sprite in LoadPICTResources() in `src/renderer.c` (after each LoadPICTToGWorld call, around lines 297-330). Store results in the corresponding mask region variables. Log success/failure via CLOG_INFO. NULL result means fallback to transparent (no error, just slower).
- [x] T013 [US2] Modify Renderer_DrawPlayer() color Mac path in `src/renderer.c` (lines 692-700). If gPlayerMaskRgn[playerID] is not NULL: OffsetRgn(gPlayerMaskRgn[playerID], dstRect.left, dstRect.top), call CopyBits with srcCopy and gPlayerMaskRgn[playerID] instead of transparent/NULL, then OffsetRgn(gPlayerMaskRgn[playerID], -dstRect.left, -dstRect.top) to restore origin. Else: keep existing transparent mode as fallback.
- [x] T014 [P] [US2] Modify Renderer_DrawBomb() color Mac path in `src/renderer.c` (lines 748-755). Same OffsetRgn/CopyBits(srcCopy, maskRgn)/OffsetRgn pattern as T013, using gBombMaskRgn. Fallback to transparent if NULL.
- [x] T015 [P] [US2] Modify Renderer_DrawExplosion() color Mac path in `src/renderer.c` (lines 775-782). Same pattern as T013, using gExplosionMaskRgn. Fallback to transparent if NULL.
- [x] T016 [US2] Add mask region disposal to Renderer_Shutdown() in `src/renderer.c` (before the existing DisposeGWorld calls for sprites, around line 470). For each non-NULL mask region: DisposeRgn(). Set to NULL after disposal. Handle the gPlayerSprites duplicate-check pattern: only dispose mask regions for non-duplicate player sprite GWorlds.
- [x] T017 [US2] Build all three targets and verify clean compilation. The Mac SE build must not reference any mask region code (all behind `!gGame.isMacSE && gPICTsLoaded` guards).

**Checkpoint**: Color Mac sprite blits use srcCopy+maskRgn. Mac SE unaffected. Sprites render correctly on all targets.

---

## Phase 5: User Story 3 - Reduced Hitch During Background Rebuild (Priority: P3)

**Goal**: Batch tile drawing by type in RebuildBackground() to reduce ForeColor trap calls from O(tiles) to O(tile_types).

**Independent Test**: Build 68k target. Place bomb that destroys 3+ blocks. Background rebuild should show identical visual result. Code inspection: ForeColor called at most 4 times (Mac SE) or 8 times (color) per rebuild.

### Implementation for User Story 3

- [x] T018 [US3] Refactor the tile drawing loop in Renderer_RebuildBackground() in `src/renderer.c` (lines 569-624). Replace the single row/col loop that calls DrawTileRect() per tile with a batched approach. For Mac SE path: (Pass 1) Set ForeColor(whiteColor), iterate all tiles, PaintRect for TILE_FLOOR and TILE_SPAWN tiles only, then ForeColor(blackColor). (Pass 2) Iterate all tiles, PaintRect for TILE_WALL only (ForeColor already black). (Pass 3) Iterate all tiles, FillRect(&r, &qd.gray) for TILE_BLOCK only (no ForeColor needed). For color Mac path: (Pass 1) RGBForeColor(&kTileGreen), iterate and PaintRect for TILE_FLOOR/TILE_SPAWN. (Pass 2) RGBForeColor(&kTileGray) + PaintRect for TILE_WALL, then RGBForeColor(&kTileDarkGray) + FrameRect for TILE_WALL. (Pass 3) RGBForeColor(&kTileBrown) + PaintRect for TILE_BLOCK, then RGBForeColor(&kTileDarkBrown) + FrameRect for TILE_BLOCK. Preserve the tile sheet CopyBits path (DrawTileFromSheet) when gTileSheet is loaded -- only batch the fallback rectangle path.
- [x] T019 [US3] Build all three targets and verify clean compilation. Visual comparison: background must be pixel-identical to pre-optimization.

**Checkpoint**: Background rebuilds after explosions execute with ~4-8 ForeColor calls instead of ~165. No visual change.

---

## Phase 6: User Story 4 - Faster Bomb Raycast and Player Collision (Priority: P4)

**Goal**: Use TILEMAP_TILE macro in bomb raycast and collision hot paths to eliminate function call overhead.

**Independent Test**: Build 68k target. Play game with multiple simultaneous explosions at max range. Verify identical behavior: same tiles destroyed, same kill detection, same collision clamping.

### Implementation for User Story 4

- [x] T020 [US4] Modify ExplodeBomb() in `src/bomb.c` (lines 99-147). At the start of the function, add `TileMap *map = TileMap_Get();`. Replace the two TileMap_GetTile() calls inside the raycast loop (lines 123 and 129) with `TILEMAP_TILE(map, col, row)`. The bounds check at lines 120-121 (`col < 0 || col >= TileMap_GetCols() || row < 0 || row >= TileMap_GetRows()`) already validates before the macro is used, so this is safe. Include `tilemap.h` if not already included.
- [x] T021 [P] [US4] Modify CheckTileSolid() in `src/player.c` (lines 136-150). Add a `const TileMap *map` parameter to CheckTileSolid's signature. In CollideAxis() (the sole caller), cache `const TileMap *map = TileMap_Get();` at the top of the function and pass it to CheckTileSolid. Inside CheckTileSolid, replace TileMap_IsSolid(col, row) with inline equivalent: `{ unsigned char t = TILEMAP_TILE(map, col, row); if (t == TILE_WALL || t == TILE_BLOCK) return TRUE; }`. The bounds are already validated by the minCol/maxCol/minRow/maxRow clamping in CollideAxis (lines 200-203). Include `tilemap.h` if not already included.
- [x] T022 [US4] Build all three targets and verify clean compilation with no warnings.

**Checkpoint**: Hot-path tilemap lookups use direct array access. Behavior identical.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final verification and documentation.

- [x] T023 Build all three targets clean: `cd build-68k && make`, `cd build-ppc-ot && make`, `cd build-ppc-mactcp && make`. Verify zero warnings.
- [ ] T024 Deploy 68k build to Mac SE via classic-mac-hardware MCP. Run 4-player game. Verify: sprites render correctly, bombs explode correctly, no visual artifacts, no crashes. Compare rendering against pre-optimization build screenshots to confirm pixel-identical output (FR-009). Observe FPS improvement.
- [ ] T025 [P] Deploy PPC OT build to Performa 6400 via classic-mac-hardware MCP. Verify: mask region sprites render correctly (no white boxes around sprites), fallback path works if PICTs removed. Compare rendering against pre-optimization build screenshots to confirm pixel-identical output (FR-009).
- [x] T026 Update CLAUDE.md with 006-renderer-optimization changes: new Renderer_BeginSpriteDraw/EndSpriteDraw API, TILEMAP_TILE macro, mask region sprite blitting, batched tile drawing.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: N/A -- no setup needed
- **Phase 2 (Foundational)**: T001, T002, T003 -- adds macro and API declarations. BLOCKS all user stories.
- **Phase 3 (US1)**: Depends on T002, T003. Can start after Foundational.
- **Phase 4 (US2)**: No dependency on US1. Can start after Foundational.
- **Phase 5 (US3)**: No dependency on US1 or US2. Can start after Foundational.
- **Phase 6 (US4)**: Depends on T001 (TILEMAP_TILE macro). Can start after Foundational.
- **Phase 7 (Polish)**: Depends on all user stories being complete.

### User Story Dependencies

- **US1 (Port hoisting + color state)**: Independent. Modifies renderer.c and screen_game.c.
- **US2 (Mask regions)**: Independent. Modifies renderer.c (different sections than US1).
- **US3 (Tile batching)**: Independent. Modifies renderer.c RebuildBackground (different section).
- **US4 (Tilemap macro)**: Independent. Modifies bomb.c and player.c only.

### Within Each User Story

- Implementation tasks execute sequentially within a story (file dependencies)
- Tasks marked [P] within a story can run in parallel (different functions/files)
- Build verification (T009, T017, T019, T022) must be last in each phase

### Parallel Opportunities

- T001 and T002 can run in parallel (different files: tilemap.h vs renderer.h)
- T005, T006, T007 modify different functions in renderer.c -- T006 and T007 are parallel with each other (marked [P])
- T013, T014, T015 modify different functions -- T014 and T015 are parallel
- T020 and T021 modify different files (bomb.c vs player.c) -- parallel
- US1, US2, US3, US4 are all independent and could theoretically be worked in parallel (different code sections)

---

## Parallel Example: User Story 1

```bash
# After T004 (Game_Draw bracket), these can run in parallel:
Task T006: "Modify Renderer_DrawBomb() fallback path in src/renderer.c"
Task T007: "Modify Renderer_DrawExplosion() fallback path in src/renderer.c"
# Then T005 (DrawPlayer) sequentially (same file section)
# Then T008 (BeginFrame color state) sequentially
# Then T009 (build verification)
```

## Parallel Example: User Story 2

```bash
# After T013 (DrawPlayer mask region), these can run in parallel:
Task T014: "Modify Renderer_DrawBomb() color Mac path in src/renderer.c"
Task T015: "Modify Renderer_DrawExplosion() color Mac path in src/renderer.c"
# Then T016 (shutdown cleanup) sequentially
# Then T017 (build verification)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 2: Foundational (T001-T003)
2. Complete Phase 3: User Story 1 (T004-T009)
3. **STOP and VALIDATE**: Build 68k, deploy to Mac SE, verify identical rendering with fewer trap calls
4. This alone delivers the highest-impact optimization (~35 fewer traps/frame on SE)

### Incremental Delivery

1. Foundational (T001-T003) -- macro + API ready
2. US1 (T004-T009) -- port hoisting + color state -- **biggest SE impact**
3. US2 (T010-T017) -- mask regions -- **biggest PPC impact**
4. US3 (T018-T019) -- tile batching -- **rebuild hitch reduction**
5. US4 (T020-T022) -- tilemap macro -- **micro-optimization**
6. Polish (T023-T026) -- verification + docs

Each story adds measurable value without breaking previous stories.

---

## Notes

- All tasks modify existing files only -- no new source files created
- Line numbers reference current codebase state; may shift as earlier tasks are completed
- C89 compliance: no // comments, no mixed declarations, no inline keyword
- Mac SE has no color QuickDraw: mask region code must be behind `!gGame.isMacSE` guards
- PICT fallback: all sprite draw functions must still work without PICTs loaded
