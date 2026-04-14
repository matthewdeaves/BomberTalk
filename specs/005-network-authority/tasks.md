# Tasks: Network Authority & Robustness Improvements

**Input**: Design documents from `/specs/005-network-authority/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/network-protocol.md, quickstart.md

**Tests**: No automated tests (manual hardware testing on 3 Classic Macs). Verification via debug log analysis.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Constants & State)

**Purpose**: Add new constants and GameState fields needed by all user stories

- [x] T001 Bump BT_PROTOCOL_VERSION from 4 to 5 in include/game.h
- [x] T002 Add new timing constants to include/game.h: DISCONNECT_GRACE_TICKS (30), MESH_STAGGER_PER_RANK (30), GAME_OVER_FAILSAFE_TICKS (60), LOW_HEAP_WARNING_BYTES (262144L), HEAP_CHECK_INTERVAL_TICKS (1800)
- [x] T003 Add new fields to GameState struct in include/game.h: disconnectGraceTimer (short), meshStaggerTimer (short), gameOverAuthority (int), localGameOverDetected (int), gameOverFailsafeTimer (short), heapCheckTimer (long)
- [x] T004 Initialize all new GameState fields to zero/FALSE in InitGameState() in src/main.c

**Checkpoint**: All three targets compile cleanly with new constants and fields (unused so far).

---

## Phase 2: Foundational (Net_IsLowestRankConnected)

**Purpose**: Core helper function needed by US2 (game-over authority)

- [x] T005 Declare Net_IsLowestRankConnected(void) returning int in include/net.h
- [x] T006 Implement Net_IsLowestRankConnected() in src/net.c. Use PT_GetPeerRank(gPTCtx, NULL) for local rank. Iterate all peers via PT_GetPeerCount/PT_GetPeer, checking PT_GetPeerState == PT_PEER_CONNECTED, and find the minimum rank among connected peers. Return TRUE if local rank is the minimum (or if no peers are connected). Add CLOG_DEBUG logging of the authority decision.

**Checkpoint**: All three targets compile. Net_IsLowestRankConnected() exists but is not yet called.

---

## Phase 3: User Story 1 - Owner-Authoritative Bomb Explosions (Priority: P1)

**Goal**: Only the bomb owner broadcasts MSG_BOMB_EXPLODE and MSG_BLOCK_DESTROYED when a fuse expires. Chain explosions propagate the parent's broadcast flag. Reduces TCP reliable traffic by ~67%.

**Independent Test**: Play a 3-player game. In debug logs, verify each bomb explosion produces exactly 1 TX bomb-explode message (from the owner's machine). Other machines show local explosion processing without TX. Force-explode still works on slow machines.

### Implementation

- [x] T007 [US1] In Bomb_Update() in src/bomb.c, change the fuse-expiry call from `ExplodeBomb(&gGame.bombs[i], TRUE)` to `ExplodeBomb(&gGame.bombs[i], gGame.localPlayerID >= 0 && gGame.localPlayerID == (short)gGame.bombs[i].ownerID)`. This makes only the bomb owner broadcast the explosion. Non-owners explode locally with broadcast=FALSE.
- [x] T008 [US1] Add a code comment in ExplodeBomb() in src/bomb.c noting that chain explosions (bomb A's raycast triggering bomb B) are not currently implemented. The raycast checks for walls and blocks only. If chain explosions are added in the future, the broadcast flag must propagate from the parent explosion to chained bombs (owner's chain = broadcast TRUE, non-owner's chain = broadcast FALSE).
- [x] T009 [US1] Add a CLOG_INFO line in Bomb_Update() in src/bomb.c when a fuse expires on a non-owner machine, logging "Bomb at (%d,%d) fuse expired locally (owner=P%d, not local P%d)" to distinguish owner vs non-owner explosions in debug logs.
- [x] T010 [US1] Build all three targets (68k, PPC OT, PPC MacTCP) and verify clean compilation with the bomb authority changes.

**Checkpoint**: Bomb explosions are owner-authoritative. Block destruction (MSG_BLOCK_DESTROYED) follows automatically — the existing `if (broadcast) Net_SendBlockDestroyed()` in ExplodeBomb is gated by the same broadcast parameter changed in T007. Verify in logs: block-destroyed TX only appears on the bomb owner's machine. Force-explode path unchanged (Bomb_ForceExplodeAt always passes broadcast=FALSE).

---

## Phase 4: User Story 2 - Authority-Based Game Over (Priority: P1)

**Goal**: Only the lowest-rank connected player broadcasts MSG_GAME_OVER. Non-authority machines use a 60-tick failsafe timeout.

**Independent Test**: Play a 3-player game to completion. In debug logs, verify exactly 1 TX game-over message (from the lowest-rank connected player). Test authority fallback by having rank 0 disconnect before game over.

### Implementation

- [x] T011 [US2] In the local game-over detection block in Game_Update() in src/screen_game.c (the `if (!anyDying && aliveCount <= 1 && gGame.numPlayers > 1)` block around line 230), add an authority check: call Net_IsLowestRankConnected(). If TRUE (authority), send Net_SendGameOver(winner) as before. If FALSE (non-authority), set gGame.localGameOverDetected = TRUE and gGame.gameOverFailsafeTimer = GAME_OVER_FAILSAFE_TICKS. Log the authority decision at CLOG_INFO level.
- [x] T012 [US2] Add failsafe timer logic in Game_Update() in src/screen_game.c. Before the game-over detection block, add a check: if gGame.localGameOverDetected is TRUE, decrement gameOverFailsafeTimer by deltaTicks. If it reaches 0 and pendingGameOver is still FALSE (no network game-over received), send Net_SendGameOver with the locally computed winner and log "Game over failsafe: authority timeout, sending as backup". Then proceed with the normal game-over transition.
- [x] T013 [US2] In the on_game_over handler in src/net.c, after setting gGame.pendingGameOver = TRUE, also set gGame.localGameOverDetected = FALSE and gGame.gameOverFailsafeTimer = 0 to cancel any active failsafe timer (the authority's message arrived, no need for backup).
- [x] T014 [US2] Clear localGameOverDetected and gameOverFailsafeTimer in Lobby_Init() in src/screen_lobby.c alongside the existing pendingGameOver/gameStartReceived resets.
- [x] T015 [US2] Build all three targets and verify clean compilation with game-over authority changes.

**Checkpoint**: Game over is authority-based with failsafe. Combined with US1, TCP reliable messages reduced by ~67%.

---

## Phase 5: User Story 3 - Graceful Game-Over Shutdown (Priority: P2)

**Goal**: After game-over processing completes, wait 30 ticks before TCP teardown. Eliminates Mac SE disconnect reason 2.

**Independent Test**: Play a 3-player game to completion. Check Mac SE logs for disconnect reason 0 (clean) instead of 2 (connection lost).

### Implementation

- [x] T016 [US3] Refactor the local game-over transition in Game_Update() in src/screen_game.c. Instead of immediately calling Net_StopDiscovery() + Net_DisconnectAllPeers() + Screens_TransitionTo(SCREEN_LOBBY) after detecting game over, set gGame.disconnectGraceTimer = DISCONNECT_GRACE_TICKS and gGame.gameRunning = FALSE. Log "Game over: starting grace period (%d ticks)" at CLOG_INFO.
- [x] T017 [US3] Refactor the pending (remote) game-over transition in Game_Update() in src/screen_game.c. Same change: when pending game over resolves (death anims done or timeout), instead of immediate disconnect, set disconnectGraceTimer = DISCONNECT_GRACE_TICKS. Do NOT call disconnect yet.
- [x] T018 [US3] Add grace period handler at the top of Game_Update() in src/screen_game.c. If disconnectGraceTimer > 0, decrement by deltaTicks. If it reaches 0, call Net_StopDiscovery() + Net_DisconnectAllPeers() + Screens_TransitionTo(SCREEN_LOBBY) and return. While the timer is active, return early after decrementing (skip all gameplay logic — game is already over, just waiting for TCP flush). Note: Net_Poll() runs in MainLoop() (src/main.c) before Screens_Update(), so TCP buffers continue to flush during the grace period without any additional code. Verify this call order in MainLoop() and add a comment documenting the dependency.
- [x] T019 [US3] Clear disconnectGraceTimer in Lobby_Init() in src/screen_lobby.c alongside the other game-over state resets.
- [x] T020 [US3] Build all three targets and verify clean compilation.

**Checkpoint**: Game-over transitions include a 0.5-second grace period. Mac SE should see clean disconnects.

---

## Phase 6: User Story 4 - Staggered Mesh Connect (Priority: P2)

**Goal**: After receiving MSG_GAME_START, delay first TCP connect by rank * 30 ticks. Eliminates 6200 TCP connect failure.

**Independent Test**: Play 4 consecutive 3-player games. Verify zero TCP connect failures in 6200 logs during mesh formation.

### Implementation

- [x] T021 [US4] Add Net_GetLocalRank() helper in src/net.c (declare in include/net.h): returns (short)PT_GetPeerRank(gPTCtx, NULL) or 0 if gPTCtx is NULL. This avoids calling Net_ComputeLocalPlayerID() (which assigns peer pointers) prematurely in the stagger path.
- [x] T022 [US4] In the receiver path in Lobby_Update() in src/screen_lobby.c (the `if (gGame.gameStartReceived && !gWaitingForMesh)` block around line 125), instead of immediately setting gWaitingForMesh = TRUE and connecting, compute the stagger delay using Net_GetLocalRank(). Set gGame.meshStaggerTimer = rank * MESH_STAGGER_PER_RANK. Set gWaitingForMesh = TRUE and gConnectStartTick = TickCount(). Log "Mesh stagger: rank %d, delay %d ticks" at CLOG_INFO.
- [x] T023 [US4] Add stagger timer logic at the top of the gWaitingForMesh block in Lobby_Update() in src/screen_lobby.c. If gGame.meshStaggerTimer > 0, decrement by gGame.deltaTicks and return (skip the connect and retry logic). When it reaches 0, call Net_ConnectToAllPeers() for the first time and log "Mesh stagger complete, connecting". After this, the existing 2-second retry and 15-second timeout handle the rest unchanged.
- [x] T024 [US4] Clear meshStaggerTimer in Lobby_Init() in src/screen_lobby.c.
- [x] T025 [US4] Build all three targets and verify clean compilation. Confirm the 2-second mesh retry code path (TickCount() - gLastMeshRetryTick > 120) is unmodified — the stagger only delays the first connect, not the retry mechanism.

**Checkpoint**: Mesh formation uses rank-based stagger. Existing retry and timeout unchanged.

---

## Phase 7: User Story 5 - Heap Monitoring (Priority: P3)

**Goal**: Log warnings when free heap is below 256KB at init and periodically during gameplay.

**Independent Test**: Build and run on 6200. Verify heap report in init logs. Temporarily lower threshold to test warning trigger.

### Implementation

- [x] T026 [P] [US5] In src/main.c, after the existing "Free heap after init" CLOG_INFO line (around line 345), add a conditional: if FreeMem() < LOW_HEAP_WARNING_BYTES, log CLOG_WARN("Low heap warning: %ld bytes free (threshold: %ld)", FreeMem(), (long)LOW_HEAP_WARNING_BYTES).
- [x] T027 [P] [US5] In the MainLoop() function in src/main.c, add periodic heap checking inside the frame update block. Increment gGame.heapCheckTimer by gGame.deltaTicks. If heapCheckTimer >= HEAP_CHECK_INTERVAL_TICKS, reset to 0 and check FreeMem(). If below LOW_HEAP_WARNING_BYTES, log CLOG_WARN with the current free amount. Only check when currentScreen == SCREEN_GAME (avoid noise during menu/lobby).
- [x] T028 [US5] Build all three targets and verify clean compilation. The 6200 (462KB free) should NOT trigger the warning with the 256KB default threshold.

**Checkpoint**: Heap monitoring active. No false positives on current hardware.

---

## Phase 8: User Story 6 - PeerTalk Drain Error Log Levels (Priority: P3)

**Goal**: Document benign drain errors and file a PeerTalk SDK issue.

**Independent Test**: After PeerTalk change is applied, verify drain errors appear at DBG level in logs.

### Implementation

- [x] T029 [P] [US6] Create a PeerTalk issue document at specs/005-network-authority/peertalk-drain-errors.md documenting: (1) OTRcv returning -3155 (kOTNoDataErr) during TCP drain — recommend CLOG_DBG, (2) TCPRcv returning -23008 (connectionDoesntExist) during MacTCP cleanup — recommend CLOG_DBG, (3) OT endpoint yielding T_DISCONNECT during drain — recommend CLOG_DBG. Include reproduction steps (play 3-player game to completion, check logs) and proposed PeerTalk code locations (pt_ot.c, pt_mactcp.c).
- [x] T030 [US6] File the issue on the PeerTalk GitHub repository using `gh issue create` with the content from the document. Tag as "enhancement" and "log-levels".

**Checkpoint**: PeerTalk drain error issue filed. BomberTalk deliverable complete for this story.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final verification, documentation, and CLAUDE.md updates

- [x] T031 Build all three targets clean (68k MacTCP, PPC OT, PPC MacTCP) with all changes applied
- [x] T032 Update CLAUDE.md Network Layer section to document v5 authority model: bomb owner broadcasts explosions (chain cascade), lowest-rank sends game over (failsafe timeout), block-destroyed follows explosion authority
- [x] T033 Update CLAUDE.md Recent Changes section with 005-network-authority summary: owner-authoritative explosions, authority-based game over, graceful shutdown grace period, mesh stagger, heap monitoring, protocol v5
- [x] T034 Update CLAUDE.md Timing Model section to add new constants: DISCONNECT_GRACE_TICKS, MESH_STAGGER_PER_RANK, GAME_OVER_FAILSAFE_TICKS, HEAP_CHECK_INTERVAL_TICKS
- [x] T035 Verify all new code is C89 clean: no // comments, no mixed declarations, no VLAs, no C99 features. Check with `-std=c89 -Wall -Wextra` flags.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 (needs new constants/fields)
- **Phase 3 (US1 - Bomb Authority)**: Depends on Phase 1 only (does not use Net_IsLowestRankConnected)
- **Phase 4 (US2 - Game Over Authority)**: Depends on Phase 2 (needs Net_IsLowestRankConnected)
- **Phase 5 (US3 - Grace Period)**: Depends on Phase 4 (modifies same game-over code path)
- **Phase 6 (US4 - Mesh Stagger)**: Depends on Phase 1 only (independent of US1-US3)
- **Phase 7 (US5 - Heap Monitoring)**: Depends on Phase 1 only (independent of all other stories)
- **Phase 8 (US6 - PeerTalk Issue)**: No code dependencies — can start any time
- **Phase 9 (Polish)**: Depends on all previous phases

### User Story Dependencies

- **US1 (Bomb Authority)** and **US4 (Mesh Stagger)** and **US5 (Heap Monitoring)** and **US6 (PeerTalk Issue)**: All independent — can run in parallel after Phase 1
- **US2 (Game Over Authority)**: Requires Phase 2 (Net_IsLowestRankConnected)
- **US3 (Grace Period)**: Requires US2 (modifies same game-over transition code)

### Within Each User Story

- Implementation tasks are sequential (same file modifications)
- Build verification is always the last task in each phase

### Parallel Opportunities

After Phase 1 completes, these can run in parallel:
- US1 (bomb.c changes)
- US4 (screen_lobby.c changes)
- US5 (main.c changes)
- US6 (documentation only)

After Phase 2 completes:
- US2 (screen_game.c + net.c changes)

After US2 completes:
- US3 (screen_game.c changes, same code path as US2)

---

## Parallel Example: After Phase 1

```
Agent 1: T007-T010 (US1 - bomb.c owner-authoritative explosions)
Agent 2: T021-T025 (US4 - net.c helper + screen_lobby.c mesh stagger)
Agent 3: T026-T028 (US5 - main.c heap monitoring)
Agent 4: T029-T030 (US6 - PeerTalk drain error documentation)
```

---

## Implementation Strategy

### MVP First (US1 Only)

1. Complete Phase 1: Setup (T001-T004)
2. Complete Phase 3: US1 - Bomb Authority (T007-T010)
3. **STOP and VALIDATE**: Deploy to 3 Macs, play a game, verify 1 TX per explosion in logs
4. This alone delivers the highest-impact improvement (~67% TCP traffic reduction for explosions)

### Incremental Delivery

1. Phase 1 + Phase 3 (US1) → Bomb authority (MVP)
2. Phase 2 + Phase 4 (US2) → Game over authority
3. Phase 5 (US3) → Graceful shutdown
4. Phase 6 (US4) → Mesh stagger (can be done before/after US2-US3)
5. Phase 7 (US5) → Heap monitoring (independent)
6. Phase 8 (US6) → PeerTalk issue (independent)
7. Phase 9 → Polish and documentation

---

## Notes

- All tasks modify existing files — no new source files created
- C89 compliance required for all changes (no // comments, no mixed declarations)
- Build all 3 targets after each phase to catch platform-specific issues early
- Debug log analysis via `socat -u UDP-RECV:7356,reuseaddr -` is the primary verification method
- Protocol version 5 is backward-compatible with v4 clients (no wire format changes)
