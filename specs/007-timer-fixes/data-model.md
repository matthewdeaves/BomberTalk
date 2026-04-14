# Data Model: Wall-Clock Timers & Log Cleanup

**Feature**: 007-timer-fixes
**Date**: 2026-04-14

## GameState Timer Field Changes

Four fields in the `GameState` struct (`game.h`) change type and semantics:

### Before (countdown pattern)

```
short  disconnectGraceTimer;   /* ticks remaining, decrement by deltaTicks */
short  gameOverFailsafeTimer;  /* ticks remaining, decrement by deltaTicks */
short  gameOverTimeout;        /* ticks remaining, decrement by deltaTicks */
short  meshStaggerTimer;       /* ticks remaining, decrement by deltaTicks */
```

- **Type**: `short` (16-bit signed)
- **Inactive**: value == 0
- **Start**: `timer = CONSTANT`
- **Tick**: `timer -= deltaTicks`
- **Expired**: `timer <= 0` (after decrement)

### After (wall-clock pattern)

```
unsigned long  disconnectGraceStart;   /* TickCount() when started, 0 = inactive */
unsigned long  gameOverFailsafeStart;  /* TickCount() when started, 0 = inactive */
unsigned long  gameOverTimeoutStart;   /* TickCount() when started, 0 = inactive */
unsigned long  meshStaggerStart;       /* TickCount() when started, 0 = inactive */
```

- **Type**: `unsigned long` (32-bit unsigned, matches TickCount() return type)
- **Inactive**: value == 0
- **Start**: `timer = TickCount()`
- **Tick**: (none — no per-frame update needed)
- **Expired**: `TickCount() - timer >= CONSTANT`

### Field Rename Rationale

Fields are renamed from `*Timer` to `*Start` to reflect the semantic change: they no longer hold a countdown value, they hold the TickCount() at which the timer was started. This makes the pattern self-documenting and prevents accidentally decrementing a start-tick value.

### Reset Points

All four fields are set to 0 (inactive) in:
- `main.c` game reset (~line 302-307)
- `screen_lobby.c` lobby init (~line 49-53)

These assignments change from `gGame.xxxTimer = 0` to `gGame.xxxStart = 0`. Semantics unchanged (0 = inactive in both patterns).

## Constants (unchanged)

| Constant | Value | Duration | Usage |
|----------|-------|----------|-------|
| `DISCONNECT_GRACE_TICKS` | 90 | ~1.5s | Grace period before TCP teardown after game over |
| `GAME_OVER_FAILSAFE_TICKS` | 120 | ~2s | Non-authority timeout waiting for MSG_GAME_OVER |
| `GAME_OVER_TIMEOUT_TICKS` | 180 | ~3s | Safety timeout for pending game-over with death anims |
| `MESH_STAGGER_PER_RANK` | 30 | ~0.5s/rank | Per-rank delay before first TCP connect |

Values stay the same. They are already in tick units (1/60s), which is what TickCount() counts.

## Conversion Pattern Reference

Existing proven pattern from `screen_lobby.c`:

```c
/* Start: */
gLastMeshRetryTick = TickCount();

/* Check: */
if (TickCount() - gLastMeshRetryTick > 120) { ... }
```

New pattern for all four timers follows this exactly:

```c
/* Start: */
gGame.disconnectGraceStart = TickCount();

/* Check: */
if (gGame.disconnectGraceStart != 0 &&
    TickCount() - gGame.disconnectGraceStart >= DISCONNECT_GRACE_TICKS) { ... }
```

## Mesh Stagger Start Value

The mesh stagger timer has a per-rank multiplied threshold. The start value is still `TickCount()`, but the elapsed comparison uses `rank * MESH_STAGGER_PER_RANK`:

```c
/* Start (in net.c MSG_GAME_START handler): */
gGame.meshStaggerStart = TickCount();
/* stagger amount stored separately or computed at check time */

/* Check (in screen_lobby.c): */
if (gGame.meshStaggerStart != 0 &&
    TickCount() - gGame.meshStaggerStart >= stagger) { ... }
```

The `stagger` value (`rank * MESH_STAGGER_PER_RANK`) is computed when MSG_GAME_START is received and can be stored in the existing code flow. Currently `screen_lobby.c:147` sets `gGame.meshStaggerTimer = stagger` — this becomes `gGame.meshStaggerStart = TickCount()` with the stagger threshold stored or recomputed.

Note: The stagger threshold needs to be stored somewhere since it varies by rank. Options:
1. Keep a separate `short meshStaggerDelay` field in GameState (clearest)
2. Recompute `Net_GetLocalRank() * MESH_STAGGER_PER_RANK` at check time (avoids new field but couples to Net API)

Option 2 is simpler since `Net_GetLocalRank()` is already available and the value doesn't change during a session.
