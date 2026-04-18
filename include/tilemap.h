/*
 * tilemap.h -- Tile map data structures and queries
 *
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 2.
 */

#ifndef TILEMAP_H
#define TILEMAP_H

#include "game.h"

void TileMap_Init(void);
void TileMap_Reset(void);
unsigned char TileMap_GetTile(short col, short row);
void TileMap_SetTile(short col, short row, unsigned char type);
/* TileMap_IsSolid removed 008 FR-005 — unused. For solid-tile tests, read
 * TileMap_GetTile(col,row) == TILE_WALL || == TILE_BLOCK, or use the
 * TILEMAP_TILE(map,col,row) macro directly in bounds-checked hot paths. */
short TileMap_GetCols(void);
short TileMap_GetRows(void);
short TileMap_GetSpawnCol(short index);
short TileMap_GetSpawnRow(short index);
/* Access the global tilemap */
TileMap *TileMap_Get(void);

/*
 * TILEMAP_TILE -- Direct array access, no bounds checking.
 * Callers MUST validate col/row are in range before use.
 * For hot-path inner loops only; use TileMap_GetTile() elsewhere.
 */
#define TILEMAP_TILE(map, col, row) ((map)->tiles[(row)][(col)])

#endif /* TILEMAP_H */
