# Known Issues & Future Improvements

Observations from hardware testing that don't block current functionality but could be addressed in future work. Each entry includes context, severity, and suggested fix.

---

## KI-001: Grace Period Race Between Fast and Slow Machines

**Observed**: v1.7.0+, game 1 of 006 testing session (2026-04-14)
**Severity**: Low (cosmetic — game completes correctly)
**Status**: FIXED in 007-timer-fixes — `disconnectGraceStart` now uses TickCount() wall-clock timing
**Machines**: Mac SE (P0, authority) vs Performa 6400 (P1)

**Symptoms**: The 6400 receives `MSG_GAME_OVER`, runs its 90-tick grace period (~1.5s at 30fps), and tears down TCP. The Mac SE, running slower, takes longer to complete its grace period. The 6400 disconnects before the SE finishes, causing a `reason: 2` (remote disconnect) on the SE.

**Log evidence**:
```
[SE 121233] Game over: starting grace period (90 ticks)
[SE 122766] Peer BomberTalk disconnected (reason: 2)  <- 6400 tore down first
[SE 123066] Grace period complete, disconnecting       <- SE finished later
```

**Impact**: The SE still transitions to lobby correctly. The disconnect is handled gracefully. No crash or data loss. The `reason: 2` is logged but has no gameplay effect.

**Root cause**: The grace period timer uses `deltaTicks` decrement (`screen_game.c:68`). Although `deltaTicks` is actual elapsed ticks (not 1 per frame), it is capped at 10 (`main.c:241`). At normal gameplay fps (~10fps on SE, deltaTicks ≈ 6), the timer tracks real time accurately (~1.5s). But if fps drops below ~6 during game-over processing (e.g. ~3fps), deltaTicks is capped at 10 while real elapsed time per frame is ~20 ticks — the timer undercounts by ~2x, taking ~3s instead of ~1.5s. The log timestamps (1833 ticks elapsed for 90 timer-ticks) suggest the SE dropped to very low fps during this transition, amplifying the drift.

**Book reference**: *Sex, Lies and Video Games* (lines 11787-11823) explicitly covers this: frame-rate-dependent timers cause different wall-clock durations across machines. The book recommends wall-clock timing (Time Manager or TickCount) for anything that must be speed-independent.

**Suggested fix**: Switch `disconnectGraceTimer` from `deltaTicks` decrement to raw `TickCount()` comparison, matching the pattern already used by lobby timers (`screen_lobby.c:120`, `screen_lobby.c:128`). The grace period waits for TCP buffer flush — a real-time operation, not a gameplay simulation timer.

**Scope note**: This same `deltaTicks` vs wall-clock issue affects three other timers — see KI-005.

**Frequency**: 1 out of 3 games in testing. Games 2 and 3 had clean `reason: 0` disconnects on all machines.

---

## KI-002: TCP Connect Failure on Performa 6200 During Mesh Formation

**Observed**: v1.6.0+, games 2 and 3 of 006 testing session (2026-04-14)
**Severity**: Low (mesh stagger recovers automatically)
**Status**: IMPROVED in 007-timer-fixes — `meshStaggerStart` now uses TickCount() wall-clock timing (stagger duration consistent across machines). Initial TCP connect failure still expected; retry mechanism handles it.
**Machines**: Performa 6200 (MacTCP) connecting to Mac SE (MacTCP)

**Symptoms**: The 6200 fails its initial TCP connect to the Mac SE with `Error 3: TCP connect failed`. The mesh stagger mechanism (introduced in 005) delays the retry by `rank * 30 ticks`, after which the connection succeeds.

**Log evidence**:
```
[6200 48966] Error 3: TCP connect failed
[6200 49000] Game start received, mesh stagger: rank 1, delay 30 ticks
[6200 49300] Re-discovered disconnected peer: BomberTalk
[SE  128383] Incoming TCP connection accepted
```

**Impact**: None — mesh completes successfully every time. The 30-tick stagger was specifically designed for this scenario. The game starts normally.

**Additional note**: The mesh stagger timer itself uses `deltaTicks` decrement (`screen_lobby.c:101-102`), so the 30-tick-per-rank stagger varies with frame rate when fps drops below ~6 (where the deltaTicks cap at 10 kicks in). In the lobby the SE runs at ~3fps, so deltaTicks is capped at 10 while real elapsed time per frame is ~20 ticks — the stagger takes ~2x longer than intended. The intent is to give listeners real-time to become ready — a wall-clock concern. See KI-005 for the general fix.

**Suggested fix**: The retry mechanism is robust and handles this well. Converting `meshStaggerTimer` to `TickCount()` (see KI-005) would make the stagger delay consistent regardless of which machine is at which rank.

**Frequency**: 2 out of 3 games. This appears to happen when the 6200 is not the game initiator (i.e., when it receives `MSG_GAME_START` from another machine and immediately tries to connect to all peers).

---

## KI-003: OTRcv Error -3155 During Connection Tiebreaker on 6400

**Observed**: v1.6.0+, game 1 of 006 testing session (2026-04-14)
**Severity**: Informational (debug-level, handled correctly)
**Status**: DOCUMENTED in 007-timer-fixes — confirmed as expected PeerTalk OT behavior, no BomberTalk fix possible
**Machines**: Performa 6400 (Open Transport)

**Symptoms**: During mesh formation, when both the 6400 and another peer simultaneously try to connect to each other, the tiebreaker cancels the outgoing connection. While draining the cancelled endpoint, `OTRcv` returns `-3155` (`kOTLookErr`), meaning an asynchronous event arrived during the drain.

**Log evidence**:
```
[6400 55450] OTRcv error -3155 in disconnect drain
[6400 55450] OTRcv error -3155 in disconnect drain
[6400 55800] Tiebreaker: cancelling outgoing, accepting incoming
[6400 55816] Incoming TCP connection accepted
```

**Impact**: None — the tiebreaker resolves correctly and the mesh completes. This is expected OT behavior during simultaneous connect scenarios.

**Suggested fix**: Could suppress the `-3155` log message during tiebreaker drain since it's expected, or downgrade from `DBG` to a lower level. Not worth changing the drain logic — it's working correctly.

**Note (007)**: This log message originates in PeerTalk's OT transport layer, not in BomberTalk code. No BomberTalk code change can address it. If the log noise is problematic, it would need to be addressed in PeerTalk itself.

**Frequency**: 1 out of 3 games (only when simultaneous connect happens, which is timing-dependent).

---

## KI-004: Fuse Expiry Before Network Bomb Explode Message

**Observed**: v1.7.0+ (005-network-authority), all 3 games of 006 testing session
**Severity**: Informational (by design — safety net working as intended)
**Status**: FIXED in 007-timer-fixes — log downgraded from CLOG_INFO to CLOG_DEBUG
**Machines**: Performa 6400 (8 events), Performa 6200 (7 events), Mac SE (1 event)

**Symptoms**: A non-owner machine's local bomb fuse timer expires before it receives `MSG_BOMB_EXPLODE` from the bomb owner. The bomb explodes locally without broadcasting (since only the owner broadcasts). When the network message eventually arrives, it's a no-op (bomb already gone).

**Log evidence**:
```
[6400 63866] Bomb at (1,10) fuse expired locally (owner=P2, not local P1)
[6400 63866] Bomb exploding at (1,10) range=1 owner=P2 remote
```

**Impact**: None — this is the force-explode safety net from 005 working exactly as designed. The bomb still explodes at the correct time. The Mac SE has fewer occurrences (1 vs 7-8) because it's slower — by the time its fuse expires, the network message has usually arrived.

**Suggested fix**: Not a bug — the force-explode safety net is working as designed. Reduce log level from `INF` to `DBG` since this is expected behavior and clutters the log. The asymmetry (fast machines expire first) is inherent to the owner-authoritative design and doesn't cause desync because explosions are idempotent.

**Note on fuse padding**: Adding artificial fuse padding on non-owner machines would be wrong — the fuse timer is a gameplay timer that *should* use `deltaTicks` so bombs feel the same to the local player. The current design (local force-explode + idempotent network message) is correct.

**Frequency**: 16 total across 3 games (consistently happens for non-owner bombs on faster machines).

---

## KI-005: Four Network/Coordination Timers Use Frame-Rate-Dependent Timing

**Observed**: Code review cross-referenced with *Sex, Lies and Video Games* (1996), lines 11787-11823
**Severity**: Low (drift only manifests when fps drops below ~6, where the deltaTicks cap at 10 kicks in; KI-001 is the visible symptom)
**Status**: FIXED in 007-timer-fixes — all four timers converted to TickCount() wall-clock timing

**Description**: The codebase has two timer patterns: `deltaTicks` decrement and raw `TickCount()` comparison (wall-clock). Lobby connection timers correctly use `TickCount()` because they wait on real-time external events. Four network/coordination timers use `deltaTicks`. Since `deltaTicks` is actual elapsed ticks (not 1 per frame), these timers track real time accurately when fps >= 6. However, `deltaTicks` is capped at 10 (`main.c:241`), so when fps drops below ~6 (common on Mac SE in lobby at ~3fps, and possibly during game-over transitions), the timers undercount real time. The drift factor equals `(real_ticks_per_frame / 10)` — at 3fps that's ~2x, at 1fps ~6x.

**Affected timers**:

| Timer | Location | Constant | Wall-clock (>=6fps) | Wall-clock (~3fps SE) | Purpose |
|-------|----------|----------|--------------------|-----------------------|---------|
| `disconnectGraceTimer` | `screen_game.c:68` | 90 ticks | ~1.5s | ~3s | TCP flush after game over |
| `gameOverFailsafeTimer` | `screen_game.c:234` | 120 ticks | ~2s | ~4s | Wait for authority's MSG_GAME_OVER |
| `gameOverTimeout` | `screen_game.c:254` | 180 ticks | ~3s | ~6s | Safety timeout for pending game over |
| `meshStaggerTimer` | `screen_lobby.c:102` | 30/rank ticks | ~0.5s/rank | ~1s/rank | Delay before first TCP connect |

**Timers that are correct as-is** (gameplay simulation, should feel the same locally):
- `fuseTimer` (bomb fuse) — `deltaTicks` is correct
- explosion duration timers — `deltaTicks` is correct
- movement accumulators — `deltaTicks` is correct

**Book reference**: *Sex, Lies and Video Games* distinguishes frame-rate timing ("the problem arises when the frame rate of the game varies") from wall-clock timing ("two pixels every 20 milliseconds no matter what the speed of the host processor"). The book recommends wall-clock timing for anything that must behave consistently regardless of CPU speed. All four affected timers coordinate between machines or wait on external events — wall-clock concerns, not gameplay simulation. The `deltaTicks` cap at 10 is necessary for gameplay simulation (prevents physics jumps on lag spikes) but makes these coordination timers drift on slow machines.

**Suggested fix**: Convert all four timers from `deltaTicks` decrement to `TickCount()` start/elapsed comparison, matching the existing lobby pattern (`screen_lobby.c:120`). Each timer would store a `startTick` (set via `TickCount()` when the timer begins) and check `TickCount() - startTick >= threshold`. The `short` timer fields in `GameState` would become `long` start-tick fields (or reuse existing `long` fields).

**Impact of fix**: Grace period, failsafe, timeout, and mesh stagger would all take the same wall-clock duration on every machine regardless of fps. KI-001 would be fully resolved. KI-002's stagger would become predictable across rank/machine combinations. At normal gameplay fps (>=6), the difference is negligible, but the fix eliminates the risk entirely.
