/*
 * tilemap_parse.c -- Portable TMAP data parsing & validation. See
 * tilemap_parse.h.
 *
 * Extracted verbatim from tilemap.c's resource loader (011-macosx-sdl2) so the
 * clamp/size-check/sanitise logic is host-testable without the Resource
 * Manager. Behaviour is unchanged: the Mac loader now supplies the resource
 * bytes and calls this.
 */
#include "tilemap_parse.h"
#include <string.h>   /* memset */

int TileMap_ParseData(const unsigned char *data, long size,
                      unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS],
                      short *outCols, short *outRows)
{
    short cols, rows, r, c;
    long expectedSize;

    if (data == NULL || size < 4) return 0;

    /* Big-endian short: cols then rows */
    cols = (short)((data[0] << 8) | data[1]);
    rows = (short)((data[2] << 8) | data[3]);

    /* Clamp dimensions to the legal grid */
    if (cols < 7) cols = 7;
    if (cols > MAX_GRID_COLS) cols = MAX_GRID_COLS;
    if (rows < 7) rows = 7;
    if (rows > MAX_GRID_ROWS) rows = MAX_GRID_ROWS;

    expectedSize = 4 + (long)cols * rows;
    if (size < expectedSize) return 0;

    /* Clear the entire grid first -- load-bearing, since the fill loop only
     * covers the active [rows x cols] sub-region (008). */
    memset(tiles, TILE_FLOOR, (size_t)MAX_GRID_ROWS * MAX_GRID_COLS);

    /* Copy tile data, sanitising unknown values to floor. */
    data += 4;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            unsigned char tile = data[r * cols + c];
            if (tile > TILE_SPAWN) tile = TILE_FLOOR;
            tiles[r][c] = tile;
        }
    }

    *outCols = cols;
    *outRows = rows;
    return 1;
}

int TileMap_ApplySnapshot(unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS],
                          short *curCols, short *curRows,
                          short cols, short rows,
                          const unsigned char *src, long srcLen)
{
    short r, c;
    int changed = 0;

    if (src == NULL) return -1;

    /* Reject, never clamp: the byte count validated below must be the same
     * cols*rows the loop reads. Validate fully before touching any state. */
    if (cols < 7 || cols > MAX_GRID_COLS) return -1;
    if (rows < 7 || rows > MAX_GRID_ROWS) return -1;
    if (srcLen < (long)cols * rows) return -1;

    if (cols != *curCols || rows != *curRows) changed = 1;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            unsigned char t = src[r * cols + c];
            if (t > TILE_SPAWN) t = TILE_FLOOR;
            if (tiles[r][c] != t) {
                tiles[r][c] = t;
                changed = 1;
            }
        }
    }

    *curCols = cols;
    *curRows = rows;
    return changed;
}
