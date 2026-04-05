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
 * Source: Black Art (1996) texture loading, Tricks of the Gurus (1995),
 *         Sex Lies and Video Games (1996) buffered animation.
 */

#include "renderer.h"
#include "tilemap.h"
#include <clog.h>

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

/* ==== Offscreen buffer abstraction ==== */

/*
 * GetBgBits / GetWorkBits -- Return BitMap* for CopyBits calls.
 * On color Macs this dereferences the GWorld PixMap handle.
 * On Mac SE this returns the static BitMap.
 */
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

/*
 * LockBg / UnlockBg / LockWork / UnlockWork
 * GWorld pixels must be locked before access; BitMaps need no locking.
 */
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

/*
 * SetPortBg / SetPortWork -- Direct drawing to the offscreen buffer.
 * Caller must save/restore port.
 */
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

/*
 * SavePort / RestorePort -- Save and restore the current port.
 * Must handle both GWorld (CGrafPtr+GDHandle) and plain GrafPort.
 */
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

/*
 * AllocOffscreenBitMap -- Create an offscreen BitMap + GrafPort for Mac SE.
 *
 * Source: Macintosh Game Animation (1985), Black Art (1996) Ch. 5
 *         for pre-Color-QuickDraw offscreen drawing.
 *
 * rowBytes must be even (word-aligned) for QuickDraw.
 * Memory: 240x208 at 1-bit = 6240 bytes per buffer.
 */
static int AllocOffscreenBitMap(GrafPort *port, BitMap *bits, Ptr *storage,
                                 short width, short height)
{
    short rowBytes;
    long bufSize;

    rowBytes = ((width + 15) / 16) * 2; /* round up to word boundary */
    bufSize = (long)rowBytes * height;

    *storage = NewPtrClear(bufSize);
    if (*storage == NULL) return FALSE;

    bits->baseAddr = *storage;
    bits->rowBytes = rowBytes;
    SetRect(&bits->bounds, 0, 0, width, height);

    OpenPort(port);
    SetPortBits(bits);
    port->portRect = bits->bounds;

    /* Clip to bounds */
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
        /* Mac SE: offscreen BitMap + GrafPort (no Color QuickDraw) */
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
        /* Color Macs: GWorld offscreen buffers */
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

/*
 * DrawTileRect -- Fallback: draw a colored/patterned rectangle for a tile.
 * Mac SE: 1-bit patterns (black, white, gray).
 * Color Macs: RGBForeColor rectangles.
 */
static void DrawTileRect(short tileType, short col, short row)
{
    Rect r;
    short ts = gGame.tileSize;

    SetRect(&r, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    SavePort();
    SetPortBg();
    LockBg();

    if (gGame.isMacSE) {
        /* 1-bit: use black/white/gray patterns.
         * Must reset fore/back color before FillRect -- pattern
         * uses foreColor for black bits, backColor for white bits. */
        ForeColor(blackColor);
        BackColor(whiteColor);
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
        /* Color: RGB rectangles */
        RGBColor green, gray, darkGray, brown, darkBrown;

        green.red = 0x5500; green.green = 0xAA00; green.blue = 0x5500;
        gray.red = 0x7700; gray.green = 0x7700; gray.blue = 0x7700;
        darkGray.red = 0x5500; darkGray.green = 0x5500; darkGray.blue = 0x5500;
        brown.red = 0x9900; brown.green = 0x6600; brown.blue = 0x3300;
        darkBrown.red = 0x7700; darkBrown.green = 0x4400; darkBrown.blue = 0x2200;

        switch (tileType) {
        case TILE_FLOOR:
        case TILE_SPAWN:
            RGBForeColor(&green);
            PaintRect(&r);
            break;
        case TILE_WALL:
            RGBForeColor(&gray);
            PaintRect(&r);
            RGBForeColor(&darkGray);
            FrameRect(&r);
            break;
        case TILE_BLOCK:
            RGBForeColor(&brown);
            PaintRect(&r);
            RGBForeColor(&darkBrown);
            FrameRect(&r);
            break;
        default:
            ForeColor(whiteColor);
            PaintRect(&r);
            break;
        }
    }

    UnlockBg();
    RestorePort();
}

static void DrawTileFromSheet(short tileIndex, short col, short row)
{
    Rect srcRect, dstRect;
    short ts = gGame.tileSize;

    SetRect(&srcRect, tileIndex * ts, 0, (tileIndex + 1) * ts, ts);
    SetRect(&dstRect, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    LockPixels(GetGWorldPixMap(gTileSheet));
    LockBg();

    CopyBits(
        (BitMap *)*GetGWorldPixMap(gTileSheet),
        GetBgBits(),
        &srcRect, &dstRect, srcCopy, NULL);

    UnlockBg();
    UnlockPixels(GetGWorldPixMap(gTileSheet));
}

void Renderer_RebuildBackground(void)
{
    TileMap *map;
    short r, c;
    unsigned char tile;

    map = TileMap_Get();

    for (r = 0; r < GRID_ROWS; r++) {
        for (c = 0; c < GRID_COLS; c++) {
            tile = map->tiles[r][c];
            if (gPICTsLoaded && !gGame.isMacSE) {
                short idx = (tile == TILE_SPAWN) ? 0 : tile;
                if (idx > 3) idx = 0;
                DrawTileFromSheet(idx, c, r);
            } else {
                DrawTileRect(tile, c, r);
            }
        }
    }
}

/* ==== Per-Frame Rendering ==== */

void Renderer_BeginFrame(void)
{
    Rect bounds;
    SetRect(&bounds, 0, 0, gGame.playWidth, gGame.playHeight);

    LockBg();
    LockWork();

    /* Ensure clean port state before CopyBits (Mac SE fix).
     * CopyBits on classic QuickDraw can be affected by the
     * destination port's fore/back color for pattern transfers.
     * Source: Tricks of the Mac Game Programming Gurus (1995). */
    if (gGame.isMacSE) {
        SavePort();
        SetPortWork();
        ForeColor(blackColor);
        BackColor(whiteColor);
        RestorePort();
    }

    CopyBits(GetBgBits(), GetWorkBits(),
             &bounds, &bounds, srcCopy, NULL);

    UnlockBg();
    /* Keep work buffer locked for sprite drawing */
}

void Renderer_DrawPlayer(short playerID, short col, short row, short facing)
{
    Rect dstRect;
    short ts = gGame.tileSize;

    (void)facing;

    SetRect(&dstRect, col * ts, row * ts, (col + 1) * ts, (row + 1) * ts);

    if (!gGame.isMacSE && gPICTsLoaded && gPlayerSprites[playerID] != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        LockPixels(GetGWorldPixMap(gPlayerSprites[playerID]));
        CopyBits(
            (BitMap *)*GetGWorldPixMap(gPlayerSprites[playerID]),
            GetWorkBits(),
            &srcRect, &dstRect, transparent, NULL);
        UnlockPixels(GetGWorldPixMap(gPlayerSprites[playerID]));
    } else {
        /* Fallback: colored/patterned rectangle per player */
        SavePort();
        SetPortWork();

        InsetRect(&dstRect, 2, 2);

        if (gGame.isMacSE) {
            /* 1-bit: black filled square with white marker inside.
             * Local player: white cross. Remote: white center dot.
             * dstRect is already inset by 2, so 12x12 on 16px tiles. */
            short w = dstRect.right - dstRect.left;
            short h = dstRect.bottom - dstRect.top;
            short cx = dstRect.left + w / 2;
            short cy = dstRect.top + h / 2;
            Rect mark;

            ForeColor(blackColor);
            BackColor(whiteColor);
            PaintRect(&dstRect);

            ForeColor(whiteColor);
            if (playerID == gGame.localPlayerID) {
                /* Horizontal bar of cross */
                SetRect(&mark, dstRect.left + 1, cy - 1,
                        dstRect.right - 1, cy + 1);
                PaintRect(&mark);
                /* Vertical bar of cross */
                SetRect(&mark, cx - 1, dstRect.top + 1,
                        cx + 1, dstRect.bottom - 1);
                PaintRect(&mark);
            } else {
                /* Center dot */
                SetRect(&mark, cx - 2, cy - 2, cx + 2, cy + 2);
                PaintRect(&mark);
            }
            ForeColor(blackColor);
        } else {
            RGBColor colors[4];
            colors[0].red = 0xFFFF; colors[0].green = 0xFFFF; colors[0].blue = 0xFFFF;
            colors[1].red = 0xFFFF; colors[1].green = 0x0000; colors[1].blue = 0x0000;
            colors[2].red = 0x0000; colors[2].green = 0x0000; colors[2].blue = 0xFFFF;
            colors[3].red = 0xFFFF; colors[3].green = 0xFFFF; colors[3].blue = 0x0000;

            RGBForeColor(&colors[playerID & 3]);
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

    if (!gGame.isMacSE && gPICTsLoaded && gBombSprite != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        LockPixels(GetGWorldPixMap(gBombSprite));
        CopyBits(
            (BitMap *)*GetGWorldPixMap(gBombSprite),
            GetWorkBits(),
            &srcRect, &dstRect, transparent, NULL);
        UnlockPixels(GetGWorldPixMap(gBombSprite));
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

    if (!gGame.isMacSE && gPICTsLoaded && gExplosionSprite != NULL) {
        Rect srcRect;
        SetRect(&srcRect, 0, 0, ts, ts);

        LockPixels(GetGWorldPixMap(gExplosionSprite));
        CopyBits(
            (BitMap *)*GetGWorldPixMap(gExplosionSprite),
            GetWorkBits(),
            &srcRect, &dstRect, transparent, NULL);
        UnlockPixels(GetGWorldPixMap(gExplosionSprite));
    } else {
        SavePort();
        SetPortWork();

        if (gGame.isMacSE) {
            /* 1-bit: invert the rect for explosion effect */
            InvertRect(&dstRect);
        } else {
            RGBColor orange;
            orange.red = 0xFFFF;
            orange.green = 0x6600;
            orange.blue = 0x0000;
            RGBForeColor(&orange);
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

    /* Convert C string to Pascal string */
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
    UnlockWork();
    Renderer_BlitToWindow(window);
}

void Renderer_BlitToWindow(WindowPtr window)
{
    Rect bounds;
    GrafPtr savePort;

    if (window == NULL) return;
    if (!gGame.isMacSE && gWorkBuffer == NULL) return;
    if (gGame.isMacSE && gWorkStorageSE == NULL) return;

    SetRect(&bounds, 0, 0, gGame.playWidth, gGame.playHeight);

    GetPort(&savePort);
    SetPort(window);

    LockWork();

    CopyBits(GetWorkBits(), &window->portBits,
             &bounds, &bounds, srcCopy, NULL);

    UnlockWork();

    SetPort(savePort);
}

/* ==== Screen Draw Helpers ==== */

void Renderer_BeginScreenDraw(void)
{
    /* Clear work buffer to black */
    Renderer_ClearWork();

    /* Save current port and switch to work buffer */
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
    /* Restore port */
    if (gGame.isMacSE) {
        SetPort((GrafPtr)gSavedScreenCPort);
    } else {
        UnlockPixels(GetGWorldPixMap(gWorkBuffer));
        SetGWorld(gSavedScreenCPort, gSavedScreenDevice);
    }
    gSavedScreenCPort = NULL;
    gSavedScreenDevice = NULL;

    /* Blit to window */
    Renderer_BlitToWindow(window);
}

/*
 * Renderer_DrawFPS -- Draw FPS counter in bottom-right corner of window.
 *
 * Draws directly to the window port (overlay, not buffered).
 * Source: Black Art (1996) Ch. 8 frame rate monitoring.
 */
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

    /* Build "XX fps" Pascal string */
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

    /* Background rectangle for readability */
    SetRect(&bgRect, x - 2, y - 10, gGame.playWidth, gGame.playHeight);
    ForeColor(blackColor);
    PaintRect(&bgRect);

    ForeColor(whiteColor);
    MoveTo(x, y);
    DrawString(fpsStr);

    /* Restore port state */
    ForeColor(blackColor);
    BackColor(whiteColor);
    SetPort(savePort);
}
