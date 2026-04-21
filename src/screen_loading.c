/*
 * screen_loading.c -- Loading screen
 *
 * Shows "BomberTalk" title centered, "Loading..." text,
 * auto-transitions to menu after 120 ticks (2 seconds).
 *
 * Draws to offscreen work buffer via Renderer helpers to avoid flicker.
 */

#include "screens.h"
#include "renderer.h"
#include <clog.h>

static long gLoadingStartTick;

/* Pre-built Pascal strings */
static const unsigned char kLoadSub[] = {10, 'L','o','a','d','i','n','g','.','.','.'};

/* Cached widths */
static short gLoadSubW = 0;
static int gLoadWidthsCached = FALSE;

void Loading_Init(void)
{
    gLoadingStartTick = TickCount();
    CLOG_INFO("Loading screen started");
}

void Loading_Update(void)
{
    if (TickCount() - gLoadingStartTick >= 120) {
        Renderer_ReleaseSplash();
        Screens_TransitionTo(SCREEN_MENU);
    }
}

void Loading_Draw(WindowPtr window)
{
    short centerX, baseY;
    Rect pill;
    short halfW;

    centerX = gGame.playWidth / 2;
    /* "Loading..." sits near the bottom — splash already has the title
     * artwork baked in, so an overlay title would just fight the image. */
    baseY = gGame.playHeight - 24;

    Renderer_BeginScreenDraw();

    /* Splash background PICT fills the whole window; no-op if the PICT
     * is missing from the resource fork, in which case the pill below
     * still provides a readable "Loading..." indicator on black. */
    Renderer_DrawSplashBackground();

    /* Cache width on first draw (needs valid port). */
    TextSize(12);
    if (!gLoadWidthsCached) {
        gLoadSubW = StringWidth((ConstStr255Param)kLoadSub);
        gLoadWidthsCached = TRUE;
    }

    /* Black pill behind the text for readability against a colourful
     * splash. 6px horizontal padding, 4px vertical. */
    halfW = gLoadSubW / 2 + 6;
    SetRect(&pill, centerX - halfW, baseY - 12, centerX + halfW, baseY + 4);
    ForeColor(blackColor);
    PaintRect(&pill);

    ForeColor(whiteColor);
    MoveTo(centerX - gLoadSubW / 2, baseY);
    DrawString((ConstStr255Param)kLoadSub);

    Renderer_EndScreenDraw(window);
}
