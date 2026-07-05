/*
 * test_collision.c -- Host unit tests for axis-separated AABB tile collision
 * (collision.c).
 *
 * Pins the movement-resolution behaviour the game depends on: free movement
 * over floor, flush stop against walls/blocks on every axis, bomb tiles that
 * are solid unless the player is walking off of them (pass-through), and
 * clamping to the play area. This is the geometry that was previously only
 * exercised by a human on real hardware (specs/004).
 */
#include "collision.h"
#include "test_util.h"
#include <string.h>

#define TS       32
#define COLS     15
#define ROWS     13
#define PW       (COLS * TS)   /* play width  480 */
#define PH       (ROWS * TS)   /* play height 416 */

static unsigned char gTiles[MAX_GRID_ROWS * MAX_GRID_COLS];
static unsigned char gBombs[MAX_GRID_ROWS * MAX_GRID_COLS];

static void reset(void)
{
    memset(gTiles, TILE_FLOOR, sizeof(gTiles));
    memset(gBombs, 0, sizeof(gBombs));
}

static void setTile(short col, short row, unsigned char v)
{
    gTiles[row * MAX_GRID_COLS + col] = v;
}

static void setBomb(short col, short row)
{
    gBombs[row * MAX_GRID_COLS + col] = 1;
}

/* Convenience: resolve one X move from (px,py); returns resulting pixelX. */
static short moveX(short px, short py, short dx, short ptCol, short ptRow)
{
    short ox, oy;
    Collide_ResolveAxis(px, py, dx, 0, TS, PW, PH, COLS, ROWS,
                        gTiles, gBombs, ptCol, ptRow, &ox, &oy);
    return ox;
}

static short moveY(short px, short py, short dy, short ptCol, short ptRow)
{
    short ox, oy;
    Collide_ResolveAxis(px, py, 0, dy, TS, PW, PH, COLS, ROWS,
                        gTiles, gBombs, ptCol, ptRow, &ox, &oy);
    return oy;
}

static void test_free_movement(void)
{
    printf("test_free_movement\n");
    reset();
    /* Player at col 3 (px 96), row 1, all floor -> moves the full delta. */
    CHECK_EQ(moveX(96, 32, 8, -1, -1), 104, "free +8 X over floor");
    CHECK_EQ(moveX(96, 32, -8, -1, -1), 88, "free -8 X over floor");
    CHECK_EQ(moveY(96, 32, 8, -1, -1), 40, "free +8 Y over floor");
    CHECK_EQ(moveY(96, 32, -8, -1, -1), 24, "free -8 Y over floor");
}

static void test_wall_stop(void)
{
    printf("test_wall_stop\n");
    reset();
    setTile(5, 1, TILE_WALL);   /* wall at col 5, row 1 */
    /* Player col 3 (px 96), row 1, big move right: stops flush at col 4
     * (px 128), right edge = 160 = left edge of the wall column. */
    CHECK_EQ(moveX(96, 32, 64, -1, -1), 128, "right stop flush at wall");

    reset();
    setTile(1, 1, TILE_WALL);   /* wall at col 1 */
    /* Player col 3 (px 96) moving left stops flush at col 2 (px 64). */
    CHECK_EQ(moveX(96, 32, -64, -1, -1), 64, "left stop flush at wall");

    reset();
    setTile(1, 1, TILE_WALL);   /* wall above the player */
    /* Player row 3 (py 96) col 1 moving up stops flush at row 2 (py 64). */
    CHECK_EQ(moveY(32, 96, -64, -1, -1), 64, "up stop flush at wall");

    reset();
    setTile(1, 5, TILE_WALL);
    /* Player row 3 (py 96) col 1 moving down stops flush at row 4 (py 128). */
    CHECK_EQ(moveY(32, 96, 64, -1, -1), 128, "down stop flush at wall");
}

static void test_block_stops_like_wall(void)
{
    printf("test_block_stops_like_wall\n");
    reset();
    setTile(5, 1, TILE_BLOCK);
    CHECK_EQ(moveX(96, 32, 64, -1, -1), 128, "destructible block stops sprite");
}

static void test_bomb_solid_and_passthrough(void)
{
    printf("test_bomb_solid_and_passthrough\n");
    reset();
    setBomb(5, 1);
    /* No pass-through: the bomb tile stops the sprite like a wall. */
    CHECK_EQ(moveX(96, 32, 64, -1, -1), 128, "bomb tile is solid by default");
    /* Pass-through that exact cell (walk-off): sprite moves through it. */
    CHECK_EQ(moveX(96, 32, 64, 5, 1), 160, "pass-through bomb cell not solid");
    /* Pass-through a DIFFERENT cell must not unlock this bomb. */
    CHECK_EQ(moveX(96, 32, 64, 6, 1), 128, "wrong pass-through cell stays solid");
}

static void test_play_bounds_clamp(void)
{
    printf("test_play_bounds_clamp\n");
    reset();
    /* Far-right move on empty map clamps the sprite inside the play area. */
    CHECK_EQ(moveX(PW - TS - 4, 32, 100, -1, -1), PW - TS,
             "right edge clamps to playWidth - ts");
    CHECK_EQ(moveX(4, 32, -100, -1, -1), 0, "left edge clamps to 0");
    CHECK_EQ(moveY(32, 4, -100, -1, -1), 0, "top edge clamps to 0");
    CHECK_EQ(moveY(32, PH - TS - 4, 100, -1, -1), PH - TS,
             "bottom edge clamps to playHeight - ts");
}

static void test_null_bombgrid(void)
{
    printf("test_null_bombgrid\n");
    reset();
    /* A NULL bomb grid means no bomb occupancy anywhere -> pure floor move. */
    {
        short ox, oy;
        Collide_ResolveAxis(96, 32, 8, 0, TS, PW, PH, COLS, ROWS,
                            gTiles, 0, -1, -1, &ox, &oy);
        CHECK_EQ(ox, 104, "NULL bombGrid treats all cells bomb-free");
    }
}

int main(void)
{
    printf("== test_collision ==\n");
    test_free_movement();
    test_wall_stop();
    test_block_stops_like_wall();
    test_bomb_solid_and_passthrough();
    test_play_bounds_clamp();
    test_null_bombgrid();
    return TEST_RESULT();
}
