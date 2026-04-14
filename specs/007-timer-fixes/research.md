# Research: Wall-Clock Timers & Log Cleanup

**Feature**: 007-timer-fixes
**Date**: 2026-04-14

## R1: deltaTicks Cap and Timer Drift Mechanism

**Decision**: Convert four coordination timers from `timer -= deltaTicks` to `TickCount() - startTick >= threshold`.

**Rationale**: The main loop caps deltaTicks at 10 (main.c:241: `if (elapsed > 10) elapsed = 10`). This cap prevents physics jumps on lag spikes — correct for gameplay simulation (bomb fuse, movement). But for coordination timers that must match wall-clock time, the cap causes undercounting when fps drops below ~6 (i.e., when real ticks per frame exceed 10).

Observed drift from 006 hardware testing (KI-001): The Mac SE's grace period timer took 1833 real ticks to count down 90 timer-ticks, implying ~1fps during game-over transition. At 1fps, each frame is ~60 real ticks but deltaTicks is capped at 10 — a 6x drift factor. The PPC finished in ~90 real ticks (1:1 ratio at 30fps where deltaTicks ≈ 2).

The lobby already uses TickCount() for its connection timers (screen_lobby.c:120 `gLastMeshRetryTick`, line 128 `gConnectStartTick`). These were written correctly from the start because they coordinate with external network events. The four affected timers have the same coordination-with-external-events nature but were implemented with deltaTicks.

**Alternatives considered**:
- Raise the deltaTicks cap (e.g., to 60): Would fix timer accuracy but breaks gameplay simulation — large deltaTicks causes physics jumps (player teleports, bomb fuse burns instantly). The cap exists for a good reason. Rejected.
- Remove the cap entirely: Same problem as above, worse. Rejected.
- Add separate uncapped deltaTicks for coordination timers: Adds complexity to main loop, two time sources to reason about. TickCount() is simpler and proven. Rejected.
- Use Time Manager (hardware timer): Overkill — TickCount() resolution (1/60s) is sufficient for coordination delays of 0.5-3s. Time Manager adds interrupt-time complexity that violates Principle VII. Rejected.

## R2: Timer Field Type Change — short to unsigned long

**Decision**: Change the four timer fields in GameState from `short` (countdown value) to `unsigned long` (TickCount start tick). Value 0 means inactive; non-zero means timer started at that tick.

**Rationale**: TickCount() returns `unsigned long` on Classic Mac (32-bit unsigned). The current `short` fields (16-bit signed) cannot hold TickCount() values. The semantic also changes: instead of "ticks remaining" (decrement toward 0), the field stores "tick when started" (compare elapsed against threshold).

The "active" check changes from `timer > 0` to `timer != 0`. The "start" operation changes from `timer = CONSTANT` to `timer = TickCount()`. The "expired" check changes from `timer <= 0` (after decrement) to `TickCount() - timer >= CONSTANT`.

Edge case — TickCount() wrap: TickCount() wraps after ~18.6 hours at 60Hz (2^32 / 60 / 3600). Unsigned subtraction handles this correctly: `(small_current - large_start)` wraps to the correct positive elapsed value on unsigned long. No special handling needed.

Edge case — TickCount() returns 0: This happens exactly once, at boot time. By the time the game launches, TickCount() is non-zero. Even if it did return 0, the timer would appear inactive (false negative) — the worst case is one missed timer start, which the existing retry/failsafe mechanisms handle.

**Alternatives considered**:
- Keep short fields and store elapsed-since-start divided by some factor: Lossy, adds division, confusing semantics. Rejected.
- Add new unsigned long fields alongside existing short fields: Wastes 16 bytes of GameState for no benefit. Rejected.
- Use a separate timer struct: Over-engineered for 4 fields with identical semantics. Rejected.

## R3: Timer Constant Reuse

**Decision**: Reuse existing `#define` constants unchanged (DISCONNECT_GRACE_TICKS=90, GAME_OVER_FAILSAFE_TICKS=120, GAME_OVER_TIMEOUT_TICKS=180, MESH_STAGGER_PER_RANK=30). Their values are in ticks (1/60s units), which is exactly what TickCount() counts.

**Rationale**: The constants already express durations in the same unit as TickCount(). `TickCount() - startTick >= 90` means "90 ticks have elapsed" = 1.5 seconds. No conversion needed. The bug was never in the constants — it was in the decrement mechanism losing ticks to the cap.

**Alternatives considered**:
- Rename constants to _WALL_TICKS or _DURATION: Adds churn for no semantic benefit — all timer constants in the codebase are in tick units. Rejected.
- Convert constants to milliseconds: Would require `TickCount() * 1000 / 60` math with overflow risk on 32-bit unsigned. Ticks are the native unit. Rejected.

## R4: Bomb Fuse Log Level

**Decision**: Change bomb.c:213 from CLOG_INFO to CLOG_DEBUG.

**Rationale**: The "fuse expired locally" message fires for every non-owner bomb on faster machines — 16 times across 3 games in the 006 test session. This is the force-explode safety net working exactly as designed (005-network-authority). At INFO level it clutters logs and obscures genuine warnings. At DEBUG level it remains available for troubleshooting but doesn't pollute normal test output.

**Alternatives considered**:
- Remove the log entirely: Loses diagnostic value for future network timing issues. Rejected.
- Add a counter and log a summary: Over-engineered for a debug message. Rejected.
- Keep at INFO: Status quo; no action. Rejected because 16 messages per session is too noisy for expected behavior.

## R5: KI-003 OTRcv -3155 Documentation

**Decision**: Document in known-issues.md that OTRcv -3155 (kOTLookErr) during tiebreaker drain is expected PeerTalk behavior. No BomberTalk code change.

**Rationale**: The -3155 log message originates in PeerTalk's OT transport layer, not in BomberTalk code. Searching the BomberTalk codebase confirms no reference to -3155 or kOTLookErr. The error is correct OT behavior — an async event arrived during endpoint drain after the tiebreaker cancelled the outgoing connection. PeerTalk handles it gracefully and the mesh completes.

**Alternatives considered**:
- Patch PeerTalk to suppress the log: Out of scope — PeerTalk is an external dependency. Would need to be raised as a PeerTalk issue. Rejected for this feature.
- Add a BomberTalk-side filter on clog output: clog doesn't support per-source filtering. Rejected.

## Book Reference

*Sex, Lies and Video Games* (1996), lines 11787-11823:
- Line 11796: "The problem arises when the frame rate of the game varies."
- Lines 11810-11813: "say, two pixels every twenty milliseconds...your sprites will move two pixels every 20 milliseconds no matter what the speed of the host processor is."
- The book distinguishes frame-rate timing from wall-clock timing and recommends wall-clock for anything that must be speed-independent. All four affected timers coordinate between machines or wait on external events — wall-clock concerns, not gameplay simulation.
