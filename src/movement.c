/*
 * movement.c -- Resolution-independent movement accumulator. See movement.h.
 *
 * Extracted from player.c (011-macosx-sdl2) so the accumulator arithmetic is
 * host unit testable, isolated from the Player/Toolbox state around it.
 */
#include "movement.h"

short Move_AccumStep(short accum, short tileSize, short deltaTicks,
                     short ticksPerTile, short *outAccum)
{
    short a;

    if (ticksPerTile <= 0) {   /* defensive: never true for valid stats */
        *outAccum = accum;
        return 0;
    }

    a = (short)(accum + tileSize * deltaTicks);
    *outAccum = (short)(a % ticksPerTile);
    return (short)(a / ticksPerTile);
}
