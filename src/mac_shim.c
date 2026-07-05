/*
 * mac_shim.c -- QuickDraw text/rect subset for the POSIX/SDL2 build.
 *
 * Implements the tiny slice of QuickDraw that the shared screens call directly
 * between Renderer_BeginScreenDraw and Renderer_EndScreenDraw (MoveTo,
 * DrawString, TextSize, StringWidth, ForeColor, BackColor, PaintRect,
 * FrameRect). Everything draws into a single "target" SDL_Surface that the SDL
 * renderer points here via MacShim_SetTarget; the renderer then uploads that
 * surface to the window. Text uses the embedded 8x8 bitmap font (font8x8.h)
 * scaled to the requested TextSize, drawn transparently in the current fore
 * colour (matching QuickDraw's default srcOr text mode).
 *
 * This lets screen_menu.c / screen_lobby.c / screen_loading.c stay byte-for-
 * byte identical across the classic, Carbon, and SDL backends (R4 in
 * specs/011-macosx-sdl2/research.md). POSIX/SDL2 build only.
 */
#include "mac_shim.h"
#include "sdl_backend.h"
#include "font8x8.h"

/* ---- Shim graphics state (QuickDraw pen/text globals, emulated) ---- */
static SDL_Surface   *gTarget   = NULL;   /* current draw surface (may be NULL) */
static short          gPenH     = 0;      /* pen position, window coords */
static short          gPenV     = 0;
static short          gTextSize = 12;     /* points; maps to integer font scale */
static unsigned char  gForeR = 0, gForeG = 0, gForeB = 0;       /* black */
static unsigned char  gBackR = 255, gBackG = 255, gBackB = 255; /* white */

/* Fixed 8px cell so StringWidth and DrawString advance identically (the
 * screens center text by StringWidth, so the two MUST agree exactly). */
#define SHIM_CELL 8

void MacShim_SetTarget(SDL_Surface *surface)
{
    gTarget = surface;
}

/* Integer font scale from a QuickDraw point size (never below 1x). */
static short ShimScale(void)
{
    short s = (short)(gTextSize / SHIM_CELL);
    return (s < 1) ? 1 : s;
}

long TickCount(void); /* provided by platform_posix.c */

void ForeColor(long color)
{
    switch (color) {
    case whiteColor:  gForeR = 255; gForeG = 255; gForeB = 255; break;
    case redColor:    gForeR = 255; gForeG = 0;   gForeB = 0;   break;
    case greenColor:  gForeR = 0;   gForeG = 200; gForeB = 0;   break;
    case blueColor:   gForeR = 0;   gForeG = 0;   gForeB = 255; break;
    case yellowColor: gForeR = 255; gForeG = 255; gForeB = 0;   break;
    case blackColor:  /* fall through */
    default:          gForeR = 0;   gForeG = 0;   gForeB = 0;   break;
    }
}

void BackColor(long color)
{
    switch (color) {
    case blackColor:  gBackR = 0;   gBackG = 0;   gBackB = 0;   break;
    case whiteColor:  /* fall through */
    default:          gBackR = 255; gBackG = 255; gBackB = 255; break;
    }
}

void TextSize(short size)
{
    gTextSize = (size < 1) ? 1 : size;
}

void MoveTo(short h, short v)
{
    gPenH = h;
    gPenV = v;
}

short StringWidth(ConstStr255Param s)
{
    short len = (short)s[0];
    return (short)(len * SHIM_CELL * ShimScale());
}

/* Fill a rect in the current fore colour, clipped by SDL_FillRect. */
static void ShimFill(short left, short top, short right, short bottom,
                     unsigned char r, unsigned char g, unsigned char b)
{
    SDL_Rect rc;
    if (gTarget == NULL) return;
    if (right <= left || bottom <= top) return;
    rc.x = left;
    rc.y = top;
    rc.w = right - left;
    rc.h = bottom - top;
    SDL_FillRect(gTarget, &rc, SDL_MapRGB(gTarget->format, r, g, b));
}

void PaintRect(const Rect *r)
{
    ShimFill(r->left, r->top, r->right, r->bottom, gForeR, gForeG, gForeB);
}

void FrameRect(const Rect *r)
{
    /* 1px outline in the fore colour. */
    ShimFill(r->left,      r->top,        r->right,     r->top + 1,    gForeR, gForeG, gForeB);
    ShimFill(r->left,      r->bottom - 1, r->right,     r->bottom,     gForeR, gForeG, gForeB);
    ShimFill(r->left,      r->top,        r->left + 1,  r->bottom,     gForeR, gForeG, gForeB);
    ShimFill(r->right - 1, r->top,        r->right,     r->bottom,     gForeR, gForeG, gForeB);
}

void DrawString(ConstStr255Param s)
{
    short len = (short)s[0];
    short scale = ShimScale();
    short glyphTop = (short)(gPenV - SHIM_CELL * scale); /* baseline at penV */
    short i;

    for (i = 1; i <= len; i++) {
        unsigned char ch = s[i];
        const unsigned char *glyph;
        short row, col;

        if (ch > 0x7F) ch = '?';
        glyph = kFont8x8[ch];

        for (row = 0; row < 8; row++) {
            unsigned char bits = glyph[row];
            if (bits == 0) continue;
            for (col = 0; col < 8; col++) {
                if (bits & (1 << col)) {
                    short x = (short)(gPenH + (i - 1) * SHIM_CELL * scale + col * scale);
                    short y = (short)(glyphTop + row * scale);
                    ShimFill(x, y, (short)(x + scale), (short)(y + scale),
                             gForeR, gForeG, gForeB);
                }
            }
        }
    }

    gPenH = (short)(gPenH + len * SHIM_CELL * scale);
}
