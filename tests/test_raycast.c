/*
 * test_raycast.c -- Host unit tests for bomb explosion reach (raycast.c).
 *
 * Pins the cross-blast reach rule: floors are covered, a wall stops the ray
 * short (not covered), a destructible block is covered+destroyed+stops, and the
 * map edge stops it. Previously only exercised by blowing up blocks by hand on
 * real Macs.
 */
#include "raycast.h"
#include "test_util.h"
#include <string.h>

#define COLS 15
#define ROWS 13

static unsigned char gTiles[MAX_GRID_ROWS * MAX_GRID_COLS];

static void reset(void)
{
    memset(gTiles, TILE_FLOOR, sizeof(gTiles));
}

static void setTile(short col, short row, unsigned char v)
{
    gTiles[row * MAX_GRID_COLS + col] = v;
}

static short reach(short oc, short or_, short dc, short dr, short range, int *hb)
{
    return Ray_Reach(oc, or_, dc, dr, range, COLS, ROWS, gTiles, hb);
}

static void test_open_floor(void)
{
    int hb;
    printf("test_open_floor\n");
    reset();
    /* Bomb at (7,6), range 2, all floor: reaches the full range every way. */
    CHECK_EQ(reach(7, 6, 1, 0, 2, &hb), 2, "right reaches full range");
    CHECK_EQ(hb, 0, "no block hit going right");
    CHECK_EQ(reach(7, 6, -1, 0, 2, &hb), 2, "left reaches full range");
    CHECK_EQ(reach(7, 6, 0, -1, 2, &hb), 2, "up reaches full range");
    CHECK_EQ(reach(7, 6, 0, 1, 2, &hb), 2, "down reaches full range");
    CHECK_EQ(reach(7, 6, 1, 0, 0, &hb), 0, "zero range covers nothing");
}

static void test_wall_stops_short(void)
{
    int hb;
    printf("test_wall_stops_short\n");
    reset();
    setTile(9, 6, TILE_WALL);   /* two tiles right of (7,6) */
    CHECK_EQ(reach(7, 6, 1, 0, 3, &hb), 1, "wall at dist 2 -> reach 1");
    CHECK_EQ(hb, 0, "wall is not a destroyed block");

    reset();
    setTile(8, 6, TILE_WALL);   /* immediately right */
    CHECK_EQ(reach(7, 6, 1, 0, 3, &hb), 0, "wall at dist 1 -> reach 0");
    CHECK_EQ(hb, 0, "adjacent wall covers nothing");
}

static void test_block_destroyed_and_stops(void)
{
    int hb;
    printf("test_block_destroyed_and_stops\n");
    reset();
    setTile(9, 6, TILE_BLOCK);  /* dist 2 */
    CHECK_EQ(reach(7, 6, 1, 0, 3, &hb), 2, "block at dist 2 -> reach 2");
    CHECK_EQ(hb, 1, "block flagged for destruction");

    reset();
    setTile(8, 6, TILE_BLOCK);  /* dist 1 */
    CHECK_EQ(reach(7, 6, 1, 0, 3, &hb), 1, "block at dist 1 -> reach 1");
    CHECK_EQ(hb, 1, "adjacent block flagged for destruction");
}

static void test_block_beyond_wall_unreached(void)
{
    int hb;
    printf("test_block_beyond_wall_unreached\n");
    reset();
    setTile(8, 6, TILE_WALL);   /* dist 1 */
    setTile(9, 6, TILE_BLOCK);  /* dist 2, shadowed by the wall */
    CHECK_EQ(reach(7, 6, 1, 0, 3, &hb), 0, "wall shadows the block behind it");
    CHECK_EQ(hb, 0, "shadowed block is not destroyed");
}

static void test_map_edge_stops(void)
{
    int hb;
    printf("test_map_edge_stops\n");
    reset();
    /* Bomb at col 1, range 3, blasting left: col 0 covered, col -1 off map. */
    CHECK_EQ(reach(1, 6, -1, 0, 3, &hb), 1, "left edge stops at col 0");
    CHECK_EQ(hb, 0, "edge is not a block");
    /* Bomb at last col blasting right hits the edge immediately. */
    CHECK_EQ(reach((short)(COLS - 1), 6, 1, 0, 3, &hb), 0,
             "right edge at last col -> reach 0");
    /* Top edge. */
    CHECK_EQ(reach(7, 1, 0, -1, 3, &hb), 1, "top edge stops at row 0");
    /* Bottom edge. */
    CHECK_EQ(reach(7, (short)(ROWS - 2), 0, 1, 3, &hb), 1,
             "bottom edge stops at last row");
}

int main(void)
{
    printf("== test_raycast ==\n");
    test_open_floor();
    test_wall_stops_short();
    test_block_destroyed_and_stops();
    test_block_beyond_wall_unreached();
    test_map_edge_stops();
    return TEST_RESULT();
}
