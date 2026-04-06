/*
 * tilemap.h -- Tile map data structures and queries
 *
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 2.
 */

#ifndef TILEMAP_H
#define TILEMAP_H

#include "game.h"

void TileMap_Init(void);
unsigned char TileMap_GetTile(short col, short row);
void TileMap_SetTile(short col, short row, unsigned char type);
int  TileMap_IsSolid(short col, short row);
short TileMap_PixelToCol(short pixelX);
short TileMap_PixelToRow(short pixelY);
short TileMap_ColToPixel(short col);
short TileMap_RowToPixel(short row);

/* Access the global tilemap */
TileMap *TileMap_Get(void);

#endif /* TILEMAP_H */
