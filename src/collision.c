/*
 * collision.c -- Axis-separated AABB tile collision. See collision.h.
 *
 * Extracted verbatim (behaviour-preserving) from player.c CollideAxis +
 * CheckTileSolid (011-macosx-sdl2) so the geometry is host unit testable.
 */
#include "collision.h"

/* Direct row-major reads, stride MAX_GRID_COLS -- identical codegen to the
 * old TILEMAP_TILE(map,col,row) / gBombGrid[row][col] 2D indexing. Callers
 * clamp col/row to the map range before the inner loop, same as before. */
#define TILE_AT(t, col, row)  ((t)[(row) * MAX_GRID_COLS + (col)])

void Collide_ResolveAxis(short pixelX, short pixelY, short dx, short dy,
                         short ts, short playWidth, short playHeight,
                         short mapCols, short mapRows,
                         const unsigned char *tiles,
                         const unsigned char *bombGrid,
                         short ptCol, short ptRow,
                         short *outPX, short *outPY)
{
    short newPX, newPY;
    short hLeft, hTop, hRight, hBottom;
    short minCol, maxCol, minRow, maxRow;
    short c, r;

    newPX = (short)(pixelX + dx);
    newPY = (short)(pixelY + dy);

    /* Full sprite rect at the proposed position (no hitbox inset) */
    hLeft   = newPX;
    hTop    = newPY;
    hRight  = (short)(newPX + ts);
    hBottom = (short)(newPY + ts);

    /* Clamp to play area bounds */
    if (hLeft < 0) { newPX = 0; hLeft = 0; hRight = ts; }
    if (hTop < 0) { newPY = 0; hTop = 0; hBottom = ts; }
    if (hRight > playWidth) {
        newPX = (short)(playWidth - ts);
        hLeft = newPX;
        hRight = playWidth;
    }
    if (hBottom > playHeight) {
        newPY = (short)(playHeight - ts);
        hTop = newPY;
        hBottom = playHeight;
    }

    /* Tiles overlapped by the sprite rect */
    minCol = (short)(hLeft / ts);
    maxCol = (short)((hRight - 1) / ts);
    minRow = (short)(hTop / ts);
    maxRow = (short)((hBottom - 1) / ts);

    if (minCol < 0) minCol = 0;
    if (minRow < 0) minRow = 0;
    if (maxCol >= mapCols) maxCol = (short)(mapCols - 1);
    if (maxRow >= mapRows) maxRow = (short)(mapRows - 1);

    for (r = minRow; r <= maxRow; r++) {
        for (c = minCol; c <= maxCol; c++) {
            unsigned char t = TILE_AT(tiles, c, r);
            int solid = (t == TILE_WALL || t == TILE_BLOCK);
            if (!solid && bombGrid != 0 && TILE_AT(bombGrid, c, r)) {
                /* A bomb tile is solid unless it is the pass-through cell. */
                solid = !(c == ptCol && r == ptRow);
            }
            if (solid) {
                /* Push the sprite flush against the tile boundary */
                if (dx > 0) {
                    newPX = (short)(c * ts - ts);
                } else if (dx < 0) {
                    newPX = (short)((c + 1) * ts);
                }
                if (dy > 0) {
                    newPY = (short)(r * ts - ts);
                } else if (dy < 0) {
                    newPY = (short)((r + 1) * ts);
                }
                goto done;
            }
        }
    }

done:
    *outPX = newPX;
    *outPY = newPY;
}
