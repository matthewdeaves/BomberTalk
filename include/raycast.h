/*
 * raycast.h -- Bomb explosion reach along one direction (portable core).
 *
 * A bomb explodes in a cross: from the centre tile, a ray travels up to
 * `range` tiles in each of the four cardinal directions. The ray covers floor
 * tiles, is stopped short by a solid WALL, and is stopped AT (and destroys) the
 * first destructible BLOCK it meets. Out-of-bounds stops it too.
 *
 * Extracted from bomb.c ExplodeBomb (011-macosx-sdl2) as pure geometry with no
 * Toolbox, no globals, no side effects, so the reach rule is host unit tested
 * in tests/test_raycast.c. ExplodeBomb calls this per direction, then re-walks
 * the covered tiles to add explosions / mark dirty and destroys the block at
 * the reach. Explosions are occasional, so the tiny re-walk costs nothing on
 * the Mac SE (Constitution V; see notes/deepening-and-testability.md).
 */
#ifndef RAYCAST_H
#define RAYCAST_H

#include "coredefs.h"

/*
 * Ray_Reach -- How far a bomb blast reaches in one direction.
 *
 *   originCol, originRow  bomb centre tile
 *   dCol, dRow            unit step for this direction (one is +/-1, other 0)
 *   range                 blast range in tiles (>= 0)
 *   mapCols, mapRows      valid tilemap dimensions
 *   tiles                 row-major tile bytes, stride MAX_GRID_COLS
 *   hitBlockOut           receives 1 if the ray stopped on a destructible
 *                         BLOCK (the tile at the returned reach), else 0
 *
 * Returns the reach: the greatest distance (1..range) whose tile is covered by
 * the blast, or 0 if the very first step is a wall or off the map. Tiles at
 * distance 1..reach are covered; if *hitBlockOut is set, the tile at `reach`
 * is the block to destroy. Pure: no globals, no I/O.
 */
short Ray_Reach(short originCol, short originRow, short dCol, short dRow,
                short range, short mapCols, short mapRows,
                const unsigned char *tiles, int *hitBlockOut);

/*
 * ---- Whole-blast plan and hit test ----
 *
 * Blast_Plan runs Ray_Reach in all four directions and lists the covered
 * tiles (centre first, then up, down, left, right, each in distance order --
 * the order the explosion sprites were always added in) and the blocks the
 * rays stopped on. bomb.c applies the plan (sprites, tilemap, network
 * messages); keeping the geometry here keeps the simulation host tested and
 * free of the Net_* calls.
 */
typedef struct {
    short col;
    short row;
} BlastCell;

/* 1 centre + at most (dim - 1) covered tiles each way on the biggest map. */
#define BLAST_MAX_CELLS   (1 + 2 * (MAX_GRID_COLS - 1) + 2 * (MAX_GRID_ROWS - 1))
#define BLAST_MAX_BLOCKS  4

/*
 * Blast_Plan -- Plan a cross blast from (originCol, originRow).
 *
 *   cells        receives up to BLAST_MAX_CELLS covered tiles
 *   blocks       receives up to BLAST_MAX_BLOCKS destroyed blocks
 *   numBlocksOut receives the block count
 *
 * Returns the number of covered tiles (always >= 1, the centre). Pure.
 */
short Blast_Plan(short originCol, short originRow, short range,
                 short mapCols, short mapRows, const unsigned char *tiles,
                 BlastCell *cells, BlastCell *blocks, short *numBlocksOut);

/*
 * Blast_CellHitsBox -- Does tile (col,row), tileSize square, overlap the box
 * [left,right) x [top,bottom)? Strict AABB: touching edges do not overlap.
 */
int Blast_CellHitsBox(short col, short row, short tileSize,
                      short left, short top, short right, short bottom);

/* Same test as an expression macro, for the per-frame explosion loop on the
 * 68k where 008 FR-002 deliberately avoids a call per cell. Arguments are
 * evaluated more than once: pass plain variables. Blast_CellHitsBox is a thin
 * wrapper over this, so the host tests cover the macro. */
#define BLAST_CELL_HITS_BOX(col, row, ts, left, top, right, bottom) \
    (!((right) <= (short)((col) * (ts)) || \
       (left)  >= (short)((short)((col) * (ts)) + (ts)) || \
       (bottom) <= (short)((row) * (ts)) || \
       (top)   >= (short)((short)((row) * (ts)) + (ts))))

#endif /* RAYCAST_H */
