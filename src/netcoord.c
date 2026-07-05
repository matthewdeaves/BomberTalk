/*
 * netcoord.c -- Tile-independent network position coordinates. See netcoord.h.
 *
 * Extracted from net.c (011-macosx-sdl2) so the conversion is one unit with
 * host unit tests, rather than two inline expressions that must stay inverse.
 */
#include "netcoord.h"

short NetCoord_ToWire(short pixel, short tileSize)
{
    /* wire = pixel * 256 / tileSize. tileSize is a power of two (16 or 32),
     * so use a shift instead of a divide -- Net_SendPosition runs every frame
     * the local player moves, and a 68k soft-divide (__divsi3) costs ~200
     * cycles. 16px: 256/16 = 1<<4.  32px: 256/32 = 1<<3. A new tile size
     * would need another entry here (and must be a power of two). */
    short shift = (tileSize == 16) ? 4 : 3;
    return (short)((long)pixel << shift);
}

short NetCoord_ToLocal(short wire, short tileSize)
{
    /* pixel = wire * tileSize / 256. Multiply-then-shift works for any tile
     * size; the receive path is not the per-frame hot path the send is. */
    return (short)(((long)wire * tileSize) >> 8);
}
