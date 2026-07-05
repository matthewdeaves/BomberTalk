/*
 * raycast.c -- Bomb explosion reach along one direction. See raycast.h.
 *
 * Extracted (behaviour-preserving) from bomb.c ExplodeBomb's per-direction
 * loop (011-macosx-sdl2) so the reach rule is host unit testable.
 */
#include "raycast.h"

#define TILE_AT(t, col, row)  ((t)[(row) * MAX_GRID_COLS + (col)])

short Ray_Reach(short originCol, short originRow, short dCol, short dRow,
                short range, short mapCols, short mapRows,
                const unsigned char *tiles, int *hitBlockOut)
{
    short dist, col, row;
    unsigned char t;

    *hitBlockOut = 0;

    for (dist = 1; dist <= range; dist++) {
        col = (short)(originCol + dCol * dist);
        row = (short)(originRow + dRow * dist);

        /* Off the map stops the ray short of this step. */
        if (col < 0 || col >= mapCols || row < 0 || row >= mapRows) {
            return (short)(dist - 1);
        }

        t = TILE_AT(tiles, col, row);

        /* A wall blocks the blast without being covered. */
        if (t == TILE_WALL) {
            return (short)(dist - 1);
        }

        /* A destructible block is covered, destroyed, and stops the ray. */
        if (t == TILE_BLOCK) {
            *hitBlockOut = 1;
            return dist;
        }

        /* Floor (or spawn/bomb): covered, ray continues. */
    }

    return range;
}
