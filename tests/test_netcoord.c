/*
 * test_netcoord.c -- Host unit tests for the tile-independent network
 * position conversion (netcoord.c).
 *
 * Regression coverage for the specs/004 class of bugs: raw pixel coords sent
 * between machines with different tile sizes (16px Mac SE vs 32px PPC) caused
 * false explosion kills (wrong grid mapping) and out-of-bounds crashes. The
 * fix normalises to 256 units per tile. These tests pin (a) exact round-trip
 * on one machine and (b) grid-cell agreement across tile sizes -- the whole
 * point of the normalisation.
 */
#include "netcoord.h"
#include "test_util.h"

/* Grid cell of a pixel position, via the tile centre (matches game.h:
 * gridCol = (pixel + tileSize/2) / tileSize). */
static short grid_of(short pixel, short tileSize)
{
    return (short)((pixel + tileSize / 2) / tileSize);
}

/* Round-trip on a single machine must be exact for every tile size. */
static void test_roundtrip_same_size(void)
{
    short sizes[2];
    short si, p;

    printf("test_roundtrip_same_size\n");
    sizes[0] = 16; sizes[1] = 32;

    for (si = 0; si < 2; si++) {
        short ts = sizes[si];
        for (p = 0; p <= ts * 20; p += 3) {
            short wire = NetCoord_ToWire(p, ts);
            short back = NetCoord_ToLocal(wire, ts);
            CHECK_EQ(back, p, "roundtrip exact");
        }
    }
}

/*
 * The property that matters: a player's grid cell on the sending machine is
 * the SAME grid cell after the receiver converts to its own tile size --
 * whatever cell that is. (The center-of-tile pixel col*ts+ts/2 rounds up to
 * col+1 under (pixel+ts/2)/ts, and it does so identically on both sizes, so
 * we compare the two grid_of results directly rather than assuming a cell.)
 * Sweep every column and sub-tile offset, both directions.
 */
static void test_cross_size_grid_agreement(void)
{
    short col, off;

    printf("test_cross_size_grid_agreement\n");

    /* 16px sender -> 32px receiver (exact 2x up-scale). */
    for (col = 0; col < 25; col++) {
        for (off = 0; off < 16; off += 2) {
            short p16 = (short)(col * 16 + off);
            short p32 = NetCoord_ToLocal(NetCoord_ToWire(p16, 16), 32);
            CHECK_EQ(grid_of(p32, 32), grid_of(p16, 16),
                     "16px->32px preserves grid cell");
        }
    }

    /* 32px sender -> 16px receiver (2x down-scale; even offsets so the
     * receiver's 16px grid can represent the position exactly). */
    for (col = 0; col < 25; col++) {
        for (off = 0; off < 32; off += 2) {
            short p32 = (short)(col * 32 + off);
            short p16 = NetCoord_ToLocal(NetCoord_ToWire(p32, 32), 16);
            CHECK_EQ(grid_of(p16, 16), grid_of(p32, 32),
                     "32px->16px preserves grid cell");
        }
    }
}

/*
 * Wire values for the largest legal play area (31 cols) must stay within a
 * 16-bit short -- an overflow here is exactly what crashed the smaller-tiled
 * machine when it indexed an out-of-bounds grid cell.
 */
static void test_no_overflow_max_field(void)
{
    short maxPixel32 = (short)(31 * 32);   /* 992 */
    short maxPixel16 = (short)(31 * 16);   /* 496 */
    short w32, w16, back;

    printf("test_no_overflow_max_field\n");
    w32 = NetCoord_ToWire(maxPixel32, 32);
    w16 = NetCoord_ToWire(maxPixel16, 16);
    CHECK(w32 > 0, "max 32px wire positive (no short overflow)");
    CHECK(w16 > 0, "max 16px wire positive (no short overflow)");
    CHECK_EQ(w32, w16, "same physical field maps to same wire range");

    back = NetCoord_ToLocal(w32, 32);
    CHECK_EQ(back, maxPixel32, "max field round-trips");
}

int main(void)
{
    printf("== test_netcoord ==\n");
    test_roundtrip_same_size();
    test_cross_size_grid_agreement();
    test_no_overflow_max_field();
    return TEST_RESULT();
}
