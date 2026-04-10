/*
 * player.c -- Player state, movement, collision
 *
 * Grid-locked movement with cooldown timer.
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 3,
 *         Mac Game Programming (2002) collision.
 */

#include "player.h"
#include "input.h"
#include "tilemap.h"
#include "bomb.h"
#include <clog.h>

#define MOVE_COOLDOWN_TICKS 12  /* ~200ms at 60 ticks/sec */

static short gMoveCooldown[MAX_PLAYERS];

void Player_Init(short playerID, short spawnCol, short spawnRow)
{
    Player *p;

    if (playerID < 0 || playerID >= MAX_PLAYERS) return;

    p = &gGame.players[playerID];
    p->gridCol = spawnCol;
    p->gridRow = spawnRow;
    p->pixelX = TileMap_ColToPixel(spawnCol);
    p->pixelY = TileMap_RowToPixel(spawnRow);
    p->facing = DIR_DOWN;
    p->animFrame = 0;
    p->alive = TRUE;
    p->active = TRUE;
    p->deathTimer = 0;
    p->stats.bombsMax = 1;
    p->stats.bombRange = 1;
    p->stats.speedTicks = 12;
    p->bombsAvailable = p->stats.bombsMax;
    p->playerID = (unsigned char)playerID;
    p->peer = NULL;

    gMoveCooldown[playerID] = 0;

    CLOG_INFO("Player %d init at (%d,%d)", playerID, spawnCol, spawnRow);
}

Player *Player_Get(short playerID)
{
    if (playerID < 0 || playerID >= MAX_PLAYERS) return NULL;
    return &gGame.players[playerID];
}

Player *Player_GetLocal(void)
{
    if (gGame.localPlayerID < 0 || gGame.localPlayerID >= MAX_PLAYERS)
        return NULL;
    return &gGame.players[gGame.localPlayerID];
}

void Player_SetPosition(short playerID, short col, short row, short facing)
{
    Player *p;

    if (playerID < 0 || playerID >= MAX_PLAYERS) return;

    p = &gGame.players[playerID];
    p->gridCol = col;
    p->gridRow = row;
    p->pixelX = TileMap_ColToPixel(col);
    p->pixelY = TileMap_RowToPixel(row);
    p->facing = facing;
    CLOG_DEBUG("Player %d remote pos (%d,%d) f=%d", playerID, col, row, facing);
}

void Player_Update(short playerID)
{
    Player *p;
    short newCol, newRow;
    short newFacing;

    if (playerID < 0 || playerID >= MAX_PLAYERS) return;
    p = &gGame.players[playerID];

    if (!p->active || !p->alive || p->deathTimer > 0) return;

    /* Only handle input for local player */
    if (playerID != gGame.localPlayerID) return;

    /* Movement cooldown (tick-based) */
    if (gMoveCooldown[playerID] > 0) {
        gMoveCooldown[playerID] -= gGame.deltaTicks;
        if (gMoveCooldown[playerID] > 0) return;
        /* Cooldown expired this frame -- fall through to check input */
    }

    newCol = p->gridCol;
    newRow = p->gridRow;
    newFacing = p->facing;

    /* Check held keys first, then accumulated edges for quick taps
     * between frames (critical on Mac SE at ~3-10fps where a brief
     * tap may complete entirely between Player_Update calls). */
    if (Input_IsKeyDown(KEY_UP_ARROW) || Input_WasKeyPressed(KEY_UP_ARROW)) {
        newRow--;
        newFacing = DIR_UP;
    } else if (Input_IsKeyDown(KEY_DOWN_ARROW) || Input_WasKeyPressed(KEY_DOWN_ARROW)) {
        newRow++;
        newFacing = DIR_DOWN;
    } else if (Input_IsKeyDown(KEY_LEFT_ARROW) || Input_WasKeyPressed(KEY_LEFT_ARROW)) {
        newCol--;
        newFacing = DIR_LEFT;
    } else if (Input_IsKeyDown(KEY_RIGHT_ARROW) || Input_WasKeyPressed(KEY_RIGHT_ARROW)) {
        newCol++;
        newFacing = DIR_RIGHT;
    }

    p->facing = newFacing;

    /* Check collision with tilemap */
    if ((newCol != p->gridCol || newRow != p->gridRow) &&
        !TileMap_IsSolid(newCol, newRow) &&
        !Bomb_ExistsAt(newCol, newRow)) {
        CLOG_DEBUG("P%d move (%d,%d)->(%d,%d) f=%d",
                   playerID, p->gridCol, p->gridRow, newCol, newRow, newFacing);
        p->gridCol = newCol;
        p->gridRow = newRow;
        p->pixelX = TileMap_ColToPixel(newCol);
        p->pixelY = TileMap_RowToPixel(newRow);
        gMoveCooldown[playerID] = p->stats.speedTicks;
    } else if (newCol != p->gridCol || newRow != p->gridRow) {
        CLOG_DEBUG("P%d blocked (%d,%d)->(%d,%d)", playerID,
                   p->gridCol, p->gridRow, newCol, newRow);
    }
}
