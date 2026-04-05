# Data Model: BomberTalk v1.0-alpha

**Feature**: `001-v1-alpha`
**Date**: 2026-04-05

## Game Entities

### Player

```c
#define MAX_PLAYERS     4

typedef struct {
    short       gridCol;        /* Current tile column (0-14) */
    short       gridRow;        /* Current tile row (0-12) */
    short       pixelX;         /* Pixel X (gridCol * TILE_SIZE) */
    short       pixelY;         /* Pixel Y (gridRow * TILE_SIZE) */
    short       facing;         /* DIR_UP/DOWN/LEFT/RIGHT */
    short       animFrame;      /* Animation frame (future) */
    int         alive;          /* Is player alive this round? */
    int         active;         /* Is this slot in use? */
    short       bombsAvailable; /* Bombs available to place (default 1) */
    short       bombRange;      /* Explosion range in tiles (default 1) */
    unsigned char playerID;     /* Unique ID (0-3) */
    char        name[32];       /* Player name from PeerTalk */
    PT_Peer     *peer;          /* PeerTalk peer (NULL for local) */
} Player;
```

### Bomb

```c
#define MAX_BOMBS       16  /* Max simultaneous bombs on the map */
#define BOMB_FUSE_TICKS 180 /* 3 seconds at 60 ticks/sec */

typedef struct {
    short       gridCol;        /* Bomb tile column */
    short       gridRow;        /* Bomb tile row */
    short       fuseTimer;      /* Ticks until explosion */
    short       range;          /* Explosion range in tiles */
    unsigned char ownerID;      /* Player who placed it */
    int         active;         /* Is this bomb slot in use? */
} Bomb;
```

### TileMap

```c
typedef struct {
    unsigned char tiles[GRID_ROWS][GRID_COLS]; /* 15x13 = 195 bytes */
} TileMap;

/* Tile types */
#define TILE_FLOOR      0
#define TILE_WALL       1
#define TILE_BLOCK      2
#define TILE_SPAWN      3
#define TILE_BOMB       4   /* Visual marker (bomb on floor) */
```

### GameState

```c
typedef enum {
    SCREEN_LOADING,
    SCREEN_MENU,
    SCREEN_LOBBY,
    SCREEN_GAME
} ScreenState;

typedef struct {
    ScreenState     currentScreen;
    Player          players[MAX_PLAYERS];
    short           numPlayers;
    short           localPlayerID;  /* Assigned by IP sort at game start */
    Bomb            bombs[MAX_BOMBS];
    short           numBombs;
    int             gameRunning;    /* Is a round in progress? */
    long            roundStartTick; /* When the round started */
} GameState;
```

## Network Messages

All messages are packed structs sent via PeerTalk. Byte order is big-endian
(native to 68k and PPC — no conversion needed on Classic Mac).

### MSG_POSITION (0x01) — PT_FAST

```c
typedef struct {
    unsigned char playerID;     /* 1 byte */
    unsigned char gridCol;      /* 1 byte */
    unsigned char gridRow;      /* 1 byte */
    unsigned char facing;       /* 1 byte */
    unsigned char animFrame;    /* 1 byte */
} MsgPosition;                  /* 5 bytes total */
```

Sent every time the local player moves. Broadcast to all connected peers.

### MSG_BOMB_PLACED (0x02) — PT_RELIABLE

```c
typedef struct {
    unsigned char playerID;     /* 1 byte */
    unsigned char gridCol;      /* 1 byte */
    unsigned char gridRow;      /* 1 byte */
    unsigned char range;        /* 1 byte */
    unsigned char fuseFrames;   /* 1 byte (for sync) */
} MsgBombPlaced;                /* 5 bytes total */
```

### MSG_BOMB_EXPLODE (0x03) — PT_RELIABLE

```c
typedef struct {
    unsigned char gridCol;      /* 1 byte */
    unsigned char gridRow;      /* 1 byte */
    unsigned char range;        /* 1 byte */
} MsgBombExplode;               /* 3 bytes total */
```

### MSG_BLOCK_DESTROYED (0x04) — PT_RELIABLE

```c
typedef struct {
    unsigned char gridCol;      /* 1 byte */
    unsigned char gridRow;      /* 1 byte */
} MsgBlockDestroyed;            /* 2 bytes total */
```

### MSG_PLAYER_KILLED (0x05) — PT_RELIABLE

```c
typedef struct {
    unsigned char playerID;     /* 1 byte */
    unsigned char killerID;     /* 1 byte (0xFF = self) */
} MsgPlayerKilled;              /* 2 bytes total */
```

### MSG_GAME_START (0x06) — PT_RELIABLE

```c
typedef struct {
    unsigned char numPlayers;   /* 1 byte */
    unsigned char pad;          /* 1 byte (alignment) */
} MsgGameStart;                 /* 2 bytes total */
```

Broadcast by whichever player presses Start in the lobby. All peers are equal —
any player can send this. Clients honor the first MSG_GAME_START received and
ignore duplicates. Player IDs are computed locally by sorting connected peer IPs
(lowest IP = player 0). Spawn corners map to player ID:
0=(1,1), 1=(13,1), 2=(1,11), 3=(13,11).

### MSG_GAME_OVER (0x07) — PT_RELIABLE

```c
typedef struct {
    unsigned char winnerID;     /* 1 byte (0xFF = draw) */
    unsigned char pad;          /* 1 byte */
} MsgGameOver;                  /* 2 bytes total */
```

## PICT Resource IDs

```c
/* Color Macs (32x32 tiles, 8-bit) — IDs 128-199 */
#define rPictTiles          128  /* Tile sheet: 128x32, 4 tiles in row */
#define rPictPlayerP0       129  /* Player 0: white (base sprite) */
#define rPictBomb           130  /* Bomb: 32x32 */
#define rPictExplosion      131  /* Explosion: 32x32 */
#define rPictTitle          132  /* Title graphic: 320x128 */
#define rPictPlayerP1       133  /* Player 1: red (palette swap of P0) */
#define rPictPlayerP2       134  /* Player 2: blue (palette swap of P0) */
#define rPictPlayerP3       135  /* Player 3: yellow (palette swap of P0) */

/* Mac SE (16x16 tiles, 1-bit) — IDs 200-255 */
#define rPictTilesSE        200  /* Tile sheet: 64x16, 4 tiles */
#define rPictPlayerSE       201  /* Player: 16x16, 1-bit (same for all) */
#define rPictBombSE         202  /* Bomb: 16x16 */
#define rPictExplosionSE    203  /* Explosion: 16x16 */
#define rPictTitleSE        204  /* Title graphic: 240x80 */

/* Player PICT lookup: rPictPlayerP0 + playerID */
/* Left/right facing: horizontal flip via CopyBits at runtime */
/* On SE: all players use rPictPlayerSE (mono, indistinguishable) */
```

## Memory Layout Summary

| Entity | Size | Count | Total |
|--------|------|-------|-------|
| Player | ~48 bytes | 4 | 192 bytes |
| Bomb | ~12 bytes | 16 | 192 bytes |
| TileMap | 195 bytes | 1 | 195 bytes |
| GameState | ~100 bytes | 1 | 100 bytes |
| GWorld (background) | ~200 KB | 1 | 200 KB |
| GWorld (work buffer) | ~200 KB | 1 | 200 KB |
| GWorld (tile sheet) | ~4 KB | 1 | 4 KB |
| GWorld (player sprites) | ~4 KB | 1 | 4 KB |
| GWorld (bomb+explosion) | ~2 KB | 2 | 4 KB |
| **Total game data** | | | **~413 KB** |

Plus PeerTalk allocation (~100-200 KB) and Toolbox overhead (~100 KB).
Fits comfortably in 1 MB application heap.
