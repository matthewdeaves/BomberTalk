/*
 * pixfmt.c -- Depth-independent pixel colour extraction. See pixfmt.h.
 *
 * Extracted from renderer.c (011-macosx-sdl2) so the depth handling that
 * caused KI-008 has one home and can be unit tested on the host.
 */
#include "pixfmt.h"
#include <stddef.h>   /* NULL (Toolbox provides it on Mac; explicit for host) */

void PixFmt_ReadRGB(const unsigned char *rowBase, short col,
                    short pixelSize, CTabHandle ctab, RGBColor *out)
{
    if (pixelSize <= 8) {
        short pIdx = rowBase[col];
        if (ctab != NULL && *ctab != NULL) {
            if (pIdx > (*ctab)->ctSize) pIdx = 0;
            *out = (*ctab)->ctTable[pIdx].rgb;
        } else {
            /* No ctab (unexpected for indexed) -- treat index as grey. */
            out->red = out->green = out->blue =
                (unsigned short)(rowBase[col] * 0x0101);
        }
    } else if (pixelSize == 16) {
        /* 5-5-5 direct, big-endian: 0 rrrrr ggggg bbbbb. Replicate each
         * 5-bit channel up to 16 bits. */
        const unsigned char *p = rowBase + (long)col * 2;
        unsigned short px = (unsigned short)((p[0] << 8) | p[1]);
        unsigned short r5 = (unsigned short)((px >> 10) & 0x1F);
        unsigned short g5 = (unsigned short)((px >> 5) & 0x1F);
        unsigned short b5 = (unsigned short)(px & 0x1F);
        out->red   = (unsigned short)((r5 << 11) | (r5 << 6) | (r5 << 1) | (r5 >> 4));
        out->green = (unsigned short)((g5 << 11) | (g5 << 6) | (g5 << 1) | (g5 >> 4));
        out->blue  = (unsigned short)((b5 << 11) | (b5 << 6) | (b5 << 1) | (b5 >> 4));
    } else {
        /* 32-bit direct, big-endian [unused, R, G, B]. Replicate each
         * 8-bit channel into 16 bits. */
        const unsigned char *p = rowBase + (long)col * 4;
        unsigned short r8 = p[1], g8 = p[2], b8 = p[3];
        out->red   = (unsigned short)((r8 << 8) | r8);
        out->green = (unsigned short)((g8 << 8) | g8);
        out->blue  = (unsigned short)((b8 << 8) | b8);
    }
}
