/*
 * coredefs.h -- Portable gameplay constants (no Toolbox, no platform).
 *
 * These are pure integer constants that the gameplay core (tilemap parsing,
 * collision, bombs, movement, win condition) reasons about. Keeping them free
 * of any Mac Toolbox dependency lets those core units compile and be unit
 * tested on the host with native gcc, not just cross-compiled for a Mac.
 * game.h includes this; nothing here needs game.h. See
 * notes/deepening-and-testability.md.
 */
#ifndef COREDEFS_H
#define COREDEFS_H

/* ---- Grid Constants ---- */
#define GRID_COLS       15
#define GRID_ROWS       13
#define MAX_GRID_COLS   31
#define MAX_GRID_ROWS   25

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
#define PLAYER_NAME_MAX 31  /* max chars in player name (matches PT_NAME_MAX) */
#define MAX_BOMBS       16

#endif /* COREDEFS_H */
