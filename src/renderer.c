/*
 * renderer.c -- Double-buffered rendering
 *
 * Two offscreen buffers:
 *   background -- pre-rendered tile map (rebuilt when blocks destroyed)
 *   work       -- per-frame copy of bg + sprites
 *
 * Color Macs: GWorld-based (Color QuickDraw / QDOffscreen).
 * Mac SE:     BitMap + GrafPort (classic QuickDraw, 1-bit monochrome).
 *
 * Performance optimizations (002-perf-extensibility):
 *   - Dirty rectangle tracking at tile granularity
 *   - Frame-level sprite LockPixels batching
 *   - Static const color constants (no stack allocation)
 *   - Cached PixMap pointers
 *   - 32-bit aligned CopyBits rectangles
 *
 * Source: Black Art (1996) texture loading, Tricks of the Gurus (1995),
 *         Sex Lies and Video Games (1996) buffered animation,
 *         Mac Game Programming (2002) Ch.6 dirty rectangles,
 *         Macintosh Game Programming Techniques (1996) Ch.7 LockPixels.
 */

#include "renderer.h"
#include "tilemap.h"
#include <clog.h>
#include <string.h>

/* ---- Color Mac: GWorld-based offscreen ---- */
#include <QDOffscreen.h>

static GWorldPtr gBackground = NULL;
static GWorldPtr gWorkBuffer = NULL;
static GWorldPtr gTileSheet = NULL;
static GWorldPtr gPlayerSprites[MAX_PLAYERS];
static GWorldPtr gBombSprites[BOMB_ANIM_FRAMES];
static GWorldPtr gExplosionSprite = NULL;
static GWorldPtr gTitleSprite = NULL;

/* ---- Sprite mask regions for srcCopy blitting (006-renderer-optimization) ---- */
static RgnHandle gPlayerMaskRgn[MAX_PLAYERS];
static RgnHandle gBombMaskRgn[BOMB_ANIM_FRAMES];
static RgnHandle gExplosionMaskRgn = NULL;
static RgnHandle gTitleMaskRgn = NULL;

/* ---- Mac SE: 1-bit bomb sprite + flood-fill mask (per frame). ----
 *
 * 16x16 each. Sprite carries the source PICT pixels as a 1-bit BitMap
 * (white = transparent, black = bomb body). Mask is the silhouette —
 * 1 wherever the bomb is drawn, including internal white highlights
 * — built by flood-filling "outside" white from the corners. Renderer
 * composites with srcBic(mask) + srcOr(sprite), preserving highlights
 * and letting the tile background show through around the bomb.
 *
 * Storage allocated in LoadSEBombSprites, freed in Renderer_Shutdown.
 * A NULL baseAddr on gBombSpriteSE[i] means the PICT wasn't loaded
 * for that frame (missing from resource fork) — the draw code falls
 * back to the animated oval path. */
static BitMap gBombSpriteSE[BOMB_ANIM_FRAMES];
static BitMap gBombMaskSE[BOMB_ANIM_FRAMES];
static Ptr    gBombSpriteSEStorage[BOMB_ANIM_FRAMES];
static Ptr    gBombMaskSEStorage[BOMB_ANIM_FRAMES];

/* ---- Mac SE: BitMap-based offscreen ---- */
static GrafPort gBgPortSE;
static GrafPort gWorkPortSE;
static BitMap   gBgBitsSE;
static BitMap   gWorkBitsSE;
static Ptr      gBgStorageSE   = NULL;
static Ptr      gWorkStorageSE = NULL;

/* ---- Saved port for screen draw helpers ---- */
static CGrafPtr gSavedScreenCPort = NULL;
static GDHandle gSavedScreenDevice = NULL;

static int gPICTsLoaded = FALSE;

/* ---- Sprite draw bracket state (006-renderer-optimization) ---- */
static int gSpriteDrawActive = FALSE;

/* ---- Static color constants (T012) ---- */
static const RGBColor kPlayerWhite  = {0xFFFF, 0xFFFF, 0xFFFF};
static const RGBColor kPlayerRed    = {0xFFFF, 0x0000, 0x0000};
static const RGBColor kPlayerBlue   = {0x0000, 0x0000, 0xFFFF};
static const RGBColor kPlayerYellow = {0xFFFF, 0xFFFF, 0x0000};
static const RGBColor kExplosionOrange = {0xFFFF, 0x6600, 0x0000};

static const RGBColor *kPlayerColors[MAX_PLAYERS] = {
    &kPlayerWhite, &kPlayerRed, &kPlayerBlue, &kPlayerYellow
};

/* ---- Tile drawing colors ---- */
static const RGBColor kTileGreen     = {0x5500, 0xAA00, 0x5500};
static const RGBColor kTileGray      = {0x7700, 0x7700, 0x7700};
static const RGBColor kTileDarkGray  = {0x5500, 0x5500, 0x5500};
static const RGBColor kTileBrown     = {0x9900, 0x6600, 0x3300};
static const RGBColor kTileDarkBrown = {0x7700, 0x4400, 0x2200};

/* ---- Cached PixMap pointers (T013) ---- */
static BitMap *gCachedPlayerPM[MAX_PLAYERS];
static BitMap *gCachedBombPM[BOMB_ANIM_FRAMES];
static BitMap *gCachedExplosionPM = NULL;

/* ---- Bomb PICT resource ID tables ---- */
static const short kBombColorIds[BOMB_ANIM_FRAMES] = {
    rPictBombFrame0, rPictBombFrame1, rPictBombFrame2
};
static const short kBombSEIds[BOMB_ANIM_FRAMES] = {
    rPictBombSEFrame0, rPictBombSEFrame1, rPictBombSEFrame2
};

/* ---- Deferred background rebuild flag ---- */
static int gNeedRebuildBg = FALSE;

/* ---- Dirty rectangle grid + list (T015) ---- */
static unsigned char gDirtyGrid[MAX_GRID_ROWS][MAX_GRID_COLS];
static short gDirtyCount = 0;
static short gDirtyTotal = 0;
static int   gAllDirty = FALSE;

/* Dirty list: parallel arrays for O(dirty) iteration.
 * Grid retained for O(1) duplicate detection in MarkDirty. */
static short gDirtyListCol[MAX_GRID_ROWS * MAX_GRID_COLS];
static short gDirtyListRow[MAX_GRID_ROWS * MAX_GRID_COLS];

/* ==== Dirty Rectangle API ==== */

void Renderer_MarkDirty(short col, short row)
{
    if (col < 0 || col >= TileMap_GetCols() ||
        row < 0 || row >= TileMap_GetRows()) return;
    if (gDirtyGrid[row][col]) return;
    gDirtyGrid[row][col] = 1;
    gDirtyListCol[gDirtyCount] = col;
    gDirtyListRow[gDirtyCount] = row;
    gDirtyCount++;
}

static void Renderer_MarkAllDirty(void)
{
    memset(gDirtyGrid, 1, sizeof(gDirtyGrid));
    gDirtyCount = gDirtyTotal;
    gAllDirty = TRUE;
    /* List not populated -- full-screen CopyBits path doesn't use it */
}

static void Renderer_ClearDirty(void)
{
    if (gAllDirty) {
        memset(gDirtyGrid, 0, sizeof(gDirtyGrid));
        gAllDirty = FALSE;
    } else {
        short i;
        for (i = 0; i < gDirtyCount; i++) {
            gDirtyGrid[gDirtyListRow[i]][gDirtyListCol[i]] = 0;
        }
    }
    gDirtyCount = 0;
}

/* ==== Sprite lock batching (T013-T014) ==== */

static void LockAllSprites(void)
{
    short i;
    if (gGame.isMacSE) return;

    for (i = 0; i < MAX_PLAYERS; i++) {
        if (gPlayerSprites[i] != NULL) {
            LockPixels(GetGWorldPixMap(gPlayerSprites[i]));
            gCachedPlayerPM[i] = (BitMap *)*GetGWorldPixMap(gPlayerSprites[i]);
        } else {
            gCachedPlayerPM[i] = NULL;
        }
    }
    for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
        if (gBombSprites[i] != NULL) {
            LockPixels(GetGWorldPixMap(gBombSprites[i]));
            gCachedBombPM[i] = (BitMap *)*GetGWorldPixMap(gBombSprites[i]);
        } else {
            gCachedBombPM[i] = NULL;
        }
    }
    if (gExplosionSprite != NULL) {
        LockPixels(GetGWorldPixMap(gExplosionSprite));
        gCachedExplosionPM = (BitMap *)*GetGWorldPixMap(gExplosionSprite);
    }
}

static void UnlockAllSprites(void)
{
    short i;
    if (gGame.isMacSE) return;

    for (i = 0; i < MAX_PLAYERS; i++) {
        if (gPlayerSprites[i] != NULL) {
            UnlockPixels(GetGWorldPixMap(gPlayerSprites[i]));
        }
        gCachedPlayerPM[i] = NULL;
    }
    for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
        if (gBombSprites[i] != NULL) {
            UnlockPixels(GetGWorldPixMap(gBombSprites[i]));
        }
        gCachedBombPM[i] = NULL;
    }
    if (gExplosionSprite != NULL) {
        UnlockPixels(GetGWorldPixMap(gExplosionSprite));
    }
    gCachedExplosionPM = NULL;
}

/* ==== 32-bit alignment helper (T016) ==== */

static void AlignRect32(Rect *r)
{
    if (gGame.isMacSE) {
        /* 1-bit: align to 32-pixel boundaries */
        r->left &= ~31;
        r->right = (r->right + 31) & ~31;
    } else {
        /* 8-bit: align to 4-pixel boundaries */
        r->left &= ~3;
        r->right = (r->right + 3) & ~3;
    }
    if (r->right > gGame.playWidth)
        r->right = gGame.playWidth;
}

/* ==== Offscreen buffer abstraction ==== */

static BitMap *GetBgBits(void)
{
    if (gGame.isMacSE)
        return &gBgBitsSE;
    return (BitMap *)*GetGWorldPixMap(gBackground);
}

static BitMap *GetWorkBits(void)
{
    if (gGame.isMacSE)
        return &gWorkBitsSE;
    return (BitMap *)*GetGWorldPixMap(gWorkBuffer);
}

static void LockBg(void)
{
    if (!gGame.isMacSE)
        LockPixels(GetGWorldPixMap(gBackground));
}

static void UnlockBg(void)
{
    if (!gGame.isMacSE)
        UnlockPixels(GetGWorldPixMap(gBackground));
}

static void LockWork(void)
{
    if (!gGame.isMacSE)
        LockPixels(GetGWorldPixMap(gWorkBuffer));
}

static void UnlockWork(void)
{
    if (!gGame.isMacSE)
        UnlockPixels(GetGWorldPixMap(gWorkBuffer));
}

static void SetPortBg(void)
{
    if (gGame.isMacSE)
        SetPort(&gBgPortSE);
    else
        SetGWorld(gBackground, NULL);
}

static void SetPortWork(void)
{
    if (gGame.isMacSE)
        SetPort(&gWorkPortSE);
    else
        SetGWorld(gWorkBuffer, NULL);
}

static CGrafPtr gSavedBuildPort = NULL;
static GDHandle gSavedBuildDevice = NULL;

static void SavePort(void)
{
    if (gGame.isMacSE)
        GetPort((GrafPtr *)&gSavedBuildPort);
    else
        GetGWorld(&gSavedBuildPort, &gSavedBuildDevice);
}

static void RestorePort(void)
{
    if (gGame.isMacSE)
        SetPort((GrafPtr)gSavedBuildPort);
    else
        SetGWorld(gSavedBuildPort, gSavedBuildDevice);
}

/* ==== PICT Loading (color Macs only) ==== */

static GWorldPtr LoadPICTToGWorld(short pictID, short width, short height)
{
    GWorldPtr gw;
    PicHandle pic;
    CGrafPtr oldPort;
    GDHandle oldDevice;
    QDErr err;
    Rect bounds;

    pic = GetPicture(pictID);
    if (pic == NULL) return NULL;

    SetRect(&bounds, 0, 0, width, height);
    err = NewGWorld(&gw, 0, &bounds, NULL, NULL, 0);
    if (err != noErr) {
        ReleaseResource((Handle)pic);
        return NULL;
    }

    GetGWorld(&oldPort, &oldDevice);
    SetGWorld(gw, NULL);
    LockPixels(GetGWorldPixMap(gw));
    EraseRect(&bounds);
    DrawPicture(pic, &bounds);
    UnlockPixels(GetGWorldPixMap(gw));
    SetGWorld(oldPort, oldDevice);

    ReleaseResource((Handle)pic);
    return gw;
}

/*
 * CreateMaskFromGWorld -- Build a mask Region from a sprite GWorld.
 *
 * Scans the GWorld for non-background pixels and creates a 1-bit mask,
 * then converts to a Region via BitMapToRegion(). Returns NULL on failure.
 * Source: Sex, Lies and Video Games (1996) p.6615-6700.
 */
static RgnHandle CreateMaskFromGWorld(GWorldPtr gw, short width, short height)
{
    PixMapHandle pmh;
    Ptr pixBase;
    long pixRowBytes;
    short maskRowBytes;
    Ptr maskStorage;
    BitMap maskBM;
    RgnHandle rgn;
    CTabHandle ctab;
    RGBColor bgRgb;
    short bgIndex;
    short row, col;
    const unsigned char *srcRow;
    long threshSq;
    int hasCtab;

    if (gw == NULL) return NULL;

    pmh = GetGWorldPixMap(gw);
    if (pmh == NULL) return NULL;
    LockPixels(pmh);

    pixBase = GetPixBaseAddr(pmh);
    pixRowBytes = (*pmh)->rowBytes & 0x3FFF;
    ctab = (*pmh)->pmTable;

    /* Read the background reference from the top-left pixel's RGB (via
     * the ctab), not its palette index. DrawPicture colour-matches onto
     * the device CLUT on load, and multiple distinct source colours can
     * end up at the same device index -- so comparing indices caused
     * the bomb's white highlight to collapse into the same slot as the
     * transparent background on Quadra 800. Comparing RGB sidesteps
     * that: we only mask pixels whose actual colour is close to the
     * background colour. */
    bgIndex = ((const unsigned char *)pixBase)[0];
    if (ctab != NULL && *ctab != NULL) {
        if (bgIndex > (*ctab)->ctSize) bgIndex = 0;
        bgRgb = (*ctab)->ctTable[bgIndex].rgb;
    } else {
        /* No ctab (unexpected for 8-bit GWorld) -- fall back to index compare. */
        bgRgb.red = bgRgb.green = bgRgb.blue = 0xFFFF;
    }

    /* Allocate 1-bit mask bitmap */
    maskRowBytes = ((width + 15) / 16) * 2;
    maskStorage = NewPtrClear((long)maskRowBytes * height);
    if (maskStorage == NULL) {
        UnlockPixels(pmh);
        return NULL;
    }

    /* Tolerance for "matches background". ~3% of the 16-bit range per
     * channel, squared Euclidean distance. Generous enough to absorb
     * colour-matching rounding, tight enough that a bomb's near-black
     * shading (which lands on a distinct gray slot) stays opaque. */
    threshSq = 3L * 0x0800L * 0x0800L;
    hasCtab = (ctab != NULL && *ctab != NULL);

    for (row = 0; row < height; row++) {
        unsigned char *dstRow;
        srcRow = (const unsigned char *)pixBase + (long)row * pixRowBytes;
        dstRow = (unsigned char *)maskStorage + (long)row * maskRowBytes;
        if (hasCtab) {
            for (col = 0; col < width; col++) {
                short pIdx = srcRow[col];
                RGBColor p;
                long dr, dg, db;
                if (pIdx > (*ctab)->ctSize) pIdx = 0;
                p = (*ctab)->ctTable[pIdx].rgb;
                dr = (long)p.red   - (long)bgRgb.red;
                dg = (long)p.green - (long)bgRgb.green;
                db = (long)p.blue  - (long)bgRgb.blue;
                dr >>= 4; dg >>= 4; db >>= 4;
                if ((dr*dr + dg*dg + db*db) > (threshSq >> 8)) {
                    dstRow[col >> 3] |= (0x80 >> (col & 7));
                }
            }
        } else {
            for (col = 0; col < width; col++) {
                if (srcRow[col] != (unsigned char)bgIndex) {
                    dstRow[col >> 3] |= (0x80 >> (col & 7));
                }
            }
        }
    }

    UnlockPixels(pmh);

    /* Convert 1-bit mask to Region */
    rgn = NewRgn();
    if (rgn == NULL) {
        DisposePtr(maskStorage);
        return NULL;
    }

    maskBM.baseAddr = maskStorage;
    maskBM.rowBytes = maskRowBytes;
    SetRect(&maskBM.bounds, 0, 0, width, height);

    if (BitMapToRegion(rgn, &maskBM) != noErr) {
        DisposeRgn(rgn);
        DisposePtr(maskStorage);
        return NULL;
    }

    DisposePtr(maskStorage);
    return rgn;
}

/* ==== Mac SE: 1-bit PICT sprite loading ==== */

/*
 * LoadPICTToBitMap -- Load a PICT resource into a 1-bit BitMap.
 *
 * Mac SE has no Color QuickDraw, so NewGWorld isn't available. Instead
 * we set up a monochrome GrafPort backed by a caller-supplied BitMap
 * and DrawPicture into it. White pixels (bit=0) in the resulting
 * bitmap correspond to the PICT's transparent background (exported
 * from pixelcraft with the "white" bg-picker setting).
 *
 * Returns non-zero on success. outBM fields and outStorage are
 * populated with ownership transferred to caller (must DisposePtr
 * the storage when done).
 */
/* File-scope temp port used only by LoadPICTToBitMap. Static (not stack)
 * so that if anything downstream inadvertently captures thePort during
 * this function, the pointer stays valid memory after the function
 * returns -- a closed port is safer than a dangling stack reference. */
static GrafPort gLoadPICTTempPort;

static int LoadPICTToBitMap(short pictID, short width, short height,
                            BitMap *outBM, Ptr *outStorage)
{
    PicHandle pic;
    GrafPtr savePort;
    short rowBytes;
    Ptr storage;

    pic = GetPicture(pictID);
    if (pic == NULL) return 0;

    rowBytes = ((width + 15) / 16) * 2;  /* 2-byte aligned */
    storage = NewPtrClear((long)rowBytes * height);
    if (storage == NULL) {
        ReleaseResource((Handle)pic);
        return 0;
    }

    outBM->baseAddr = storage;
    outBM->rowBytes = rowBytes;
    SetRect(&outBM->bounds, 0, 0, width, height);

    /* Save the CURRENT port first, then open/configure our temp port.
     * Previous bug: GetPort was called AFTER SetPort, so savePort ended
     * up pointing at the temp port itself and SetPort(savePort) at the
     * end was a no-op -- thePort was left pointing at a closed stack-
     * local GrafPort, corrupting QuickDraw state for subsequent draws. */
    GetPort(&savePort);

    OpenPort(&gLoadPICTTempPort);
    SetPort(&gLoadPICTTempPort);
    gLoadPICTTempPort.portBits = *outBM;
    gLoadPICTTempPort.portRect = outBM->bounds;
    ClipRect(&outBM->bounds);

    EraseRect(&outBM->bounds);   /* fill with port bg (white) first */
    DrawPicture(pic, &outBM->bounds);

    /* Restore the caller's port BEFORE closing the temp. */
    SetPort(savePort);
    ClosePort(&gLoadPICTTempPort);

    ReleaseResource((Handle)pic);

    /* Auto-detect polarity: srcOr compositing wants bit=1 for the bomb
     * body and bit=0 for transparent. Pixelcraft can export either
     * polarity depending on the chosen bg-picker setting. If the corner
     * pixel (conventionally "transparent") is black (bit=1), the bitmap
     * was rendered inverted -- XOR every byte to flip it. Corner white
     * (bit=0) is the target polarity; leave untouched. */
    {
        unsigned char *bits = (unsigned char *)storage;
        if (bits[0] & 0x80) {
            long total = (long)rowBytes * height;
            long k;
            for (k = 0; k < total; k++) bits[k] = (unsigned char)(bits[k] ^ 0xFF);
        }
    }

    *outStorage = storage;
    return 1;
}

/*
 * BuildBombMaskByFloodFill -- Derive a silhouette mask from a 1-bit
 * sprite bitmap by flood-filling "outside" white from the four corners.
 *
 * Sprite convention: bit=1 is black (bomb body), bit=0 is white
 * (transparent OR internal highlight). The flood fill starts at each
 * corner and walks 4-connected white pixels — these are the "outside"
 * transparent pixels. Anything NOT reached (internal white highlights,
 * plus the black bomb body itself) becomes mask bit=1 = "inside the
 * sprite silhouette, draw this tile".
 *
 * maskBM / maskStorage get allocated here; caller DisposePtrs.
 * Returns non-zero on success.
 */
static int BuildBombMaskByFloodFill(const BitMap *sprite,
                                    BitMap *maskBM, Ptr *maskStorage)
{
    short w = sprite->bounds.right - sprite->bounds.left;
    short h = sprite->bounds.bottom - sprite->bounds.top;
    short rowBytes = ((w + 15) / 16) * 2;
    Ptr storage;
    short sp = 0;
    short c;
    short cx, cy;
    short x, y;
    short spriteRowBytes;
    const unsigned char *spriteBits;
    unsigned char *maskBits;
    /* Stack arrays sized for TILE_SIZE_LARGE (32x32 = 1024 pixels max).
     * Eliminates 6 Memory Manager traps per frame and avoids heap
     * fragmentation on Mac SE. See Black Art (1996) Ch.3. */
    unsigned char visited[TILE_SIZE_LARGE * TILE_SIZE_LARGE];
    short stackX[TILE_SIZE_LARGE * TILE_SIZE_LARGE];
    short stackY[TILE_SIZE_LARGE * TILE_SIZE_LARGE];
    short stackCap = TILE_SIZE_LARGE * TILE_SIZE_LARGE;

    if (w > TILE_SIZE_LARGE || h > TILE_SIZE_LARGE) return 0;

    storage = NewPtrClear((long)rowBytes * h);
    if (storage == NULL) return 0;
    memset(visited, 0, (size_t)(w * h));

    spriteRowBytes = sprite->rowBytes & 0x3FFF;
    spriteBits = (const unsigned char *)sprite->baseAddr;

    for (c = 0; c < 4; c++) {
        switch (c) {
            case 0: cx = 0;     cy = 0;     break;
            case 1: cx = w - 1; cy = 0;     break;
            case 2: cx = 0;     cy = h - 1; break;
            default: cx = w - 1; cy = h - 1; break;
        }
        if (spriteBits[cy * spriteRowBytes + (cx >> 3)] & (0x80 >> (cx & 7))) {
            continue;
        }
        if (visited[cy * w + cx]) continue;
        stackX[sp] = cx; stackY[sp] = cy; sp++; visited[cy * w + cx] = 1;

        while (sp > 0) {
            short dx;
            sp--;
            x = stackX[sp]; y = stackY[sp];
            for (dx = 0; dx < 4; dx++) {
                short nx = x, ny = y;
                switch (dx) {
                    case 0: nx++; break;
                    case 1: nx--; break;
                    case 2: ny++; break;
                    default: ny--; break;
                }
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                if (visited[ny * w + nx]) continue;
                if (spriteBits[ny * spriteRowBytes + (nx >> 3)] & (0x80 >> (nx & 7))) continue;
                visited[ny * w + nx] = 1;
                if (sp < stackCap) {
                    stackX[sp] = nx; stackY[sp] = ny; sp++;
                }
            }
        }
    }

    maskBits = (unsigned char *)storage;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (!visited[y * w + x]) {
                maskBits[y * rowBytes + (x >> 3)] |= (0x80 >> (x & 7));
            }
        }
    }

    maskBM->baseAddr = storage;
    maskBM->rowBytes = rowBytes;
    SetRect(&maskBM->bounds, 0, 0, w, h);

    *maskStorage = storage;
    return 1;
}

static void LoadSEBombSprites(void)
{
    short ts = gGame.tileSize;  /* 16 on Mac SE */
    short i;
    for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
        int ok;
        gBombSpriteSE[i].baseAddr = NULL;
        gBombMaskSE[i].baseAddr = NULL;
        gBombSpriteSEStorage[i] = NULL;
        gBombMaskSEStorage[i] = NULL;
        ok = LoadPICTToBitMap(kBombSEIds[i], ts, ts,
                              &gBombSpriteSE[i], &gBombSpriteSEStorage[i]);
        if (!ok) {
            CLOG_WARN("SE bomb PICT %d not found -- using oval fallback for frame %d",
                      kBombSEIds[i], (int)i);
            continue;
        }
        if (!BuildBombMaskByFloodFill(&gBombSpriteSE[i],
                                      &gBombMaskSE[i], &gBombMaskSEStorage[i])) {
            CLOG_WARN("SE bomb mask build failed for frame %d", (int)i);
            DisposePtr(gBombSpriteSEStorage[i]);
            gBombSpriteSEStorage[i] = NULL;
            gBombSpriteSE[i].baseAddr = NULL;
        }
    }
    CLOG_INFO("SE bomb sprites: f0=%s f1=%s f2=%s",
              gBombSpriteSE[0].baseAddr ? "ok" : "fallback",
              gBombSpriteSE[1].baseAddr ? "ok" : "fallback",
              gBombSpriteSE[2].baseAddr ? "ok" : "fallback");
}

static void LoadPICTResources(void)
{
    short ts = gGame.tileSize;
    short tileSheetW = ts * 4;
    short i;

    /* Color Macs only. Mac SE runs the rectangle fallback path and never
     * reaches here (call-site in Renderer_Init guards on !gGame.isMacSE).
     * The rPict*SE resource IDs (see include/game.h) remain reserved for
     * future Mac SE PICT support — re-add an isMacSE branch to use them.
     * 008 FR-006: previous dead SE branches and ternaries removed. */
    gTileSheet = LoadPICTToGWorld(rPictTiles, tileSheetW, ts);
    gPlayerSprites[0] = LoadPICTToGWorld(rPictPlayerP0, ts, ts);
    gPlayerSprites[1] = LoadPICTToGWorld(rPictPlayerP1, ts, ts);
    gPlayerSprites[2] = LoadPICTToGWorld(rPictPlayerP2, ts, ts);
    gPlayerSprites[3] = LoadPICTToGWorld(rPictPlayerP3, ts, ts);
    for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
        gBombSprites[i] = LoadPICTToGWorld(kBombColorIds[i], ts, ts);
    }
    gExplosionSprite = LoadPICTToGWorld(rPictExplosion, ts, ts);
    gTitleSprite = LoadPICTToGWorld(rPictTitle, 320, 128);

    if (gTileSheet != NULL) {
        gPICTsLoaded = TRUE;
        CLOG_INFO("PICT resources loaded successfully");
    } else {
        gPICTsLoaded = FALSE;
        CLOG_WARN("PICT resources not found, using rectangle fallback");
    }

    /* Create mask regions for srcCopy sprite blitting (006-renderer-optimization). */
    for (i = 0; i < MAX_PLAYERS; i++) {
        gPlayerMaskRgn[i] = CreateMaskFromGWorld(gPlayerSprites[i], ts, ts);
    }
    for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
        gBombMaskRgn[i] = CreateMaskFromGWorld(gBombSprites[i], ts, ts);
    }
    gExplosionMaskRgn = CreateMaskFromGWorld(gExplosionSprite, ts, ts);
    gTitleMaskRgn = CreateMaskFromGWorld(gTitleSprite, 320, 128);
    CLOG_INFO("Mask regions: P0=%s P1=%s P2=%s P3=%s bombF0=%s bombF1=%s bombF2=%s expl=%s title=%s",
              gPlayerMaskRgn[0] ? "ok" : "FAIL",
              gPlayerMaskRgn[1] ? "ok" : "FAIL",
              gPlayerMaskRgn[2] ? "ok" : "FAIL",
              gPlayerMaskRgn[3] ? "ok" : "FAIL",
              gBombMaskRgn[0] ? "ok" : "FAIL",
              gBombMaskRgn[1] ? "ok" : "FAIL",
              gBombMaskRgn[2] ? "ok" : "FAIL",
              gExplosionMaskRgn ? "ok" : "FAIL",
              gTitleMaskRgn ? "ok" : "FAIL");
}

/* ==== Mac SE: Offscreen BitMap allocation ==== */

static int AllocOffscreenBitMap(GrafPort *port, BitMap *bits, Ptr *storage,
                                 short width, short height)
{
    short rowBytes;
    long bufSize;

    rowBytes = ((width + 15) / 16) * 2;
    bufSize = (long)rowBytes * height;

    *storage = NewPtrClear(bufSize);
    if (*storage == NULL) return FALSE;

    bits->baseAddr = *storage;
    bits->rowBytes = rowBytes;
    SetRect(&bits->bounds, 0, 0, width, height);

    OpenPort(port);
    SetPortBits(bits);
    port->portRect = bits->bounds;

    ClipRect(&bits->bounds);

    return TRUE;
}

/* ==== Init / Shutdown ==== */

void Renderer_Init(WindowPtr window)
{
    Rect bounds;

    (void)window;

    SetRect(&bounds, 0, 0, gGame.playWidth, gGame.playHeight);

    if (gGame.isMacSE) {
        if (!AllocOffscreenBitMap(&gBgPortSE, &gBgBitsSE, &gBgStorageSE,
                                   gGame.playWidth, gGame.playHeight)) {
            CLOG_ERR("Failed to create SE background bitmap");
            SysBeep(30);
            ExitToShell();
        }
        if (!AllocOffscreenBitMap(&gWorkPortSE, &gWorkBitsSE, &gWorkStorageSE,
                                   gGame.playWidth, gGame.playHeight)) {
            CLOG_ERR("Failed to create SE work bitmap");
            SysBeep(30);
            ExitToShell();
        }
        CLOG_INFO("Mac SE offscreen bitmaps allocated: %ldB each",
                   (long)gBgBitsSE.rowBytes * gGame.playHeight);
    } else {
        QDErr err;

        err = NewGWorld(&gBackground, 0, &bounds, NULL, NULL, 0);
        if (err != noErr || gBackground == NULL) {
            CLOG_ERR("Failed to create background GWorld");
            SysBeep(30);
            ExitToShell();
        }

        err = NewGWorld(&gWorkBuffer, 0, &bounds, NULL, NULL, 0);
        if (err != noErr || gWorkBuffer == NULL) {
            CLOG_ERR("Failed to create work buffer GWorld");
            SysBeep(30);
            ExitToShell();
        }
    }

    /* Set clean color state on work buffer once (006-renderer-optimization).
     * CopyBits requires ForeColor(black)/BackColor(white) on the dest port
     * for correct srcCopy behavior. Persists across frames. */
    SavePort();
    SetPortWork();
    ForeColor(blackColor);
    BackColor(whiteColor);
    RestorePort();

    /* Load PICT resources (fall back to rectangles if missing) */
    if (!gGame.isMacSE) {
        LoadPICTResources();
    } else {
        LoadSEBombSprites();
    }

    /* Initialize dirty rect tracking */
    gDirtyTotal = TileMap_GetCols() * TileMap_GetRows();
    Renderer_ClearDirty();

    /* Build the initial background */
    Renderer_RebuildBackground();

    CLOG_INFO("Renderer initialized: %dx%d, tile=%d, SE=%s, PICTs=%s",
              gGame.playWidth, gGame.playHeight, gGame.tileSize,
              gGame.isMacSE ? "yes" : "no",
              gPICTsLoaded ? "yes" : "no");
}

void Renderer_Shutdown(void)
{
    short i;

    /* Redirect QuickDraw away from our offscreens before disposing them.
     * Per Sex, Lies and Video Games (1996) p.104: disposing the current
     * port is "a quick trip to MacsBug" — the Font Manager retains
     * System heap references to the freed port's font data, crashing
     * the next app (Finder) that calls StdText. */
    if (gGame.window) {
        if (gGame.isMacSE) {
            SetPort(gGame.window);
        } else {
            SetGWorld((CGrafPtr)gGame.window, GetMainDevice());
        }
    }

    if (gGame.isMacSE) {
        ClosePort(&gBgPortSE);
        ClosePort(&gWorkPortSE);
        if (gBgStorageSE) { DisposePtr(gBgStorageSE); gBgStorageSE = NULL; }
        if (gWorkStorageSE) { DisposePtr(gWorkStorageSE); gWorkStorageSE = NULL; }
        for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
            if (gBombSpriteSEStorage[i]) {
                DisposePtr(gBombSpriteSEStorage[i]);
                gBombSpriteSEStorage[i] = NULL;
                gBombSpriteSE[i].baseAddr = NULL;
            }
            if (gBombMaskSEStorage[i]) {
                DisposePtr(gBombMaskSEStorage[i]);
                gBombMaskSEStorage[i] = NULL;
                gBombMaskSE[i].baseAddr = NULL;
            }
        }
    } else {
        /* UnlockPixels before DisposeGWorld per Black Art (1996) Ch. 5:
         * "The GWorld's PixMapHandle should be unlocked before calling
         * DisposeGWorld."  Inside Mac VI confirms unlocking already-unlocked
         * pixels is harmless, so unconditional unlock is safe here. */
        if (gBackground) {
            UnlockPixels(GetGWorldPixMap(gBackground));
            DisposeGWorld(gBackground);
            gBackground = NULL;
        }
        if (gWorkBuffer) {
            UnlockPixels(GetGWorldPixMap(gWorkBuffer));
            DisposeGWorld(gWorkBuffer);
            gWorkBuffer = NULL;
        }
    }

    if (gTileSheet) {
        UnlockPixels(GetGWorldPixMap(gTileSheet));
        DisposeGWorld(gTileSheet);
        gTileSheet = NULL;
    }

    /* Dispose mask regions before sprite GWorlds (006-renderer-optimization) */
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (gPlayerMaskRgn[i] != NULL) {
            short j;
            int dup = FALSE;
            for (j = 0; j < i; j++) {
                if (gPlayerMaskRgn[j] == gPlayerMaskRgn[i]) {
                    dup = TRUE;
                    break;
                }
            }
            if (!dup) {
                DisposeRgn(gPlayerMaskRgn[i]);
            }
            gPlayerMaskRgn[i] = NULL;
        }
    }
    for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
        if (gBombMaskRgn[i]) { DisposeRgn(gBombMaskRgn[i]); gBombMaskRgn[i] = NULL; }
    }
    if (gExplosionMaskRgn) { DisposeRgn(gExplosionMaskRgn); gExplosionMaskRgn = NULL; }
    if (gTitleMaskRgn) { DisposeRgn(gTitleMaskRgn); gTitleMaskRgn = NULL; }

    for (i = 0; i < MAX_PLAYERS; i++) {
        if (gPlayerSprites[i] != NULL) {
            short j;
            int dup = FALSE;
            for (j = 0; j < i; j++) {
                if (gPlayerSprites[j] == gPlayerSprites[i]) {
                    dup = TRUE;
                    break;
                }
            }
            if (!dup) {
                UnlockPixels(GetGWorldPixMap(gPlayerSprites[i]));
                DisposeGWorld(gPlayerSprites[i]);
            }
            gPlayerSprites[i] = NULL;
        }
    }

    for (i = 0; i < BOMB_ANIM_FRAMES; i++) {
        if (gBombSprites[i]) {
            UnlockPixels(GetGWorldPixMap(gBombSprites[i]));
            DisposeGWorld(gBombSprites[i]);
            gBombSprites[i] = NULL;
        }
    }
    if (gExplosionSprite) {
        UnlockPixels(GetGWorldPixMap(gExplosionSprite));
        DisposeGWorld(gExplosionSprite);
        gExplosionSprite = NULL;
    }
    if (gTitleSprite) {
        UnlockPixels(GetGWorldPixMap(gTitleSprite));
        DisposeGWorld(gTitleSprite);
        gTitleSprite = NULL;
    }
}

/* ==== Tile Drawing ==== */

static void DrawTileFromSheet(short tileIndex, short col, short row,
                              BitMap *sheetBits)
{
    Rect srcRect, dstRect;
    short ts = gGame.tileSize;

    SetRect(&srcRect, tileIndex * ts, 0, (tileIndex + 1) * ts, ts);
    SetRect(&dstRect, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    CopyBits(sheetBits, GetBgBits(),
             &srcRect, &dstRect, srcCopy, NULL);
}

/*
 * Renderer_RebuildBackground -- Redraw all tiles to the background buffer.
 *
 * Port save/lock hoisted here (once) instead of per-tile.
 * Source: Tricks of the Mac Game Programming Gurus (1995).
 */
void Renderer_RebuildBackground(void)
{
    TileMap *map;
    short r, c;
    short mapCols, mapRows;
    unsigned char tile;
    int useSheet;
    BitMap *sheetBits = NULL;

    CLOG_INFO("RebuildBackground");

    map = TileMap_Get();
    mapCols = TileMap_GetCols();
    mapRows = TileMap_GetRows();
    useSheet = (gPICTsLoaded && !gGame.isMacSE);

    SavePort();
    SetPortBg();
    LockBg();

    ForeColor(blackColor);
    BackColor(whiteColor);

    if (useSheet) {
        LockPixels(GetGWorldPixMap(gTileSheet));
        sheetBits = (BitMap *)*GetGWorldPixMap(gTileSheet);

        for (r = 0; r < mapRows; r++) {
            for (c = 0; c < mapCols; c++) {
                short idx;
                tile = map->tiles[r][c];
                idx = (tile == TILE_SPAWN) ? 0 : tile;
                if (idx > 3) idx = 0;
                DrawTileFromSheet(idx, c, r, sheetBits);
            }
        }
    } else {
        /* Batched tile drawing: one ForeColor per tile type instead of
         * per tile. Reduces ForeColor trap calls from O(tiles) to
         * O(tile_types). Source: Sex Lies (1996) p.5645-5653. */
        short ts = gGame.tileSize;

        if (gGame.isMacSE) {
            /* Pass 1: Floor/spawn (white) */
            ForeColor(whiteColor);
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    tile = map->tiles[r][c];
                    if (tile == TILE_FLOOR || tile == TILE_SPAWN ||
                        (tile != TILE_WALL && tile != TILE_BLOCK)) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        PaintRect(&tr);
                    }
                }
            }
            /* Pass 2: Walls (black) */
            ForeColor(blackColor);
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    if (map->tiles[r][c] == TILE_WALL) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        PaintRect(&tr);
                    }
                }
            }
            /* Pass 3: Blocks (gray pattern, no ForeColor needed) */
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    if (map->tiles[r][c] == TILE_BLOCK) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        FillRect(&tr, &qd.gray);
                    }
                }
            }
        } else {
            /* Pass 1: Floor/spawn (green) */
            RGBForeColor(&kTileGreen);
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    tile = map->tiles[r][c];
                    if (tile == TILE_FLOOR || tile == TILE_SPAWN ||
                        (tile != TILE_WALL && tile != TILE_BLOCK)) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        PaintRect(&tr);
                    }
                }
            }
            /* Pass 2: Walls (gray fill + dark gray frame) */
            RGBForeColor(&kTileGray);
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    if (map->tiles[r][c] == TILE_WALL) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        PaintRect(&tr);
                    }
                }
            }
            RGBForeColor(&kTileDarkGray);
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    if (map->tiles[r][c] == TILE_WALL) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        FrameRect(&tr);
                    }
                }
            }
            /* Pass 3: Blocks (brown fill + dark brown frame) */
            RGBForeColor(&kTileBrown);
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    if (map->tiles[r][c] == TILE_BLOCK) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        PaintRect(&tr);
                    }
                }
            }
            RGBForeColor(&kTileDarkBrown);
            for (r = 0; r < mapRows; r++) {
                for (c = 0; c < mapCols; c++) {
                    if (map->tiles[r][c] == TILE_BLOCK) {
                        Rect tr;
                        SetRect(&tr, c * ts, r * ts, (c+1) * ts, (r+1) * ts);
                        FrameRect(&tr);
                    }
                }
            }
        }
        /* Restore ForeColor for CopyBits after rebuild */
        ForeColor(blackColor);
    }

    if (useSheet) {
        UnlockPixels(GetGWorldPixMap(gTileSheet));
    }

    UnlockBg();
    RestorePort();

    /* Restore work buffer color state after rebuild (006-renderer-optimization).
     * RebuildBackground operates on the bg port; restore work port colors
     * so BeginFrame's CopyBits gets correct srcCopy behavior. */
    SavePort();
    SetPortWork();
    ForeColor(blackColor);
    BackColor(whiteColor);
    RestorePort();

    /* Mark all tiles dirty after background rebuild */
    Renderer_MarkAllDirty();
}

void Renderer_RequestRebuildBackground(void)
{
    gNeedRebuildBg = TRUE;
}

/* ==== Sprite Draw Bracket (006-renderer-optimization) ==== */

void Renderer_BeginSpriteDraw(void)
{
    SavePort();
    SetPortWork();
    ForeColor(blackColor);
    BackColor(whiteColor);
    gSpriteDrawActive = TRUE;
}

void Renderer_EndSpriteDraw(void)
{
    gSpriteDrawActive = FALSE;
    RestorePort();
}

/* ==== Per-Frame Rendering ==== */

void Renderer_BeginFrame(void)
{
    /* Check deferred rebuild flag before dirty rect processing */
    if (gNeedRebuildBg) {
        gNeedRebuildBg = FALSE;
        Renderer_RebuildBackground();
    }

    LockBg();
    LockWork();

    /* Color state set once at init + after rebuild (006-renderer-optimization).
     * No code path between EndFrame and BeginFrame modifies the work port. */

    /* Dirty rect optimization (T017):
     * If all dirty or >50% dirty, do full-screen CopyBits.
     * Otherwise copy only dirty tile rects with 32-bit alignment. */
    if (gDirtyCount >= gDirtyTotal || gDirtyCount > gDirtyTotal / 2) {
        Rect bounds;
        SetRect(&bounds, 0, 0, gGame.playWidth, gGame.playHeight);
        CopyBits(GetBgBits(), GetWorkBits(),
                 &bounds, &bounds, srcCopy, NULL);
    } else {
        short di;
        short ts = gGame.tileSize;

        for (di = 0; di < gDirtyCount; di++) {
            Rect tileRect;
            short c = gDirtyListCol[di];
            short r = gDirtyListRow[di];
            SetRect(&tileRect, c * ts, r * ts,
                    (c + 1) * ts, (r + 1) * ts);
            AlignRect32(&tileRect);
            CopyBits(GetBgBits(), GetWorkBits(),
                     &tileRect, &tileRect, srcCopy, NULL);
        }
    }

    UnlockBg();
    /* Keep work buffer locked for sprite drawing */

    /* Lock all sprite GWorlds and cache PixMap pointers (T014) */
    LockAllSprites();
}

void Renderer_DrawPlayer(short playerID, short pixelX, short pixelY, short facing)
{
    Rect dstRect;
    short ts = gGame.tileSize;

    (void)facing;

    SetRect(&dstRect, pixelX, pixelY, pixelX + ts, pixelY + ts);

    if (!gGame.isMacSE && gPICTsLoaded &&
        gCachedPlayerPM[playerID] != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        if (gPlayerMaskRgn[playerID] != NULL) {
            OffsetRgn(gPlayerMaskRgn[playerID], dstRect.left, dstRect.top);
            CopyBits(
                gCachedPlayerPM[playerID],
                GetWorkBits(),
                &srcRect, &dstRect, srcCopy, gPlayerMaskRgn[playerID]);
            OffsetRgn(gPlayerMaskRgn[playerID], -dstRect.left, -dstRect.top);
        } else {
            CopyBits(
                gCachedPlayerPM[playerID],
                GetWorkBits(),
                &srcRect, &dstRect, transparent, NULL);
        }
    } else {
        if (!gSpriteDrawActive) {
            SavePort();
            SetPortWork();
        }

        InsetRect(&dstRect, 2, 2);

        if (gGame.isMacSE) {
            short w = dstRect.right - dstRect.left;
            short cx = dstRect.left + w / 2;
            short cy = dstRect.top + (dstRect.bottom - dstRect.top) / 2;
            Rect mark;

            /* ForeColor=black, BackColor=white already set by
             * Renderer_BeginSpriteDraw bracket (006) and restored by the
             * trailing ForeColor(blackColor) below — no per-player re-assert
             * needed (008 FR-003 minimal; saves ~8 traps/frame on SE). */
            PaintRect(&dstRect);

            ForeColor(whiteColor);
            if (playerID == gGame.localPlayerID) {
                SetRect(&mark, dstRect.left + 1, cy - 1,
                        dstRect.right - 1, cy + 1);
                PaintRect(&mark);
                SetRect(&mark, cx - 1, dstRect.top + 1,
                        cx + 1, dstRect.bottom - 1);
                PaintRect(&mark);
            } else {
                SetRect(&mark, cx - 2, cy - 2, cx + 2, cy + 2);
                PaintRect(&mark);
            }
            ForeColor(blackColor); /* reset for next player's body PaintRect */
        } else {
            RGBForeColor(kPlayerColors[playerID & 3]);
            PaintRect(&dstRect);
            ForeColor(blackColor);
            FrameRect(&dstRect);
        }

        if (!gSpriteDrawActive) {
            RestorePort();
        }
    }
}

void Renderer_DrawBomb(short col, short row, short frameIndex)
{
    Rect dstRect;
    short ts = gGame.tileSize;
    short f;

    if (frameIndex < 0) f = 0;
    else if (frameIndex >= BOMB_ANIM_FRAMES) f = BOMB_ANIM_FRAMES - 1;
    else f = frameIndex;

    SetRect(&dstRect, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    /* Mark tile dirty so next frame's BeginFrame re-copies bg here before
     * we draw the possibly-different frame. Without this, an animating
     * bomb sitting on the same tile would only redraw on the first frame. */
    Renderer_MarkDirty(col, row);

    /* Bomb PICTs ship in the resource fork independently of other sprite
     * categories -- don't gate on gPICTsLoaded (which is FALSE whenever
     * rPictTiles is missing, even if bomb PICTs loaded fine). Per-frame
     * NULL check on the cached PixMap is the right guard. */
    if (!gGame.isMacSE && gCachedBombPM[f] != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        if (gBombMaskRgn[f] != NULL) {
            OffsetRgn(gBombMaskRgn[f], dstRect.left, dstRect.top);
            CopyBits(
                gCachedBombPM[f],
                GetWorkBits(),
                &srcRect, &dstRect, srcCopy, gBombMaskRgn[f]);
            OffsetRgn(gBombMaskRgn[f], -dstRect.left, -dstRect.top);
        } else {
            CopyBits(
                gCachedBombPM[f],
                GetWorkBits(),
                &srcRect, &dstRect, transparent, NULL);
        }
    } else if (gGame.isMacSE && gBombSpriteSE[f].baseAddr != NULL) {
        /* Mac SE PICT path: two-pass 1-bit composite.
         *  1) srcBic with mask -- punches a white hole in the tile where
         *     the bomb silhouette goes (including internal highlights).
         *  2) srcOr with sprite -- fills the hole with the sprite's
         *     black pixels; white pixels stay as the just-cleared hole,
         *     so internal highlights remain white, not tile-coloured.
         * Source: Mac Game Programming (2002) Ch. 7, and the mask-blit
         * pattern in Sex, Lies and Video Games (1996) p.6620.          */
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);
        if (!gSpriteDrawActive) {
            SavePort();
            SetPortWork();
        }
        CopyBits(&gBombMaskSE[f], GetWorkBits(),
                 &srcRect, &dstRect, srcBic, NULL);
        CopyBits(&gBombSpriteSE[f], GetWorkBits(),
                 &srcRect, &dstRect, srcOr, NULL);
        if (!gSpriteDrawActive) {
            RestorePort();
        }
    } else {
        /* Fallback: animated black oval. Inset shrinks with frame index
         * so the bomb appears to pulse (frame 0 = smallest/4px inset,
         * frame 2 = largest). Used on Mac SE when the SE bomb PICTs are
         * missing from the resource fork, and on colour Macs when the
         * PICTs fail to load. */
        short inset = 4 - f;
        if (!gSpriteDrawActive) {
            SavePort();
            SetPortWork();
        }

        InsetRect(&dstRect, inset, inset);
        ForeColor(blackColor);
        PaintOval(&dstRect);

        if (!gSpriteDrawActive) {
            RestorePort();
        }
    }
}

void Renderer_DrawExplosion(short col, short row)
{
    Rect dstRect;
    short ts = gGame.tileSize;

    SetRect(&dstRect, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    if (!gGame.isMacSE && gPICTsLoaded && gCachedExplosionPM != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        if (gExplosionMaskRgn != NULL) {
            OffsetRgn(gExplosionMaskRgn, dstRect.left, dstRect.top);
            CopyBits(
                gCachedExplosionPM,
                GetWorkBits(),
                &srcRect, &dstRect, srcCopy, gExplosionMaskRgn);
            OffsetRgn(gExplosionMaskRgn, -dstRect.left, -dstRect.top);
        } else {
            CopyBits(
                gCachedExplosionPM,
                GetWorkBits(),
                &srcRect, &dstRect, transparent, NULL);
        }
    } else {
        if (!gSpriteDrawActive) {
            SavePort();
            SetPortWork();
        }

        if (gGame.isMacSE) {
            InvertRect(&dstRect);
        } else {
            RGBForeColor(&kExplosionOrange);
            PaintRect(&dstRect);
        }

        if (!gSpriteDrawActive) {
            RestorePort();
        }
    }
}


static void Renderer_ClearWork(void)
{
    Rect bounds;

    SetRect(&bounds, 0, 0, gGame.playWidth, gGame.playHeight);

    SavePort();
    SetPortWork();
    LockWork();

    ForeColor(blackColor);
    PaintRect(&bounds);

    UnlockWork();
    RestorePort();
}

void Renderer_EndFrame(WindowPtr window)
{
    /* Unlock all sprite GWorlds (T014) */
    UnlockAllSprites();

    UnlockWork();
    Renderer_BlitToWindow(window);
}

void Renderer_BlitToWindow(WindowPtr window)
{
    GrafPtr savePort;

    if (window == NULL) return;
    if (!gGame.isMacSE && gWorkBuffer == NULL) return;
    if (gGame.isMacSE && gWorkStorageSE == NULL) return;

    GetPort(&savePort);
    SetPort(window);

    /* Ensure clean color state before srcCopy CopyBits (all platforms) */
    ForeColor(blackColor);
    BackColor(whiteColor);

    LockWork();

    /* Dirty rect optimization for work->window blit (T018) */
    if (gDirtyCount >= gDirtyTotal || gDirtyCount > gDirtyTotal / 2) {
        Rect bounds;
        SetRect(&bounds, 0, 0, gGame.playWidth, gGame.playHeight);
        CopyBits(GetWorkBits(), &window->portBits,
                 &bounds, &bounds, srcCopy, NULL);
    } else {
        short di;
        short ts = gGame.tileSize;

        for (di = 0; di < gDirtyCount; di++) {
            Rect tileRect;
            short c = gDirtyListCol[di];
            short r = gDirtyListRow[di];
            SetRect(&tileRect, c * ts, r * ts,
                    (c + 1) * ts, (r + 1) * ts);
            AlignRect32(&tileRect);
            CopyBits(GetWorkBits(), &window->portBits,
                     &tileRect, &tileRect, srcCopy, NULL);
        }
    }

    /* Clear dirty grid after blit */
    Renderer_ClearDirty();

    UnlockWork();

    SetPort(savePort);
}

/* ==== Screen Draw Helpers ==== */

/*
 * Renderer_DrawSplashBackground -- Draw the loading-splash PICT into the
 * currently-active port. Meant to be called between BeginScreenDraw and
 * EndScreenDraw so the splash lands on the work buffer and gets blitted
 * to the window by EndScreenDraw.
 *
 * Uses DrawPicture() directly (no GWorld) so the same code path works
 * on Mac SE (GrafPort work buffer) and colour Macs (GWorld work buffer).
 * The PicHandle is lazy-loaded on first call, cached for subsequent.
 * Resource selection depends on gGame.isMacSE.
 */
void Renderer_DrawSplashBackground(void)
{
    static PicHandle splashPic = NULL;
    static int loadAttempted = FALSE;
    Rect dstRect;

    if (!loadAttempted) {
        short resId = gGame.isMacSE ? rPictSplashSE : rPictSplashColor;
        splashPic = GetPicture(resId);
        loadAttempted = TRUE;
        if (splashPic == NULL) {
            CLOG_WARN("Splash PICT %d not found in resource fork", resId);
        } else {
            CLOG_INFO("Splash PICT %d loaded", resId);
        }
    }

    if (splashPic == NULL) return;

    SetRect(&dstRect, 0, 0, gGame.playWidth, gGame.playHeight);
    DrawPicture(splashPic, &dstRect);

    ReleaseResource((Handle)splashPic);
    splashPic = NULL;
}

void Renderer_BeginScreenDraw(void)
{
    Renderer_ClearWork();

    if (gGame.isMacSE) {
        GetPort((GrafPtr *)&gSavedScreenCPort);
        SetPort(&gWorkPortSE);
    } else {
        GetGWorld(&gSavedScreenCPort, &gSavedScreenDevice);
        SetGWorld(gWorkBuffer, NULL);
        LockPixels(GetGWorldPixMap(gWorkBuffer));
    }
}

void Renderer_EndScreenDraw(WindowPtr window)
{
    if (gGame.isMacSE) {
        SetPort((GrafPtr)gSavedScreenCPort);
    } else {
        UnlockPixels(GetGWorldPixMap(gWorkBuffer));
        SetGWorld(gSavedScreenCPort, gSavedScreenDevice);
    }
    gSavedScreenCPort = NULL;
    gSavedScreenDevice = NULL;

    /* For screen draws, mark all dirty so full blit happens */
    Renderer_MarkAllDirty();
    Renderer_BlitToWindow(window);
}

void Renderer_DrawFPS(short fps)
{
    GrafPtr savePort;
    Rect bgRect;
    Str255 fpsStr;
    short strW;
    short x, y;
    short tens, ones;

    if (gGame.window == NULL) return;

    GetPort(&savePort);
    SetPort(gGame.window);

    tens = fps / 10;
    ones = fps % 10;
    if (tens > 0) {
        fpsStr[0] = 6;
        fpsStr[1] = (unsigned char)('0' + tens);
        fpsStr[2] = (unsigned char)('0' + ones);
        fpsStr[3] = ' ';
        fpsStr[4] = 'f';
        fpsStr[5] = 'p';
        fpsStr[6] = 's';
    } else {
        fpsStr[0] = 5;
        fpsStr[1] = (unsigned char)('0' + ones);
        fpsStr[2] = ' ';
        fpsStr[3] = 'f';
        fpsStr[4] = 'p';
        fpsStr[5] = 's';
    }

    TextSize(10);
    strW = StringWidth(fpsStr);
    x = gGame.playWidth - strW - 4;
    y = gGame.playHeight - 4;

    SetRect(&bgRect, x - 2, y - 10, gGame.playWidth, gGame.playHeight);
    ForeColor(blackColor);
    PaintRect(&bgRect);

    ForeColor(whiteColor);
    MoveTo(x, y);
    DrawString(fpsStr);

    ForeColor(blackColor);
    BackColor(whiteColor);
    SetPort(savePort);
}
