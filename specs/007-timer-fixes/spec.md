# Feature Specification: Wall-Clock Timers & Log Cleanup

**Feature Branch**: `007-timer-fixes`  
**Created**: 2026-04-14  
**Status**: Draft  
**Input**: Convert four network/coordination timers from deltaTicks decrement to TickCount()-based wall-clock timing, and downgrade two noisy log messages. Addresses KI-001 through KI-005 from 006 hardware testing.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Grace Period Consistency Across Machines (Priority: P1)

When a multiplayer game ends, all machines must complete their TCP flush grace period in approximately the same wall-clock time, regardless of frame rate. Currently the Mac SE (which can drop to ~1fps during game-over transitions) takes up to 20x longer than PPC machines to complete the grace period, causing the faster machine to disconnect before the slower one finishes — logged as `reason: 2` on the SE.

**Why this priority**: This is the only KI that produces visible symptoms (spurious disconnect warnings in logs, potential for missed final messages). KI-001 was observed in 1 of 3 games during the 006 testing session.

**Independent Test**: Start a 2-player game (Mac SE + PPC). End the game. Both machines should complete their grace period and disconnect cleanly with `reason: 0` within ~1.5 seconds of each other.

**Acceptance Scenarios**:

1. **Given** a game-over on a Mac SE at any fps and PPC at ~30fps, **When** the grace period starts, **Then** both machines disconnect within ~1.5s wall-clock (90 ticks), not 9-30s on the SE.
2. **Given** a game-over on any machine, **When** the grace period timer is running, **Then** elapsed time is measured via TickCount() and is independent of frame rate or the deltaTicks cap.

---

### User Story 2 - Consistent Failsafe and Timeout Timers (Priority: P1)

The game-over failsafe timer (wait for authority's MSG_GAME_OVER) and the game-over timeout timer (safety timeout for pending game-over) must fire at consistent wall-clock intervals regardless of machine speed.

**Why this priority**: These timers protect against authority disconnection and stuck game-over states. If they run too slowly on the SE (due to deltaTicks cap), the SE could wait much longer than intended in a stalled state before recovering.

**Independent Test**: Kill the authority machine mid-game. The non-authority SE should trigger its failsafe within ~2s (120 ticks), not 12+s.

**Acceptance Scenarios**:

1. **Given** a non-authority machine running at any fps, **When** the authority disconnects without sending MSG_GAME_OVER, **Then** the failsafe timer fires within ~2s wall-clock.
2. **Given** a pending remote game-over with active death animations, **When** the timeout expires, **Then** it fires within ~3s wall-clock regardless of frame rate.

---

### User Story 3 - Predictable Mesh Stagger Timing (Priority: P2)

After MSG_GAME_START, each receiver delays its first TCP connect by `rank * 30 ticks`. This stagger must take the same wall-clock time regardless of which machine is at which rank. Currently in the lobby (~3fps on SE), the deltaTicks cap causes the stagger to take ~2x longer than intended.

**Why this priority**: The mesh stagger works correctly today due to the retry mechanism (KI-002 showed recovery every time). Converting to wall-clock makes the stagger predictable but is not fixing a failure — it's preventing a latent issue.

**Independent Test**: Start a 3-player game. Observe mesh formation logs. Rank 1's stagger should complete in ~0.5s and rank 2's in ~1.0s, regardless of which machine holds which rank.

**Acceptance Scenarios**:

1. **Given** a rank-1 machine (SE at ~3fps lobby), **When** MSG_GAME_START is received, **Then** the stagger delay completes in ~0.5s wall-clock, not ~1s.
2. **Given** any rank/machine combination, **When** the stagger runs, **Then** the delay duration is `rank * 0.5s` regardless of frame rate.

---

### User Story 4 - Reduced Log Noise from Expected Events (Priority: P3)

Two log messages fire frequently during normal gameplay but describe expected, by-design behavior. They clutter logs and make it harder to spot genuine issues.

**Why this priority**: Cosmetic improvement to log readability. No gameplay impact.

**Independent Test**: Play a 3-player game with multiple bomb placements. The log should not contain CLOG_INFO-level "fuse expired locally" messages. OTRcv -3155 during tiebreaker should be documented as expected PeerTalk behavior (not a BomberTalk change).

**Acceptance Scenarios**:

1. **Given** a non-owner machine where a bomb fuse expires before MSG_BOMB_EXPLODE arrives, **When** the force-explode fires, **Then** the log message is at CLOG_DEBUG level, not CLOG_INFO.
2. **Given** OTRcv -3155 during tiebreaker drain, **When** this occurs, **Then** it is documented as expected PeerTalk behavior in the known-issues notes (no BomberTalk code change needed — the log is in PeerTalk).

---

### Edge Cases

- What happens if TickCount() wraps around (after ~18 hours of uptime at 60 ticks/sec)? Unsigned subtraction handles wrap correctly — `(small - large)` on unsigned long produces the correct elapsed value.
- What happens if a timer start tick is set but the check never fires (e.g., transition to a different screen)? The timer fields are reset to 0 on lobby init and game reset, same as today.
- What happens if the frame rate fluctuates rapidly (e.g., SE at 10fps then drops to 1fps mid-grace-period)? With TickCount(), the timer fires at the correct wall-clock time regardless of fps changes.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: `disconnectGraceTimer` MUST use TickCount()-based wall-clock timing instead of deltaTicks decrement.
- **FR-002**: `gameOverFailsafeTimer` MUST use TickCount()-based wall-clock timing instead of deltaTicks decrement.
- **FR-003**: `gameOverTimeout` MUST use TickCount()-based wall-clock timing instead of deltaTicks decrement.
- **FR-004**: `meshStaggerTimer` MUST use TickCount()-based wall-clock timing instead of deltaTicks decrement.
- **FR-005**: All four converted timers MUST follow the same start/elapsed pattern already used by lobby timers (`gLastMeshRetryTick`, `gConnectStartTick`): store TickCount() at start, check `TickCount() - startTick >= threshold` for expiry.
- **FR-006**: The short timer fields in GameState MUST be changed to unsigned long to hold TickCount() values (0 = inactive, non-zero = tick when timer started).
- **FR-007**: Timer reset on lobby init and game reset MUST continue to disable all four timers (value 0 = inactive).
- **FR-008**: Gameplay simulation timers (bomb fuse, explosion duration, movement accumulators) MUST NOT be changed — they correctly use deltaTicks.
- **FR-009**: The bomb fuse-expiry log at bomb.c MUST be downgraded from CLOG_INFO to CLOG_DEBUG.
- **FR-010**: KI-003 (OTRcv -3155) MUST be documented as expected PeerTalk behavior — no BomberTalk code change required.

### Key Entities

- **GameState timer fields**: Four fields changing from countdown-short to startTick-unsigned-long. The "active" semantic changes from `> 0` to `!= 0` (where 0 means inactive and any non-zero value is the TickCount() when the timer started).
- **Timer constants**: DISCONNECT_GRACE_TICKS (90), GAME_OVER_FAILSAFE_TICKS (120), GAME_OVER_TIMEOUT_TICKS (180), MESH_STAGGER_PER_RANK (30) — values unchanged, but now compared against TickCount() elapsed rather than decremented.

## Assumptions

- TickCount() is available and returns unsigned long on all three targets (68k MacTCP, PPC MacTCP, PPC OT). This is a standard Toolbox call available since System 1.
- The existing lobby timer pattern (screen_lobby.c:120, 128) is the proven correct approach — this change extends it to four more timers.
- KI-003's OTRcv -3155 log is inside PeerTalk library code, not BomberTalk. The fix for KI-003 is documentation only.
- No protocol version bump is needed — this is a local timing change with no wire-format impact.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Grace period wall-clock duration on Mac SE is within 0.5s of PPC duration (currently can differ by 10-20x at low fps).
- **SC-002**: All four converted timers fire at the intended wall-clock time (±1 frame duration) regardless of frame rate, verified via clog timestamps in hardware test.
- **SC-003**: Bomb fuse-expiry "locally expired" messages no longer appear at CLOG_INFO level in test logs — only visible at CLOG_DEBUG.
- **SC-004**: All three build targets (68k MacTCP, PPC MacTCP, PPC OT) compile clean with no new warnings.
- **SC-005**: No regression in gameplay behavior — bombs, explosions, movement, and game-over flow work identically to v1.7.0.
