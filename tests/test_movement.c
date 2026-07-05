/*
 * test_movement.c -- Host unit tests for the movement accumulator
 * (movement.c).
 *
 * The whole point of the fractional accumulator (specs/004) is that a player
 * crosses one tile in the same number of ticks whether tiles are 16px or
 * 32px -- speed is resolution-independent. These tests pin that property and
 * the remainder-carry invariants.
 */
#include "movement.h"
#include "test_util.h"

/* Sum the pixels moved over `ticks` frames of 1 tick each, starting at rest. */
static short cross(short tileSize, short ticksPerTile, short ticks, short *finalAccum)
{
    short accum = 0;
    short total = 0;
    short t;
    for (t = 0; t < ticks; t++) {
        total = (short)(total + Move_AccumStep(accum, tileSize, 1,
                                               ticksPerTile, &accum));
    }
    *finalAccum = accum;
    return total;
}

/*
 * Cross exactly one tile in exactly ticksPerTile ticks -- for every tile size.
 * This is resolution independence: 16px and 32px take the SAME time.
 */
static void test_resolution_independent(void)
{
    short sizes[2];
    short speeds[3];
    short si, vi;

    printf("test_resolution_independent\n");
    sizes[0] = 16; sizes[1] = 32;
    speeds[0] = 8; speeds[1] = 12; speeds[2] = 20;

    for (si = 0; si < 2; si++) {
        for (vi = 0; vi < 3; vi++) {
            short ts = sizes[si];
            short tpt = speeds[vi];
            short finalAccum;
            short moved = cross(ts, tpt, tpt, &finalAccum);
            CHECK_EQ(moved, ts, "one tile crossed in ticksPerTile ticks");
            CHECK_EQ(finalAccum, 0, "accumulator clean after exact crossing");
        }
    }
}

/* Remainder always stays below ticksPerTile; big deltaTicks behaves the same
 * as the equivalent number of 1-tick steps for whole-tile spans. */
static void test_accumulator_invariants(void)
{
    short accum = 0;
    short move, i;

    printf("test_accumulator_invariants\n");

    /* Remainder < ticksPerTile at every step. */
    for (i = 0; i < 100; i++) {
        move = Move_AccumStep(accum, 16, 1, 12, &accum);
        CHECK(accum >= 0 && accum < 12, "remainder in [0, ticksPerTile)");
        CHECK(move >= 0, "move non-negative");
    }

    /* One big frame crossing a whole tile == one tile, clean remainder. */
    accum = 0;
    move = Move_AccumStep(accum, 16, 12, 12, &accum);   /* 16*12 / 12 */
    CHECK_EQ(move, 16, "12-tick frame crosses a 16px tile in one step");
    CHECK_EQ(accum, 0, "no remainder for a whole-tile step");
}

/* Defensive: a zero/negative speed must not divide by zero. */
static void test_zero_speed_guard(void)
{
    short accum = 7;
    short move;

    printf("test_zero_speed_guard\n");
    move = Move_AccumStep(accum, 16, 1, 0, &accum);
    CHECK_EQ(move, 0, "zero ticksPerTile yields no movement");
    CHECK_EQ(accum, 7, "zero ticksPerTile leaves accumulator untouched");
}

int main(void)
{
    printf("== test_movement ==\n");
    test_resolution_independent();
    test_accumulator_invariants();
    test_zero_speed_guard();
    return TEST_RESULT();
}
