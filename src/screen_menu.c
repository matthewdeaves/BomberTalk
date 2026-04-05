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
            ExitToShell();
        }
    }
}

void Menu_Draw(WindowPtr window)
{
    short centerX, centerY;
    Str255 titleStr, playStr, quitStr, arrowStr;
    short titleW, playW, quitW, arrowW;

    titleStr[0] = 10;
    titleStr[1] = 'B'; titleStr[2] = 'o'; titleStr[3] = 'm'; titleStr[4] = 'b';
    titleStr[5] = 'e'; titleStr[6] = 'r'; titleStr[7] = 'T'; titleStr[8] = 'a';
    titleStr[9] = 'l'; titleStr[10] = 'k';

    playStr[0] = 4;
    playStr[1] = 'P'; playStr[2] = 'l'; playStr[3] = 'a'; playStr[4] = 'y';

    quitStr[0] = 4;
    quitStr[1] = 'Q'; quitStr[2] = 'u'; quitStr[3] = 'i'; quitStr[4] = 't';

    arrowStr[0] = 2;
    arrowStr[1] = '>'; arrowStr[2] = ' ';

    centerX = gGame.playWidth / 2;
    centerY = gGame.playHeight / 2;

    /* Draw to offscreen work buffer, then blit */
    Renderer_BeginScreenDraw();

    /* Title */
    TextSize(24);
    ForeColor(whiteColor);
    titleW = StringWidth(titleStr);
    MoveTo(centerX - titleW / 2, centerY - 60);
    DrawString(titleStr);

    /* Menu items */
    TextSize(18);

    /* Play */
    playW = StringWidth(playStr);
    if (gMenuSelection == 0) {
        arrowW = StringWidth(arrowStr);
        MoveTo(centerX - playW / 2 - arrowW, centerY);
        DrawString(arrowStr);
    }
    MoveTo(centerX - playW / 2, centerY);
    DrawString(playStr);

    /* Quit */
    quitW = StringWidth(quitStr);
    if (gMenuSelection == 1) {
        arrowW = StringWidth(arrowStr);
        MoveTo(centerX - quitW / 2 - arrowW, centerY + 30);
        DrawString(arrowStr);
    }
    MoveTo(centerX - quitW / 2, centerY + 30);
    DrawString(quitStr);

    Renderer_EndScreenDraw(window);
}
