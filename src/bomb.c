/*
 * bomb.c -- Bomb placement, fuse countdown, explosion logic
 */

#include "bomb.h"
#include "tilemap.h"
#include "net.h"
#include "renderer.h"
#include <clog.h>

static Explosion gExplosions[MAX_EXPLOSIONS];
static short gExplosionCount = 0;

#define EXPLOSION_DURATION_TICKS 20  /* ~0.33 sec at 60 ticks/sec */

void Bomb_Init(void)
{
    short i;
    for (i = 0; i < MAX_BOMBS; i++) {
        gGame.bombs[i].active = FALSE;
    }
    gExplosionCount = 0;
}

int Bomb_PlaceAt(short col, short row, short range, unsigned char ownerID)
{
    short i;
    Bomb *b;

    /* Check if bomb already at this location */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active &&
            gGame.bombs[i].gridCol == col &&
            gGame.bombs[i].gridRow == row) {
            return FALSE;
        }
    }

    /* Find empty slot */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (!gGame.bombs[i].active) {
            b = &gGame.bombs[i];
            b->gridCol = col;
            b->gridRow = row;
            b->fuseTimer = BOMB_FUSE_TICKS;
            b->range = range;
            b->ownerID = ownerID;
            b->active = TRUE;
            gGame.numActiveBombs++;
            Renderer_MarkDirty(col, row);
            CLOG_DEBUG("Bomb placed at (%d,%d) by player %d", col, row, ownerID);
            return TRUE;
        }
    }
    return FALSE;
}

int Bomb_ExistsAt(short col, short row)
{
    short i;
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active &&
            gGame.bombs[i].gridCol == col &&
            gGame.bombs[i].gridRow == row) {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * AddExplosion -- Mark a tile as exploding (for rendering)
 */
static void AddExplosion(short col, short row)
{
    if (gExplosionCount >= MAX_EXPLOSIONS) return;
    gExplosions[gExplosionCount].col = col;
    gExplosions[gExplosionCount].row = row;
    gExplosions[gExplosionCount].timer = EXPLOSION_DURATION_TICKS;
    gExplosionCount++;
}

/*
 * ExplodeBomb -- Process explosion in cross pattern
 * broadcast: TRUE if this machine should send network messages
 *            FALSE if triggered by a remote MSG_BOMB_EXPLODE
 */
static void ExplodeBomb(Bomb *b, int broadcast)
{
    short dir, dist;
    short col, row;
    short dCol[4] = {0, 0, -1, 1};
    short dRow[4] = {-1, 1, 0, 0};
    int blocksDestroyed = FALSE;

    CLOG_DEBUG("Bomb exploding at (%d,%d) range=%d",
               b->gridCol, b->gridRow, b->range);

    /* Center tile */
    AddExplosion(b->gridCol, b->gridRow);
    Renderer_MarkDirty(b->gridCol, b->gridRow);

    /* Four directions */
    for (dir = 0; dir < 4; dir++) {
        for (dist = 1; dist <= b->range; dist++) {
            col = b->gridCol + dCol[dir] * dist;
            row = b->gridRow + dRow[dir] * dist;

            /* Stop at walls */
            if (col < 0 || col >= TileMap_GetCols() ||
                row < 0 || row >= TileMap_GetRows()) break;

            if (TileMap_GetTile(col, row) == TILE_WALL) break;

            AddExplosion(col, row);
            Renderer_MarkDirty(col, row);

            /* Destroy blocks and stop */
            if (TileMap_GetTile(col, row) == TILE_BLOCK) {
                TileMap_SetTile(col, row, TILE_FLOOR);
                if (broadcast) Net_SendBlockDestroyed(col, row);
                blocksDestroyed = TRUE;
                break;
            }
        }
    }

    /* Rebuild background once after all blocks destroyed (not per-block) */
    if (blocksDestroyed) {
        Renderer_RebuildBackground();
    }

    /* Broadcast explosion (only if local fuse expired) */
    if (broadcast) {
        Net_SendBombExplode(b->gridCol, b->gridRow, b->range);
    }

    /* Check player kills */
    {
        short p, e;
        for (p = 0; p < MAX_PLAYERS; p++) {
            Player *pl = &gGame.players[p];
            if (!pl->active || !pl->alive || pl->deathTimer > 0) continue;
            for (e = 0; e < gExplosionCount; e++) {
                if (gExplosions[e].timer == EXPLOSION_DURATION_TICKS &&
                    pl->gridCol == gExplosions[e].col &&
                    pl->gridRow == gExplosions[e].row) {
                    pl->deathTimer = DEATH_FLASH_TICKS;
                    if (broadcast) {
                        Net_SendPlayerKilled(pl->playerID, b->ownerID);
                    }
                    CLOG_INFO("Player %d killed by player %d",
                              pl->playerID, b->ownerID);
                    break;
                }
            }
        }
    }

    /* Return bomb to owner */
    {
        short ownerIdx = b->ownerID;
        if (ownerIdx >= 0 && ownerIdx < MAX_PLAYERS) {
            gGame.players[ownerIdx].bombsAvailable++;
        }
    }

    b->active = FALSE;
    gGame.numActiveBombs--;
}

void Bomb_Update(void)
{
    short i;
    short dt = gGame.deltaTicks;

    /* Update bomb fuses (tick-based, not frame-based) */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active) {
            gGame.bombs[i].fuseTimer -= dt;
            if (gGame.bombs[i].fuseTimer <= 0) {
                ExplodeBomb(&gGame.bombs[i], TRUE);
            }
        }
    }

    /* Update explosion timers (tick-based) */
    for (i = gExplosionCount - 1; i >= 0; i--) {
        gExplosions[i].timer -= dt;
        if (gExplosions[i].timer <= 0) {
            /* Mark tile dirty when explosion expires */
            Renderer_MarkDirty(gExplosions[i].col, gExplosions[i].row);
            /* Remove by swapping with last */
            gExplosions[i] = gExplosions[gExplosionCount - 1];
            gExplosionCount--;
        }
    }
}

void Bomb_ForceExplodeAt(short col, short row)
{
    short i;
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active &&
            gGame.bombs[i].gridCol == col &&
            gGame.bombs[i].gridRow == row) {
            CLOG_DEBUG("Force-exploding bomb at (%d,%d) via network", col, row);
            ExplodeBomb(&gGame.bombs[i], FALSE);
            return;
        }
    }
}

Bomb *Bomb_GetActive(short index)
{
    short count = 0;
    short i;
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active) {
            if (count == index) return &gGame.bombs[i];
            count++;
        }
    }
    return NULL;
}

short Bomb_GetActiveCount(void)
{
    return gGame.numActiveBombs;
}

Explosion *Bomb_GetExplosions(short *count)
{
    *count = gExplosionCount;
    return gExplosions;
}
