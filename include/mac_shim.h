/*
 * mac_shim.h -- Minimal Mac Toolbox shim for the POSIX / SDL2 build.
 *
 * The classic Mac and OS X Carbon builds get their types and QuickDraw from
 * the real Toolbox. The modern SDL2 build (BT_POSIX) has none of that, yet the
 * shared game core still leans on a small, well-bounded slice of it:
 *
 *   - value types:   Rect, Point, RGBColor, Str255 / StringPtr / ConstStr255Param
 *   - a WindowPtr:   only stored in GameState; opaque on SDL (the SDL renderer
 *                    owns the real window)
 *   - a QuickDraw text/rect subset the screens call directly between
 *     Renderer_BeginScreenDraw / Renderer_EndScreenDraw: TickCount, MoveTo,
 *     DrawString, TextSize, StringWidth, ForeColor, PaintRect, FrameRect.
 *
 * None of these need the Toolbox to *behave* — they are plain structs plus a
 * handful of functions that draw into the SDL work surface (implemented in
 * mac_shim.c using an embedded bitmap font). This keeps screen_*.c byte-for-
 * byte identical across all backends (the R4 decision in
 * specs/011-macosx-sdl2/research.md).
 *
 * Included ONLY when BT_POSIX is defined (see game.h). This header is Toolbox-
 * free and SDL-free so it is safe to pull into every shared-core translation
 * unit; the SDL-typed glue lives in sdl_backend.h.
 */
#ifndef MAC_SHIM_H
#define MAC_SHIM_H

#include <stddef.h>  /* NULL (the real Toolbox headers provide it transitively) */

/* ---- Geometry (Mac field order) ---- */
typedef struct { short v, h; } Point;
typedef struct { short top, left, bottom, right; } Rect;

/* ---- Colour ---- */
typedef struct { unsigned short red, green, blue; } RGBColor;

/* ---- Pascal strings ---- */
typedef unsigned char        Str255[256];
typedef unsigned char       *StringPtr;
typedef const unsigned char *ConstStr255Param;

/* ---- Window handle (opaque on SDL; the renderer owns the real window) ---- */
typedef void *WindowPtr;

/* ---- Classic QuickDraw colour constants (values match Inside Macinintosh;
 *      only their identity matters to ForeColor's dispatch). ---- */
enum {
    blackColor  = 33,
    whiteColor  = 30,
    redColor    = 205,
    greenColor  = 341,
    blueColor   = 409,
    yellowColor = 69
};

/* SetRect(r, left, top, right, bottom) -- Toolbox argument order. */
#define SetRect(r, l, t, rr, b) \
    ((r)->left = (short)(l), (r)->top = (short)(t), \
     (r)->right = (short)(rr), (r)->bottom = (short)(b))

/* ---- Time base (60.15 ticks/sec on real hardware; 60 Hz here). ---- */
long TickCount(void);

/* ---- QuickDraw text/rect subset (draws into the current SDL target
 *      surface, set by the SDL renderer's BeginScreenDraw). ---- */
void  MoveTo(short h, short v);
void  DrawString(ConstStr255Param s);
void  TextSize(short size);
short StringWidth(ConstStr255Param s);
void  ForeColor(long color);
void  BackColor(long color);
void  PaintRect(const Rect *r);
void  FrameRect(const Rect *r);

#endif /* MAC_SHIM_H */
