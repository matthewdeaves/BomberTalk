# Feature Specification: Network Authority & Robustness Improvements

**Feature Branch**: `005-network-authority`
**Created**: 2026-04-14
**Status**: Draft
**Input**: 4-game 3-player test session findings across Mac SE (68k MacTCP), Performa 6200 (PPC MacTCP), Performa 6400 (PPC OT). Seven issues identified spanning mesh formation timing, redundant message traffic, graceful shutdown coordination, heap monitoring, and log noise.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Owner-Authoritative Bomb Explosions (Priority: P1)

When a bomb's fuse expires, only the player who placed it broadcasts the explosion message. All other machines still compute the explosion locally for responsiveness but do not send redundant messages. The existing force-explode sync mechanism continues to work as a safety net for slow machines.

**Why this priority**: Eliminates 3x redundant TCP traffic for every explosion in every game. This is the highest-impact change because explosions are the most frequent reliable message and the redundancy compounds with player count. Also naturally resolves the force-explode bias observed on the 6200 (Issue 5).

**Independent Test**: Play a 3-player game. Monitor debug logs (`socat -u UDP-RECV:7356,reuseaddr -`). Verify that for each bomb explosion, only one TX bomb-explode message appears across all machines (from the bomb owner). Other machines should show local explosion processing without TX. Force-explode should still trigger on slow machines when the owner's message arrives before their local fuse expires.

**Acceptance Scenarios**:

1. **Given** a 3-player game with player 0 placing a bomb, **When** the fuse expires, **Then** only player 0's machine sends MSG_BOMB_EXPLODE; the other two machines explode the bomb locally without broadcasting
2. **Given** player 1 places a bomb and player 1's machine is slower than player 0's, **When** player 0's machine ticks the fuse to zero first, **Then** player 0's machine explodes locally with broadcast=FALSE (not the owner); player 1's machine sends the broadcast when its fuse expires
3. **Given** player 2 places a bomb and disconnects before the fuse expires, **When** the fuse expires on remaining machines, **Then** remaining machines explode locally without broadcasting (no owner to send); game state remains consistent

---

### User Story 2 - Authority-Based Game Over (Priority: P1)

When the game-over condition is detected (one or zero players alive), only the lowest-rank connected player broadcasts MSG_GAME_OVER. Other machines detect game over locally for responsiveness but do not send redundant messages. A safety timeout ensures game over is still broadcast if the authority disconnects.

**Why this priority**: Same redundancy pattern as bomb explosions. Every game currently produces 3 identical game-over messages. Combined with the graceful shutdown fix (Story 3), this ensures clean end-of-game behavior.

**Independent Test**: Play a 3-player game to completion. Monitor debug logs. Verify only one MSG_GAME_OVER is transmitted (from the lowest-rank connected player). If the lowest-rank player is killed and disconnects, verify the next-lowest-rank player sends the message instead.

**Acceptance Scenarios**:

1. **Given** a 3-player game where all 3 machines detect game over simultaneously, **When** game-over condition is met, **Then** only the lowest-rank connected player (rank 0) sends MSG_GAME_OVER
2. **Given** rank 0 player has disconnected before game over, **When** game-over condition is met, **Then** the next-lowest-rank connected player sends MSG_GAME_OVER
3. **Given** a non-authority machine detects game over locally, **When** no MSG_GAME_OVER is received within 60 ticks (~1 second), **Then** that machine sends MSG_GAME_OVER as a failsafe

---

### User Story 3 - Graceful Game-Over Shutdown (Priority: P2)

After detecting game over and sending MSG_GAME_OVER, machines wait a short grace period before tearing down TCP connections. This allows slower machines to receive and process the game-over message before the connection is closed, resulting in clean disconnects (reason 0) instead of connection-lost errors (reason 2).

**Why this priority**: Cosmetic improvement that eliminates noisy disconnect logs on the Mac SE. No gameplay impact but improves log clarity for debugging and demonstrates clean protocol behavior.

**Independent Test**: Play a 3-player game to completion. Check Mac SE logs. Verify disconnect reason is 0 (clean) instead of 2 (connection lost) in all cases.

**Acceptance Scenarios**:

1. **Given** a PPC machine detects game over first, **When** it sends MSG_GAME_OVER, **Then** it waits 30 ticks (~0.5 seconds) before calling disconnect, continuing to poll the network during the grace period
2. **Given** the Mac SE is the slowest machine, **When** PPC machines observe the grace period, **Then** the Mac SE receives and processes MSG_GAME_OVER before TCP teardown begins
3. **Given** all three machines are connected, **When** game over occurs and the grace period elapses, **Then** all machines see disconnect reason 0 (clean) in their logs

---

### User Story 4 - Staggered Mesh Connect (Priority: P2)

After receiving MSG_GAME_START, machines stagger their initial TCP connect attempts based on peer rank. Higher-rank peers wait slightly longer, giving slower machines time to prepare their TCP listener. The existing mesh retry mechanism remains as a safety net.

**Why this priority**: Reduces the recurring TCP connect failure seen on the 6200 in 3 of 4 test games. The failure always recovers via retry, but the stagger eliminates the initial failure and reduces mesh formation time by 100-400ms.

**Independent Test**: Play 4 consecutive 3-player games. Monitor 6200 logs. Verify zero TCP connect failures during mesh formation (previously 3 of 4 games had failures).

**Acceptance Scenarios**:

1. **Given** 3 machines in lobby and game start is initiated, **When** MSG_GAME_START is received, **Then** rank 0 connects immediately, rank 1 waits ~0.5 seconds, rank 2 waits ~1.0 second before first connect attempt
2. **Given** the Mac SE (typically lowest rank due to lowest IP) receives MSG_GAME_START, **When** the stagger delay elapses for higher-rank machines, **Then** the SE's TCP listener is ready and accepts connections without failure
3. **Given** a machine's staggered connect fails despite the delay, **When** the 2-second mesh retry fires, **Then** the connection succeeds on retry (existing safety net unchanged)

---

### User Story 5 - Heap Monitoring (Priority: P3)

The game monitors available heap memory after initialization and periodically during gameplay. When free heap drops below a warning threshold, a log message is emitted. This provides early warning as features are added, particularly for the memory-constrained 6200.

**Why this priority**: Defensive monitoring only. No gameplay impact. The 6200's 280KB heap deficit is not a problem today but could become one as features are added.

**Independent Test**: Build and launch on the 6200. Check init log for heap report. Verify a warning appears if free heap is below the threshold (currently 461KB, above the 256KB default threshold). Artificially lower the threshold to verify the warning triggers.

**Acceptance Scenarios**:

1. **Given** the game initializes on any machine, **When** free heap after init is below the warning threshold, **Then** a warning-level log message is emitted with the exact free byte count
2. **Given** the game is running, **When** free heap drops below the threshold during gameplay, **Then** a warning is logged (checked every ~30 seconds)
3. **Given** free heap is above the threshold, **When** the periodic check runs, **Then** no log output is produced (silent success)

---

### User Story 6 - PeerTalk Drain Error Log Levels (Priority: P3)

Known-benign network drain errors during connection teardown are documented with recommended log level downgrades. These errors are expected during normal cleanup and should not appear at WARNING or ERROR level.

**Why this priority**: Log noise reduction only. These errors are already handled correctly. This is a PeerTalk SDK change, not a BomberTalk change, so the deliverable is documentation and a PeerTalk issue/PR.

**Independent Test**: After the PeerTalk change is applied, play a 3-player game to completion. Verify that OTRcv -3155, TCPRcv -23008, and T_DISCONNECT drain messages no longer appear at WRN/ERR level in logs.

**Acceptance Scenarios**:

1. **Given** a game ends and TCP connections are torn down, **When** OTRcv returns -3155 (no data) during drain, **Then** the message is logged at DEBUG level (not WARNING)
2. **Given** a game ends and MacTCP cleanup runs, **When** TCPRcv returns -23008 (connection doesn't exist), **Then** the message is logged at DEBUG level (not ERROR)
3. **Given** OT endpoint drain encounters a T_DISCONNECT event, **When** the event is consumed, **Then** it is logged at DEBUG level

---

### Edge Cases

- What happens when the bomb owner disconnects mid-fuse? Remaining machines explode locally without broadcasting (no owner to send). All machines still compute identical explosions from their local bomb state.
- What happens when rank 0 disconnects during the game-over grace period? The grace period timer still elapses on remaining machines; they disconnect normally after their own grace period.
- What happens when all machines detect game over at nearly the same time? Only rank 0 sends. If two machines both think they're authority due to a near-simultaneous disconnect of rank 0, deduplication (screen check) prevents double-processing.
- What happens when the mesh stagger delay exceeds the mesh timeout? The stagger is at most ~1.5 seconds for rank 2 in a 4-player game; the mesh timeout is 15 seconds. No conflict.
- What happens when bomb A (Player 0) chains into bomb B (Player 1)? Player 0's machine broadcasts both explosions. Player 1's machine receives force-explode for bomb B and processes locally without re-broadcasting. If Player 1 disconnected, Player 0's machine still broadcasts bomb B's explosion.
- What happens when a v4 client plays with v5 clients? v4 clients still broadcast all explosions and game-overs (old behavior). v5 clients already deduplicate received messages. The extra messages are harmless. No wire format change.

## Clarifications

### Session 2026-04-14

- Q: Who is authoritative for chain explosions (bomb A triggers bomb B owned by a different player)? → A: The chain-triggering machine sends MSG_BOMB_EXPLODE for all bombs in the chain regardless of bomb ownership. The initiator "owns" the entire cascade. Other machines receive force-explode and process locally.
- Q: Should MSG_BLOCK_DESTROYED follow the same owner-authority pattern as MSG_BOMB_EXPLODE? → A: Yes. Block-destroyed follows explosion authority — the same machine that sends MSG_BOMB_EXPLODE also sends the associated MSG_BLOCK_DESTROYED messages. Non-authority machines process block destruction locally without broadcasting.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: When a bomb fuse expires, the system MUST only broadcast MSG_BOMB_EXPLODE if the local player owns the bomb. Note: chain explosions (bomb A's raycast triggering bomb B) are not currently implemented in the codebase. If chain explosions are added in the future, the chain-triggering machine MUST broadcast all chained explosions regardless of bomb ownership.
- **FR-002**: When a non-owner machine's local fuse expires, the system MUST explode the bomb locally without broadcasting.
- **FR-003**: The force-explode mechanism MUST continue to work unchanged as a sync safety net for messages arriving before local fuse expiry
- **FR-003a**: MSG_BLOCK_DESTROYED MUST follow the same authority as its parent explosion — only the machine that broadcasts MSG_BOMB_EXPLODE sends the associated MSG_BLOCK_DESTROYED messages. Non-authority machines process block destruction locally without broadcasting.
- **FR-004**: When game-over is detected, only the lowest-rank connected player MUST broadcast MSG_GAME_OVER
- **FR-005**: If the authority player (lowest rank) has disconnected, the next-lowest-rank connected player MUST assume authority
- **FR-006**: Non-authority machines MUST send MSG_GAME_OVER as a failsafe if no network game-over is received within 60 ticks
- **FR-007**: After sending MSG_GAME_OVER, machines MUST wait 30 ticks before disconnecting TCP, continuing to poll the network during the wait
- **FR-008**: After receiving MSG_GAME_START, machines MUST delay their first TCP connect attempt by (peer_rank * 30) ticks
- **FR-009**: The existing 2-second mesh retry MUST remain unchanged as a safety net. Verify by confirming the retry code path in screen_lobby.c is unmodified after stagger changes.
- **FR-010**: The system MUST log a warning when free heap after initialization is below a configurable threshold
- **FR-011**: The system MUST check free heap periodically during gameplay (~30 seconds) and log a warning if below threshold
- **FR-012**: Known-benign drain errors (OTRcv -3155, TCPRcv -23008, T_DISCONNECT) MUST be documented with recommended DEBUG-level log treatment in a PeerTalk issue
- **FR-013**: Protocol version MUST be bumped to 5 (BT_PROTOCOL_VERSION) to indicate the behavioral change
- **FR-014**: All changes MUST compile cleanly on all three build targets (68k MacTCP, PPC MacTCP, PPC OT)
- **FR-015**: All changes MUST be C89/C90 compliant

### Key Entities

- **Bomb**: Existing entity. Gains owner-authoritative broadcast behavior. `ownerID` field already exists and is used to determine broadcast authority.
- **Game Over Authority**: New concept. Lowest-rank connected player at the time game-over is detected. Determined via existing `PT_GetPeerRank()` mechanism.
- **Disconnect Grace Period**: New timing state. 30-tick countdown after MSG_GAME_OVER before TCP teardown. Tracks whether the local machine is in the grace period.
- **Mesh Stagger Delay**: New timing state. Per-rank delay (rank * 30 ticks) before first TCP connect attempt after MSG_GAME_START.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In a 3-player game, each bomb explosion generates exactly 1 MSG_BOMB_EXPLODE on the network (down from 3)
- **SC-002**: In a 3-player game, game-over generates exactly 1 MSG_GAME_OVER on the network (down from 3)
- **SC-003**: TCP reliable message volume for explosions and block destruction reduced by ~67% (from 3 messages per event to 1)
- **SC-004**: Mac SE shows disconnect reason 0 (clean) in 100% of normal game-over transitions (up from ~25%)
- **SC-005**: TCP connect failures during mesh formation reduced to 0 in a 4-game test session (down from 3 of 4)
- **SC-006**: Free heap is reported at init and monitored during gameplay on all machines, with warnings for low conditions
- **SC-007**: Zero regressions: all existing gameplay, collision, movement, and rendering behavior unchanged
- **SC-008**: All three build targets compile cleanly with no new warnings

## Assumptions

- PeerTalk's `PT_GetPeerRank()` remains stable and deterministic across all platforms during gameplay (not just at game start). If rank changes during gameplay due to disconnects, the authority fallback handles it.
- The 30-tick grace period is sufficient for the Mac SE to receive and process MSG_GAME_OVER. Based on observed Mac SE frame rates of 3-10 fps in lobby and 10-19 fps in gameplay, 30 ticks (~0.5 seconds) provides 5-10 frames of processing time.
- The 30-tick-per-rank stagger is sufficient for the Mac SE to prepare its TCP listener after processing MSG_GAME_START. The current failure recovery takes 100-400ms; 30 ticks (~0.5s) provides comfortable margin.
- v4/v5 client interop works without issues because v5 behavioral changes (fewer broadcasts) are a subset of v4 behavior (all broadcasts), and v5 clients already deduplicate incoming messages.
- The heap warning threshold of 256KB is appropriate. The 6200 currently has ~462KB free, so it won't trigger false warnings. The threshold can be adjusted via compile-time constant.
- PeerTalk drain error log levels are a PeerTalk SDK concern. BomberTalk will document the issue and file against PeerTalk rather than patching locally.

## Repository Ownership

| Issue | Change | Repository |
|-------|--------|------------|
| 1     | Staggered mesh connect delay | BomberTalk (screen_lobby.c) |
| 2     | Game-over graceful shutdown grace period | BomberTalk (screen_game.c) |
| 3     | Owner-authoritative bomb explosions | BomberTalk (bomb.c) |
| 4     | Authority-based game over broadcast | BomberTalk (screen_game.c, net.c) |
| 5     | Force-explode bias | Resolved by Issue 3 (no separate change) |
| 6     | Heap monitoring | BomberTalk (main.c) |
| 7     | Drain error log level downgrades | PeerTalk SDK (transport layer) |

All BomberTalk changes are in this spec. Issue 7 requires a separate PeerTalk SDK issue/PR documenting the specific error codes and recommended log levels. No clog changes needed.
