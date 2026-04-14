# Quickstart: Wall-Clock Timers & Log Cleanup

**Feature**: 007-timer-fixes
**Date**: 2026-04-14

## What This Changes

Four network/coordination timers that currently drift on slow machines (due to the deltaTicks cap at 10) are converted to TickCount()-based wall-clock timing. One noisy log message is downgraded. One PeerTalk log is documented as expected.

## Files Modified

| File | Change |
|------|--------|
| `include/game.h` | 4 fields: `short xxxTimer` → `unsigned long xxxStart` |
| `src/screen_game.c` | 3 timers converted: grace, failsafe, timeout |
| `src/screen_lobby.c` | 1 timer converted: mesh stagger |
| `src/bomb.c` | 1 line: CLOG_INFO → CLOG_DEBUG |
| `notes/known-issues.md` | KI-003 documented as expected PeerTalk behavior |

## The Pattern

**Before** (drifts when fps < 6):
```c
gGame.disconnectGraceTimer = DISCONNECT_GRACE_TICKS;  /* start */
gGame.disconnectGraceTimer -= gGame.deltaTicks;        /* tick */
if (gGame.disconnectGraceTimer <= 0) { ... }           /* expired */
```

**After** (wall-clock accurate):
```c
gGame.disconnectGraceStart = TickCount();                              /* start */
/* no per-frame update needed */
if (gGame.disconnectGraceStart != 0 &&
    TickCount() - gGame.disconnectGraceStart >= DISCONNECT_GRACE_TICKS) { ... }  /* expired */
```

## Build & Test

```bash
# Build all three targets
cd build-68k && make && cd ..
cd build-ppc-ot && make && cd ..
cd build-ppc-mactcp && make && cd ..

# Hardware test: 2+ player game, end game, check logs
# Expected: grace period completes in ~1.5s on all machines
# Expected: no CLOG_INFO "fuse expired locally" messages
socat -u UDP-RECV:7356,reuseaddr -
```

## What NOT to Change

- Bomb fuse timers (deltaTicks is correct — gameplay simulation)
- Explosion duration timers (deltaTicks is correct)
- Movement accumulators (deltaTicks is correct)
- Any wire protocol (no version bump needed)
