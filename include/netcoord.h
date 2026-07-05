/*
 * netcoord.h -- Tile-independent network position coordinates.
 *
 * Positions travel between machines in a tile-independent fixed-point space:
 * 256 units == one tile. A 16px-tile Mac SE and a 32px-tile PPC therefore
 * agree on where a player is, mapping into their own pixel grids on receipt.
 * Getting this wrong caused false explosion kills (wrong grid mapping) and
 * out-of-bounds crashes on the smaller-tiled machine (see specs/004).
 *
 * Pure integer math, no Toolbox, no globals -- host unit tested in
 * tests/test_netcoord.c. tileSize is always a power of two (16 or 32; see
 * game.h), which the send path exploits to avoid a 68k soft-divide.
 */
#ifndef NETCOORD_H
#define NETCOORD_H

/* Local pixel position -> wire coordinate (256 units per tile). */
short NetCoord_ToWire(short pixel, short tileSize);

/* Wire coordinate -> local pixel position for this machine's tile size. */
short NetCoord_ToLocal(short wire, short tileSize);

#endif /* NETCOORD_H */
