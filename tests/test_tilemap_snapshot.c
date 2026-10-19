/*
 * test_tilemap_snapshot.c -- Host tests for TileMap_ApplySnapshot, the
 * validation behind the join-in-progress MSG_MAP_STATE handler.
 *
 * Regression: the net layer length-checked the RAW cols/rows but the apply
 * step clamped them up to >= 7 before reading, so a tiny (e.g. 2x2) packet
 * passed the check and the loop over-read the payload. Run under ASan the
 * over-read is reported; the explicit checks also assert "rejected packet
 * leaves the live map untouched".
 */
#include "tilemap_parse.h"
#include "test_util.h"
#include <stdlib.h>
#include <string.h>

typedef unsigned char Grid[MAX_GRID_ROWS][MAX_GRID_COLS];

/* The live map every peer shares (kLevel1 / TMAP): 15x13. */
#define LIVE_COLS 15
#define LIVE_ROWS 13

static void fill_live(Grid g)
{
    memset(g, TILE_WALL, sizeof(Grid));
}

static int grid_is_wall(Grid g)
{
    int r, c;
    for (r = 0; r < MAX_GRID_ROWS; r++)
        for (c = 0; c < MAX_GRID_COLS; c++)
            if (g[r][c] != TILE_WALL) return 0;
    return 1;
}

/* Exactly-sized heap payload so ASan flags any read past the end. */
static unsigned char *mk(long n, unsigned char v)
{
    unsigned char *p = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (p != NULL) memset(p, v, (size_t)n);
    return p;
}

static void test_valid_and_exact(void)
{
    Grid g; int rc;
    unsigned char *p = mk(LIVE_COLS * LIVE_ROWS, TILE_FLOOR);

    printf("test_valid_and_exact\n");
    fill_live(g);
    p[0] = TILE_BLOCK;
    p[LIVE_COLS * LIVE_ROWS - 1] = 99;                       /* unknown -> floor */
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS, LIVE_ROWS, p, LIVE_COLS * LIVE_ROWS);
    CHECK_EQ(rc, 1, "exactly-sized valid payload applies, map changed");
    CHECK_EQ(g[0][0], TILE_BLOCK, "tile copied");
    CHECK_EQ(g[LIVE_ROWS - 1][LIVE_COLS - 1], TILE_FLOOR, "unknown tile sanitised");

    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS, LIVE_ROWS, p, LIVE_COLS * LIVE_ROWS);
    CHECK_EQ(rc, 0, "identical snapshot reports unchanged");
    free(p);
}

/*
 * Regression: a snapshot whose dimensions differ from the live map used to be
 * applied and resize gMap.cols/rows, while gGame.playWidth/playHeight (window,
 * offscreen buffers, collision clamp, renderer bounds) were only ever computed
 * once at startup. All peers run the same map, so a differing snapshot is
 * rejected outright and the live map is left alone.
 */
static void test_dimension_mismatch_rejected(void)
{
    Grid g; int rc;
    long n = (long)MAX_GRID_COLS * MAX_GRID_ROWS;
    unsigned char *p = mk(n, TILE_FLOOR);

    printf("test_dimension_mismatch_rejected\n");
    fill_live(g);
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, MAX_GRID_COLS, MAX_GRID_ROWS, p, n);
    CHECK_EQ(rc, -1, "max-size snapshot rejected against 15x13 live map");
    CHECK(grid_is_wall(g), "grid untouched");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, 9, 8, p, 9 * 8);
    CHECK_EQ(rc, -1, "9x8 snapshot rejected");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS + 1, LIVE_ROWS, p, n);
    CHECK_EQ(rc, -1, "cols off by one rejected");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS, LIVE_ROWS - 1, p, n);
    CHECK_EQ(rc, -1, "rows off by one rejected");
    CHECK(grid_is_wall(g), "grid untouched");
    /* The same max-size snapshot is fine when the live map IS max-size. */
    rc = TileMap_ApplySnapshot(g, MAX_GRID_COLS, MAX_GRID_ROWS, MAX_GRID_COLS, MAX_GRID_ROWS, p, n);
    CHECK_EQ(rc, 1, "matching max-size map applies");
    free(p);
}

static void test_tiny_rejected(void)
{
    Grid g; int rc;
    unsigned char *p = mk(2 * 2, TILE_FLOOR);

    printf("test_tiny_rejected\n");
    fill_live(g);
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, 2, 2, p, 4);
    CHECK_EQ(rc, -1, "2x2 rejected (would be clamped to 7x7 and over-read)");
    CHECK(grid_is_wall(g), "grid untouched");
    free(p);
}

static void test_zero_rejected(void)
{
    Grid g; int rc;
    unsigned char *p = mk(1, TILE_FLOOR);

    printf("test_zero_rejected\n");
    fill_live(g);
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, 0, 0, p, 0);
    CHECK_EQ(rc, -1, "0x0 rejected");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, 0, 9, p, 0);
    CHECK_EQ(rc, -1, "0xN rejected");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, -3, 9, p, 1);
    CHECK_EQ(rc, -1, "negative cols rejected");
    CHECK(grid_is_wall(g), "grid untouched");
    free(p);
}

static void test_oversize_rejected(void)
{
    Grid g; int rc;
    unsigned char *p = mk(32 * 26, TILE_FLOOR);

    printf("test_oversize_rejected\n");
    fill_live(g);
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, MAX_GRID_COLS + 1, 10, p, 32 * 26);
    CHECK_EQ(rc, -1, "cols > max rejected");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, 10, MAX_GRID_ROWS + 1, p, 32 * 26);
    CHECK_EQ(rc, -1, "rows > max rejected");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, 255, 255, p, 32 * 26);
    CHECK_EQ(rc, -1, "255x255 rejected");
    CHECK(grid_is_wall(g), "grid untouched");
    free(p);
}

static void test_truncated_rejected(void)
{
    Grid g; int rc;
    unsigned char *p = mk(LIVE_COLS * LIVE_ROWS - 1, TILE_FLOOR);

    printf("test_truncated_rejected\n");
    fill_live(g);
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS, LIVE_ROWS, p, LIVE_COLS * LIVE_ROWS - 1);
    CHECK_EQ(rc, -1, "one byte short rejected");
    CHECK(grid_is_wall(g), "no partial state change");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS, LIVE_ROWS, p, 0);
    CHECK_EQ(rc, -1, "empty payload rejected");
    rc = TileMap_ApplySnapshot(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS, LIVE_ROWS, NULL, 72);
    CHECK_EQ(rc, -1, "NULL payload rejected");
    free(p);
}

/*
 * Regression: the MSG_BLOCK_DESTROYED handler turned ANY in-range non-floor
 * tile into floor, so a peer could delete indestructible walls (or spawn
 * markers). Only TILE_BLOCK may be destroyed.
 */
static void test_destroy_block_only_blocks(void)
{
    Grid g;
    printf("test_destroy_block_only_blocks\n");
    fill_live(g);
    g[3][4] = TILE_BLOCK;
    g[5][6] = TILE_SPAWN;
    g[7][8] = TILE_FLOOR;

    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 4, 3), 1, "block destroyed");
    CHECK_EQ(g[3][4], TILE_FLOOR, "block now floor");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 4, 3), 0, "duplicate is a no-op");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 0, 0), 0, "wall not destroyed");
    CHECK_EQ(g[0][0], TILE_WALL, "wall intact");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 6, 5), 0, "spawn not destroyed");
    CHECK_EQ(g[5][6], TILE_SPAWN, "spawn intact");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 8, 7), 0, "floor is a no-op");
}

static void test_destroy_block_bounds(void)
{
    Grid g;
    printf("test_destroy_block_bounds\n");
    fill_live(g);
    /* Outside the live map but inside the 31x25 array: must not be touched
     * (and must not be reported destroyed). */
    g[LIVE_ROWS][2] = TILE_BLOCK;
    g[2][LIVE_COLS] = TILE_BLOCK;
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 2, LIVE_ROWS), 0, "row == rows rejected");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, LIVE_COLS, 2), 0, "col == cols rejected");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, -1, 2), 0, "negative col rejected");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 2, -1), 0, "negative row rejected");
    CHECK_EQ(TileMap_ClearBlock(g, LIVE_COLS, LIVE_ROWS, 255, 255), 0, "255,255 rejected");
    CHECK_EQ(g[LIVE_ROWS][2], TILE_BLOCK, "out-of-map cell untouched");
    CHECK_EQ(g[2][LIVE_COLS], TILE_BLOCK, "out-of-map cell untouched");
}

int main(void)
{
    printf("test_tilemap_snapshot\n");
    test_valid_and_exact();
    test_dimension_mismatch_rejected();
    test_destroy_block_only_blocks();
    test_destroy_block_bounds();
    test_tiny_rejected();
    test_zero_rejected();
    test_oversize_rejected();
    test_truncated_rejected();
    return TEST_RESULT();
}
