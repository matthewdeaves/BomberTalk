# Tasks: Performance & Correctness Optimizations

**Input**: Design documents from `/specs/003-optimize-correctness/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: No new files or dependencies needed. This feature modifies existing files only.

- [x] T001 Create feature branch 003-optimize-correctness from main (already done)

---

## Phase 2: Foundational (No blocking prerequisites)

**Purpose**: No shared infrastructure changes needed. All user stories modify independent code paths and can proceed directly.

**Checkpoint**: No foundational work required. Proceed to user stories.

---

## Phase 3: User Story 1 - Faster Rendering on All Macs (Priority: P1)

**Goal**: Normalize ForeColor/BackColor to black/white before all srcCopy-mode CopyBits calls on every platform, not just Mac SE. Up to 2.5x CopyBits speedup on color Macs if QuickDraw was previously performing colorization.

**Independent Test**: Deploy to Performa 6200 or 6400, press F to show FPS counter, compare with previous build. Mac SE should show no regression.

### Implementation for User Story 1

- [x] T002 [US1] Remove `if (gGame.isMacSE)` guard around ForeColor/BackColor calls in Renderer_RebuildBackground() in src/renderer.c so color normalization applies to all platforms
- [x] T003 [US1] Remove `if (gGame.isMacSE)` guard around ForeColor/BackColor calls in Renderer_BeginFrame() in src/renderer.c so bg-to-work CopyBits is normalized on all platforms
- [x] T004 [US1] Add ForeColor(blackColor) and BackColor(whiteColor) before work-to-window CopyBits in Renderer_BlitToWindow() in src/renderer.c
- [x] T005 [US1] Build all three targets (68k, PPC MacTCP, PPC OT) and verify clean compilation

**Checkpoint**: ForeColor/BackColor normalization active on all platforms. FPS should be equal or improved on color Macs, unchanged on Mac SE.

---

## Phase 4: User Story 2 - Smooth Explosions on Remote Machines (Priority: P2)

**Goal**: Add deferred background rebuild mechanism so multiple block-destroy events per frame result in exactly one background rebuild.

**Independent Test**: In networked game, explode blocks on remote machine. Observe smooth explosion with no frame hitch. Check clog for single rebuild per explosion frame.

### Implementation for User Story 2

- [x] T006 [US2] Add `Renderer_RequestRebuildBackground()` declaration to include/renderer.h
- [x] T007 [US2] Add static `gNeedRebuildBg` flag and implement `Renderer_RequestRebuildBackground()` in src/renderer.c
- [x] T008 [US2] Add deferred rebuild check at start of `Renderer_BeginFrame()` in src/renderer.c -- if flag is set, call RebuildBackground and clear flag before dirty rect processing
- [x] T009 [US2] Replace direct `Renderer_RebuildBackground()` call with `Renderer_RequestRebuildBackground()` in ExplodeBomb() in src/bomb.c
- [x] T010 [US2] Replace direct `Renderer_RebuildBackground()` call with `Renderer_RequestRebuildBackground()` in on_block_destroyed() in src/net.c
- [x] T011 [US2] Add CLOG_INFO in Renderer_RebuildBackground() to log each rebuild call for verification in src/renderer.c
- [x] T012 [US2] Build all three targets and verify clean compilation

**Checkpoint**: Background rebuilds batched to once per frame. Explosion rendering smooth on remote machines. Log shows single rebuild per explosion.

---

## Phase 5: User Story 3 - Efficient Tilemap Reloading Between Rounds (Priority: P3)

**Goal**: Cache initial tilemap state after first load. Add TileMap_Reset() that restores from cache without Resource Manager calls.

**Independent Test**: Play multiple rounds. Verify blocks restore correctly each round. No additional resource loads during round transition.

### Implementation for User Story 3

- [x] T013 [US3] Add `TileMap_Reset()` declaration to include/tilemap.h
- [x] T014 [US3] Add static `gInitialMap` cache (TileMap struct) to src/tilemap.c and populate it at end of TileMap_Init() after TileMap_ScanSpawns()
- [x] T015 [US3] Implement `TileMap_Reset()` in src/tilemap.c that copies gInitialMap back to gMap
- [x] T016 [US3] Replace `TileMap_Init()` call with `TileMap_Reset()` in Game_Init() in src/screen_game.c
- [x] T017 [US3] Build all three targets and verify clean compilation

**Checkpoint**: Round restarts restore tilemap from cache. No Resource Manager calls on round transition.

---

## Phase 6: User Story 4 - Instant Bomb Collision Check (Priority: P4)

**Depends on**: US2 (Phase 4) — both modify bomb.c (ExplodeBomb). Complete US2 first.

**Goal**: Add spatial bomb grid for O(1) Bomb_ExistsAt() lookups. Replace both the Bomb_ExistsAt() linear scan and the Bomb_PlaceAt() inline duplicate check with array lookups.

**Independent Test**: Play game, place bombs, verify movement is correctly blocked by bombs and allowed after explosion. Behavior identical to previous build.

### Implementation for User Story 4

- [x] T018 [US4] Add static `gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS]` to src/bomb.c and zero it in Bomb_Init()
- [x] T019 [US4] Set `gBombGrid[row][col] = 1` in Bomb_PlaceAt() after successful placement in src/bomb.c
- [x] T020 [US4] Clear `gBombGrid[b->gridRow][b->gridCol] = 0` when bomb is deactivated in ExplodeBomb() in src/bomb.c
- [x] T021 [US4] Replace linear scan in Bomb_ExistsAt() with `return gBombGrid[row][col]` in src/bomb.c
- [x] T022 [US4] Replace inline duplicate-check linear scan in Bomb_PlaceAt() with gBombGrid lookup in src/bomb.c
- [x] T023 [US4] Build all three targets and verify clean compilation

**Checkpoint**: Bomb collision checks are O(1). Gameplay behavior unchanged.

---

## Phase 7: User Story 5 - Clean Peer Pointer on Disconnect (Priority: P5)

**Goal**: NULL the peer pointer when a player disconnects to prevent stale pointer references.

**Independent Test**: In networked game, disconnect and reconnect a player. Verify no crashes and correct reconnection behavior.

### Implementation for User Story 5

- [x] T024 [US5] Add `gGame.players[i].peer = NULL` after marking player inactive in on_disconnected() in src/net.c
- [x] T025 [US5] Build all three targets and verify clean compilation

**Checkpoint**: Peer pointers correctly NULLed on disconnect. No stale pointer references.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Final validation across all changes.

- [x] T026 Update CLAUDE.md with new renderer API (RequestRebuildBackground), tilemap API (TileMap_Reset), and bomb grid optimization notes
- [x] T027 Build all three targets one final time and verify zero warnings
- [ ] T028 Deploy to all three Macs and run hardware validation per quickstart.md

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: Already complete
- **User Stories (Phase 3-7)**: No foundational phase needed. All stories are independent.
- **Polish (Phase 8)**: Depends on all user stories being complete

### User Story Dependencies

- **User Story 1 (P1)**: Independent. Modifies renderer.c only (ForeColor/BackColor paths).
- **User Story 2 (P2)**: Independent. Adds new function to renderer.c/renderer.h, modifies bomb.c and net.c.
- **User Story 3 (P3)**: Independent. Modifies tilemap.c/tilemap.h and screen_game.c.
- **User Story 4 (P4)**: Independent. Modifies bomb.c only (internal data structure).
- **User Story 5 (P5)**: Independent. Modifies net.c only (one line).

**Note**: US1 and US2 both modify renderer.c so they should be done sequentially. US2 and US4 both modify bomb.c so they should be done sequentially. US2 and US5 both modify net.c so they should be done sequentially. Recommended order: US1 → US2 → US3/US4/US5 (US3, US4, US5 can run in parallel after US2).

### Parallel Opportunities

```text
After US2 completes:
  US3 (tilemap.c, tilemap.h, screen_game.c) -- no overlap
  US4 (bomb.c internal only) -- no overlap with US3/US5
  US5 (net.c one line) -- no overlap with US3/US4
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete US1 (T002-T005): ForeColor/BackColor fix
2. **STOP and VALIDATE**: Deploy to color Mac, check FPS improvement
3. This alone could deliver the biggest performance gain

### Incremental Delivery

1. US1: ForeColor/BackColor fix → Deploy, measure FPS
2. US2: Deferred rebuild → Deploy, test explosions
3. US3 + US4 + US5 in parallel → Deploy, full validation
4. Polish → Final hardware test on all three Macs

---

## Notes

- No test tasks included (manual hardware testing only per project constraints)
- All changes are C89/C90 compliant -- verify with `-std=c89` flag
- Total new static memory: ~1.6 KB (well within Mac SE budget)
- Each build verification step (T005, T012, T017, T023, T025) covers all three targets
