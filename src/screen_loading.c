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

static long gLoadingStartTick;

void Loading_Init(void)
{
    gLoadingStartTick = TickCount();
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
    Str255 title;
    Str255 subtitle;
    short titleWidth, subtitleWidth;

    centerX = gGame.playWidth / 2;
    centerY = gGame.playHeight / 2;

    /* Pascal strings */
    title[0] = 10;
    title[1] = 'B'; title[2] = 'o'; title[3] = 'm'; title[4] = 'b';
    title[5] = 'e'; title[6] = 'r'; title[7] = 'T'; title[8] = 'a';
    title[9] = 'l'; title[10] = 'k';

    subtitle[0] = 10;
    subtitle[1] = 'L'; subtitle[2] = 'o'; subtitle[3] = 'a';
    subtitle[4] = 'd'; subtitle[5] = 'i'; subtitle[6] = 'n';
    subtitle[7] = 'g'; subtitle[8] = '.'; subtitle[9] = '.';
    subtitle[10] = '.';

    /* Draw to offscreen work buffer, then blit */
    Renderer_BeginScreenDraw();

    TextSize(24);
    ForeColor(whiteColor);
    titleWidth = StringWidth(title);
    MoveTo(centerX - titleWidth / 2, centerY - 20);
    DrawString(title);

    TextSize(12);
    subtitleWidth = StringWidth(subtitle);
    MoveTo(centerX - subtitleWidth / 2, centerY + 20);
    DrawString(subtitle);

    Renderer_EndScreenDraw(window);
}
