# Tasks: Wall-Clock Timers & Log Cleanup

**Input**: Design documents from `/specs/007-timer-fixes/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: No automated tests — verification is manual hardware testing with log analysis.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Change GameState field types and names that all timer conversions depend on

- [x] T001 Rename and retype four timer fields in GameState struct in include/game.h: `short disconnectGraceTimer` → `unsigned long disconnectGraceStart`, `short gameOverFailsafeTimer` → `unsigned long gameOverFailsafeStart`, `short gameOverTimeout` → `unsigned long gameOverTimeoutStart`, `short meshStaggerTimer` → `unsigned long meshStaggerStart`. Update field comments to document "0 = inactive, non-zero = TickCount() when started".
- [x] T002 Update timer reset assignments in src/main.c (~lines 302-307): change `gGame.disconnectGraceTimer = 0` to `gGame.disconnectGraceStart = 0` for all four renamed fields.
- [x] T003 Update timer reset assignments in src/screen_lobby.c Lobby_Init (~lines 49-53): change `gGame.disconnectGraceTimer = 0` to `gGame.disconnectGraceStart = 0` for all four renamed fields.

**Checkpoint**: Code compiles with field renames but timer logic is still referencing old names (compiler errors expected in screen_game.c, screen_lobby.c, net.c until subsequent phases complete)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: No additional foundational work needed — Phase 1 covers all shared changes.

---

## Phase 3: User Story 1 — Grace Period Consistency (Priority: P1) 🎯 MVP

**Goal**: The disconnectGraceTimer fires at consistent wall-clock time (~1.5s) regardless of frame rate.

**Independent Test**: 2-player game (SE + PPC), end game, both disconnect with `reason: 0` within ~1.5s. Check clog timestamps: elapsed ticks between "starting grace period" and "Grace period complete" should be ~90 on all machines.

### Implementation for User Story 1

- [x] T004 [US1] Convert disconnectGraceTimer start points in src/screen_game.c: change all `gGame.disconnectGraceTimer = DISCONNECT_GRACE_TICKS` (~lines 245, 262, 295) to `gGame.disconnectGraceStart = TickCount()`.
- [x] T005 [US1] Convert disconnectGraceTimer check in src/screen_game.c Game_Update (~lines 67-78): replace `if (gGame.disconnectGraceTimer > 0) { gGame.disconnectGraceTimer -= gGame.deltaTicks; if (gGame.disconnectGraceTimer <= 0)` with `if (gGame.disconnectGraceStart != 0) { if (TickCount() - gGame.disconnectGraceStart >= DISCONNECT_GRACE_TICKS)`. Remove the deltaTicks decrement line. Keep the body (CLOG_INFO, state reset, Net_StopDiscovery, Net_DisconnectAllPeers, Screens_TransitionTo) unchanged.

**Checkpoint**: Grace period fires at wall-clock time on all machines. Build all three targets.

---

## Phase 4: User Story 2 — Consistent Failsafe and Timeout Timers (Priority: P1)

**Goal**: The gameOverFailsafeTimer (~2s) and gameOverTimeout (~3s) fire at consistent wall-clock time.

**Independent Test**: Kill authority mid-game. Non-authority SE triggers failsafe within ~2s (check clog timestamps). Pending game-over with death anims times out within ~3s.

### Implementation for User Story 2

- [x] T006 [US2] Convert gameOverFailsafeTimer start in src/net.c: change `gGame.gameOverFailsafeTimer = 0` (~line 257) to `gGame.gameOverFailsafeStart = 0`, and the start assignment in src/screen_game.c (~line 290) from `gGame.gameOverFailsafeTimer = GAME_OVER_FAILSAFE_TICKS` to `gGame.gameOverFailsafeStart = TickCount()`.
- [x] T007 [US2] Convert gameOverFailsafeTimer check in src/screen_game.c (~lines 232-249): replace `gGame.gameOverFailsafeTimer -= gGame.deltaTicks; if (!gGame.pendingGameOver && (authorityGone || gGame.gameOverFailsafeTimer <= 0))` with `if (!gGame.pendingGameOver && (authorityGone || (gGame.gameOverFailsafeStart != 0 && TickCount() - gGame.gameOverFailsafeStart >= GAME_OVER_FAILSAFE_TICKS)))`. Remove the deltaTicks decrement line.
- [x] T008 [US2] Convert gameOverTimeout start in src/net.c: change `gGame.gameOverTimeout = GAME_OVER_TIMEOUT_TICKS` (~line 253) to `gGame.gameOverTimeoutStart = TickCount()`.
- [x] T009 [US2] Convert gameOverTimeout check in src/screen_game.c (~lines 253-264): replace `gGame.gameOverTimeout -= gGame.deltaTicks; if (!anyDying || gGame.gameOverTimeout <= 0)` with `if (!anyDying || (gGame.gameOverTimeoutStart != 0 && TickCount() - gGame.gameOverTimeoutStart >= GAME_OVER_TIMEOUT_TICKS))`. Remove the deltaTicks decrement line. Keep the timeout warning log unchanged.

**Checkpoint**: Failsafe and timeout fire at wall-clock time. Build all three targets.

---

## Phase 5: User Story 3 — Predictable Mesh Stagger (Priority: P2)

**Goal**: The meshStaggerTimer fires at consistent wall-clock time (~0.5s per rank) regardless of frame rate.

**Independent Test**: 3-player game, observe mesh formation logs. Rank 1 stagger completes in ~0.5s, rank 2 in ~1.0s regardless of machine.

### Implementation for User Story 3

- [x] T010 [US3] Convert meshStaggerTimer start in src/screen_lobby.c (~line 147): change `gGame.meshStaggerTimer = stagger` to `gGame.meshStaggerStart = TickCount()`. Compute stagger threshold at check time via `Net_GetLocalRank() * MESH_STAGGER_PER_RANK` (avoids adding a new GameState field; Net_GetLocalRank is already available and rank doesn't change during a session).
- [x] T011 [US3] Convert meshStaggerTimer check in src/screen_lobby.c (~lines 100-108): replace `if (gGame.meshStaggerTimer > 0) { gGame.meshStaggerTimer -= gGame.deltaTicks; if (gGame.meshStaggerTimer <= 0)` with `if (gGame.meshStaggerStart != 0) { unsigned long stagger = (unsigned long)(Net_GetLocalRank() * MESH_STAGGER_PER_RANK); if (TickCount() - gGame.meshStaggerStart >= stagger)`. Note: C89 requires variable declarations at block start — declare `stagger` at top of the enclosing block. Remove the deltaTicks decrement line. Change `gGame.meshStaggerTimer = 0` reset (~line 104) to `gGame.meshStaggerStart = 0`.

**Checkpoint**: Mesh stagger fires at wall-clock time. Build all three targets.

---

## Phase 6: User Story 4 — Reduced Log Noise (Priority: P3)

**Goal**: Noisy expected-behavior log messages are downgraded or documented.

**Independent Test**: Play a 3-player game with multiple bombs. Confirm no CLOG_INFO "fuse expired locally" messages in log output. Confirm KI-003 is documented in known-issues.md.

### Implementation for User Story 4

- [x] T012 [P] [US4] Downgrade bomb fuse-expiry log in src/bomb.c (~line 213): change `CLOG_INFO("Bomb at (%d,%d) fuse expired locally "` to `CLOG_DEBUG("Bomb at (%d,%d) fuse expired locally "`.
- [x] T013 [P] [US4] Update KI-003 in notes/known-issues.md: add a note under KI-003 stating that -3155 (kOTLookErr) originates in PeerTalk's OT transport layer, not BomberTalk code, and no BomberTalk code change is needed. The log can only be addressed in PeerTalk itself.

**Checkpoint**: Log output is cleaner. No code changes needed for KI-003.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Documentation and final verification

- [x] T014 Update CLAUDE.md timing model section: document that coordination timers (grace, failsafe, timeout, mesh stagger) now use TickCount() wall-clock pattern, while gameplay timers (fuse, explosion, movement) continue using deltaTicks. Update field names in any GameState references.
- [x] T015 Build all three targets (68k MacTCP, PPC MacTCP, PPC OT) and verify clean compilation with no new warnings.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: N/A
- **Phase 3 (US1)**: Depends on Phase 1 completion
- **Phase 4 (US2)**: Depends on Phase 1 completion. Can run in parallel with Phase 3 (different code sections in screen_game.c, but same file — sequential is safer)
- **Phase 5 (US3)**: Depends on Phase 1 completion. Can run in parallel with Phases 3-4 (different file: screen_lobby.c)
- **Phase 6 (US4)**: No dependencies on Phases 3-5. Can run in parallel (different files: bomb.c, known-issues.md)
- **Phase 7 (Polish)**: Depends on all previous phases

### User Story Dependencies

- **US1 (Grace Period)**: Depends on Phase 1 field renames only
- **US2 (Failsafe/Timeout)**: Depends on Phase 1 field renames only. Independent of US1.
- **US3 (Mesh Stagger)**: Depends on Phase 1 field renames only. Independent of US1/US2.
- **US4 (Log Noise)**: Fully independent of all other stories and Phase 1.

### Parallel Opportunities

- T012 and T013 (US4) can run in parallel with each other and with any other phase
- T002 and T003 (Phase 1 resets) can run in parallel with each other
- Phase 5 (screen_lobby.c) can run in parallel with Phases 3-4 (screen_game.c) after Phase 1

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Field renames in game.h, main.c, screen_lobby.c
2. Complete Phase 3: Grace period conversion in screen_game.c
3. **STOP and VALIDATE**: Build all three targets, test grace period on hardware

### Incremental Delivery

1. Phase 1 → Field renames compile clean
2. Phase 3 (US1) → Grace period wall-clock → Build & test (MVP)
3. Phase 4 (US2) → Failsafe/timeout wall-clock → Build & test
4. Phase 5 (US3) → Mesh stagger wall-clock → Build & test
5. Phase 6 (US4) → Log cleanup → Build & test
6. Phase 7 → Documentation, final build verification

---

## Notes

- All timer conversions follow the same mechanical pattern (research.md R1-R3)
- C89 constraint: declare `unsigned long` variables at block top, not inline
- No protocol version bump — all changes are local timing, no wire format impact
- TickCount() wrap-around after ~18.6 hours handled correctly by unsigned subtraction
