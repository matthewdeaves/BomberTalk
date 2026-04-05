/*
 * tilemap.c -- Tile map data structures and queries
 *
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 2.
 */

#include "tilemap.h"
#include "../maps/level1.h"

static TileMap gMap;

void TileMap_Init(void)
{
    short r, c;
    for (r = 0; r < GRID_ROWS; r++) {
        for (c = 0; c < GRID_COLS; c++) {
            gMap.tiles[r][c] = kLevel1[r][c];
        }
    }
}

TileMap *TileMap_Get(void)
{
    return &gMap;
}

unsigned char TileMap_GetTile(short col, short row)
{
    if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) {
        return TILE_WALL;
    }
    return gMap.tiles[row][col];
}

void TileMap_SetTile(short col, short row, unsigned char type)
{
    if (col >= 0 && col < GRID_COLS && row >= 0 && row < GRID_ROWS) {
        gMap.tiles[row][col] = type;
    }
}

int TileMap_IsSolid(short col, short row)
{
    unsigned char tile;
    if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) {
        return TRUE;
    }
    tile = gMap.tiles[row][col];
    return (tile == TILE_WALL || tile == TILE_BLOCK);
}

short TileMap_PixelToCol(short pixelX)
{
    return pixelX / gGame.tileSize;
}

short TileMap_PixelToRow(short pixelY)
{
    return pixelY / gGame.tileSize;
}

short TileMap_ColToPixel(short col)
{
    return col * gGame.tileSize;
}

short TileMap_RowToPixel(short row)
{
    return row * gGame.tileSize;
}
