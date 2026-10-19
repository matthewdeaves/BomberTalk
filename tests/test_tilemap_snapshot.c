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

static void fill_live(Grid g, short *cols, short *rows)
{
    memset(g, TILE_WALL, sizeof(Grid));
    *cols = 15;
    *rows = 13;
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
    Grid g; short oc, orr; int rc;
    unsigned char *p = mk(9 * 8, TILE_FLOOR);

    printf("test_valid_and_exact\n");
    fill_live(g, &oc, &orr);
    p[0] = TILE_BLOCK;
    p[9 * 8 - 1] = 99;                       /* unknown -> floor */
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 9, 8, p, 9 * 8);
    CHECK_EQ(rc, 1, "exactly-sized valid payload applies, map changed");
    CHECK_EQ(oc, 9, "cols updated");
    CHECK_EQ(orr, 8, "rows updated");
    CHECK_EQ(g[0][0], TILE_BLOCK, "tile copied");
    CHECK_EQ(g[7][8], TILE_FLOOR, "unknown tile sanitised");

    rc = TileMap_ApplySnapshot(g, &oc, &orr, 9, 8, p, 9 * 8);
    CHECK_EQ(rc, 0, "identical snapshot reports unchanged");
    free(p);
}

static void test_full_size_valid(void)
{
    Grid g; short oc, orr; int rc;
    long n = (long)MAX_GRID_COLS * MAX_GRID_ROWS;
    unsigned char *p = mk(n, TILE_FLOOR);

    printf("test_full_size_valid\n");
    fill_live(g, &oc, &orr);
    rc = TileMap_ApplySnapshot(g, &oc, &orr, MAX_GRID_COLS, MAX_GRID_ROWS, p, n);
    CHECK_EQ(rc, 1, "max-size map applies");
    CHECK_EQ(oc, MAX_GRID_COLS, "max cols");
    CHECK_EQ(orr, MAX_GRID_ROWS, "max rows");
    free(p);
}

static void test_tiny_rejected(void)
{
    Grid g; short oc, orr; int rc;
    unsigned char *p = mk(2 * 2, TILE_FLOOR);

    printf("test_tiny_rejected\n");
    fill_live(g, &oc, &orr);
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 2, 2, p, 4);
    CHECK_EQ(rc, -1, "2x2 rejected (would be clamped to 7x7 and over-read)");
    CHECK_EQ(oc, 15, "cols untouched");
    CHECK_EQ(orr, 13, "rows untouched");
    CHECK(grid_is_wall(g), "grid untouched");
    free(p);
}

static void test_zero_rejected(void)
{
    Grid g; short oc, orr; int rc;
    unsigned char *p = mk(1, TILE_FLOOR);

    printf("test_zero_rejected\n");
    fill_live(g, &oc, &orr);
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 0, 0, p, 0);
    CHECK_EQ(rc, -1, "0x0 rejected");
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 0, 9, p, 0);
    CHECK_EQ(rc, -1, "0xN rejected");
    rc = TileMap_ApplySnapshot(g, &oc, &orr, -3, 9, p, 1);
    CHECK_EQ(rc, -1, "negative cols rejected");
    CHECK_EQ(oc, 15, "cols untouched");
    CHECK(grid_is_wall(g), "grid untouched");
    free(p);
}

static void test_oversize_rejected(void)
{
    Grid g; short oc, orr; int rc;
    unsigned char *p = mk(32 * 26, TILE_FLOOR);

    printf("test_oversize_rejected\n");
    fill_live(g, &oc, &orr);
    rc = TileMap_ApplySnapshot(g, &oc, &orr, MAX_GRID_COLS + 1, 10, p, 32 * 26);
    CHECK_EQ(rc, -1, "cols > max rejected");
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 10, MAX_GRID_ROWS + 1, p, 32 * 26);
    CHECK_EQ(rc, -1, "rows > max rejected");
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 255, 255, p, 32 * 26);
    CHECK_EQ(rc, -1, "255x255 rejected");
    CHECK_EQ(orr, 13, "rows untouched");
    CHECK(grid_is_wall(g), "grid untouched");
    free(p);
}

static void test_truncated_rejected(void)
{
    Grid g; short oc, orr; int rc;
    unsigned char *p = mk(9 * 8 - 1, TILE_FLOOR);

    printf("test_truncated_rejected\n");
    fill_live(g, &oc, &orr);
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 9, 8, p, 9 * 8 - 1);
    CHECK_EQ(rc, -1, "one byte short rejected");
    CHECK_EQ(oc, 15, "cols untouched");
    CHECK(grid_is_wall(g), "no partial state change");
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 9, 8, p, 0);
    CHECK_EQ(rc, -1, "empty payload rejected");
    rc = TileMap_ApplySnapshot(g, &oc, &orr, 9, 8, NULL, 72);
    CHECK_EQ(rc, -1, "NULL payload rejected");
    free(p);
}

int main(void)
{
    printf("test_tilemap_snapshot\n");
    test_valid_and_exact();
    test_full_size_valid();
    test_tiny_rejected();
    test_zero_rejected();
    test_oversize_rejected();
    test_truncated_rejected();
    return TEST_RESULT();
}
