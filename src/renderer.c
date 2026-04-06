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
static GWorldPtr gBombSprite = NULL;
static GWorldPtr gExplosionSprite = NULL;
static GWorldPtr gTitleSprite = NULL;

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
static BitMap *gCachedBombPM = NULL;
static BitMap *gCachedExplosionPM = NULL;

/* ---- Deferred background rebuild flag ---- */
static int gNeedRebuildBg = FALSE;

/* ---- Dirty rectangle grid (T015) ---- */
static unsigned char gDirtyGrid[MAX_GRID_ROWS][MAX_GRID_COLS];
static short gDirtyCount = 0;
static short gDirtyTotal = 0;

/* ==== Dirty Rectangle API ==== */

void Renderer_MarkDirty(short col, short row)
{
    if (col < 0 || col >= TileMap_GetCols() ||
        row < 0 || row >= TileMap_GetRows()) return;
    if (gDirtyGrid[row][col]) return;
    gDirtyGrid[row][col] = 1;
    gDirtyCount++;
}

void Renderer_MarkAllDirty(void)
{
    memset(gDirtyGrid, 1, sizeof(gDirtyGrid));
    gDirtyCount = gDirtyTotal;
}

void Renderer_ClearDirty(void)
{
    memset(gDirtyGrid, 0, sizeof(gDirtyGrid));
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
    if (gBombSprite != NULL) {
        LockPixels(GetGWorldPixMap(gBombSprite));
        gCachedBombPM = (BitMap *)*GetGWorldPixMap(gBombSprite);
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
    if (gBombSprite != NULL) {
        UnlockPixels(GetGWorldPixMap(gBombSprite));
    }
    gCachedBombPM = NULL;
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

static CGrafPtr gSavedPort = NULL;
static GDHandle gSavedDevice = NULL;

static void SavePort(void)
{
    if (gGame.isMacSE)
        GetPort((GrafPtr *)&gSavedPort);
    else
        GetGWorld(&gSavedPort, &gSavedDevice);
}

static void RestorePort(void)
{
    if (gGame.isMacSE)
        SetPort((GrafPtr)gSavedPort);
    else
        SetGWorld(gSavedPort, gSavedDevice);
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

static void LoadPICTResources(void)
{
    short ts = gGame.tileSize;
    short tileSheetW = ts * 4;
    short i;

    if (gGame.isMacSE) {
        gTileSheet = LoadPICTToGWorld(rPictTilesSE, tileSheetW, ts);
        gPlayerSprites[0] = LoadPICTToGWorld(rPictPlayerSE, ts, ts);
        for (i = 1; i < MAX_PLAYERS; i++) {
            gPlayerSprites[i] = gPlayerSprites[0];
        }
        gBombSprite = LoadPICTToGWorld(rPictBombSE, ts, ts);
        gExplosionSprite = LoadPICTToGWorld(rPictExplosionSE, ts, ts);
        gTitleSprite = LoadPICTToGWorld(rPictTitleSE, 240, 80);
    } else {
        gTileSheet = LoadPICTToGWorld(rPictTiles, tileSheetW, ts);
        gPlayerSprites[0] = LoadPICTToGWorld(rPictPlayerP0, ts, ts);
        gPlayerSprites[1] = LoadPICTToGWorld(rPictPlayerP1, ts, ts);
        gPlayerSprites[2] = LoadPICTToGWorld(rPictPlayerP2, ts, ts);
        gPlayerSprites[3] = LoadPICTToGWorld(rPictPlayerP3, ts, ts);
        gBombSprite = LoadPICTToGWorld(rPictBomb, ts, ts);
        gExplosionSprite = LoadPICTToGWorld(rPictExplosion, ts, ts);
        gTitleSprite = LoadPICTToGWorld(rPictTitle, 320, 128);
    }

    if (gTileSheet != NULL) {
        gPICTsLoaded = TRUE;
        CLOG_INFO("PICT resources loaded successfully");
    } else {
        gPICTsLoaded = FALSE;
        CLOG_WARN("PICT resources not found, using rectangle fallback");
    }
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

    /* Load PICT resources (fall back to rectangles if missing) */
    if (!gGame.isMacSE) {
        LoadPICTResources();
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

    if (gGame.isMacSE) {
        ClosePort(&gBgPortSE);
        ClosePort(&gWorkPortSE);
        if (gBgStorageSE) { DisposePtr(gBgStorageSE); gBgStorageSE = NULL; }
        if (gWorkStorageSE) { DisposePtr(gWorkStorageSE); gWorkStorageSE = NULL; }
    } else {
        if (gBackground) { DisposeGWorld(gBackground); gBackground = NULL; }
        if (gWorkBuffer) { DisposeGWorld(gWorkBuffer); gWorkBuffer = NULL; }
    }

    if (gTileSheet) { DisposeGWorld(gTileSheet); gTileSheet = NULL; }

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
            if (!dup) DisposeGWorld(gPlayerSprites[i]);
            gPlayerSprites[i] = NULL;
        }
    }

    if (gBombSprite) { DisposeGWorld(gBombSprite); gBombSprite = NULL; }
    if (gExplosionSprite) { DisposeGWorld(gExplosionSprite); gExplosionSprite = NULL; }
    if (gTitleSprite) { DisposeGWorld(gTitleSprite); gTitleSprite = NULL; }
}

/* ==== Tile Drawing ==== */

static void DrawTileRect(short tileType, short col, short row)
{
    Rect r;
    short ts = gGame.tileSize;

    SetRect(&r, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    if (gGame.isMacSE) {
        switch (tileType) {
        case TILE_FLOOR:
        case TILE_SPAWN:
            ForeColor(whiteColor);
            PaintRect(&r);
            ForeColor(blackColor);
            break;
        case TILE_WALL:
            PaintRect(&r);
            break;
        case TILE_BLOCK:
            FillRect(&r, &qd.gray);
            break;
        default:
            ForeColor(whiteColor);
            PaintRect(&r);
            ForeColor(blackColor);
            break;
        }
    } else {
        switch (tileType) {
        case TILE_FLOOR:
        case TILE_SPAWN:
            RGBForeColor(&kTileGreen);
            PaintRect(&r);
            break;
        case TILE_WALL:
            RGBForeColor(&kTileGray);
            PaintRect(&r);
            RGBForeColor(&kTileDarkGray);
            FrameRect(&r);
            break;
        case TILE_BLOCK:
            RGBForeColor(&kTileBrown);
            PaintRect(&r);
            RGBForeColor(&kTileDarkBrown);
            FrameRect(&r);
            break;
        default:
            ForeColor(whiteColor);
            PaintRect(&r);
            break;
        }
    }
}

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
    }

    for (r = 0; r < mapRows; r++) {
        for (c = 0; c < mapCols; c++) {
            tile = map->tiles[r][c];
            if (useSheet) {
                short idx = (tile == TILE_SPAWN) ? 0 : tile;
                if (idx > 3) idx = 0;
                DrawTileFromSheet(idx, c, r, sheetBits);
            } else {
                DrawTileRect(tile, c, r);
            }
        }
    }

    if (useSheet) {
        UnlockPixels(GetGWorldPixMap(gTileSheet));
    }

    UnlockBg();
    RestorePort();

    /* Mark all tiles dirty after background rebuild */
    Renderer_MarkAllDirty();
}

void Renderer_RequestRebuildBackground(void)
{
    gNeedRebuildBg = TRUE;
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

    /* Ensure clean color state before srcCopy CopyBits (all platforms) */
    SavePort();
    SetPortWork();
    ForeColor(blackColor);
    BackColor(whiteColor);
    RestorePort();

    /* Dirty rect optimization (T017):
     * If all dirty or >50% dirty, do full-screen CopyBits.
     * Otherwise copy only dirty tile rects with 32-bit alignment. */
    if (gDirtyCount >= gDirtyTotal || gDirtyCount > gDirtyTotal / 2) {
        Rect bounds;
        SetRect(&bounds, 0, 0, gGame.playWidth, gGame.playHeight);
        CopyBits(GetBgBits(), GetWorkBits(),
                 &bounds, &bounds, srcCopy, NULL);
    } else {
        short r, c;
        short ts = gGame.tileSize;
        short mapCols = TileMap_GetCols();
        short mapRows = TileMap_GetRows();

        for (r = 0; r < mapRows; r++) {
            for (c = 0; c < mapCols; c++) {
                if (gDirtyGrid[r][c]) {
                    Rect tileRect;
                    SetRect(&tileRect, c * ts, r * ts,
                            (c + 1) * ts, (r + 1) * ts);
                    AlignRect32(&tileRect);
                    CopyBits(GetBgBits(), GetWorkBits(),
                             &tileRect, &tileRect, srcCopy, NULL);
                }
            }
        }
    }

    UnlockBg();
    /* Keep work buffer locked for sprite drawing */

    /* Lock all sprite GWorlds and cache PixMap pointers (T014) */
    LockAllSprites();
}

void Renderer_DrawPlayer(short playerID, short col, short row, short facing)
{
    Rect dstRect;
    short ts = gGame.tileSize;

    (void)facing;

    SetRect(&dstRect, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    if (!gGame.isMacSE && gPICTsLoaded &&
        gCachedPlayerPM[playerID] != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        CopyBits(
            gCachedPlayerPM[playerID],
            GetWorkBits(),
            &srcRect, &dstRect, transparent, NULL);
    } else {
        SavePort();
        SetPortWork();

        InsetRect(&dstRect, 2, 2);

        if (gGame.isMacSE) {
            short w = dstRect.right - dstRect.left;
            short h = dstRect.bottom - dstRect.top;
            short cx = dstRect.left + w / 2;
            short cy = dstRect.top + h / 2;
            Rect mark;

            (void)h;

            ForeColor(blackColor);
            BackColor(whiteColor);
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
            ForeColor(blackColor);
        } else {
            RGBForeColor(kPlayerColors[playerID & 3]);
            PaintRect(&dstRect);
            ForeColor(blackColor);
            FrameRect(&dstRect);
        }

        RestorePort();
    }
}

void Renderer_DrawBomb(short col, short row)
{
    Rect dstRect;
    short ts = gGame.tileSize;

    SetRect(&dstRect, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    if (!gGame.isMacSE && gPICTsLoaded && gCachedBombPM != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        CopyBits(
            gCachedBombPM,
            GetWorkBits(),
            &srcRect, &dstRect, transparent, NULL);
    } else {
        SavePort();
        SetPortWork();

        InsetRect(&dstRect, 4, 4);
        ForeColor(blackColor);
        PaintOval(&dstRect);

        RestorePort();
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

        CopyBits(
            gCachedExplosionPM,
            GetWorkBits(),
            &srcRect, &dstRect, transparent, NULL);
    } else {
        SavePort();
        SetPortWork();

        if (gGame.isMacSE) {
            InvertRect(&dstRect);
        } else {
            RGBForeColor(&kExplosionOrange);
            PaintRect(&dstRect);
        }

        RestorePort();
    }
}

void Renderer_DrawTile(short tileIndex, short col, short row)
{
    (void)tileIndex;
    (void)col;
    (void)row;
}

void Renderer_DrawText(const char *text, short x, short y)
{
    Str255 pstr;
    short len = 0;

    while (text[len] && len < 255) {
        pstr[len + 1] = text[len];
        len++;
    }
    pstr[0] = (unsigned char)len;

    SavePort();
    SetPortWork();
    LockWork();

    ForeColor(whiteColor);
    MoveTo(x, y);
    DrawString(pstr);

    UnlockWork();
    RestorePort();
}

void Renderer_ClearWork(void)
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
        short r, c;
        short ts = gGame.tileSize;
        short mapCols = TileMap_GetCols();
        short mapRows = TileMap_GetRows();

        for (r = 0; r < mapRows; r++) {
            for (c = 0; c < mapCols; c++) {
                if (gDirtyGrid[r][c]) {
                    Rect tileRect;
                    SetRect(&tileRect, c * ts, r * ts,
                            (c + 1) * ts, (r + 1) * ts);
                    AlignRect32(&tileRect);
                    CopyBits(GetWorkBits(), &window->portBits,
                             &tileRect, &tileRect, srcCopy, NULL);
                }
            }
        }
    }

    /* Clear dirty grid after blit */
    Renderer_ClearDirty();

    UnlockWork();

    SetPort(savePort);
}

/* ==== Screen Draw Helpers ==== */

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
