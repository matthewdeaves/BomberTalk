/*
 * screen_menu.c -- Main menu screen
 *
 * Two options: "Play" and "Quit".
 * Keyboard navigation with up/down arrows, Enter/Return to select.
 *
 * Draws to offscreen work buffer via Renderer helpers to avoid flicker.
 */

#include "screens.h"
#include "input.h"
#include "renderer.h"
#include <clog.h>

static short gMenuSelection = 0;  /* 0=Play, 1=Quit */

/* Pre-built Pascal strings (avoid per-frame construction) */
static const unsigned char kMenuTitle[] = {10, 'B','o','m','b','e','r','T','a','l','k'};
static const unsigned char kMenuPlay[]  = {4, 'P','l','a','y'};
static const unsigned char kMenuQuit[]  = {4, 'Q','u','i','t'};
static const unsigned char kMenuArrow[] = {2, '>',' '};

/* Cached StringWidth values (computed once) */
static short gTitleW = 0, gPlayW = 0, gQuitW = 0, gArrowW = 0;
static int gWidthsCached = FALSE;

void Menu_Init(void)
{
    gMenuSelection = 0;
}

void Menu_Update(void)
{
    static long sLastLogTick = 0;
    long now = TickCount();

    /* Log raw key state every 2 seconds for diagnostics */
    if (now - sLastLogTick >= 120) {
        sLastLogTick = now;
        if (Input_IsKeyDown(KEY_UP_ARROW) || Input_IsKeyDown(KEY_DOWN_ARROW) ||
            Input_IsKeyDown(KEY_RETURN) || Input_IsKeyDown(KEY_SPACE)) {
            CLOG_INFO("Menu: key held: up=%d dn=%d ret=%d spc=%d",
                      Input_IsKeyDown(KEY_UP_ARROW),
                      Input_IsKeyDown(KEY_DOWN_ARROW),
                      Input_IsKeyDown(KEY_RETURN),
                      Input_IsKeyDown(KEY_SPACE));
        }
    }

    if (Input_WasKeyPressed(KEY_UP_ARROW)) {
        CLOG_INFO("Menu: UP pressed, sel=%d", gMenuSelection);
        gMenuSelection--;
        if (gMenuSelection < 0) gMenuSelection = 1;
    }
    if (Input_WasKeyPressed(KEY_DOWN_ARROW)) {
        CLOG_INFO("Menu: DOWN pressed, sel=%d", gMenuSelection);
        gMenuSelection++;
        if (gMenuSelection > 1) gMenuSelection = 0;
    }

    if (Input_WasKeyPressed(KEY_RETURN) || Input_WasKeyPressed(KEY_SPACE)) {
        CLOG_INFO("Menu: RETURN/SPACE pressed, sel=%d", gMenuSelection);
        if (gMenuSelection == 0) {
            Screens_TransitionTo(SCREEN_LOBBY);
        } else {
            Game_RequestQuit();
        }
    }
}

void Menu_Draw(WindowPtr window)
{
    short centerX, centerY;

    centerX = gGame.playWidth / 2;
    centerY = gGame.playHeight / 2;

    /* Draw to offscreen work buffer, then blit */
    Renderer_BeginScreenDraw();

    /* Cache StringWidth on first draw (needs valid port) */
    if (!gWidthsCached) {
        TextSize(24);
        gTitleW = StringWidth((ConstStr255Param)kMenuTitle);
        TextSize(18);
        gPlayW = StringWidth((ConstStr255Param)kMenuPlay);
        gQuitW = StringWidth((ConstStr255Param)kMenuQuit);
        gArrowW = StringWidth((ConstStr255Param)kMenuArrow);
        gWidthsCached = TRUE;
    }

    /* Title */
    TextSize(24);
    ForeColor(whiteColor);
    MoveTo(centerX - gTitleW / 2, centerY - 60);
    DrawString((ConstStr255Param)kMenuTitle);

    /* Menu items */
    TextSize(18);

    /* Play */
    if (gMenuSelection == 0) {
        MoveTo(centerX - gPlayW / 2 - gArrowW, centerY);
        DrawString((ConstStr255Param)kMenuArrow);
    }
    MoveTo(centerX - gPlayW / 2, centerY);
    DrawString((ConstStr255Param)kMenuPlay);

    /* Quit */
    if (gMenuSelection == 1) {
        MoveTo(centerX - gQuitW / 2 - gArrowW, centerY + 30);
        DrawString((ConstStr255Param)kMenuArrow);
    }
    MoveTo(centerX - gQuitW / 2, centerY + 30);
    DrawString((ConstStr255Param)kMenuQuit);

    Renderer_EndScreenDraw(window);
}
