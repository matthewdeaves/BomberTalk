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

/* Explosion directions: up, down, left, right. */
static const short kDCol[4] = {0, 0, -1, 1};
static const short kDRow[4] = {-1, 1, 0, 0};

short Blast_Plan(short originCol, short originRow, short range,
                 short mapCols, short mapRows, const unsigned char *tiles,
                 BlastCell *cells, BlastCell *blocks, short *numBlocksOut)
{
    short n = 0, nb = 0, dir, dist, reach;

    cells[n].col = originCol;
    cells[n].row = originRow;
    n++;

    for (dir = 0; dir < 4; dir++) {
        int hitBlock = 0;
        reach = Ray_Reach(originCol, originRow, kDCol[dir], kDRow[dir],
                          range, mapCols, mapRows, tiles, &hitBlock);
        for (dist = 1; dist <= reach; dist++) {
            cells[n].col = (short)(originCol + kDCol[dir] * dist);
            cells[n].row = (short)(originRow + kDRow[dir] * dist);
            n++;
        }
        if (hitBlock) {
            blocks[nb].col = (short)(originCol + kDCol[dir] * reach);
            blocks[nb].row = (short)(originRow + kDRow[dir] * reach);
            nb++;
        }
    }
    *numBlocksOut = nb;
    return n;
}

int Blast_CellHitsBox(short col, short row, short tileSize,
                      short left, short top, short right, short bottom)
{
    return BLAST_CELL_HITS_BOX(col, row, tileSize, left, top, right, bottom);
}
