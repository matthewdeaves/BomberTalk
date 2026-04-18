/*
 * bomb.h -- Bomb state, fuse timer, explosion logic
 */

#ifndef BOMB_H
#define BOMB_H

#include "game.h"

void Bomb_Init(void);
int  Bomb_PlaceAt(short col, short row, short range, unsigned char ownerID);
void Bomb_Update(void);
void Bomb_ForceExplodeAt(short col, short row);
/* Bomb_ExistsAt removed 008 FR-001 — use BOMB_GRID_CELL(col,row) macro
 * with caller-side bounds check. */

/* Explosion state (for rendering) */
#define MAX_EXPLOSIONS 64

typedef struct {
    short col;
    short row;
    short timer;    /* ticks remaining to display */
} Explosion;

const Explosion *Bomb_GetExplosions(short *count);

/*
 * BOMB_GRID_CELL -- Direct bomb-grid read, no bounds check.
 * Callers MUST validate col/row are in range before use.
 * Mirrors TILEMAP_TILE in tilemap.h.
 */
extern unsigned char gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS];
#define BOMB_GRID_CELL(col, row) (gBombGrid[(row)][(col)])

#endif /* BOMB_H */
