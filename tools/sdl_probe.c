/*
 * sdl_probe.c -- Headless visual smoke test for the SDL2 gameplay renderer.
 *
 * Networking makes the in-game screen hard to reach headlessly, so this probe
 * drives the gameplay draw calls (RebuildBackground tiles + player/bomb/
 * explosion sprites) directly and dumps one composited frame to a BMP. It is a
 * dev tool, not part of the shipped build. Build/run via tools/build-sdl.sh's
 * probe target or standalone (see the bottom of build-sdl.sh).
 *
 *   ./sdl_probe out.bmp
 */
#include "game.h"
#include "renderer.h"
#include "tilemap.h"
#include "sdl_backend.h"
#include <SDL2/SDL.h>
#include <stdio.h>

GameState gGame;
void Game_RequestQuit(void) {}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "usage: %s out.bmp\n", argv[0]); return 2; }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    gGame.isMacSE = FALSE;
    gGame.tileSize = TILE_SIZE_LARGE;
    TileMap_Init();
    gGame.playWidth = TileMap_GetCols() * gGame.tileSize;
    gGame.playHeight = TileMap_GetRows() * gGame.tileSize;
    gGame.localPlayerID = 0;

    Renderer_Init(NULL);

    Renderer_BeginFrame();
    Renderer_BeginSpriteDraw();
    Renderer_DrawPlayer(0, 1 * 32, 1 * 32, 0);     /* top-left spawn */
    Renderer_DrawPlayer(1, 13 * 32, 1 * 32, 0);    /* top-right spawn */
    Renderer_DrawPlayer(2, 1 * 32, 11 * 32, 0);    /* bottom-left */
    Renderer_DrawBomb(3, 3, 1);                    /* a ticking bomb */
    Renderer_DrawExplosion(5, 5);                  /* explosion cross */
    Renderer_DrawExplosion(6, 5);
    Renderer_DrawExplosion(4, 5);
    Renderer_DrawExplosion(5, 4);
    Renderer_DrawExplosion(5, 6);
    Renderer_EndSpriteDraw();
    Renderer_EndFrame(NULL);

    if (Renderer_SaveScreenshot(argv[1]) != 0) {
        fprintf(stderr, "save failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("wrote %s\n", argv[1]);

    Renderer_Shutdown();
    SDL_Quit();
    return 0;
}
