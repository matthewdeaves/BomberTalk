# Tasks: BomberTalk v1.0-alpha

**Input**: Design documents from `/specs/001-v1-alpha/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/network-protocol.md

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Exact file paths included in descriptions

---

## Phase 1: Setup (Build System and Project Skeleton)

**Purpose**: Get the project compiling on all three targets with an empty window.

- [x] T001 Create `CMakeLists.txt` with 68k MacTCP, PPC OT, and PPC MacTCP targets. Link PeerTalk and clog. Use `add_application()` from Retro68.
- [x] T002 [P] Create `resources/bombertalk.r` — Rez source for MENU resources (Apple menu, File menu with Quit)
- [x] T003 [P] Create `resources/bombertalk_size.r` — SIZE resource: 1.5 MB preferred, 1 MB minimum
- [x] T004 [P] Create `include/game.h` — Master header with grid constants, tile types, key codes, resource IDs, timing constants, boolean defines (from BOMBERMAN_CLONE_PLAN.md Phase 1)
- [x] T005 [P] Create `maps/level1.h` — Hardcoded 15x13 level data as const array (from plan Phase 2)
- [ ] T056 [P] Generate artwork and convert to PICT resources: (1) Generate 11 PNGs via Rockport (see resources/rockport-prompts.md), (2) run through ~/Desktop/pixelcraft pipeline (mac-system7 palette, edge_aware algorithm, atkinson dither) at 32x32 for color and 16x16 for SE to produce PixelGrid JSON, (3) run ~/pixelforge/tools/grid2pict --no-header to create .pict files, (4) combine tile strips and player strips, (5) embed in resource fork via Rez read directive or pict2macbin, (6) add bombertalk_gfx.r to CMakeLists.txt. See contracts/asset-pipeline.md for full pipeline.
- [x] T006 Create `src/main.c` — InitToolbox(), minimal event loop with WaitNextEvent, Cmd-Q quit. Open a window with NewCWindow. Verify builds on all three targets.
- [x] T007 Build all three targets and verify clean compilation with no warnings

**Checkpoint**: Empty window opens on all three Macs. Cmd-Q quits cleanly.

---

## Phase 2: Core Subsystems (Blocking Prerequisites)

**Purpose**: Implement the foundational modules that all screens depend on.

**CRITICAL**: No screen implementation can begin until these are complete.

- [x] T008 [P] Create `include/input.h` and `src/input.c` — GetKeys() polling, Input_Init/Poll/IsKeyDown (from plan Phase 3)
- [x] T009 [P] Create `include/tilemap.h` and `src/tilemap.c` — TileMap struct, Init/GetTile/SetTile/IsSolid/PixelToCol/ColToPixel (from plan Phase 2)
- [x] T010 [P] Create `include/renderer.h` and `src/renderer.c` — GWorld double-buffering, Init/Shutdown/DrawFrame/BlitToWindow/RebuildBackground. Detect screen dimensions via screenBits.bounds at init: use 16x16 tiles on 512x342 (Mac SE, PICT IDs 200+), 32x32 tiles on 640x480+ (color Macs, PICT IDs 128+). Load tile sheet, player sprites, bomb, and explosion PICTs into GWorlds at init. Draw tiles/sprites via CopyBits from sheet GWorld. Fall back to colored rectangles if PICT loading fails (FR-017, FR-018). See contracts/asset-pipeline.md for loading pattern.
- [x] T011 [P] Create `include/player.h` and `src/player.c` — Player struct, Init/Update/GetState, grid-locked movement with cooldown, collision via TileMap_IsSolid (from plan Phase 3)
- [x] T012 [P] Create `include/net.h` and `src/net.c` — PeerTalk wrapper: Net_Init/Shutdown/Poll/StartDiscovery/StopDiscovery, register all 7 message types (no MSG_PLAYER_INFO), register all callbacks, implement Net_ComputeLocalPlayerID (IP sort) (from contracts/network-protocol.md)
- [x] T013 Create `include/screens.h` and `src/screens.c` — Screen state machine: ScreenState enum, Screens_Init/TransitionTo/Update/Draw, dispatch to current screen's functions
- [x] T014 Create `include/bomb.h` and `src/bomb.c` — Bomb struct, Bomb_Init/PlaceAt/Update/GetActive, fuse countdown, explosion cross pattern, block destruction

**Checkpoint**: All subsystems compile. Core game logic works in isolation.

---

## Phase 3: User Story 1 — Game Boots and Shows Menu (P1) MVP

**Goal**: App launches on all three Macs, shows loading screen, transitions to menu.

**Independent Test**: Launch BomberTalk, see loading screen, see menu, quit cleanly.

- [x] T015 [P] [US1] Create `src/screen_loading.c` — Loading_Init/Update/Draw: draw "BomberTalk" title text centered, draw "Loading..." text, auto-transition to menu after 120 ticks (2 seconds)
- [x] T016 [P] [US1] Create `src/screen_menu.c` — Menu_Init/Update/Draw: draw two options ("Play", "Quit"), keyboard navigation with up/down arrows, Enter/Return to select, highlight current selection
- [x] T017 [US1] Wire screens into main.c — Replace bare window with Screens_Init(SCREEN_LOADING), call Screens_Update/Draw from main loop, handle Quit selection from menu
- [x] T018 [US1] Build all three targets, verify loading screen and menu work

**Checkpoint**: BomberTalk launches on all three Macs, shows loading -> menu -> quit works.

---

## Phase 4: User Story 3 — Single-Player Movement and Map (P3)

**Goal**: Player moves around the Bomberman grid with arrow keys. Map renders correctly.

**Note**: Doing US3 before US2 because local gameplay must work before we add networking.

**Independent Test**: Start game, move with arrows, walls block movement.

- [x] T019 [P] [US3] Create `src/screen_game.c` — Game_Init/Update/Draw: initialize tilemap, player, renderer. Update: poll input, update player, update bombs. Draw: render frame to window.
- [x] T020 [US3] Wire "Play" menu selection to transition to SCREEN_LOBBY (or SCREEN_GAME for single-player testing with local player at spawn point (1,1))
- [x] T021 [US3] Verify tile map renders correctly with colored rectangles (green floor, gray walls, brown blocks, light green spawns)
- [x] T022 [US3] Verify player movement: arrow keys move 1 tile, cooldown prevents too-fast movement, walls and blocks reject movement
- [x] T023 [US3] Build and test on all three targets

**Checkpoint**: Single player can move around the Bomberman grid on all three Macs.

---

## Phase 5: User Story 2 — Player Discovery and Lobby (P2)

**Goal**: Players discover each other on LAN and connect via PeerTalk.

**Independent Test**: Launch on 2+ Macs, see each other in lobby, any player starts game.

- [x] T024 [US2] Create `src/screen_lobby.c` — Lobby_Init/Update/Draw: show local player name, list discovered peers (from PT_OnPeerDiscovered), show "Searching..." while discovering, show "Start" option enabled when 2+ peers connected
- [x] T025 [US2] Implement Net_Init with player name from machine name or hardcoded default. Start discovery when entering lobby.
- [x] T026 [US2] Implement on_peer_discovered callback — add discovered peer to lobby list, display name via PT_PeerName
- [x] T027 [US2] Implement on_peer_lost callback — remove peer from lobby list
- [x] T028 [US2] Add "Start Game" option for any player — connects to all discovered peers via Net_ConnectToAllPeers, broadcasts MSG_GAME_START. Enabled only when 2+ peers connected. No host concept.
- [x] T029 [US2] Implement on_connected callback — mark peer as connected
- [x] T030 [US2] Implement on_game_start message handler — compute player IDs via Net_ComputeLocalPlayerID (IP sort), assign spawn corners, transition to game screen. Ignore duplicate MSG_GAME_START.
- [x] T031 [US2] Wire "Play" menu selection to transition to SCREEN_LOBBY
- [x] T032 [US2] Build and test with 2 Macs on same LAN (verify cross-platform: 68k MacTCP <-> PPC OT)

**Checkpoint**: Two+ Macs discover each other, connect, and transition to gameplay together.

---

## Phase 6: User Story 4 — Multiplayer Position Sync (P4)

**Goal**: Players see each other's movement in real-time across the network.

**Independent Test**: Two Macs in game, player A moves, player B sees it.

- [x] T033 [US4] Implement Net_SendPosition — broadcast MSG_POSITION via PT_FAST when local player moves
- [x] T034 [US4] Implement on_position callback — update remote player's gridCol/gridRow/facing from received message
- [x] T035 [US4] Modify screen_game.c to render all active players (not just local) — each player drawn as colored rectangle with different colors per playerID
- [x] T036 [US4] Implement on_disconnected callback — set disconnected player's active=FALSE, remove from rendering
- [x] T037 [US4] Handle player spawn positions — computed locally from player ID (IP sort): 0=(1,1), 1=(13,1), 2=(1,11), 3=(13,11). No network message needed.
- [x] T038 [US4] Build and test with 3 Macs — verify position sync works across 68k MacTCP, PPC MacTCP, PPC OT

**Checkpoint**: 3 players see each other move in real-time across all three networking stacks.

---

## Phase 7: User Story 5 — Bombs and Explosions (P5)

**Goal**: Players place bombs that explode and destroy blocks.

**Independent Test**: Place bomb, wait 3 seconds, blocks destroyed, players killed.

- [x] T039 [US5] Implement bomb placement — spacebar places bomb at current tile if no bomb there, decrement bombsAvailable
- [x] T040 [US5] Implement Net_SendBombPlaced — broadcast via PT_RELIABLE when bomb placed
- [x] T041 [US5] Implement on_bomb_placed callback — create bomb on remote clients
- [x] T042 [US5] Implement bomb fuse countdown in Bomb_Update — decrement timer each frame
- [x] T043 [US5] Implement explosion logic — cross pattern (up/down/left/right) with range, stop at walls
- [x] T044 [US5] Implement block destruction — explosion hits TILE_BLOCK -> set to TILE_FLOOR, broadcast MSG_BLOCK_DESTROYED, rebuild background GWorld
- [x] T045 [US5] Implement player kill detection — if player position in explosion radius, set alive=FALSE, broadcast MSG_PLAYER_KILLED
- [x] T046 [US5] Implement explosion rendering — flash affected tiles red/orange for ~10 frames
- [x] T047 [US5] Implement bomb rendering — draw bomb as dark circle/rectangle on tile
- [x] T048 [US5] Implement on_bomb_explode, on_block_destroyed, on_player_killed message handlers
- [x] T049 [US5] Build and test multiplayer bomb gameplay across all three Macs

**Checkpoint**: Full Bomberman gameplay loop works across 3 Macs — the PeerTalk SDK demo is complete.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Final cleanup and verification.

- [x] T050 Add game over detection — last player standing wins, broadcast MSG_GAME_OVER, transition all players back to lobby screen preserving existing PeerTalk connections (FR-016)
- [x] T051 Add error handling — PT_OnError callback shows alert dialog, graceful recovery from network errors
- [ ] T052 Verify Mac SE performance: memory usage under 1 MB (FreeMem after init) AND frame rate 10+ fps during 4-player gameplay (measure via TickCount delta per frame, per SC-003 and SC-006)
- [ ] T053 Test all edge cases: 4-player game, simultaneous bombs, disconnect mid-game, rejoin lobby after game
- [x] T054 Code cleanup — ensure all C89 clean, no warnings on any target, consistent style
- [x] T055 Update quickstart.md with any build changes discovered during development

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Core Subsystems)**: Depends on Phase 1 — BLOCKS all screens
- **Phase 3 (US1 - Menu)**: Depends on Phase 2 — first deliverable
- **Phase 4 (US3 - Single Player)**: Depends on Phase 3 — needs screen system
- **Phase 5 (US2 - Lobby)**: Depends on Phase 3 — needs screen system and net.c
- **Phase 6 (US4 - Multiplayer Sync)**: Depends on Phase 4 + Phase 5
- **Phase 7 (US5 - Bombs)**: Depends on Phase 6
- **Phase 8 (Polish)**: Depends on all prior phases

### Within Each Phase

- Tasks marked [P] can run in parallel (different files)
- Unmarked tasks are sequential
- Build-and-test tasks (T007, T018, T023, T032, T038, T049) are phase gates

### Parallel Opportunities

```
Phase 2: T008, T009, T010, T011, T012, T014 all in parallel (different files)
Phase 3: T015, T016 in parallel (different files)
Phase 5: T024-T027 partially parallel (lobby UI vs callbacks)
Phase 7: T039-T041 partially parallel, T042-T048 sequential (explosion chain)
```

## Implementation Strategy

### MVP First (Phase 1-3)

1. Get the project building on all three targets
2. Implement core subsystems
3. Deliver loading screen + menu = **first demo on real hardware**

### Incremental Delivery

4. Add single-player gameplay = **playable on one Mac**
5. Add lobby + discovery = **PeerTalk showcase begins**
6. Add multiplayer sync = **multiple Macs playing together**
7. Add bombs = **full Bomberman gameplay**

Each phase is independently demonstrable on real hardware.
