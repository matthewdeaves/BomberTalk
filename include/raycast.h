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

#endif /* RAYCAST_H */
