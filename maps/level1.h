/*
 * level1.h -- Classic Bomberman level layout
 *
 * 15 columns x 13 rows.
 * 0=floor, 1=wall, 2=block, 3=spawn
 *
 * Walls form a grid of pillars at every even-row/even-column.
 * Border is all walls. Corners have spawn points with clear space.
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 2.
 */

#ifndef LEVEL1_H
#define LEVEL1_H

#include "game.h"

static const unsigned char kLevel1[GRID_ROWS][GRID_COLS] = {
/*       0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  */
/* 0 */ {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
/* 1 */ {1, 3, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 3, 1},
/* 2 */ {1, 0, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1},
/* 3 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/* 4 */ {1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
/* 5 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/* 6 */ {1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
/* 7 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/* 8 */ {1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
/* 9 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/*10 */ {1, 0, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1},
/*11 */ {1, 3, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 3, 1},
/*12 */ {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
};

#endif /* LEVEL1_H */
