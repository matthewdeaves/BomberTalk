/*
 * test_tilemap_parse.c -- Host unit tests for TileMap_ParseData
 * (tilemap_parse.c).
 *
 * The TMAP parser is the guard against a malformed map crashing the game: it
 * clamps declared dimensions into the legal grid (an over-large value would
 * otherwise index out of bounds on the machine with the smaller tile grid)
 * and sanitises unknown tile bytes to floor. These tests pin that.
 */
#include "tilemap_parse.h"
#include "test_util.h"
#include <string.h>

static void put_header(unsigned char *buf, short cols, short rows)
{
    buf[0] = (unsigned char)((cols >> 8) & 0xFF);
    buf[1] = (unsigned char)(cols & 0xFF);
    buf[2] = (unsigned char)((rows >> 8) & 0xFF);
    buf[3] = (unsigned char)(rows & 0xFF);
}

/* A well-formed 15x13 map: dims, sanitised tiles, real tiles preserved. */
static void test_valid_parse(void)
{
    unsigned char buf[4 + 15 * 13];
    unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS];
    short oc, orr;
    int rc;

    printf("test_valid_parse\n");
    put_header(buf, 15, 13);
    memset(buf + 4, TILE_FLOOR, 15 * 13);
    buf[4 + 0] = TILE_WALL;             /* (row0,col0) */
    buf[4 + 1] = 99;                    /* (row0,col1) unknown -> floor */
    buf[4 + 1 * 15 + 2] = TILE_BLOCK;   /* (row1,col2) */
    buf[4 + 12 * 15 + 14] = TILE_SPAWN; /* (row12,col14) corner */

    rc = TileMap_ParseData(buf, (long)sizeof(buf), tiles, &oc, &orr);
    CHECK_EQ(rc, 1, "valid map parses");
    CHECK_EQ(oc, 15, "cols");
    CHECK_EQ(orr, 13, "rows");
    CHECK_EQ(tiles[0][0], TILE_WALL, "wall preserved");
    CHECK_EQ(tiles[0][1], TILE_FLOOR, "unknown tile sanitised to floor");
    CHECK_EQ(tiles[1][2], TILE_BLOCK, "block preserved");
    CHECK_EQ(tiles[12][14], TILE_SPAWN, "spawn preserved at corner");
}

/* Oversized dims clamp DOWN to the max grid; undersized clamp UP to 7. */
static void test_dimension_clamping(void)
{
    unsigned char big[4 + MAX_GRID_ROWS * MAX_GRID_COLS];
    unsigned char small[4 + 7 * 7];
    unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS];
    short oc, orr;
    int rc;

    printf("test_dimension_clamping\n");

    put_header(big, 200, 200);
    memset(big + 4, TILE_FLOOR, MAX_GRID_ROWS * MAX_GRID_COLS);
    rc = TileMap_ParseData(big, (long)sizeof(big), tiles, &oc, &orr);
    CHECK_EQ(rc, 1, "oversized map still parses");
    CHECK_EQ(oc, MAX_GRID_COLS, "cols clamped down to max");
    CHECK_EQ(orr, MAX_GRID_ROWS, "rows clamped down to max");

    put_header(small, 1, 1);
    memset(small + 4, TILE_FLOOR, 7 * 7);
    rc = TileMap_ParseData(small, (long)sizeof(small), tiles, &oc, &orr);
    CHECK_EQ(rc, 1, "undersized map still parses");
    CHECK_EQ(oc, 7, "cols clamped up to 7");
    CHECK_EQ(orr, 7, "rows clamped up to 7");
}

/* The whole grid is cleared before the active region is filled, so tiles
 * outside a small map are floor (not stale garbage). */
static void test_full_grid_cleared(void)
{
    unsigned char buf[4 + 7 * 7];
    unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS];
    short oc, orr;

    printf("test_full_grid_cleared\n");
    memset(tiles, 0xFF, sizeof(tiles));   /* pre-fill with garbage */
    put_header(buf, 7, 7);
    memset(buf + 4, TILE_WALL, 7 * 7);

    TileMap_ParseData(buf, (long)sizeof(buf), tiles, &oc, &orr);
    CHECK_EQ(tiles[6][6], TILE_WALL, "inside active region filled");
    CHECK_EQ(tiles[MAX_GRID_ROWS - 1][MAX_GRID_COLS - 1], TILE_FLOOR,
             "outside active region cleared to floor");
    CHECK_EQ(tiles[7][0], TILE_FLOOR, "row past active region cleared");
}

/* Absent, truncated, or size-short data is rejected (caller uses the default
 * level) and the destination is left untouched. */
static void test_rejects_bad_data(void)
{
    unsigned char buf[4 + 15 * 13];
    unsigned char tiles[MAX_GRID_ROWS][MAX_GRID_COLS];
    short oc, orr;

    printf("test_rejects_bad_data\n");
    oc = orr = -1;

    CHECK_EQ(TileMap_ParseData((const unsigned char *)0, 100, tiles, &oc, &orr),
             0, "NULL data rejected");
    CHECK_EQ(TileMap_ParseData(buf, 3, tiles, &oc, &orr), 0,
             "fewer than 4 bytes rejected");

    /* Declares 15x13 (needs 4+195=199 bytes) but only 100 supplied. */
    put_header(buf, 15, 13);
    CHECK_EQ(TileMap_ParseData(buf, 100, tiles, &oc, &orr), 0,
             "size shorter than declared map rejected");
    CHECK_EQ(oc, -1, "outputs untouched on rejection");
}

int main(void)
{
    printf("== test_tilemap_parse ==\n");
    test_valid_parse();
    test_dimension_clamping();
    test_full_grid_cleared();
    test_rejects_bad_data();
    return TEST_RESULT();
}
