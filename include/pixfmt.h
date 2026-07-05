/*
 * pixfmt.h -- Depth-independent pixel colour extraction.
 *
 * The renderer builds sprite masks by comparing pixel colours, so it must
 * read GWorld pixels correctly at 8, 16, or 32 bits per pixel (NewGWorld
 * inherits the screen depth -- 8-bit on a 256-colour Performa, 32-bit on an
 * iMac G5). Extracting this from renderer.c makes it buildable and unit
 * testable off the Mac Toolbox (see tests/test_pixfmt.c, regression for
 * KI-008: sprites invisible on displays deeper than 256 colours).
 *
 * The Mac builds get RGBColor / CTabHandle from the Toolbox; the host test
 * build provides equivalent shims. This is the first slice of the portable
 * core described in notes/deepening-and-testability.md.
 */
#ifndef PIXFMT_H
#define PIXFMT_H

#ifdef BT_HOST_TEST
#include "host_mac_types.h"
#else
#include "game.h"   /* QuickDraw RGBColor, CTabHandle (per platform) */
#endif

/*
 * PixFmt_ReadRGB -- One pixel's colour as 16-bit-per-channel RGB.
 *
 *   rowBase    pointer to the first byte of the pixel's row
 *   col        column index within that row
 *   pixelSize  bits per pixel: 8 (indexed), 16 (5-5-5 direct), else 32 (xRGB)
 *   ctab       colour table, used only for the 8-bit indexed case (may be
 *              NULL for direct pixels)
 *   out        receives the colour
 *
 * All channels are returned as 16-bit (matching a ColorTable's RGBColor) so
 * one distance threshold works across every depth.
 */
void PixFmt_ReadRGB(const unsigned char *rowBase, short col,
                    short pixelSize, CTabHandle ctab, RGBColor *out);

#endif /* PIXFMT_H */
