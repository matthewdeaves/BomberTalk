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
static const unsigned char kLoadTitle[] = {10, 'B','o','m','b','e','r','T','a','l','k'};
static const unsigned char kLoadSub[]   = {10, 'L','o','a','d','i','n','g','.','.','.'};

/* Cached widths */
static short gLoadTitleW = 0, gLoadSubW = 0;
static int gLoadWidthsCached = FALSE;

void Loading_Init(void)
{
    gLoadingStartTick = TickCount();
    CLOG_INFO("Loading screen started");
}

void Loading_Update(void)
{
    if (TickCount() - gLoadingStartTick >= 120) {
        Screens_TransitionTo(SCREEN_MENU);
    }
}

void Loading_Draw(WindowPtr window)
{
    short centerX, centerY;

    centerX = gGame.playWidth / 2;
    centerY = gGame.playHeight / 2;

    /* Draw to offscreen work buffer, then blit */
    Renderer_BeginScreenDraw();

    /* Cache widths on first draw (needs valid port) */
    if (!gLoadWidthsCached) {
        TextSize(24);
        gLoadTitleW = StringWidth((ConstStr255Param)kLoadTitle);
        TextSize(12);
        gLoadSubW = StringWidth((ConstStr255Param)kLoadSub);
        gLoadWidthsCached = TRUE;
    }

    TextSize(24);
    ForeColor(whiteColor);
    MoveTo(centerX - gLoadTitleW / 2, centerY - 20);
    DrawString((ConstStr255Param)kLoadTitle);

    TextSize(12);
    MoveTo(centerX - gLoadSubW / 2, centerY + 20);
    DrawString((ConstStr255Param)kLoadSub);

    Renderer_EndScreenDraw(window);
}
