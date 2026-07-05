/*
 * tilemap.c -- Tile map data structures and queries
 *
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 2.
 */

#include "tilemap.h"
#include "tilemap_parse.h"
#include "../maps/level1.h"
#include <clog.h>
#include <string.h>

static TileMap gMap;
static TileMap gInitialMap;

/* Default spawn corners (fallback when map has insufficient spawns) */
static const short kDefaultSpawnCols[MAX_PLAYERS] = {1, 13, 1, 13};
static const short kDefaultSpawnRows[MAX_PLAYERS] = {1, 1, 11, 11};

/*
 * TileMap_LoadFromResource -- Load map from 'TMAP' resource 128.
 *
 * Format: 2-byte cols + 2-byte rows + (cols*rows) bytes of tile data.
 * Falls back to static level1.h if resource not found or invalid.
 */
static void TileMap_LoadFromResource(void)
{
    Handle h;

    h = GetResource('TMAP', 128);
    if (h == NULL) {
        CLOG_INFO("TMAP not found, using default level");
        return;
    }

    if (GetHandleSize(h) < 4) {
        CLOG_WARN("TMAP resource too small, using default level");
        ReleaseResource(h);
        return;
    }

    HLock(h);

    /* Parse + validate (clamp dims, size-check, sanitise tiles) in the
     * portable, host-tested tilemap_parse.c. */
    if (!TileMap_ParseData((const unsigned char *)*h, GetHandleSize(h),
                           gMap.tiles, &gMap.cols, &gMap.rows)) {
        CLOG_WARN("TMAP malformed or size mismatch, using default level");
        HUnlock(h);
        ReleaseResource(h);
        return;
    }

    HUnlock(h);
    ReleaseResource(h);

    CLOG_INFO("TMAP resource loaded: %dx%d", gMap.cols, gMap.rows);
}

/*
 * TileMap_ScanSpawns -- Find TILE_SPAWN tiles and store spawn positions.
 *
 * Scans top-left to bottom-right. If fewer than MAX_PLAYERS spawns found,
 * fills remaining with default corners (skipping duplicates).
 */
static void TileMap_ScanSpawns(void)
{
    short r, c, i, j;
    short count = 0;

    /* Scan for TILE_SPAWN in row-major order */
    for (r = 0; r < gMap.rows && count < MAX_PLAYERS; r++) {
        for (c = 0; c < gMap.cols && count < MAX_PLAYERS; c++) {
            if (gMap.tiles[r][c] == TILE_SPAWN) {
                gMap.spawnCols[count] = c;
                gMap.spawnRows[count] = r;
                count++;
            }
        }
    }

    /* Fill remaining slots with default corners, skipping duplicates */
    for (i = 0; i < MAX_PLAYERS && count < MAX_PLAYERS; i++) {
        int dup = FALSE;
        for (j = 0; j < count; j++) {
            if (gMap.spawnCols[j] == kDefaultSpawnCols[i] &&
                gMap.spawnRows[j] == kDefaultSpawnRows[i]) {
                dup = TRUE;
                break;
            }
        }
        if (!dup) {
            gMap.spawnCols[count] = kDefaultSpawnCols[i];
            gMap.spawnRows[count] = kDefaultSpawnRows[i];
            count++;
        }
    }

    gMap.spawnCount = count;
    CLOG_INFO("Found %d spawn points in map", gMap.spawnCount);
}

void TileMap_Init(void)
{
    short r, c;

    /* Start with defaults from static level data */
    memset(gMap.tiles, TILE_FLOOR, sizeof(gMap.tiles));
    gMap.cols = GRID_COLS;
    gMap.rows = GRID_ROWS;

    for (r = 0; r < GRID_ROWS; r++) {
        for (c = 0; c < GRID_COLS; c++) {
            gMap.tiles[r][c] = kLevel1[r][c];
        }
    }

    /* Try to load from resource (overrides defaults if found) */
    TileMap_LoadFromResource();

    /* Scan spawn points */
    TileMap_ScanSpawns();

    /* Cache initial map state for fast round resets */
    memcpy(&gInitialMap, &gMap, sizeof(TileMap));
}

void TileMap_Reset(void)
{
    CLOG_INFO("TileMap reset to initial state");
    memcpy(&gMap, &gInitialMap, sizeof(TileMap));
}

TileMap *TileMap_Get(void)
{
    return &gMap;
}

unsigned char TileMap_GetTile(short col, short row)
{
    if (col < 0 || col >= gMap.cols || row < 0 || row >= gMap.rows) {
        return TILE_WALL;
    }
    return gMap.tiles[row][col];
}

void TileMap_SetTile(short col, short row, unsigned char type)
{
    if (col >= 0 && col < gMap.cols && row >= 0 && row < gMap.rows) {
        gMap.tiles[row][col] = type;
    }
}

short TileMap_GetCols(void)
{
    return gMap.cols;
}

short TileMap_GetRows(void)
{
    return gMap.rows;
}

short TileMap_GetSpawnCol(short index)
{
    if (index < 0 || index >= gMap.spawnCount) return 1;
    return gMap.spawnCols[index];
}

short TileMap_GetSpawnRow(short index)
{
    if (index < 0 || index >= gMap.spawnCount) return 1;
    return gMap.spawnRows[index];
}

