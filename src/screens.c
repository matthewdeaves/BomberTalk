/*
 * screens.c -- Screen state machine dispatcher
 *
 * Routes Init/Update/Draw to the current screen.
 * Source: plan.md key design decisions.
 */

#include "screens.h"
#include <clog.h>

void Screens_Init(void)
{
    gGame.currentScreen = SCREEN_LOADING;
    Loading_Init();
}

void Screens_TransitionTo(ScreenState newScreen)
{
    CLOG_INFO("Screen transition: %d -> %d", gGame.currentScreen, newScreen);
    gGame.currentScreen = newScreen;

    switch (newScreen) {
    case SCREEN_LOADING:
        Loading_Init();
        break;
    case SCREEN_MENU:
        Menu_Init();
        break;
    case SCREEN_LOBBY:
        Lobby_Init();
        break;
    case SCREEN_GAME:
        Game_Init();
        break;
    }
}

void Screens_Update(void)
{
    switch (gGame.currentScreen) {
    case SCREEN_LOADING:
        Loading_Update();
        break;
    case SCREEN_MENU:
        Menu_Update();
        break;
    case SCREEN_LOBBY:
        Lobby_Update();
        break;
    case SCREEN_GAME:
        Game_Update();
        break;
    }
}

void Screens_Draw(WindowPtr window)
{
    switch (gGame.currentScreen) {
    case SCREEN_LOADING:
        Loading_Draw(window);
        break;
    case SCREEN_MENU:
        Menu_Draw(window);
        break;
    case SCREEN_LOBBY:
        Lobby_Draw(window);
        break;
    case SCREEN_GAME:
        Game_Draw(window);
        break;
    }
}

ScreenState Screens_GetCurrent(void)
{
    return gGame.currentScreen;
}
