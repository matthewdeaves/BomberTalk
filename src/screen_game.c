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

static short gLastSentCol = -1;
static short gLastSentRow = -1;
static short gLastSentFacing = -1;

void Game_Init(void)
{
    short i;

    /* Initialize tilemap from level data */
    TileMap_Init();
    Bomb_Init();

    /* Initialize players at map-derived spawn positions (T026) */
    for (i = 0; i < gGame.numPlayers; i++) {
        Player_Init(i, TileMap_GetSpawnCol(i), TileMap_GetSpawnRow(i));
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
    short oldCols[MAX_PLAYERS], oldRows[MAX_PLAYERS];

    if (!gGame.gameRunning) return;

    /* Record positions before update for dirty tracking (T019) */
    for (i = 0; i < MAX_PLAYERS; i++) {
        oldCols[i] = gGame.players[i].gridCol;
        oldRows[i] = gGame.players[i].gridRow;
    }

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
                             local->stats.bombRange, local->playerID)) {
                local->bombsAvailable--;
                Net_SendBombPlaced(local->gridCol, local->gridRow,
                                   local->stats.bombRange);
            }
        }
    }

    /* Mark dirty tiles for players that moved or are active (T019) */
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (!gGame.players[i].active) continue;
        if (gGame.players[i].gridCol != oldCols[i] ||
            gGame.players[i].gridRow != oldRows[i]) {
            Renderer_MarkDirty(oldCols[i], oldRows[i]);
            Renderer_MarkDirty(gGame.players[i].gridCol,
                               gGame.players[i].gridRow);
        }
        /* Always mark active player positions dirty (sprites drawn into work) */
        if (gGame.players[i].alive || gGame.players[i].deathTimer > 0) {
            Renderer_MarkDirty(gGame.players[i].gridCol,
                               gGame.players[i].gridRow);
        }
    }

    /* Mark active bombs and explosions dirty (sprites drawn into work) */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active) {
            Renderer_MarkDirty(gGame.bombs[i].gridCol,
                               gGame.bombs[i].gridRow);
        }
    }
    {
        short expCount;
        Explosion *exps = Bomb_GetExplosions(&expCount);
        for (i = 0; i < expCount; i++) {
            Renderer_MarkDirty(exps[i].col, exps[i].row);
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

        /* Handle pending remote game over: wait for death anims or timeout */
        if (gGame.pendingGameOver) {
            gGame.gameOverTimeout -= gGame.deltaTicks;
            if (!anyDying || gGame.gameOverTimeout <= 0) {
                if (gGame.gameOverTimeout <= 0) {
                    CLOG_WARN("Game over timeout, forcing transition");
                }
                CLOG_INFO("Game over (remote): winner=%d", gGame.pendingWinner);
                gGame.gameRunning = FALSE;
                gGame.pendingGameOver = FALSE;
                gGame.gameStartReceived = FALSE;
                Screens_TransitionTo(SCREEN_LOBBY);
            }
            return;
        }

        if (!anyDying && aliveCount <= 1 && gGame.numPlayers > 1) {
            unsigned char winner = (aliveCount == 1) ?
                                   (unsigned char)lastAlive : 0xFF;
            CLOG_INFO("Game over! Winner: %d (alive=%d dying=%d)",
                      winner, aliveCount, anyDying);
            for (i = 0; i < gGame.numPlayers; i++) {
                CLOG_INFO("  P%d: active=%d alive=%d death=%d",
                          i, gGame.players[i].active,
                          gGame.players[i].alive,
                          gGame.players[i].deathTimer);
            }
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
            /* Flash: visible every other DEATH_FLASH_RATE ticks */
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
