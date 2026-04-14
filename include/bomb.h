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
int  Bomb_ExistsAt(short col, short row);

/* Explosion state (for rendering) */
#define MAX_EXPLOSIONS 64

typedef struct {
    short col;
    short row;
    short timer;    /* ticks remaining to display */
} Explosion;

const Explosion *Bomb_GetExplosions(short *count);

#endif /* BOMB_H */
