/*
 * collision.h -- Axis-separated AABB tile collision (portable core).
 *
 * The player collides against the tilemap one axis at a time: the full sprite
 * rect is swept by a per-axis pixel delta, clamped to the play area, then
 * pushed flush against the first solid tile it overlaps. Solid = WALL, BLOCK,
 * or a bomb tile the player is NOT currently walking off of (pass-through).
 *
 * Extracted from player.c (011-macosx-sdl2) as pure integer geometry with no
 * Toolbox and no globals, so it compiles under native gcc and is host unit
 * tested in tests/test_collision.c. The Mac build's CollideAxis wrapper gathers
 * the arguments from gGame/TileMap and applies the result. Hot inner loop is
 * unchanged direct array indexing -- no callbacks, no allocation
 * (Constitution V/VII; see notes/deepening-and-testability.md).
 */
#ifndef COLLISION_H
#define COLLISION_H

#include "coredefs.h"

/*
 * Collide_ResolveAxis -- Resolve one axis of movement against the tilemap.
 *
 *   pixelX, pixelY   current top-left sprite position (pixels)
 *   dx, dy           per-axis pixel delta this step (exactly one is non-zero)
 *   ts               tile size in pixels (16 or 32)
 *   playWidth/Height play-area bounds in pixels (sprite clamped inside)
 *   mapCols, mapRows valid tilemap dimensions
 *   tiles            row-major tile bytes, stride MAX_GRID_COLS (never NULL)
 *   bombGrid         row-major bomb-occupancy bytes, stride MAX_GRID_COLS;
 *                    NULL treats every cell as bomb-free
 *   ptCol, ptRow     grid cell of the pass-through bomb the player is walking
 *                    off of, or (-1,-1) for none -- that one bomb cell is not
 *                    solid to this player
 *   outPX, outPY     receive the resolved position for BOTH axes; the caller
 *                    applies only the moving axis (the other equals its input)
 *
 * Pure: no globals, no I/O, no allocation.
 */
void Collide_ResolveAxis(short pixelX, short pixelY, short dx, short dy,
                         short ts, short playWidth, short playHeight,
                         short mapCols, short mapRows,
                         const unsigned char *tiles,
                         const unsigned char *bombGrid,
                         short ptCol, short ptRow,
                         short *outPX, short *outPY);

#endif /* COLLISION_H */
