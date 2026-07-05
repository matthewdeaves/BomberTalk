/*
 * main_posix.c -- BomberTalk entry point for the SDL2 / POSIX build.
 *
 * The classic/Carbon entry point (main.c) drives a WaitNextEvent loop over the
 * Mac Toolbox. This is its SDL2 counterpart (011-macosx-sdl2): SDL owns the
 * window and event queue, the renderer/input/time backends are the SDL ones,
 * and networking is PeerTalk's POSIX/BSD backend (net.c unchanged). The loop
 * keeps the same shape the constitution requires -- poll everything every
 * iteration, gate the simulation on a fixed tick (Principle VII) -- so gameplay
 * timing is identical to the classic builds.
 */
#include "game.h"
#include "screens.h"
#include "renderer.h"
#include "tilemap.h"
#include "input.h"
#include "net.h"
#include "sdl_backend.h"
#include <clog.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

/* The single global game state (defined here for the SDL build; main.c defines
 * it for the classic/Carbon builds). */
GameState gGame;

static int   gQuitting = FALSE;
static long  gLastFrameTick = 0;
static short gFPSFrameCount = 0;
static long  gFPSLastTick = 0;

void Game_RequestQuit(void)
{
    gQuitting = TRUE;
}

/*
 * InitGameState -- Zero out the global game state (mirrors main.c).
 */
static void InitGameState(void)
{
    short i;

    gGame.currentScreen = SCREEN_LOADING;
    gGame.numPlayers = 0;
    gGame.localPlayerID = -1;
    gGame.numActiveBombs = 0;
    gGame.gameRunning = FALSE;
    gGame.roundStartTick = 0;
    gGame.gameStartReceived = FALSE;
    gGame.deltaTicks = FRAME_TICKS;
    gGame.showFPS = FALSE;
    gGame.fpsValue = 0;
    gGame.pendingGameOver = FALSE;
    gGame.pendingWinner = 0xFF;
    gGame.gameOverTimeoutStart = 0;
    gGame.disconnectGraceStart = 0;
    gGame.meshStaggerStart = 0;
    gGame.gameOverAuthority = FALSE;
    gGame.localGameOverDetected = FALSE;
    gGame.gameOverFailsafeStart = 0;
    gGame.heapCheckTimer = 0;
    gGame.window = NULL; /* SDL renderer owns the real window */

    for (i = 0; i < MAX_PLAYERS; i++) {
        gGame.players[i].active = FALSE;
        gGame.players[i].alive = FALSE;
        gGame.players[i].playerID = (unsigned char)i;
        gGame.players[i].bombsAvailable = 1;
        gGame.players[i].stats.bombsMax = 1;
        gGame.players[i].stats.bombRange = 1;
        gGame.players[i].stats.speedTicks = 12;
        gGame.players[i].peer = NULL;
    }

    for (i = 0; i < MAX_BOMBS; i++) {
        gGame.bombs[i].active = FALSE;
    }
}

/*
 * PumpEvents -- Drain the SDL event queue; window close / Cmd(Ctrl)-Q quit.
 * Movement/menu keys are read via SDL_GetKeyboardState in input_sdl.c.
 */
static void PumpEvents(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            gQuitting = TRUE;
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_q &&
                (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))) {
                gQuitting = TRUE;
            }
            break;
        default:
            break;
        }
    }
}

static void MainLoop(void)
{
    long currentTick, elapsed;
    /* Headless validation: BT_SCREENSHOT=<path> saves the first fully-drawn
     * MENU frame to a BMP and quits. No effect in normal play. */
    const char *shotPath = getenv("BT_SCREENSHOT");
    short menuFrames = 0;

    gLastFrameTick = TickCount();

    while (!gQuitting) {
        PumpEvents();
        Net_Poll();
        Input_Poll();

        currentTick = TickCount();
        if (currentTick - gLastFrameTick >= FRAME_TICKS) {
            elapsed = currentTick - gLastFrameTick;
            if (elapsed > 10) elapsed = 10;
            gGame.deltaTicks = (short)elapsed;
            gLastFrameTick = currentTick;

            if (Input_WasKeyPressed(KEY_F)) {
                gGame.showFPS = !gGame.showFPS;
            }

            Screens_Update();
            Screens_Draw(gGame.window);

            if (shotPath != NULL && gGame.currentScreen == SCREEN_MENU) {
                if (++menuFrames == 3) {
                    Renderer_SaveScreenshot(shotPath);
                    CLOG_INFO("Saved screenshot to %s", shotPath);
                    gQuitting = TRUE;
                }
            }

            gFPSFrameCount++;
            if (currentTick - gFPSLastTick >= 60) {
                gGame.fpsValue = gFPSFrameCount;
                gFPSFrameCount = 0;
                gFPSLastTick = currentTick;
            }

            if (gGame.showFPS) {
                Renderer_DrawFPS(gGame.fpsValue);
            }

            Input_ConsumeFrame();
        }

        /* Yield a slice so the loop doesn't pin a core (Principle VII allows
         * yielding here: modern preemptive kernel, not an 8 MHz 68000). */
        SDL_Delay(1);
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

#ifndef CLOG_STRIP
    clog_set_file("BomberTalk Log");
    clog_set_flush(CLOG_FLUSH_ERRORS);
    clog_init("BomberTalk", CLOG_LVL_DBG);
#endif

    CLOG_INFO("BomberTalk starting (SDL2)");

    /* SDL is always a colour target at the large tile size. */
    gGame.isMacSE = FALSE;
    gGame.tileSize = TILE_SIZE_LARGE;

    TileMap_Init();
    gGame.playWidth = TileMap_GetCols() * gGame.tileSize;
    gGame.playHeight = TileMap_GetRows() * gGame.tileSize;

    InitGameState();

    Input_Init();
    Renderer_Init(gGame.window);
    Net_Init("BomberTalk");
    Screens_Init();

    CLOG_INFO("Entering main loop");
    MainLoop();

    CLOG_INFO("Shutting down");
    Net_Shutdown();
    Renderer_Shutdown();

#ifndef CLOG_STRIP
    clog_shutdown();
#endif

    SDL_Quit();
    return 0;
}
