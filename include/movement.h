/*
 * movement.h -- Resolution-independent movement accumulator.
 *
 * Speed is expressed as "ticks to cross one tile" (stats.speedTicks), so a
 * player crosses a tile in the same wall-clock time whether tiles are 16px
 * (Mac SE) or 32px (PPC). Each tick adds tileSize units to a fractional
 * accumulator; whole pixels move out, the remainder carries. Pure integer
 * math -- host unit tested in tests/test_movement.c (property: identical
 * tile-crossing time across tile sizes).
 */
#ifndef MOVEMENT_H
#define MOVEMENT_H

/*
 * Move_AccumStep -- Advance one axis by one frame.
 *
 *   accum         current fractional accumulator for this axis
 *   tileSize      pixels per tile (16 or 32)
 *   deltaTicks    elapsed ticks this frame
 *   ticksPerTile  speed: ticks to cross one whole tile (stats.speedTicks)
 *   outAccum      receives the carried remainder
 *
 * Returns the whole pixels to move this frame. If ticksPerTile <= 0 (never
 * true for valid stats) it is a no-op rather than a divide-by-zero.
 */
short Move_AccumStep(short accum, short tileSize, short deltaTicks,
                     short ticksPerTile, short *outAccum);

#endif /* MOVEMENT_H */
