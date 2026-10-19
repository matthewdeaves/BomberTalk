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

/* ---- Blast_Plan / Blast_CellHitsBox ---- */

static short plan(short oc, short or_, short range, BlastCell *cells,
                  BlastCell *blocks, short *nb)
{
    return Blast_Plan(oc, or_, range, COLS, ROWS, gTiles, cells, blocks, nb);
}

static void test_plan_open_cross(void)
{
    BlastCell cells[BLAST_MAX_CELLS], blocks[BLAST_MAX_BLOCKS];
    short nb, n;
    printf("test_plan_open_cross\n");
    reset();
    n = plan(5, 5, 2, cells, blocks, &nb);
    CHECK_EQ(n, 9, "centre + 2 tiles x 4 directions");
    CHECK_EQ(nb, 0, "no blocks");
    CHECK_EQ(cells[0].col, 5, "centre first (col)");
    CHECK_EQ(cells[0].row, 5, "centre first (row)");
    /* order: up, down, left, right, each outward */
    CHECK_EQ(cells[1].row, 4, "up 1"); CHECK_EQ(cells[2].row, 3, "up 2");
    CHECK_EQ(cells[3].row, 6, "down 1"); CHECK_EQ(cells[4].row, 7, "down 2");
    CHECK_EQ(cells[5].col, 4, "left 1"); CHECK_EQ(cells[6].col, 3, "left 2");
    CHECK_EQ(cells[7].col, 6, "right 1"); CHECK_EQ(cells[8].col, 7, "right 2");
    CHECK_EQ(cells[8].row, 5, "right stays on the row");
}

static void test_plan_walls_blocks(void)
{
    BlastCell cells[BLAST_MAX_CELLS], blocks[BLAST_MAX_BLOCKS];
    short nb, n;
    printf("test_plan_walls_blocks\n");
    reset();
    setTile(5, 3, TILE_BLOCK);   /* up: covered at 2, destroyed */
    setTile(5, 6, TILE_WALL);    /* down: nothing covered */
    setTile(2, 5, TILE_BLOCK);   /* left, range 3: block at distance 3 */
    n = plan(5, 5, 3, cells, blocks, &nb);
    /* centre + up(2) + down(0) + left(3) + right(3) */
    CHECK_EQ(n, 1 + 2 + 0 + 3 + 3, "covered count");
    CHECK_EQ(nb, 2, "two blocks hit");
    CHECK_EQ(blocks[0].col, 5, "up block col");
    CHECK_EQ(blocks[0].row, 3, "up block row");
    CHECK_EQ(blocks[1].col, 2, "left block col");
    CHECK_EQ(blocks[1].row, 5, "left block row");
    CHECK_EQ(cells[2].row, 3, "block tile is itself covered");
}

static void test_plan_buffer_bound(void)
{
    BlastCell cells[BLAST_MAX_CELLS], blocks[BLAST_MAX_BLOCKS];
    short nb, n;
    printf("test_plan_buffer_bound\n");
    reset();
    /* Biggest map, huge range, from the centre: never exceeds the bound. */
    n = Blast_Plan(15, 12, 255, MAX_GRID_COLS, MAX_GRID_ROWS, gTiles,
                   cells, blocks, &nb);
    CHECK(n >= 1 && n <= BLAST_MAX_CELLS, "plan fits BLAST_MAX_CELLS");
    CHECK_EQ(n, 1 + 15 + 15 + 12 + 12, "whole cross to the map edges");
    /* Corner origin. */
    n = Blast_Plan(0, 0, 255, MAX_GRID_COLS, MAX_GRID_ROWS, gTiles,
                   cells, blocks, &nb);
    CHECK_EQ(n, 1 + 24 + 30, "corner: down the column and along the row");
}

static void test_cell_hits_box(void)
{
    printf("test_cell_hits_box\n");
    /* Tile (2,3) at 16px = [32,48) x [48,64). */
    CHECK_EQ(Blast_CellHitsBox(2, 3, 16, 40, 50, 56, 66), 1, "inside overlaps");
    CHECK_EQ(Blast_CellHitsBox(2, 3, 16, 47, 63, 60, 80), 1, "corner overlap");
    CHECK_EQ(Blast_CellHitsBox(2, 3, 16, 48, 50, 60, 66), 0, "touching right edge");
    CHECK_EQ(Blast_CellHitsBox(2, 3, 16, 16, 50, 32, 66), 0, "touching left edge");
    CHECK_EQ(Blast_CellHitsBox(2, 3, 16, 40, 64, 56, 80), 0, "touching bottom edge");
    CHECK_EQ(Blast_CellHitsBox(2, 3, 16, 40, 32, 56, 48), 0, "touching top edge");
    CHECK_EQ(Blast_CellHitsBox(2, 3, 32, 70, 100, 90, 120), 1, "32px tiles scale");
}

/* ---- Equivalence with the original interleaved ExplodeBomb loop ---- */

/* Verbatim shape of the pre-refactor ExplodeBomb ray/tile loop (main@eaa6b87):
 * per direction, add the covered tiles, then destroy the block the ray stopped
 * on in the live map (so later directions see the mutated map). */
static const short kOldDCol[4] = {0, 0, -1, 1};
static const short kOldDRow[4] = {-1, 1, 0, 0};

static short oldExplode(short bc, short br, short range, short cols, short rows,
                        unsigned char *map, BlastCell *cells, BlastCell *blocks,
                        short *nblocks)
{
    short dir, dist, col, row, n = 0;
    *nblocks = 0;
    cells[n].col = bc; cells[n].row = br; n++;
    for (dir = 0; dir < 4; dir++) {
        int hitBlock = 0;
        short reach = Ray_Reach(bc, br, kOldDCol[dir], kOldDRow[dir], range,
                                cols, rows, map, &hitBlock);
        for (dist = 1; dist <= reach; dist++) {
            col = (short)(bc + kOldDCol[dir] * dist);
            row = (short)(br + kOldDRow[dir] * dist);
            cells[n].col = col; cells[n].row = row; n++;
        }
        if (hitBlock) {
            col = (short)(bc + kOldDCol[dir] * reach);
            row = (short)(br + kOldDRow[dir] * reach);
            map[row * MAX_GRID_COLS + col] = TILE_FLOOR;
            blocks[*nblocks].col = col; blocks[*nblocks].row = row;
            (*nblocks)++;
        }
    }
    return n;
}

static unsigned long gSeed = 12345UL;
static unsigned rnd(unsigned m)
{
    gSeed = (gSeed * 1103515245UL + 12345UL) & 0x7fffffffUL;
    return (unsigned)((gSeed >> 8) % m);
}

static void test_plan_matches_old_loop(void)
{
    static unsigned char mapA[MAX_GRID_ROWS * MAX_GRID_COLS];
    static unsigned char mapB[MAX_GRID_ROWS * MAX_GRID_COLS];
    BlastCell oc[BLAST_MAX_CELLS], ob[BLAST_MAX_BLOCKS];
    BlastCell nc[BLAST_MAX_CELLS], nbk[BLAST_MAX_BLOCKS];
    int iter, i, bad = 0;
    printf("test_plan_matches_old_loop\n");
    for (iter = 0; iter < 5000; iter++) {
        short cols = (short)(3 + rnd(MAX_GRID_COLS - 2));
        short rows = (short)(3 + rnd(MAX_GRID_ROWS - 2));
        short r, c, bc, br, range, on, nn, onb, nnb;
        memset(mapA, TILE_FLOOR, sizeof(mapA));
        for (r = 0; r < rows; r++)
            for (c = 0; c < cols; c++) {
                unsigned v = rnd(10);
                mapA[r * MAX_GRID_COLS + c] =
                    (unsigned char)(v < 2 ? TILE_WALL : v < 5 ? TILE_BLOCK : TILE_FLOOR);
            }
        bc = (short)rnd(cols); br = (short)rnd(rows);
        range = (short)(rnd(4) == 0 ? rnd(256) : rnd(6));
        memcpy(mapB, mapA, sizeof(mapA));
        on = oldExplode(bc, br, range, cols, rows, mapB, oc, ob, &onb);
        nn = Blast_Plan(bc, br, range, cols, rows, mapA, nc, nbk, &nnb);
        if (on != nn || onb != nnb) { bad++; continue; }
        for (i = 0; i < on; i++)
            if (oc[i].col != nc[i].col || oc[i].row != nc[i].row) { bad++; break; }
        for (i = 0; i < onb; i++)
            if (ob[i].col != nbk[i].col || ob[i].row != nbk[i].row) { bad++; break; }
    }
    CHECK_EQ(bad, 0, "5000 random maps: cells and blocks identical, in order");
}

int main(void)
{
    printf("== test_raycast ==\n");
    test_open_floor();
    test_wall_stops_short();
    test_block_destroyed_and_stops();
    test_block_beyond_wall_unreached();
    test_map_edge_stops();
    test_plan_open_cross();
    test_plan_walls_blocks();
    test_plan_buffer_bound();
    test_cell_hits_box();
    test_plan_matches_old_loop();
    return TEST_RESULT();
}
