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

static short gLastSentPX = -1;
static short gLastSentPY = -1;
static short gLastSentFacing = -1;
static long  gHeartbeatTimer = 0;
static long  gPosSendTimer = 0;

#define POS_SEND_INTERVAL   4  /* ticks between position sends (~15/sec max) */

void Game_Init(void)
{
    short i;

    /* Restore tilemap from cached initial state (no Resource Manager calls) */
    TileMap_Reset();
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

    gLastSentPX = -1;
    gLastSentPY = -1;
    gLastSentFacing = -1;
    gHeartbeatTimer = 0;
    gPosSendTimer = 0;

    /* Build initial background tilemap rendering */
    Renderer_RebuildBackground();

    CLOG_INFO("Game started: %d players, local=%d",
              gGame.numPlayers, gGame.localPlayerID);
}

void Game_Update(void)
{
    short i;
    Player *local;
    short oldPX[MAX_PLAYERS], oldPY[MAX_PLAYERS];
    if (!gGame.gameRunning) return;

    /* Save positions before update for moved-player dirty optimization */
    for (i = 0; i < MAX_PLAYERS; i++) {
        oldPX[i] = gGame.players[i].pixelX;
        oldPY[i] = gGame.players[i].pixelY;
    }

    /* Reactivate remote players that received position data while inactive.
     * Handles transient disconnect/reconnect: net layer delivers coords to
     * inactive player (setting targetPixelX/Y), detected here by target
     * diverging from current position. */
    for (i = 0; i < gGame.numPlayers; i++) {
        if (i == gGame.localPlayerID) continue;
        if (!gGame.players[i].active &&
            (gGame.players[i].targetPixelX != gGame.players[i].pixelX ||
             gGame.players[i].targetPixelY != gGame.players[i].pixelY)) {
            gGame.players[i].active = TRUE;
            gGame.players[i].alive = TRUE;
            gGame.players[i].deathTimer = 0;
            CLOG_INFO("P%d reactivated via network data", i);
        }
    }

    /* Update all players (local moves, remotes interpolate) */
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (gGame.players[i].active) {
            Player_Update(i);
        }
    }

    /* Check if local player moved and send position */
    local = Player_GetLocal();
    if (local && local->active && local->alive && local->deathTimer <= 0) {
        gHeartbeatTimer += gGame.deltaTicks;
        gPosSendTimer += gGame.deltaTicks;
        if ((local->pixelX != gLastSentPX ||
             local->pixelY != gLastSentPY ||
             local->facing != gLastSentFacing) &&
            gPosSendTimer >= POS_SEND_INTERVAL) {
            Net_SendPosition(local->pixelX, local->pixelY, local->facing);
            gLastSentPX = local->pixelX;
            gLastSentPY = local->pixelY;
            gLastSentFacing = local->facing;
            gHeartbeatTimer = 0;
            gPosSendTimer = 0;
        } else if (gHeartbeatTimer >= HEARTBEAT_TICKS) {
            Net_SendPosition(local->pixelX, local->pixelY, local->facing);
            gLastSentPX = local->pixelX;
            gLastSentPY = local->pixelY;
            gLastSentFacing = local->facing;
            gHeartbeatTimer = 0;
            gPosSendTimer = 0;
        }

        /* Bomb placement (T012: use center-derived gridCol/gridRow) */
        if (Input_WasKeyPressed(KEY_SPACE) && local->bombsAvailable > 0) {
            if (Bomb_PlaceAt(local->gridCol, local->gridRow,
                             local->stats.bombRange, local->playerID)) {
                /* Set pass-through for the just-placed bomb (T016) */
                {
                    short bi;
                    for (bi = 0; bi < MAX_BOMBS; bi++) {
                        if (gGame.bombs[bi].active &&
                            gGame.bombs[bi].gridCol == local->gridCol &&
                            gGame.bombs[bi].gridRow == local->gridRow) {
                            local->passThroughBombIdx = bi;
                            break;
                        }
                    }
                }
                local->bombsAvailable--;
                Net_SendBombPlaced(local->gridCol, local->gridRow,
                                   local->stats.bombRange);
            }
        }
    }

    /* Mark dirty only for players that moved or are dying (flash) */
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (!gGame.players[i].active) continue;
        if (gGame.players[i].deathTimer > 0) {
            Player_MarkDirtyTiles(i);
            continue;
        }
        if (!gGame.players[i].alive) continue;
        if (gGame.players[i].pixelX == oldPX[i] &&
            gGame.players[i].pixelY == oldPY[i]) continue;
        /* Player moved: mark both old and new positions dirty */
        {
            short newPX = gGame.players[i].pixelX;
            short newPY = gGame.players[i].pixelY;
            gGame.players[i].pixelX = oldPX[i];
            gGame.players[i].pixelY = oldPY[i];
            Player_MarkDirtyTiles(i);
            gGame.players[i].pixelX = newPX;
            gGame.players[i].pixelY = newPY;
            Player_MarkDirtyTiles(i);
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
                CLOG_INFO("P%d death animation complete", i);
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
                /* Cleanly disconnect all TCP peers before lobby re-entry.
                 * Stale TCP connections cause OT freeze / MacTCP crash
                 * when lobby calls PT_StartDiscovery. */
                Net_StopDiscovery();
                Net_DisconnectAllPeers();
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
            /* Cleanly disconnect all TCP peers before lobby re-entry.
             * Stale TCP connections cause OT freeze / MacTCP crash
             * when lobby calls PT_StartDiscovery. */
            Net_StopDiscovery();
            Net_DisconnectAllPeers();
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
                    gGame.players[i].pixelX, gGame.players[i].pixelY,
                    gGame.players[i].facing);
            }
        } else if (gGame.players[i].alive) {
            Renderer_DrawPlayer(i,
                gGame.players[i].pixelX, gGame.players[i].pixelY,
                gGame.players[i].facing);
        }
    }

    /* Blit to window */
    Renderer_EndFrame(window);
}
