/*
 * game.h -- Master header for BomberTalk
 *
 * Constants, types, resource IDs, timing, key codes.
 * Source: Black Art of Macintosh Game Programming (1996),
 *         BOMBERMAN_CLONE_PLAN.md Phase 1.
 */

#ifndef GAME_H
#define GAME_H

/* Classic Mac headers via Retro68 universal headers */
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

/* ---- Grid Constants ---- */
#define GRID_COLS       15
#define GRID_ROWS       13

/* Tile size is set at runtime based on screen dimensions:
 * 32x32 for 640x480+ (color Macs), 16x16 for 512x342 (Mac SE).
 * These are the defaults for color Macs. */
#define TILE_SIZE_LARGE 32
#define TILE_SIZE_SMALL 16

#define PLAY_WIDTH_LARGE  (GRID_COLS * TILE_SIZE_LARGE)  /* 480 */
#define PLAY_HEIGHT_LARGE (GRID_ROWS * TILE_SIZE_LARGE)  /* 416 */
#define PLAY_WIDTH_SMALL  (GRID_COLS * TILE_SIZE_SMALL)  /* 240 */
#define PLAY_HEIGHT_SMALL (GRID_ROWS * TILE_SIZE_SMALL)  /* 208 */

/* ---- Tile Types ---- */
#define TILE_FLOOR      0
#define TILE_WALL       1
#define TILE_BLOCK      2
#define TILE_SPAWN      3
#define TILE_BOMB       4

/* ---- Directions ---- */
#define DIR_NONE        0
#define DIR_UP          1
#define DIR_DOWN        2
#define DIR_LEFT        3
#define DIR_RIGHT       4

/* ---- Timing ---- */
#define FRAME_TICKS     2   /* Game updates every 2 ticks (~30 fps) */
#define EVENT_TICKS     0   /* WaitNextEvent sleep (0 = don't yield) */

/* ---- Player / Bomb Limits ---- */
#define MAX_PLAYERS     4
#define MAX_BOMBS       16
#define BOMB_FUSE_TICKS 180 /* 3 seconds at 60 ticks/sec */
#define DEATH_FLASH_TICKS  60 /* ~1 second of flashing at 60 ticks/sec */
#define DEATH_FLASH_RATE    8  /* toggle visibility every 8 ticks */

/* ---- Resource IDs ---- */
#define rMenuApple      128
#define rMenuFile       129

/* Color Macs (32x32, 8-bit) -- IDs 128-199 */
#define rPictTiles          128
#define rPictPlayerP0       129
#define rPictBomb           130
#define rPictExplosion      131
#define rPictTitle          132
#define rPictPlayerP1       133
#define rPictPlayerP2       134
#define rPictPlayerP3       135

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

/* ---- Network Message Structs ---- */
typedef struct {
    unsigned char playerID;
    unsigned char gridCol;
    unsigned char gridRow;
    unsigned char facing;
    unsigned char animFrame;
} MsgPosition;

typedef struct {
    unsigned char playerID;
    unsigned char gridCol;
    unsigned char gridRow;
    unsigned char range;
    unsigned char fuseFrames;
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
    unsigned char pad;
} MsgGameStart;

typedef struct {
    unsigned char winnerID;
    unsigned char pad;
} MsgGameOver;

/* ---- Game Entity Structs ---- */
typedef struct {
    short       gridCol;
    short       gridRow;
    short       pixelX;
    short       pixelY;
    short       facing;
    short       animFrame;
    int         alive;
    int         active;
    short       deathTimer;  /* frames remaining for death flash (0=alive or fully dead) */
    short       bombsAvailable;
    short       bombRange;
    unsigned char playerID;
    char        name[32];
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
    unsigned char tiles[GRID_ROWS][GRID_COLS];
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
    WindowPtr       window;
} GameState;

/* The single global game state */
extern GameState gGame;

#endif /* GAME_H */
