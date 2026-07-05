/*
 * game.h -- Master header for BomberTalk
 *
 * Constants, types, resource IDs, timing, key codes.
 * Source: Black Art of Macintosh Game Programming (1996),
 *         BOMBERMAN_CLONE_PLAN.md Phase 1.
 */

#ifndef GAME_H
#define GAME_H

/* Mac Toolbox headers.
 *
 * On the Classic Mac builds (Retro68/RetroPPC) these are the Universal
 * Interfaces, included individually. On the OS X Carbon build (BT_CARBON,
 * see tools/build-macosx.sh) the same managers live under the single
 * <Carbon/Carbon.h> umbrella — the individual headers are not on the
 * include path there. Color QuickDraw, GWorld, Windows, Events, Menus,
 * Resources, etc. are all present via Carbon (011-macosx-sdl2). */
#ifdef BT_CARBON
#include <Carbon/Carbon.h>
#elif defined(BT_POSIX)
/* Modern/POSIX SDL2 build (011-macosx-sdl2). There is no Mac Toolbox here.
 * mac_shim.h supplies the handful of Toolbox value types (Rect/Point/RGBColor/
 * Str255) and the tiny QuickDraw text/rect subset the shared screens still
 * call between Renderer_BeginScreenDraw/EndScreenDraw. The renderer, input,
 * and main loop are provided by the SDL backends (renderer_sdl.c, input_sdl.c,
 * main_posix.c). See notes/deepening-and-testability.md and
 * specs/011-macosx-sdl2/plan.md. */
#include "mac_shim.h"
#else
#include <Quickdraw.h>
#include <Windows.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Resources.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <Sound.h>
#endif

/* Carbon compatibility shims (011-macosx-sdl2).
 *
 * On Classic Mac a WindowPtr IS a GrafPtr and a GrafPort's fields are
 * directly accessible. Under Carbon both WindowRef and GrafPort are opaque,
 * so port access must go through accessor functions. These macros keep the
 * shared renderer/main code identical on both — the classic path expands to
 * the old struct punning, the Carbon path to the accessors. */
#ifdef BT_CARBON
#define BT_SetWindowPort(w)       SetPortWindowPort(w)
#define BT_WindowCopyBitsBits(w)  GetPortBitMapForCopyBits(GetWindowPort(w))
#define BT_WindowCGrafPort(w)     GetWindowPort(w)
#elif defined(BT_POSIX)
/* No QuickDraw port on SDL2 — the SDL renderer owns the window/surfaces and
 * ignores the WindowPtr handed through the backend-agnostic renderer.h API. */
#define BT_SetWindowPort(w)       ((void)(w))
#define BT_WindowCopyBitsBits(w)  ((void)(w), (void *)0)
#define BT_WindowCGrafPort(w)     ((void)(w), (void *)0)
#else
#define BT_SetWindowPort(w)       SetPort(w)
#define BT_WindowCopyBitsBits(w)  (&(w)->portBits)
#define BT_WindowCGrafPort(w)     ((CGrafPtr)(w))
#endif

/* ---- Protocol Version ---- */
#define BT_PROTOCOL_VERSION 6  /* v6: MSG_MAP_STATE (join-in-progress map sync) */

/* Portable gameplay constants (grid, tile types, directions, timing, limits)
 * live in coredefs.h so the gameplay core is host-testable without the
 * Toolbox. See notes/deepening-and-testability.md. */
#include "coredefs.h"
#define BOMB_FUSE_TICKS             180 /* 3 seconds at 60 ticks/sec */
#define EXPLOSION_DURATION_TICKS     20 /* ~0.33 sec at 60 ticks/sec */
#define DEATH_FLASH_TICKS            60 /* ~1 second of flashing at 60 ticks/sec */
#define DEATH_FLASH_RATE              8 /* toggle visibility every 8 ticks */
#define GAME_OVER_TIMEOUT_TICKS     180 /* 3 second safety timeout for pending game over */
#define HEARTBEAT_TICKS             120 /* ~2 seconds: resend position even when idle */

/* ---- Network Authority & Robustness (005) ---- */
#define DISCONNECT_GRACE_TICKS       90 /* ~1.5s grace before TCP teardown after game over */
#define MESH_FORM_TIMEOUT_TICKS     900 /* ~15s to wait for PeerTalk auto-mesh before starting anyway */
#define GAME_OVER_FAILSAFE_TICKS    120 /* ~2s timeout before non-authority sends game over */
#define LOW_HEAP_WARNING_BYTES   262144L /* 256KB threshold for heap warning */
#define HEAP_CHECK_INTERVAL_TICKS  1800 /* ~30s between periodic heap checks */

/* ---- Resource IDs ---- */
#define rMenuApple      128
#define rMenuFile       129

/* Color Macs (32x32, 8-bit) -- IDs 128-199 */
#define rPictTiles          128
#define rPictPlayerP0       129
#define rPictBomb           130  /* Legacy single-frame; use rPictBombFrame0..2 for animation */
#define rPictExplosion      131
#define rPictTitle          132
#define rPictPlayerP1       133
#define rPictPlayerP2       134
#define rPictPlayerP3       135

/* Bomb fuse animation frames (3 frames, pulse loop 0→1→2→1). */
#define BOMB_ANIM_FRAMES         3
#define BOMB_ANIM_FRAME_TICKS    18  /* ~0.3s per frame at 60 ticks/sec */
#define rPictBombFrame0          140  /* colour Macs, 32x32, indexed */
#define rPictBombFrame1          141
#define rPictBombFrame2          142
#define rPictBombSEFrame0        145  /* Mac SE, 16x16, 1-bit (white = transparent) */
#define rPictBombSEFrame1        146
#define rPictBombSEFrame2        147

/* Splash / loading-screen backgrounds (full-window PICTs). */
#define rPictSplashColor         150  /* 480x416, 256 colours */
#define rPictSplashSE            250  /* 240x208, 1-bit */

/* Mac SE (16x16, 1-bit) -- IDs 200-255 */
#define rPictTilesSE        200
#define rPictPlayerSE       201
#define rPictBombSE         202
#define rPictExplosionSE    203
#define rPictTitleSE        204

/* ---- Key Codes (hardware scan codes for GetKeys) ---- */
/* Source: Black Art of Macintosh Game Programming, p.87 */
#define KEY_UP_ARROW    0x7E
#define KEY_DOWN_ARROW  0x7D
#define KEY_LEFT_ARROW  0x7B
#define KEY_RIGHT_ARROW 0x7C
#define KEY_SPACE       0x31
#define KEY_RETURN      0x24
#define KEY_ESCAPE      0x35
#define KEY_Q           0x0C
#define KEY_F           0x03

/* ---- Network Message Types ---- */
#define MSG_POSITION        0x01
#define MSG_BOMB_PLACED     0x02
#define MSG_BOMB_EXPLODE    0x03
#define MSG_BLOCK_DESTROYED 0x04
#define MSG_PLAYER_KILLED   0x05
#define MSG_GAME_START      0x06
#define MSG_GAME_OVER       0x07
#define MSG_MAP_STATE       0x08  /* full tilemap snapshot -> a late joiner */

/* ---- Boolean (C89 has no bool) ---- */
#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

/* ---- Screen States ---- */
typedef enum {
    SCREEN_LOADING,
    SCREEN_MENU,
    SCREEN_LOBBY,
    SCREEN_GAME
} ScreenState;

/* ---- Smooth Movement Constants ---- */
#define HITBOX_INSET_LARGE  4   /* pixels inset on 32px tiles (color Macs) */
#define HITBOX_INSET_SMALL  2   /* pixels inset on 16px tiles (Mac SE) */
#define NUDGE_THRESHOLD_LARGE 10 /* corner sliding threshold on 32px tiles */
#define NUDGE_THRESHOLD_SMALL  5 /* corner sliding threshold on 16px tiles */
#define INTERP_TICKS        4   /* interpolation half-life in ticks (~67ms) */

/* ---- Network Message Structs ---- */
typedef struct {
    unsigned char playerID;
    unsigned char facing;
    short         pixelX;
    short         pixelY;
    unsigned char pad[2];
} MsgPosition;

typedef struct {
    unsigned char playerID;
    unsigned char gridCol;
    unsigned char gridRow;
    unsigned char range;
    unsigned char fuseTicks;
} MsgBombPlaced;

typedef struct {
    unsigned char gridCol;
    unsigned char gridRow;
    unsigned char range;
} MsgBombExplode;

typedef struct {
    unsigned char gridCol;
    unsigned char gridRow;
} MsgBlockDestroyed;

typedef struct {
    unsigned char playerID;
    unsigned char killerID;
} MsgPlayerKilled;

typedef struct {
    unsigned char numPlayers;
    unsigned char version;
} MsgGameStart;

typedef struct {
    unsigned char winnerID;
    unsigned char pad;
} MsgGameOver;

/* Full tilemap snapshot sent directly to a late joiner so it inherits the
 * in-progress map (destroyed blocks), not a fresh one. All-byte layout: no
 * alignment concerns, no byte-swapping. Only 2 + cols*rows bytes are sent. */
typedef struct {
    unsigned char cols;
    unsigned char rows;
    unsigned char tiles[MAX_GRID_ROWS * MAX_GRID_COLS];  /* row-major */
} MsgMapState;

/* ---- Player Stats (for future power-ups / character editor) ---- */
typedef struct {
    short bombsMax;     /* Maximum bombs placeable at once (default 1) */
    short bombRange;    /* Explosion range in tiles (default 1) */
    short speedTicks;   /* Movement cooldown in ticks (default 12 = ~0.2s) */
} PlayerStats;

/* ---- Game Entity Structs ---- */
typedef struct {
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
    short       deathTimer;  /* ticks remaining for death flash (0=alive or fully dead) */
    short       bombsAvailable;
    PlayerStats stats;
    unsigned char playerID;
    char        name[PLAYER_NAME_MAX + 1];
    void        *peer; /* PT_Peer* -- opaque to avoid header dep */
} Player;

typedef struct {
    short       gridCol;
    short       gridRow;
    short       fuseTimer;
    short       range;
    unsigned char ownerID;
    int         active;
} Bomb;

typedef struct {
    short cols;
    short rows;
    unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS];
    short spawnCols[MAX_PLAYERS];
    short spawnRows[MAX_PLAYERS];
    short spawnCount;
} TileMap;

/* ---- Global Game State ---- */
typedef struct {
    ScreenState     currentScreen;
    Player          players[MAX_PLAYERS];
    short           numPlayers;
    short           localPlayerID;
    Bomb            bombs[MAX_BOMBS];
    short           numActiveBombs;
    int             gameRunning;
    long            roundStartTick;
    int             gameStartReceived;
    short           tileSize;
    short           playWidth;
    short           playHeight;
    int             isMacSE;
    short           deltaTicks;     /* actual elapsed ticks since last frame */
    int             showFPS;
    short           fpsValue;
    int             pendingGameOver;  /* remote game over received, wait for death anims */
    unsigned char   pendingWinner;    /* winner ID from remote MSG_GAME_OVER */
    unsigned long   gameOverTimeoutStart;  /* TickCount() when started, 0 = inactive */
    unsigned long   disconnectGraceStart;  /* TickCount() when started, 0 = inactive */
    int             gameOverAuthority;     /* TRUE if this machine sends MSG_GAME_OVER */
    int             localGameOverDetected; /* TRUE if local game over detected, not authority */
    unsigned long   gameOverFailsafeStart; /* TickCount() when started, 0 = inactive */
    long            heapCheckTimer;        /* ticks since last periodic heap check */
    WindowPtr       window;
} GameState;

/* The single global game state */
extern GameState gGame;

/* Request clean quit (sets main loop flag; do NOT call ExitToShell directly) */
void Game_RequestQuit(void);

#endif /* GAME_H */
