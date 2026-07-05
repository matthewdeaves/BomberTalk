/*
 * renderer_sdl.c -- SDL2 renderer backend (011-macosx-sdl2).
 *
 * Implements the backend-agnostic renderer.h surface for the modern SDL2
 * target (developed on Linux, ships on Apple Silicon). It mirrors the
 * QuickDraw renderer's double-buffer model with software SDL surfaces:
 *
 *   background surface -- static tilemap, rebuilt on demand
 *   work surface       -- per-frame composite (bg copy + sprites/text)
 *
 * Each presented frame the work surface is uploaded to a streaming texture and
 * scaled to the window by SDL_Renderer. The dirty-rectangle machinery that the
 * Mac SE needed is pointless on a GPU compositor, so Renderer_MarkDirty is a
 * no-op and every frame is a full blit (R4, specs/011-macosx-sdl2/research.md).
 *
 * Sprites are the colored-rectangle fallback shared with the classic renderer
 * (Constitution VI) -- no PICTs on SDL yet; asset loading is a later polish
 * (R5). Screen text (menu/lobby/loading) is drawn by mac_shim.c into the work
 * surface, which this file targets via MacShim_SetTarget.
 */
#include "renderer.h"
#include "tilemap.h"
#include "sdl_backend.h"
#include <clog.h>

/* Integer upscale of the native play area to a comfortable desktop window. */
#define SDL_WINDOW_SCALE 2

static SDL_Window   *gWindow     = NULL;
static SDL_Renderer *gSDL        = NULL;
static SDL_Texture  *gTexture    = NULL;
static SDL_Surface  *gBackground = NULL;
static SDL_Surface  *gWork       = NULL;

static int gNeedRebuildBg = FALSE;

/* ---- Tile / sprite colours (match renderer.c so the look is identical) ---- */
static const RGBColor kTileGreen     = {0x5500, 0xAA00, 0x5500};
static const RGBColor kTileGray      = {0x7700, 0x7700, 0x7700};
static const RGBColor kTileDarkGray  = {0x5500, 0x5500, 0x5500};
static const RGBColor kTileBrown     = {0x9900, 0x6600, 0x3300};
static const RGBColor kTileDarkBrown = {0x7700, 0x4400, 0x2200};

static const RGBColor kPlayerWhite   = {0xFFFF, 0xFFFF, 0xFFFF};
static const RGBColor kPlayerRed     = {0xFFFF, 0x0000, 0x0000};
static const RGBColor kPlayerBlue    = {0x0000, 0x0000, 0xFFFF};
static const RGBColor kPlayerYellow  = {0xFFFF, 0xFFFF, 0x0000};
static const RGBColor *kPlayerColors[MAX_PLAYERS] = {
    &kPlayerWhite, &kPlayerRed, &kPlayerBlue, &kPlayerYellow
};
static const RGBColor kExplosionOrange = {0xFFFF, 0x6600, 0x0000};

/* ---- Small drawing helpers over an SDL_Surface ---- */

static Uint32 MapC(SDL_Surface *s, const RGBColor *c)
{
    return SDL_MapRGB(s->format,
                      (Uint8)(c->red   >> 8),
                      (Uint8)(c->green >> 8),
                      (Uint8)(c->blue  >> 8));
}

static void FillR(SDL_Surface *s, short l, short t, short r, short b, Uint32 px)
{
    SDL_Rect rc;
    if (r <= l || b <= t) return;
    rc.x = l; rc.y = t; rc.w = r - l; rc.h = b - t;
    SDL_FillRect(s, &rc, px);
}

static void FrameR(SDL_Surface *s, short l, short t, short r, short b, Uint32 px)
{
    FillR(s, l, t, r, t + 1, px);
    FillR(s, l, b - 1, r, b, px);
    FillR(s, l, t, l + 1, b, px);
    FillR(s, r - 1, t, r, b, px);
}

/* Filled circle by horizontal spans (used for the bomb fallback). */
static void FillCircle(SDL_Surface *s, short cx, short cy, short rad, Uint32 px)
{
    short dy;
    if (rad < 1) rad = 1;
    for (dy = -rad; dy <= rad; dy++) {
        short dx = 0;
        short span;
        /* integer sqrt(rad^2 - dy^2) */
        while ((dx + 1) * (dx + 1) + dy * dy <= rad * rad) dx++;
        span = dx;
        FillR(s, (short)(cx - span), (short)(cy + dy),
                 (short)(cx + span + 1), (short)(cy + dy + 1), px);
    }
}

static void PresentWork(void)
{
    if (gWork == NULL || gTexture == NULL || gSDL == NULL) return;
    SDL_UpdateTexture(gTexture, NULL, gWork->pixels, gWork->pitch);
    SDL_RenderClear(gSDL);
    SDL_RenderCopy(gSDL, gTexture, NULL, NULL);
    SDL_RenderPresent(gSDL);
}

/* ==== Init / Shutdown ==== */

void Renderer_Init(WindowPtr window)
{
    int w = gGame.playWidth;
    int h = gGame.playHeight;

    (void)window;

    gWindow = SDL_CreateWindow("BomberTalk",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               w * SDL_WINDOW_SCALE, h * SDL_WINDOW_SCALE,
                               SDL_WINDOW_SHOWN);
    if (gWindow == NULL) {
        CLOG_ERR("SDL_CreateWindow failed: %s", SDL_GetError());
        return;
    }

    gSDL = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED);
    if (gSDL == NULL) {
        CLOG_WARN("Accelerated renderer failed (%s); trying software",
                  SDL_GetError());
        gSDL = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_SOFTWARE);
    }
    if (gSDL == NULL) {
        CLOG_ERR("SDL_CreateRenderer failed: %s", SDL_GetError());
        return;
    }
    /* Render at native play resolution; SDL scales to the window. */
    SDL_RenderSetLogicalSize(gSDL, w, h);

    gTexture = SDL_CreateTexture(gSDL, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, w, h);
    gBackground = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                 SDL_PIXELFORMAT_ARGB8888);
    gWork = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                           SDL_PIXELFORMAT_ARGB8888);
    if (gTexture == NULL || gBackground == NULL || gWork == NULL) {
        CLOG_ERR("SDL surface/texture alloc failed: %s", SDL_GetError());
        return;
    }

    Renderer_RebuildBackground();

    CLOG_INFO("SDL renderer initialized: %dx%d (window %dx%d), tile=%d",
              w, h, w * SDL_WINDOW_SCALE, h * SDL_WINDOW_SCALE, gGame.tileSize);
}

void Renderer_Shutdown(void)
{
    if (gTexture)    { SDL_DestroyTexture(gTexture); gTexture = NULL; }
    if (gBackground) { SDL_FreeSurface(gBackground); gBackground = NULL; }
    if (gWork)       { SDL_FreeSurface(gWork); gWork = NULL; }
    if (gSDL)        { SDL_DestroyRenderer(gSDL); gSDL = NULL; }
    if (gWindow)     { SDL_DestroyWindow(gWindow); gWindow = NULL; }
}

/* ==== Background ==== */

void Renderer_RebuildBackground(void)
{
    TileMap *map;
    short r, c, mapCols, mapRows, ts;
    Uint32 green, gray, dgray, brown, dbrown;

    if (gBackground == NULL) return;

    map = TileMap_Get();
    mapCols = TileMap_GetCols();
    mapRows = TileMap_GetRows();
    ts = gGame.tileSize;

    green  = MapC(gBackground, &kTileGreen);
    gray   = MapC(gBackground, &kTileGray);
    dgray  = MapC(gBackground, &kTileDarkGray);
    brown  = MapC(gBackground, &kTileBrown);
    dbrown = MapC(gBackground, &kTileDarkBrown);

    for (r = 0; r < mapRows; r++) {
        for (c = 0; c < mapCols; c++) {
            unsigned char tile = map->tiles[r][c];
            short l = (short)(c * ts), t = (short)(r * ts);
            short rr = (short)(l + ts), bb = (short)(t + ts);
            if (tile == TILE_WALL) {
                FillR(gBackground, l, t, rr, bb, gray);
                FrameR(gBackground, l, t, rr, bb, dgray);
            } else if (tile == TILE_BLOCK) {
                FillR(gBackground, l, t, rr, bb, brown);
                FrameR(gBackground, l, t, rr, bb, dbrown);
            } else {
                FillR(gBackground, l, t, rr, bb, green);
            }
        }
    }
}

void Renderer_RequestRebuildBackground(void)
{
    gNeedRebuildBg = TRUE;
}

/* ==== Per-frame gameplay rendering ==== */

void Renderer_BeginFrame(void)
{
    if (gNeedRebuildBg) {
        gNeedRebuildBg = FALSE;
        Renderer_RebuildBackground();
    }
    if (gWork && gBackground) {
        SDL_BlitSurface(gBackground, NULL, gWork, NULL);
    }
}

void Renderer_BeginSpriteDraw(void) { /* no port state on SDL */ }
void Renderer_EndSpriteDraw(void)   { /* no port state on SDL */ }

void Renderer_DrawPlayer(short playerID, short pixelX, short pixelY, short facing)
{
    short ts = gGame.tileSize;
    short l, t, rr, bb;

    (void)facing;
    if (gWork == NULL) return;

    l = (short)(pixelX + 2); t = (short)(pixelY + 2);
    rr = (short)(pixelX + ts - 2); bb = (short)(pixelY + ts - 2);
    FillR(gWork, l, t, rr, bb, MapC(gWork, kPlayerColors[playerID & 3]));
    FrameR(gWork, l, t, rr, bb, SDL_MapRGB(gWork->format, 0, 0, 0));
}

void Renderer_DrawBomb(short col, short row, short frameIndex)
{
    short ts = gGame.tileSize;
    short f, cx, cy, rad;

    Renderer_MarkDirty(col, row);
    if (gWork == NULL) return;

    if (frameIndex < 0) f = 0;
    else if (frameIndex >= BOMB_ANIM_FRAMES) f = BOMB_ANIM_FRAMES - 1;
    else f = frameIndex;

    cx = (short)(col * ts + ts / 2);
    cy = (short)(row * ts + ts / 2);
    /* Pulse: radius grows slightly with frame index. */
    rad = (short)(ts / 2 - (4 - f));
    FillCircle(gWork, cx, cy, rad, SDL_MapRGB(gWork->format, 0, 0, 0));
}

void Renderer_DrawExplosion(short col, short row)
{
    short ts = gGame.tileSize;
    short l, t;

    if (gWork == NULL) return;
    l = (short)(col * ts); t = (short)(row * ts);
    FillR(gWork, l, t, (short)(l + ts), (short)(t + ts),
          MapC(gWork, &kExplosionOrange));
}

void Renderer_EndFrame(WindowPtr window)
{
    (void)window;
    PresentWork();
}

void Renderer_BlitToWindow(WindowPtr window)
{
    (void)window;
    PresentWork();
}

void Renderer_MarkDirty(short col, short row)
{
    /* GPU compositor: full frame every present, so nothing to track. */
    (void)col; (void)row;
}

/* ==== Splash (no PICT assets on SDL yet -- Constitution VI fallback) ==== */

void Renderer_DrawSplashBackground(void) { /* black background under text */ }
void Renderer_ReleaseSplash(void)        { /* nothing to release */ }

/* ==== Screen draw helpers (menu/lobby/loading) ==== */

int Renderer_SaveScreenshot(const char *path)
{
    if (gWork == NULL) return -1;
    return SDL_SaveBMP(gWork, path);
}

void Renderer_BeginScreenDraw(void)
{
    if (gWork == NULL) return;
    SDL_FillRect(gWork, NULL, SDL_MapRGB(gWork->format, 0, 0, 0));
    MacShim_SetTarget(gWork);
}

void Renderer_EndScreenDraw(WindowPtr window)
{
    (void)window;
    MacShim_SetTarget(NULL);
    PresentWork();
}

/* ==== FPS overlay ==== */

void Renderer_DrawFPS(short fps)
{
    Str255 s;
    Rect bg;
    short strW, x, y, tens, ones;

    if (gWork == NULL) return;

    tens = (short)(fps / 10);
    ones = (short)(fps % 10);
    if (tens > 0) {
        s[0] = 6; s[1] = (unsigned char)('0' + tens); s[2] = (unsigned char)('0' + ones);
        s[3] = ' '; s[4] = 'f'; s[5] = 'p'; s[6] = 's';
    } else {
        s[0] = 5; s[1] = (unsigned char)('0' + ones);
        s[2] = ' '; s[3] = 'f'; s[4] = 'p'; s[5] = 's';
    }

    MacShim_SetTarget(gWork);
    TextSize(10);
    strW = StringWidth((ConstStr255Param)s);
    x = (short)(gGame.playWidth - strW - 4);
    y = (short)(gGame.playHeight - 4);

    SetRect(&bg, x - 2, y - 10, gGame.playWidth, gGame.playHeight);
    ForeColor(blackColor);
    PaintRect(&bg);
    ForeColor(whiteColor);
    MoveTo(x, y);
    DrawString((ConstStr255Param)s);

    MacShim_SetTarget(NULL);
    PresentWork();
}
