# Data Model: Smooth Sub-Tile Player Movement

**Feature**: 004-smooth-movement
**Date**: 2026-04-10

## Modified Data Structures

### Player Struct (game.h)

Current fields modified in semantics; new fields added for interpolation and bomb pass-through.

**Semantics change**:
- `pixelX`, `pixelY`: Change from derived (computed from grid) to **authoritative**. These are now the source of truth for player position.
- `gridCol`, `gridRow`: Change from authoritative to **derived**. Computed each frame as `(pixelX + tileSize/2) / tileSize` (center-based).

**New fields**:
- `targetPixelX` (short): Network interpolation target X. Set by on_position handler. Local player: always equals pixelX.
- `targetPixelY` (short): Network interpolation target Y. Set by on_position handler. Local player: always equals pixelY.
- `passThroughBombIdx` (short): Index into `gGame.bombs[]` of the bomb this player can walk through (-1 = none). Set on bomb placement, cleared when player hitbox fully leaves the bomb tile.
- `accumX` (short): Fractional pixel accumulator for X axis. Prevents speed drift from integer truncation. Reset to 0 on direction change or stop.
- `accumY` (short): Fractional pixel accumulator for Y axis. Same purpose as accumX.

**Memory impact**: 10 bytes per player x 4 players = 40 bytes total. Negligible.

**Full struct after changes**:
```
Player {
    short       gridCol;           /* derived: (pixelX + tileSize/2) / tileSize */
    short       gridRow;           /* derived: (pixelY + tileSize/2) / tileSize */
    short       pixelX;            /* authoritative X position (pixels) */
    short       pixelY;            /* authoritative Y position (pixels) */
    short       targetPixelX;      /* network interpolation target X */
    short       targetPixelY;      /* network interpolation target Y */
    short       accumX;            /* fractional pixel accumulator X */
    short       accumY;            /* fractional pixel accumulator Y */
    short       passThroughBombIdx; /* bomb index player can walk through (-1=none) */
    short       facing;
    short       animFrame;
    int         alive;
    int         active;
    short       deathTimer;
    short       bombsAvailable;
    PlayerStats stats;
    unsigned char playerID;
    char        name[32];
    void        *peer;
}
```

### MsgPosition Struct (game.h)

Expanded from 5 bytes to 8 bytes to carry pixel coordinates.

**Current layout** (v2, 5 bytes):
```
{ playerID: u8, gridCol: u8, gridRow: u8, facing: u8, animFrame: u8 }
```

**New layout** (v3, 8 bytes):
```
{ playerID: u8, facing: u8, pixelX: short, pixelY: short, pad: u8[2] }
```

**Notes**:
- Big-endian shorts are native on both 68k and PPC — no byte swapping.
- `animFrame` removed (unused in v2, not needed in v3).
- 2 bytes padding for 2-byte alignment and future use.

### PlayerStats Struct (game.h)

**Modified field**:
- `speedTicks`: Semantics change from "cooldown between moves" to "ticks to cross one tile". Initial value remains 12. Used to compute `pixelsPerTick = tileSize / speedTicks` at init and on speed change.

### Protocol Version Constant (game.h)

- `BT_PROTOCOL_VERSION`: Changed from 2 to 3.

## New Constants

```
HITBOX_INSET_LARGE  4    /* pixels inset on 32px tiles (color Macs) */
HITBOX_INSET_SMALL  2    /* pixels inset on 16px tiles (Mac SE) */
NUDGE_THRESHOLD_LARGE 10 /* corner sliding threshold on 32px tiles */
NUDGE_THRESHOLD_SMALL 5  /* corner sliding threshold on 16px tiles */
INTERP_TICKS        4    /* interpolation half-life in ticks (~67ms) */
```

These are compile-time constants. The appropriate value is selected at runtime based on `gGame.isMacSE` / `gGame.tileSize`.

## State Transitions

### Player Movement State

No explicit state machine needed. Movement is continuous while keys are held:

```
Idle (no key held) → Moving (key held, advance pixels per frame) → Idle (key released)
                                    ↓
                              Wall collision → clamped to boundary, still "Moving" state
```

### Bomb Pass-Through Lifecycle

```
Not on bomb (passThroughBombIdx = -1)
    ↓ Player places bomb
On own bomb (passThroughBombIdx = bomb index)
    ↓ Player hitbox fully leaves bomb tile
Free (passThroughBombIdx = -1, bomb now solid)
    ↓ Bomb explodes
Bomb gone (gBombGrid cleared, pass-through irrelevant)
```

### Disconnect Cleanup Sequence

```
on_disconnected fires
    → Mark player's bounding box tiles dirty (1-4 tiles)
    → Set player.active = FALSE
    → Set player.peer = NULL
    → Next frame: dirty tiles redrawn from background, player sprite gone
```
