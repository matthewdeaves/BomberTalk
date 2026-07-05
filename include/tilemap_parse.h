/*
 * tilemap_parse.h -- Portable TMAP data parsing & validation.
 *
 * Parses the raw 'TMAP' byte layout (2-byte cols, 2-byte rows, then row-major
 * tile bytes) into a tile grid, clamping dimensions to the legal range and
 * sanitising unknown tile values to floor. Getting this wrong means an
 * out-of-bounds grid index and a crash on the machine with the smaller tile
 * grid -- so it is validated here, host-tested (tests/test_tilemap_parse.c),
 * independent of the Resource Manager that supplies the bytes on a Mac.
 */
#ifndef TILEMAP_PARSE_H
#define TILEMAP_PARSE_H

#include "coredefs.h"   /* MAX_GRID_COLS/ROWS, TILE_* -- no Toolbox */

/*
 * TileMap_ParseData -- Validate and unpack TMAP bytes into a tile grid.
 *
 *   data      raw TMAP bytes (big-endian cols, rows, then cols*rows tiles)
 *   size      number of bytes available at data
 *   tiles     destination grid, fully cleared to TILE_FLOOR then filled
 *   outCols   receives the clamped column count (only on success)
 *   outRows   receives the clamped row count (only on success)
 *
 * Returns 1 on success, 0 if the data is absent/too small or its declared
 * size does not fit (caller then falls back to the built-in level). On
 * failure the destination is left untouched.
 *
 * Dimensions are clamped to [7, MAX_GRID_*]; tile values above TILE_SPAWN are
 * sanitised to TILE_FLOOR.
 */
int TileMap_ParseData(const unsigned char *data, long size,
                      unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS],
                      short *outCols, short *outRows);

#endif /* TILEMAP_PARSE_H */
