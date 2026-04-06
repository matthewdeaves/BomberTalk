# Data Model: Performance & Extensibility Upgrade

**Feature**: `002-perf-extensibility`
**Date**: 2026-04-06
**Extends**: [001-v1-alpha/data-model.md](../001-v1-alpha/data-model.md)

## New Entities

### PlayerStats

```c
typedef struct {
    short bombsMax;     /* Maximum bombs placeable at once (default 1) */
    short bombRange;    /* Explosion range in tiles (default 1) */
    short speedTicks;   /* Movement cooldown in ticks (default 12 = ~0.2s) */
} PlayerStats;
```

Attached to each Player. Replaces the standalone `bombsAvailable` and `bombRange` fields. `bombsAvailable` remains as a runtime counter (decremented on place, incremented on explode) but its initial/max value comes from `stats.bombsMax`.

### DirtyGrid

```c
#define MAX_GRID_COLS 31
#define MAX_GRID_ROWS 25

static unsigned char gDirtyGrid[MAX_GRID_ROWS][MAX_GRID_COLS];
static short gDirtyCount;  /* Number of dirty tiles this frame */
```

File-scope static in renderer.c. Not a public struct — accessed only via API functions:

```c
void Renderer_MarkDirty(short col, short row);   /* Mark one tile dirty */
void Renderer_MarkAllDirty(void);                 /* Mark entire grid dirty */
void Renderer_ClearDirty(void);                   /* Reset after blit */
```

### TMAP Resource Format

```
Offset  Size  Field
0       2     cols (big-endian short, 7-31)
2       2     rows (big-endian short, 7-25)
4       N     tile data (cols * rows bytes, row-major order)
```

Resource type: `'TMAP'`, ID: 128. Total size: 4 + (cols * rows) bytes.

## Modified Entities

### Player (modified)

```c
typedef struct {
    short       gridCol;
    short       gridRow;
    short       pixelX;
    short       pixelY;
    short       facing;
    short       animFrame;
    int         alive;
    int         active;
    short       bombsAvailable;     /* Runtime counter (reset from stats.bombsMax) */
    short       deathTimer;         /* Tick-based death flash timer */
    PlayerStats stats;              /* NEW: grouped attributes */
    unsigned char playerID;
    char        name[32];
    PT_Peer     *peer;
} Player;
```

**Changes**: Added `PlayerStats stats`. Removed standalone `bombRange` (now `stats.bombRange`). `bombsAvailable` retained as runtime counter, reset to `stats.bombsMax` on round start.

### TileMap (modified)

```c
typedef struct {
    short cols;                                     /* NEW: grid width (7-31) */
    short rows;                                     /* NEW: grid height (7-25) */
    unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS]; /* Sized to max */
    short spawnCols[MAX_PLAYERS];                   /* NEW: spawn X positions */
    short spawnRows[MAX_PLAYERS];                   /* NEW: spawn Y positions */
    short spawnCount;                               /* NEW: number of spawns found */
} TileMap;
```

**Changes**: Added `cols`, `rows` (replaces compile-time GRID_COLS/GRID_ROWS for gameplay logic). Added spawn point storage. Tile array sized to maximum dimensions; only `[0..rows-1][0..cols-1]` is used.

### MsgGameStart (modified)

```c
#define BT_PROTOCOL_VERSION 2

typedef struct {
    unsigned char numPlayers;   /* 1 byte */
    unsigned char version;      /* 1 byte (was: pad) */
} MsgGameStart;                 /* 2 bytes total — size unchanged */
```

**Changes**: `pad` byte replaced with `version`. Set to `BT_PROTOCOL_VERSION` on send. Checked on receive — reject if mismatch.

## Network Messages — Changes Only

Only MSG_GAME_START format changes. All other messages (MSG_POSITION, MSG_BOMB_PLACED, MSG_BOMB_EXPLODE, MSG_BLOCK_DESTROYED, MSG_PLAYER_KILLED, MSG_GAME_OVER) are unchanged.

MSG_GAME_OVER receives no format change but gains receiver-side validation: `winnerID` is bounds-checked against MAX_PLAYERS before use. Values >= MAX_PLAYERS (including 0xFF for draw) are treated as "no winner" without indexing the player array.

## Memory Layout Update

| Entity | Size | Count | Total | Change |
|--------|------|-------|-------|--------|
| Player | ~54 bytes | 4 | 216 bytes | +6 bytes/player (PlayerStats) |
| PlayerStats | 6 bytes | 4 | 24 bytes | NEW (embedded in Player) |
| TileMap | ~800 bytes | 1 | 800 bytes | +605 bytes (dims, spawns, max-sized array) |
| DirtyGrid | 775 bytes | 1 | 775 bytes | NEW |
| Cached PixMaps | ~24 bytes | 1 | 24 bytes | NEW (6 pointers) |
| **Additional memory** | | | **~1,428 bytes** | |

Total additional heap: ~1.4 KB. On Mac SE with ~587 KB free after v1.0-alpha allocations, this is negligible (0.2%).

## Renderer State — New File-Scope Statics

```c
/* Cached PixMap pointers (locked in BeginFrame, unlocked in EndFrame) */
static BitMap *gCachedPlayerPM[MAX_PLAYERS];
static BitMap *gCachedBombPM;
static BitMap *gCachedExplosionPM;

/* Static color constants (moved from stack) */
static const RGBColor kPlayerWhite  = {0xFFFF, 0xFFFF, 0xFFFF};
static const RGBColor kPlayerRed    = {0xFFFF, 0x0000, 0x0000};
static const RGBColor kPlayerBlue   = {0x0000, 0x0000, 0xFFFF};
static const RGBColor kPlayerYellow = {0xFFFF, 0xFFFF, 0x0000};
static const RGBColor kExplosionOrange = {0xFFFF, 0x6600, 0x0000};

static const RGBColor *kPlayerColors[MAX_PLAYERS] = {
    &kPlayerWhite, &kPlayerRed, &kPlayerBlue, &kPlayerYellow
};
```
