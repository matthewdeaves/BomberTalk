# Tasks: Smooth Sub-Tile Player Movement

**Input**: Design documents from `/specs/004-smooth-movement/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: Not explicitly requested. No test tasks generated.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Phase 1: Setup

**Purpose**: Data structure changes and constants needed by all user stories

- [ ] T001 Add new Player struct fields (targetPixelX, targetPixelY, accumX, accumY, passThroughBombIdx) and movement constants (HITBOX_INSET_LARGE/SMALL, NUDGE_THRESHOLD_LARGE/SMALL, INTERP_TICKS) to include/game.h
- [ ] T002 Add Player_GetHitbox and Player_MarkDirtyTiles function declarations to include/player.h

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core movement infrastructure that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T003 Implement Player_GetHitbox helper in src/player.c — returns Rect inset from full tile rect at pixelX/pixelY using HITBOX_INSET_LARGE or HITBOX_INSET_SMALL based on gGame.isMacSE
- [ ] T004 Implement Player_MarkDirtyTiles helper in src/player.c — computes min/max tile cols/rows from pixelX/pixelY bounding box, calls Renderer_MarkDirty for each (1-4 tiles)
- [ ] T005 Change Player_Init in src/player.c to set pixelX/pixelY as authoritative from spawn position, initialize accumX/accumY to 0, passThroughBombIdx to -1, targetPixelX/targetPixelY to match pixelX/pixelY
- [ ] T006 Add center-based grid derivation helper in src/player.c — gridCol = (pixelX + tileSize/2) / tileSize — and call it after every pixelX/pixelY update to keep gridCol/gridRow in sync

**Checkpoint**: Foundation ready — Player struct has all new fields, hitbox and dirty-tile helpers exist, spawn initialization is pixel-authoritative

---

## Phase 3: User Story 1 - Smooth Player Movement (Priority: P1) MVP

**Goal**: Players move smoothly pixel-by-pixel instead of snapping between tiles

**Independent Test**: Hold arrow key on any Mac, verify sprite slides smoothly between tiles. Release key, verify player stops at sub-tile position. Move into wall, verify player stops flush.

### Implementation for User Story 1

- [ ] T007 [US1] Rewrite Player_Update movement in src/player.c — replace cooldown+grid-snap with continuous pixel movement: compute pixel delta via fractional accumulator (accumX += tileSize * deltaTicks, movePixels = accumX / ticksPerTile, accumX %= ticksPerTile), advance pixelX/pixelY, derive gridCol/gridRow from center point. Keep Input_IsKeyDown + Input_WasKeyPressed checks (same priority order).
- [ ] T008 [US1] Implement axis-separated AABB wall collision in src/player.c — after computing proposed pixelX, check all tiles overlapped by player hitbox against TileMap_IsSolid. If any solid tile overlaps, clamp pixelX to tile boundary. Repeat for pixelY independently. IMPORTANT (FR-008): For large deltaTicks jumps (Mac SE can have deltaTicks=6-10), must sweep all tiles between old and new pixel position, not just the destination — iterate from old tile to new tile along movement axis to prevent tunneling through walls.
- [ ] T009 [US1] Update Renderer_DrawPlayer in src/renderer.c to accept pixel coordinates instead of grid coordinates — change signature from (playerID, col, row, facing) to (playerID, pixelX, pixelY, facing), update SetRect to use pixelX/pixelY directly instead of col*tileSize
- [ ] T010 [US1] Update screen_game.c Game_Draw to pass player pixelX/pixelY to Renderer_DrawPlayer instead of gridCol/gridRow
- [ ] T011 [US1] Update screen_game.c Game_Update dirty rect marking to use Player_MarkDirtyTiles instead of single-tile Renderer_MarkDirty — mark old position tiles dirty before update, new position tiles dirty after update (handles multi-tile straddling)
- [ ] T012 [US1] Update bomb placement in src/screen_game.c to use center-derived gridCol/gridRow for Bomb_PlaceAt call and Net_SendBombPlaced — ensures bomb goes on the tile the player is mostly on
- [ ] T013 [US1] Build all three targets (68k, PPC OT, PPC MacTCP) and verify clean compilation with new Player_Update, Renderer_DrawPlayer signature, and dirty rect changes

**Checkpoint**: Player moves smoothly on all platforms. Wall collision works. Bombs place on correct tile. Single-player gameplay functional.

---

## Phase 4: User Story 2 - Explosion Danger While Between Tiles (Priority: P1)

**Goal**: Explosions kill players whose hitbox overlaps any explosion tile, enabling near-miss gameplay

**Independent Test**: Place bomb, move partially off tile before explosion, verify death. Move fully off, verify survival.

### Implementation for User Story 2

- [ ] T014 [US2] Replace grid-equality explosion kill check in ExplodeBomb (src/bomb.c) with AABB overlap test — for each active player, call Player_GetHitbox, test overlap against each new explosion tile rect (col*ts, row*ts, (col+1)*ts, (row+1)*ts). Kill player if any overlap exists.
- [ ] T015 [US2] Also update the per-frame explosion kill check in Bomb_Update (src/bomb.c) if explosions check kills on subsequent frames — ensure AABB overlap is used consistently for all explosion-player collision

**Checkpoint**: Players can be killed by partial tile overlap with explosions. Near-misses work — dodging by a few pixels is possible.

---

## Phase 5: User Story 3 - Bomb Walk-Off (Priority: P2)

**Goal**: Players can walk off bombs they place but cannot walk back onto them

**Independent Test**: Place bomb, walk away, try to walk back — should be blocked. Place bomb, walk off in any direction — should succeed.

### Implementation for User Story 3

- [ ] T016 [US3] After successful Bomb_PlaceAt in src/screen_game.c, find the bomb at the placed (col,row) by scanning gGame.bombs[] for the active bomb at that position, then set the local player's passThroughBombIdx to that array index. Note: Bomb_PlaceAt returns TRUE/FALSE, not the index.
- [ ] T017 [US3] In Player_Update AABB wall collision (src/player.c), when checking Bomb_ExistsAt for tiles the player hitbox overlaps, skip the bomb at passThroughBombIdx if it matches — allows player to walk through their just-placed bomb
- [ ] T018 [US3] Each frame in Player_Update (src/player.c), if passThroughBombIdx != -1, check if player hitbox still overlaps the bomb's tile — if no overlap, clear passThroughBombIdx to -1 (bomb becomes solid). Also clear if the bomb is no longer active (exploded).

**Checkpoint**: Bomb walk-off works. Player can leave bomb tile, cannot return. Bomb blocks normally after player leaves.

---

## Phase 6: User Story 4 - Network Position Sync (Priority: P2)

**Goal**: Remote players move smoothly via pixel-coordinate network sync with interpolation

**Independent Test**: Two machines connected, move on one, verify smooth movement on the other. Connect v2 client to v3, verify lobby warning.

### Implementation for User Story 4

- [ ] T019 [US4] Update MsgPosition struct in include/game.h to v3 layout: playerID (u8), facing (u8), pixelX (short), pixelY (short), pad (u8[2]) = 8 bytes total. Bump BT_PROTOCOL_VERSION from 2 to 3.
- [ ] T020 [US4] Update Net_SendPosition in src/net.c — change parameters from (col, row, facing) to (pixelX, pixelY, facing), pack short pixel coordinates into MsgPosition v3
- [ ] T021 [US4] Update on_position handler in src/net.c — unpack short pixelX/pixelY from MsgPosition v3, call Player_SetPosition with pixel coords setting targetPixelX/targetPixelY (not direct pixelX/pixelY)
- [ ] T022 [US4] Update Player_SetPosition in src/player.c — change parameters from (col, row, facing) to (pixelX, pixelY, facing), set targetPixelX/targetPixelY, derive gridCol/gridRow from center point
- [ ] T023 [US4] Add interpolation logic to Player_Update for remote players in src/player.c — each frame, lerp pixelX toward targetPixelX and pixelY toward targetPixelY using tick-based rate (INTERP_TICKS). Derive gridCol/gridRow after interpolation.
- [ ] T024 [US4] Update all Net_SendPosition call sites in src/screen_game.c and src/player.c to pass pixelX/pixelY instead of gridCol/gridRow
- [ ] T025 [US4] Build all three targets and verify clean compilation with MsgPosition v3 and updated network functions

**Checkpoint**: Remote players move smoothly with interpolation. Protocol v3 works. v2 mismatch warning preserved.

---

## Phase 7: User Story 5 - Corner Sliding (Priority: P3)

**Goal**: Players are auto-nudged into corridor alignment when nearly aligned, enabling smooth turns

**Independent Test**: Move toward corridor opening while slightly misaligned, press perpendicular direction, verify nudge into corridor.

### Implementation for User Story 5

- [ ] T026 [US5] Implement corner sliding in Player_Update (src/player.c) — when player presses a perpendicular direction and is blocked, check if pixelX or pixelY offset from tile alignment is within NUDGE_THRESHOLD. If so, nudge toward alignment by pixelsPerTick * deltaTicks (same movement speed). Then re-check if the desired direction is now passable.
- [ ] T027 [US5] Tune NUDGE_THRESHOLD_LARGE (10px) and NUDGE_THRESHOLD_SMALL (5px) values in include/game.h based on playtesting feel

**Checkpoint**: Corner sliding works. Players can smoothly turn into corridors without pixel-perfect alignment.

---

## Phase 8: User Story 6 - Disconnected Player Cleanup (Priority: P1)

**Goal**: Disconnected players' sprites are immediately cleaned up with no ghost artifacts

**Independent Test**: 3-player game, one quits via Cmd-Q, verify sprite disappears on other two machines within one frame.

### Implementation for User Story 6

- [ ] T028 [US6] In on_disconnected handler in src/net.c, before setting player.active = FALSE, call Player_MarkDirtyTiles(playerID) to mark all tiles overlapped by the disconnecting player's bounding box as dirty
- [ ] T029 [US6] Verify that disconnected player's active bombs continue their fuse timers and explode normally — no changes needed to Bomb_Update (bombs reference ownerID not player active state), but verify no regression

**Checkpoint**: Disconnect cleanup works. No ghost sprites. Bombs from disconnected players still explode.

---

## Phase 9: User Story 7 - Compile-Time Debug Toggle (Priority: P2)

**Goal**: CMake option to disable clog logging for zero-overhead release builds

**Independent Test**: Build with BOMBERTALK_DEBUG=OFF, verify no UDP packets on port 7355. Compare FPS with debug ON vs OFF on Mac SE.

### Implementation for User Story 7

- [ ] T030 [US7] Add CMake option BOMBERTALK_DEBUG (default ON) to CMakeLists.txt — when OFF, add -DCLOG_STRIP to target compile definitions and skip linking libclog.a
- [ ] T031 [US7] Guard clog_init(), clog_set_file(), and clog_set_network_sink() calls in src/main.c and src/net.c with #ifndef CLOG_STRIP preprocessor checks
- [ ] T032 [US7] Verify clog's CLOG_STRIP macro (clog.h line 79) correctly expands all CLOG_ERR/CLOG_WARN/CLOG_INFO/CLOG_DEBUG calls to ((void)0) — confirmed in clog source, no wrapper macros needed
- [ ] T033 [US7] Build all three targets with BOMBERTALK_DEBUG=OFF and verify clean compilation with no clog references in the binary

**Checkpoint**: Debug toggle works. Release builds have zero logging overhead.

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final validation across all stories

- [ ] T034 Build all three targets (68k, PPC OT, PPC MacTCP) with all changes and verify clean compilation
- [ ] T035 Deploy to all three Macs via classic-mac-hardware-mcp and run full integration test: smooth movement, explosion kills, bomb walk-off, network sync, disconnect cleanup, corner sliding
- [ ] T036 Compare FPS on all three Macs against baseline (Mac SE ~10fps, 6200 ~24fps, 6400 ~26fps) to verify no regression. NOTE (from books research R9): sub-tile sprite CopyBits will be misaligned on color Macs — if PPC FPS drops measurably, consider pre-shifted sprite GWorlds as mitigation (see research.md R9)
- [ ] T037 Update CLAUDE.md with new architecture notes: pixel-authoritative positions, center-based grid derivation, AABB collision, hitbox inset, bomb pass-through, MsgPosition v3, remote interpolation, corner sliding, BOMBERTALK_DEBUG option, CopyBits alignment consideration for sub-tile sprites

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion — BLOCKS all user stories
- **US1 Smooth Movement (Phase 3)**: Depends on Foundational — MUST complete first (changes movement model everything else builds on)
- **US2 Explosion AABB (Phase 4)**: Depends on US1 (needs pixel-authoritative positions and hitbox helper)
- **US3 Bomb Walk-Off (Phase 5)**: Depends on US1 (needs AABB collision framework to add pass-through)
- **US4 Network Sync (Phase 6)**: Depends on US1 (needs pixel positions to transmit)
- **US5 Corner Sliding (Phase 7)**: Depends on US1 (adds to movement logic)
- **US6 Disconnect Cleanup (Phase 8)**: Depends on Foundational only (uses Player_MarkDirtyTiles)
- **US7 Debug Toggle (Phase 9)**: No story dependencies — CMake/build only
- **Polish (Phase 10)**: Depends on all desired user stories being complete

### User Story Dependencies

```
Phase 1: Setup
    ↓
Phase 2: Foundational
    ↓
Phase 3: US1 Smooth Movement (MVP) ←── Phase 8: US6 Disconnect (independent)
    ↓                                    Phase 9: US7 Debug Toggle (independent)
    ├── Phase 4: US2 Explosion AABB
    ├── Phase 5: US3 Bomb Walk-Off
    ├── Phase 6: US4 Network Sync
    └── Phase 7: US5 Corner Sliding
                    ↓
              Phase 10: Polish
```

### Parallel Opportunities

- **After Foundational**: US6 (disconnect) and US7 (debug toggle) can run in parallel with US1
- **After US1**: US2, US3, US4, US5 can all run in parallel (they touch different aspects of the movement system in different files/functions)
- **Within US4**: T019 (game.h struct) must precede T020-T024, but T020 and T022 can run in parallel (different files)

---

## Parallel Example: After US1 Completion

```
# These can all launch in parallel after Phase 3 (US1) completes:
Task T014: "AABB explosion kill check in src/bomb.c" (US2)
Task T016: "Bomb pass-through setup in src/screen_game.c" (US3)
Task T019: "MsgPosition v3 struct in include/game.h" (US4)
Task T026: "Corner sliding in src/player.c" (US5)
```

---

## Implementation Strategy

### MVP First (US1 Only)

1. Complete Phase 1: Setup (T001-T002)
2. Complete Phase 2: Foundational (T003-T006)
3. Complete Phase 3: US1 Smooth Movement (T007-T013)
4. **STOP and VALIDATE**: Deploy to hardware, test smooth movement on all 3 Macs
5. This alone delivers the core gameplay improvement

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. US1 -> Smooth movement works -> Deploy/Test (MVP!)
3. US2 -> Explosion AABB -> Deploy/Test (gameplay complete)
4. US3 -> Bomb walk-off -> Deploy/Test (bomb gameplay fixed)
5. US4 -> Network sync -> Deploy/Test (multiplayer works)
6. US6 -> Disconnect cleanup -> Deploy/Test (bug fixed)
7. US7 -> Debug toggle -> Deploy/Test (performance option)
8. US5 -> Corner sliding -> Deploy/Test (polish)
9. Polish -> Final validation

### Recommended Order

US1 -> US2 -> US3 -> US6 -> US4 -> US7 -> US5 -> Polish

Rationale: US1-US3 form the core single-player gameplay loop. US6 is a quick bug fix. US4 enables multiplayer testing. US7 helps performance benchmarking. US5 is polish.

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- All code must be C89/C90 compliant — no // comments, no mixed declarations
- All timing must use deltaTicks, never frame counts
- Commit after each task or logical group
- Build all three targets after each phase to catch platform-specific issues early
