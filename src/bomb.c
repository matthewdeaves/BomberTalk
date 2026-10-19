/*
 * bomb.c -- Bomb placement, fuse countdown, explosion logic
 */

#include "bomb.h"
#include "player.h"
#include "tilemap.h"
#include "net.h"
#include "renderer.h"
#include "raycast.h"
#include <clog.h>
#include <string.h>

static Explosion gExplosions[MAX_EXPLOSIONS];
static short gExplosionCount = 0;

/* Spatial grid for O(1) bomb-position lookups.
 * Exposed via extern in bomb.h for BOMB_GRID_CELL macro (008-perf-hotpath). */
unsigned char gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS];

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
 * FirstExplosionHit -- Index of the first live explosion tile the hitbox
 * overlaps, or -1. freshOnly restricts the test to explosions that started
 * this frame (timer still at EXPLOSION_DURATION_TICKS). The AABB is the
 * BLAST_CELL_HITS_BOX macro (raycast.h, host tested via Blast_CellHitsBox):
 * inline per 008 FR-002, no call or SetRect trap per cell in this per-frame
 * loop.
 */
static short FirstExplosionHit(const Rect *hitbox, int freshOnly)
{
    short e;
    short ts = gGame.tileSize;
    for (e = 0; e < gExplosionCount; e++) {
        if (freshOnly && gExplosions[e].timer != EXPLOSION_DURATION_TICKS)
            continue;
        if (BLAST_CELL_HITS_BOX(gExplosions[e].col, gExplosions[e].row, ts,
                                hitbox->left, hitbox->top,
                                hitbox->right, hitbox->bottom))
            return e;
    }
    return -1;
}

/*
 * LocalPlayerHitbox -- The local player's hitbox if it can still be killed
 * (active, alive, not already dying). Each machine is authoritative for its
 * own player's death only -- checking remote players would use stale
 * interpolated positions, causing false kills and premature game over.
 */
static int LocalPlayerHitbox(Rect *hitbox)
{
    Player *pl;
    if (gGame.localPlayerID < 0) return FALSE;
    pl = &gGame.players[gGame.localPlayerID];
    if (!(pl->active && pl->alive && pl->deathTimer <= 0)) return FALSE;
    Player_GetHitbox(gGame.localPlayerID, hitbox);
    return TRUE;
}

/*
 * ApplyBlast -- Plan the cross (Blast_Plan, pure) and apply it: explosion
 * sprites, destroyed blocks, deferred background rebuild.
 * broadcast gates Net_SendBlockDestroyed (owner-authoritative, 005).
 */
static void ApplyBlast(const Bomb *b, int broadcast)
{
    /* Static: 68k main stack is small. These buffers make ApplyBlast
     * non-reentrant: it must not be called from within itself or from
     * anything it calls. Chain reactions (see ExplodeBomb) would need
     * per-call buffers or a work queue instead. */
    static BlastCell cells[BLAST_MAX_CELLS];
    static BlastCell blocks[BLAST_MAX_BLOCKS];
    const TileMap *map = TileMap_Get();
    short n, nb, i;

    n = Blast_Plan(b->gridCol, b->gridRow, b->range,
                   TileMap_GetCols(), TileMap_GetRows(),
                   &map->tiles[0][0], cells, blocks, &nb);

    for (i = 0; i < n; i++) {
        AddExplosion(cells[i].col, cells[i].row);
        Renderer_MarkDirty(cells[i].col, cells[i].row);
    }

    /* Destroy the block each ray stopped on. */
    for (i = 0; i < nb; i++) {
        CLOG_DEBUG("Block destroyed at (%d,%d)", blocks[i].col, blocks[i].row);
        TileMap_SetTile(blocks[i].col, blocks[i].row, TILE_FLOOR);
        if (broadcast) Net_SendBlockDestroyed(blocks[i].col, blocks[i].row);
    }

    /* Request deferred rebuild -- coalesced to once per frame */
    if (nb > 0) {
        Renderer_RequestRebuildBackground();
    }
}

/*
 * ExplodeBomb -- Process explosion in cross pattern
 * broadcast: TRUE if this machine should send network messages
 *            FALSE if triggered by a remote MSG_BOMB_EXPLODE
 *
 * Owner-authoritative (005): only the bomb owner's machine broadcasts.
 * The broadcast flag also gates Net_SendBlockDestroyed() for blocks
 * hit by the raycast.
 *
 * Chain explosions (bomb A raycast triggering bomb B) are NOT currently
 * implemented — the raycast checks walls and blocks only. If chains are
 * added later, the broadcast flag must propagate from the parent
 * explosion to chained bombs.
 */
static void ExplodeBomb(Bomb *b, int broadcast)
{
    Rect hitbox;

    CLOG_INFO("Bomb exploding at (%d,%d) range=%d owner=P%d %s",
              b->gridCol, b->gridRow, b->range, b->ownerID,
              broadcast ? "local" : "remote");

    ApplyBlast(b, broadcast);

    /* Broadcast explosion (only if local fuse expired) */
    if (broadcast) {
        Net_SendBombExplode(b->gridCol, b->gridRow, b->range);
    }

    /* Check if LOCAL player killed by this explosion (AABB overlap).
     * Each machine is authoritative for its own player's death only --
     * prevents race condition when multiple machines fuse-expire the
     * same bomb independently and have stale remote positions. */
    if (LocalPlayerHitbox(&hitbox) && FirstExplosionHit(&hitbox, TRUE) >= 0) {
        Player *pl = &gGame.players[gGame.localPlayerID];
        pl->deathTimer = DEATH_FLASH_TICKS;
        Net_SendPlayerKilled(pl->playerID, b->ownerID);
        CLOG_INFO("P%d killed by P%d (AABB overlap)",
                  pl->playerID, b->ownerID);
    }

    if (b->ownerID < MAX_PLAYERS) {
        gGame.players[b->ownerID].bombsAvailable++;
    }

    b->active = FALSE;
    gBombGrid[b->gridRow][b->gridCol] = 0;
    gGame.numActiveBombs--;
}

void Bomb_Update(void)
{
    short i;
    short dt = gGame.deltaTicks;
    Rect hitbox;

    /* Update bomb fuses (tick-based, not frame-based).
     * Owner-authoritative: only the bomb owner broadcasts the explosion.
     * Non-owners explode locally with broadcast=FALSE. (005) */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active) {
            gGame.bombs[i].fuseTimer -= dt;
            if (gGame.bombs[i].fuseTimer <= 0) {
                int isOwner = (gGame.localPlayerID >= 0 &&
                               gGame.localPlayerID == (short)gGame.bombs[i].ownerID);
                if (!isOwner) {
                    CLOG_DEBUG("Bomb at (%d,%d) fuse expired locally "
                              "(owner=P%d, not local P%d)",
                              gGame.bombs[i].gridCol, gGame.bombs[i].gridRow,
                              gGame.bombs[i].ownerID, gGame.localPlayerID);
                }
                ExplodeBomb(&gGame.bombs[i], isOwner);
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

    /* Per-frame AABB kill check: local player walking into active
     * explosions. */
    if (LocalPlayerHitbox(&hitbox)) {
        i = FirstExplosionHit(&hitbox, FALSE);
        if (i >= 0) {
            Player *pl = &gGame.players[gGame.localPlayerID];
            pl->deathTimer = DEATH_FLASH_TICKS;
            CLOG_INFO("P%d walked into explosion at (%d,%d)",
                      pl->playerID, gExplosions[i].col, gExplosions[i].row);
            Net_SendPlayerKilled(pl->playerID, pl->playerID);
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

const Explosion *Bomb_GetExplosions(short *count)
{
    *count = gExplosionCount;
    return gExplosions;
}
