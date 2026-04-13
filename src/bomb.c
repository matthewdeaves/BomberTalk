/*
 * bomb.c -- Bomb placement, fuse countdown, explosion logic
 */

#include "bomb.h"
#include "player.h"
#include "tilemap.h"
#include "net.h"
#include "renderer.h"
#include <clog.h>
#include <string.h>

static Explosion gExplosions[MAX_EXPLOSIONS];
static short gExplosionCount = 0;

/* Spatial grid for O(1) bomb-position lookups */
static unsigned char gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS];

/* Explosion raycast directions (static const avoids stack init per call) */
static const short kExplodeDCol[4] = {0, 0, -1, 1};
static const short kExplodeDRow[4] = {-1, 1, 0, 0};


void Bomb_Init(void)
{
    short i;
    for (i = 0; i < MAX_BOMBS; i++) {
        gGame.bombs[i].active = FALSE;
    }
    gExplosionCount = 0;
    memset(gBombGrid, 0, sizeof(gBombGrid));
}

int Bomb_PlaceAt(short col, short row, short range, unsigned char ownerID)
{
    short i;
    Bomb *b;

    /* Bounds check before spatial grid access */
    if (col < 0 || col >= TileMap_GetCols() ||
        row < 0 || row >= TileMap_GetRows()) return FALSE;

    /* O(1) duplicate check via spatial grid */
    if (gBombGrid[row][col]) return FALSE;

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
            gBombGrid[row][col] = 1;
            gGame.numActiveBombs++;
            Renderer_MarkDirty(col, row);
            CLOG_DEBUG("Bomb placed at (%d,%d) by P%d", col, row, ownerID);
            return TRUE;
        }
    }
    return FALSE;
}

int Bomb_ExistsAt(short col, short row)
{
    if (col < 0 || col >= TileMap_GetCols() ||
        row < 0 || row >= TileMap_GetRows()) return FALSE;
    return gBombGrid[row][col];
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
    int blocksDestroyed = FALSE;

    CLOG_INFO("Bomb exploding at (%d,%d) range=%d owner=P%d %s",
              b->gridCol, b->gridRow, b->range, b->ownerID,
              broadcast ? "local" : "remote");

    /* Center tile */
    AddExplosion(b->gridCol, b->gridRow);
    Renderer_MarkDirty(b->gridCol, b->gridRow);

    /* Four directions */
    for (dir = 0; dir < 4; dir++) {
        for (dist = 1; dist <= b->range; dist++) {
            col = b->gridCol + kExplodeDCol[dir] * dist;
            row = b->gridRow + kExplodeDRow[dir] * dist;

            /* Stop at walls */
            if (col < 0 || col >= TileMap_GetCols() ||
                row < 0 || row >= TileMap_GetRows()) break;

            if (TileMap_GetTile(col, row) == TILE_WALL) break;

            AddExplosion(col, row);
            Renderer_MarkDirty(col, row);

            /* Destroy blocks and stop */
            if (TileMap_GetTile(col, row) == TILE_BLOCK) {
                CLOG_DEBUG("Block destroyed at (%d,%d)", col, row);
                TileMap_SetTile(col, row, TILE_FLOOR);
                if (broadcast) Net_SendBlockDestroyed(col, row);
                blocksDestroyed = TRUE;
                break;
            }
        }
    }

    /* Request deferred rebuild -- coalesced to once per frame */
    if (blocksDestroyed) {
        Renderer_RequestRebuildBackground();
    }

    /* Broadcast explosion (only if local fuse expired) */
    if (broadcast) {
        Net_SendBombExplode(b->gridCol, b->gridRow, b->range);
    }

    /* Check if LOCAL player killed by this explosion (AABB overlap).
     * Each machine is authoritative for its own player's death only —
     * prevents race condition when multiple machines fuse-expire the
     * same bomb independently and have stale remote positions. */
    if (gGame.localPlayerID >= 0) {
        short e;
        short ts = gGame.tileSize;
        Player *pl = &gGame.players[gGame.localPlayerID];
        if (pl->active && pl->alive && pl->deathTimer <= 0) {
            Rect hitbox;
            Player_GetHitbox(gGame.localPlayerID, &hitbox);
            for (e = 0; e < gExplosionCount; e++) {
                Rect expRect;
                if (gExplosions[e].timer != EXPLOSION_DURATION_TICKS) continue;
                SetRect(&expRect,
                        gExplosions[e].col * ts, gExplosions[e].row * ts,
                        (gExplosions[e].col + 1) * ts,
                        (gExplosions[e].row + 1) * ts);
                /* AABB overlap test */
                if (!(hitbox.right <= expRect.left ||
                      hitbox.left >= expRect.right ||
                      hitbox.bottom <= expRect.top ||
                      hitbox.top >= expRect.bottom)) {
                    pl->deathTimer = DEATH_FLASH_TICKS;
                    Net_SendPlayerKilled(pl->playerID, b->ownerID);
                    CLOG_INFO("P%d killed by P%d (AABB overlap)",
                              pl->playerID, b->ownerID);
                    break;
                }
            }
        }
    }

    /* Return bomb to owner.
     * ownerID is unsigned char so >= 0 is always true today, but kept
     * as defensive bounds check in case the type changes later. */
    {
        short ownerIdx = b->ownerID;
        if (ownerIdx >= 0 && ownerIdx < MAX_PLAYERS) {
            gGame.players[ownerIdx].bombsAvailable++;
        }
    }

    b->active = FALSE;
    gBombGrid[b->gridRow][b->gridCol] = 0;
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

    /* Per-frame AABB kill check: local player walking into active explosions.
     * Each machine is authoritative for its own player's death only —
     * checking remote players here would use stale interpolated positions,
     * causing false kills and premature game over. */
    if (gGame.localPlayerID >= 0) {
        short ts = gGame.tileSize;
        Player *pl = &gGame.players[gGame.localPlayerID];
        if (pl->active && pl->alive && pl->deathTimer <= 0) {
            Rect hitbox;
            Player_GetHitbox(gGame.localPlayerID, &hitbox);
            for (i = 0; i < gExplosionCount; i++) {
                Rect expRect;
                SetRect(&expRect,
                        gExplosions[i].col * ts, gExplosions[i].row * ts,
                        (gExplosions[i].col + 1) * ts,
                        (gExplosions[i].row + 1) * ts);
                if (!(hitbox.right <= expRect.left ||
                      hitbox.left >= expRect.right ||
                      hitbox.bottom <= expRect.top ||
                      hitbox.top >= expRect.bottom)) {
                    pl->deathTimer = DEATH_FLASH_TICKS;
                    CLOG_INFO("P%d walked into explosion at (%d,%d)",
                              pl->playerID, gExplosions[i].col,
                              gExplosions[i].row);
                    Net_SendPlayerKilled(pl->playerID, pl->playerID);
                    break;
                }
            }
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

Explosion *Bomb_GetExplosions(short *count)
{
    *count = gExplosionCount;
    return gExplosions;
}
