/*
 * screen_game.c -- Gameplay screen
 *
 * Tile map, player movement, bombs, multiplayer rendering.
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 2-6.
 */

#include "screens.h"
#include "tilemap.h"
#include "player.h"
#include "bomb.h"
#include "renderer.h"
#include "input.h"
#include "net.h"
#include <clog.h>

/* Spawn corners: player ID -> (col, row) */
static const short kSpawnCols[MAX_PLAYERS] = {1, 13, 1, 13};
static const short kSpawnRows[MAX_PLAYERS] = {1, 1, 11, 11};

static short gLastSentCol = -1;
static short gLastSentRow = -1;
static short gLastSentFacing = -1;

void Game_Init(void)
{
    short i;

    /* Initialize tilemap from level data */
    TileMap_Init();
    Bomb_Init();

    /* Initialize players at spawn positions */
    for (i = 0; i < gGame.numPlayers; i++) {
        Player_Init(i, kSpawnCols[i], kSpawnRows[i]);
    }

    /* Set local player */
    gGame.players[gGame.localPlayerID].active = TRUE;
    gGame.players[gGame.localPlayerID].alive = TRUE;

    gGame.gameRunning = TRUE;
    gGame.roundStartTick = TickCount();

    gLastSentCol = -1;
    gLastSentRow = -1;
    gLastSentFacing = -1;

    /* Build initial background tilemap rendering */
    Renderer_RebuildBackground();

    CLOG_INFO("Game started: %d players, local=%d",
              gGame.numPlayers, gGame.localPlayerID);
}

void Game_Update(void)
{
    short i;
    Player *local;

    if (!gGame.gameRunning) return;

    /* Update local player movement */
    Player_Update(gGame.localPlayerID);

    /* Check if local player moved and send position */
    local = Player_GetLocal();
    if (local && local->active && local->alive && local->deathTimer <= 0) {
        if (local->gridCol != gLastSentCol ||
            local->gridRow != gLastSentRow ||
            local->facing != gLastSentFacing) {
            Net_SendPosition(local->gridCol, local->gridRow, local->facing);
            gLastSentCol = local->gridCol;
            gLastSentRow = local->gridRow;
            gLastSentFacing = local->facing;
        }

        /* Bomb placement */
        if (Input_WasKeyPressed(KEY_SPACE) && local->bombsAvailable > 0) {
            if (Bomb_PlaceAt(local->gridCol, local->gridRow,
                             local->bombRange, local->playerID)) {
                local->bombsAvailable--;
                Net_SendBombPlaced(local->gridCol, local->gridRow,
                                   local->bombRange);
            }
        }
    }

    /* Update bombs (fuse countdown, explosions) */
    Bomb_Update();

    /* Tick death timers (tick-based, not frame-based) */
    for (i = 0; i < gGame.numPlayers; i++) {
        if (gGame.players[i].deathTimer > 0) {
            gGame.players[i].deathTimer -= gGame.deltaTicks;
            if (gGame.players[i].deathTimer <= 0) {
                gGame.players[i].alive = FALSE;
                CLOG_INFO("Player %d death animation complete", i);
            }
        }
    }

    /* Check for game over: last player standing (wait for death anims) */
    {
        short aliveCount = 0;
        short lastAlive = -1;
        int anyDying = FALSE;
        for (i = 0; i < gGame.numPlayers; i++) {
            if (gGame.players[i].active && gGame.players[i].alive) {
                if (gGame.players[i].deathTimer > 0) {
                    anyDying = TRUE;
                } else {
                    aliveCount++;
                    lastAlive = i;
                }
            }
        }

        if (!anyDying && aliveCount <= 1 && gGame.numPlayers > 1) {
            unsigned char winner = (aliveCount == 1) ?
                                   (unsigned char)lastAlive : 0xFF;
            CLOG_INFO("Game over! Winner: %d", winner);
            Net_SendGameOver(winner);
            gGame.gameRunning = FALSE;

            /* Transition back to lobby after a brief pause */
            gGame.gameStartReceived = FALSE;
            Screens_TransitionTo(SCREEN_LOBBY);
        }
    }
}

void Game_Draw(WindowPtr window)
{
    short i, explosionCount;
    Explosion *explosions;

    /* Copy background to work buffer */
    Renderer_BeginFrame();

    /* Draw bombs */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active) {
            Renderer_DrawBomb(gGame.bombs[i].gridCol, gGame.bombs[i].gridRow);
        }
    }

    /* Draw explosions */
    explosions = Bomb_GetExplosions(&explosionCount);
    for (i = 0; i < explosionCount; i++) {
        Renderer_DrawExplosion(explosions[i].col, explosions[i].row);
    }

    /* Draw players (flash dying players on/off) */
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (!gGame.players[i].active) continue;
        if (gGame.players[i].deathTimer > 0) {
            /* Flash: visible every other DEATH_FLASH_RATE frames */
            if ((gGame.players[i].deathTimer / DEATH_FLASH_RATE) & 1) {
                Renderer_DrawPlayer(i,
                    gGame.players[i].gridCol, gGame.players[i].gridRow,
                    gGame.players[i].facing);
            }
        } else if (gGame.players[i].alive) {
            Renderer_DrawPlayer(i,
                gGame.players[i].gridCol, gGame.players[i].gridRow,
                gGame.players[i].facing);
        }
    }

    /* Blit to window */
    Renderer_EndFrame(window);
}
