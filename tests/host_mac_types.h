/*
 * host_mac_types.h -- Minimal Mac Toolbox type shims for the HOST test build.
 *
 * The portable core is C89 and, on a real Mac, leans on a handful of small
 * Toolbox value types (RGBColor, ColorTable, Rect, Point). None of those need
 * the Toolbox to *behave* -- they are plain structs. This header supplies
 * equivalents so the pure logic compiles and runs under native gcc for unit
 * tests. It is included ONLY when BT_HOST_TEST is defined; the Mac builds use
 * the real Toolbox headers. See notes/deepening-and-testability.md.
 */
#ifndef HOST_MAC_TYPES_H
#define HOST_MAC_TYPES_H

/* ---- Colour ---- */
typedef struct { unsigned short red, green, blue; } RGBColor;
typedef struct { short value; RGBColor rgb; } ColorSpec;

/* Real Mac ColorTable has ctTable[1] as a trailing variable array; a fixed
 * 256 is plenty for tests and keeps allocation simple. */
typedef struct {
    long      ctSeed;
    short     ctFlags;
    short     ctSize;          /* highest index, i.e. entries - 1 */
    ColorSpec ctTable[256];
} ColorTable;
typedef ColorTable **CTabHandle;

/* ---- Geometry (Mac field order) ---- */
typedef struct { short v, h; } Point;
typedef struct { short top, left, bottom, right; } Rect;

/* SetRect(r, left, top, right, bottom) -- Toolbox argument order. */
#define SetRect(r, l, t, rr, b) \
    ((r)->left = (short)(l), (r)->top = (short)(t), \
     (r)->right = (short)(rr), (r)->bottom = (short)(b))

#endif /* HOST_MAC_TYPES_H */
