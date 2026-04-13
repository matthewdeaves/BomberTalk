/*
 * player.c -- Player state, smooth pixel movement, AABB collision
 *
 * Pixel-authoritative positions with center-based grid derivation.
 * Axis-separated AABB collision against tilemap.
 * Fractional accumulator for resolution-independent speed.
 * Corner sliding for corridor alignment assistance.
 * Network interpolation for remote players.
 *
 * Source: Mac Game Programming (2002) Ch. 9 tile collision,
 *         Sex Lies & Video Games (1996) sprite timing,
 *         Macintosh Game Animation (1985) tunneling prevention.
 */

#include "player.h"
#include "input.h"
#include "tilemap.h"
#include "bomb.h"
#include "renderer.h"
#include <clog.h>

/* ---- Grid derivation from pixel center ---- */

static void DeriveGrid(Player *p)
{
    short ts = gGame.tileSize;
    p->gridCol = (short)((p->pixelX + ts / 2) / ts);
    p->gridRow = (short)((p->pixelY + ts / 2) / ts);
}

/* ---- Hitbox helper (T003) ---- */

void Player_GetHitbox(short playerID, Rect *outRect)
{
    const Player *p;
    short inset;
    short ts = gGame.tileSize;

    if (playerID < 0 || playerID >= MAX_PLAYERS || outRect == NULL) return;
    p = &gGame.players[playerID];
    inset = gGame.isMacSE ? HITBOX_INSET_SMALL : HITBOX_INSET_LARGE;

    outRect->left   = p->pixelX + inset;
    outRect->top    = p->pixelY + inset;
    outRect->right  = p->pixelX + ts - inset;
    outRect->bottom = p->pixelY + ts - inset;
}

/* ---- Dirty tile marking (T004) ---- */

void Player_MarkDirtyTiles(short playerID)
{
    const Player *p;
    short ts = gGame.tileSize;
    short minCol, maxCol, minRow, maxRow;
    short c, r;

    if (playerID < 0 || playerID >= MAX_PLAYERS) return;
    p = &gGame.players[playerID];

    minCol = (short)(p->pixelX / ts);
    maxCol = (short)((p->pixelX + ts - 1) / ts);
    minRow = (short)(p->pixelY / ts);
    maxRow = (short)((p->pixelY + ts - 1) / ts);

    for (r = minRow; r <= maxRow; r++) {
        for (c = minCol; c <= maxCol; c++) {
            Renderer_MarkDirty(c, r);
        }
    }
}

/* ---- Init (T005) ---- */

void Player_Init(short playerID, short spawnCol, short spawnRow)
{
    Player *p;

    if (playerID < 0 || playerID >= MAX_PLAYERS) return;

    p = &gGame.players[playerID];
    p->pixelX = (short)(spawnCol * gGame.tileSize);
    p->pixelY = (short)(spawnRow * gGame.tileSize);
    p->targetPixelX = p->pixelX;
    p->targetPixelY = p->pixelY;
    p->accumX = 0;
    p->accumY = 0;
    p->passThroughBombIdx = -1;
    DeriveGrid(p);
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

    CLOG_INFO("P%d init at grid=(%d,%d) px=(%d,%d)",
              playerID, spawnCol, spawnRow, p->pixelX, p->pixelY);
}

Player *Player_GetLocal(void)
{
    if (gGame.localPlayerID < 0 || gGame.localPlayerID >= MAX_PLAYERS)
        return NULL;
    return &gGame.players[gGame.localPlayerID];
}

/* ---- Network position (T022) ---- */

void Player_SetPosition(short playerID, short pixelX, short pixelY, short facing)
{
    Player *p;

    if (playerID < 0 || playerID >= MAX_PLAYERS) return;

    p = &gGame.players[playerID];
    p->targetPixelX = pixelX;
    p->targetPixelY = pixelY;
    p->facing = facing;
    /* Grid derived when interpolation reaches target */
    CLOG_DEBUG("P%d target pos px=(%d,%d) f=%d",
               playerID, pixelX, pixelY, facing);
}

/* ---- AABB collision helpers ---- */

/*
 * CheckTileSolid -- Returns TRUE if tile at (col,row) is solid for this player.
 * Handles bomb pass-through: skip the bomb the player is walking off of.
 */
static int CheckTileSolid(const Player *p, short col, short row)
{
    if (TileMap_IsSolid(col, row)) return TRUE;
    if (Bomb_ExistsAt(col, row)) {
        /* Check pass-through */
        if (p->passThroughBombIdx >= 0 && p->passThroughBombIdx < MAX_BOMBS) {
            const Bomb *passB = &gGame.bombs[p->passThroughBombIdx];
            if (passB->active && passB->gridCol == col && passB->gridRow == row) {
                return FALSE; /* pass-through bomb */
            }
        }
        return TRUE;
    }
    return FALSE;
}

/*
 * CollideAxis -- Move player along one axis, check AABB against tilemap.
 * dx/dy: pixel delta for this axis (one must be 0).
 *
 * Uses the FULL sprite rect (no inset) for wall/block/bomb collision.
 * The hitbox inset (Player_GetHitbox) is only for explosion near-miss
 * checks — walls must stop the sprite flush with no visual overlap.
 * Corner sliding handles corridor entry alignment.
 */
static void CollideAxis(Player *p, short dx, short dy)
{
    short ts = gGame.tileSize;
    short newPX, newPY;
    short hLeft, hTop, hRight, hBottom;
    short minCol, maxCol, minRow, maxRow;
    short c, r;

    newPX = (short)(p->pixelX + dx);
    newPY = (short)(p->pixelY + dy);

    /* Compute full sprite rect at proposed position (no inset) */
    hLeft   = newPX;
    hTop    = newPY;
    hRight  = (short)(newPX + ts);
    hBottom = (short)(newPY + ts);

    /* Clamp to play area bounds */
    if (hLeft < 0) { newPX = 0; hLeft = 0; hRight = ts; }
    if (hTop < 0) { newPY = 0; hTop = 0; hBottom = ts; }
    if (hRight > gGame.playWidth) {
        newPX = (short)(gGame.playWidth - ts);
        hLeft = newPX;
        hRight = gGame.playWidth;
    }
    if (hBottom > gGame.playHeight) {
        newPY = (short)(gGame.playHeight - ts);
        hTop = newPY;
        hBottom = gGame.playHeight;
    }

    /* Find tiles overlapped by sprite rect */
    minCol = (short)(hLeft / ts);
    maxCol = (short)((hRight - 1) / ts);
    minRow = (short)(hTop / ts);
    maxRow = (short)((hBottom - 1) / ts);

    /* Clamp tile indices */
    if (minCol < 0) minCol = 0;
    if (minRow < 0) minRow = 0;
    if (maxCol >= TileMap_GetCols()) maxCol = (short)(TileMap_GetCols() - 1);
    if (maxRow >= TileMap_GetRows()) maxRow = (short)(TileMap_GetRows() - 1);

    /* Check for solid tiles */
    for (r = minRow; r <= maxRow; r++) {
        for (c = minCol; c <= maxCol; c++) {
            if (CheckTileSolid(p, c, r)) {
                /* Clamp sprite flush against tile boundary */
                if (dx > 0) {
                    newPX = (short)(c * ts - ts);
                } else if (dx < 0) {
                    newPX = (short)((c + 1) * ts);
                }
                if (dy > 0) {
                    newPY = (short)(r * ts - ts);
                } else if (dy < 0) {
                    newPY = (short)((r + 1) * ts);
                }
                goto done;
            }
        }
    }

done:
    if (dx != 0) p->pixelX = newPX;
    if (dy != 0) p->pixelY = newPY;
}

/* ---- Corner sliding (T026) ---- */

/*
 * TryCornerSlide -- Nudge player toward corridor alignment and retry movement.
 *
 * When the player presses a direction and is blocked, check if they're within
 * NUDGE_THRESHOLD of tile alignment on the perpendicular axis. If so, nudge
 * them toward alignment and retry the movement. If the retry still fails
 * (no corridor there), undo the nudge.
 *
 * The nudge amount is capped by movePixels (tick-derived) so the nudge speed
 * matches normal movement speed regardless of frame rate.
 *
 * Returns TRUE if the nudge+retry produced movement, FALSE otherwise.
 * On FALSE, player position is unchanged.
 */
static int TryCornerSlide(Player *p, short dirX, short dirY, short movePixels)
{
    short ts = gGame.tileSize;
    short nudge = gGame.isMacSE ? NUDGE_THRESHOLD_SMALL : NUDGE_THRESHOLD_LARGE;
    short offset, nudgeAmount;
    short savedPX = p->pixelX;
    short savedPY = p->pixelY;

    if (dirX != 0) {
        /* Trying to move horizontally, check vertical alignment */
        offset = (short)(p->pixelY % ts);
        if (offset == 0) return FALSE; /* already aligned */
        if (offset <= nudge) {
            nudgeAmount = offset;
            if (nudgeAmount > movePixels) nudgeAmount = movePixels;
            p->pixelY = (short)(p->pixelY - nudgeAmount);
        } else if ((ts - offset) <= nudge) {
            nudgeAmount = (short)(ts - offset);
            if (nudgeAmount > movePixels) nudgeAmount = movePixels;
            p->pixelY = (short)(p->pixelY + nudgeAmount);
        } else {
            return FALSE;
        }
        /* Retry movement at nudged position */
        CollideAxis(p, (short)(dirX * movePixels), 0);
        if (p->pixelX == savedPX) {
            /* Movement axis didn't change — no corridor here, undo nudge */
            p->pixelY = savedPY;
            return FALSE;
        }
        return TRUE;
    } else if (dirY != 0) {
        /* Trying to move vertically, check horizontal alignment */
        offset = (short)(p->pixelX % ts);
        if (offset == 0) return FALSE; /* already aligned */
        if (offset <= nudge) {
            nudgeAmount = offset;
            if (nudgeAmount > movePixels) nudgeAmount = movePixels;
            p->pixelX = (short)(p->pixelX - nudgeAmount);
        } else if ((ts - offset) <= nudge) {
            nudgeAmount = (short)(ts - offset);
            if (nudgeAmount > movePixels) nudgeAmount = movePixels;
            p->pixelX = (short)(p->pixelX + nudgeAmount);
        } else {
            return FALSE;
        }
        /* Retry movement at nudged position */
        CollideAxis(p, 0, (short)(dirY * movePixels));
        if (p->pixelY == savedPY) {
            /* Movement axis didn't change — no corridor here, undo nudge */
            p->pixelX = savedPX;
            return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

/* ---- Remote player interpolation (T023) ---- */

static void InterpolateRemote(Player *p)
{
    short dt = gGame.deltaTicks;
    short dx, dy;
    short step;

    dx = (short)(p->targetPixelX - p->pixelX);
    dy = (short)(p->targetPixelY - p->pixelY);

    if (dx == 0 && dy == 0) return;

    /* Lerp: move (dx * dt / INTERP_TICKS) pixels toward target */
    if (dx != 0) {
        step = (short)(dx * dt / INTERP_TICKS);
        if (step == 0) step = (short)(dx > 0 ? 1 : -1);
        if ((dx > 0 && step > dx) || (dx < 0 && step < dx)) step = dx;
        p->pixelX = (short)(p->pixelX + step);
    }
    if (dy != 0) {
        step = (short)(dy * dt / INTERP_TICKS);
        if (step == 0) step = (short)(dy > 0 ? 1 : -1);
        if ((dy > 0 && step > dy) || (dy < 0 && step < dy)) step = dy;
        p->pixelY = (short)(p->pixelY + step);
    }

    DeriveGrid(p);
}

/* ---- Bomb pass-through update (T018) ---- */

static void UpdatePassThrough(Player *p)
{
    short ts = gGame.tileSize;
    const Bomb *b;
    short bLeft, bTop, bRight, bBottom;
    short hLeft, hTop, hRight, hBottom;

    if (p->passThroughBombIdx < 0) return;
    if (p->passThroughBombIdx >= MAX_BOMBS) {
        p->passThroughBombIdx = -1;
        return;
    }

    b = &gGame.bombs[p->passThroughBombIdx];

    /* Clear if bomb no longer active (exploded) */
    if (!b->active) {
        p->passThroughBombIdx = -1;
        return;
    }

    /* Check if full sprite rect still overlaps the bomb tile
     * (matches CollideAxis which uses full sprite, not hitbox inset) */
    hLeft   = p->pixelX;
    hTop    = p->pixelY;
    hRight  = (short)(p->pixelX + ts);
    hBottom = (short)(p->pixelY + ts);

    bLeft   = (short)(b->gridCol * ts);
    bTop    = (short)(b->gridRow * ts);
    bRight  = (short)((b->gridCol + 1) * ts);
    bBottom = (short)((b->gridRow + 1) * ts);

    /* No overlap = player has left the bomb tile */
    if (hRight <= bLeft || hLeft >= bRight ||
        hBottom <= bTop || hTop >= bBottom) {
        CLOG_DEBUG("P%d cleared pass-through bomb %d",
                   p->playerID, p->passThroughBombIdx);
        p->passThroughBombIdx = -1;
    }
}

/* ---- Main update (T007) ---- */

void Player_Update(short playerID)
{
    Player *p;
    short dirX = 0, dirY = 0;
    short newFacing;
    short movePixels;
    short oldPX, oldPY;
    short ts;
    short ticksPerTile;

    if (playerID < 0 || playerID >= MAX_PLAYERS) return;
    p = &gGame.players[playerID];

    if (!p->active || !p->alive || p->deathTimer > 0) return;

    /* Remote players: interpolate toward network target */
    if (playerID != gGame.localPlayerID) {
        InterpolateRemote(p);
        return;
    }

    ts = gGame.tileSize;
    ticksPerTile = p->stats.speedTicks;

    /* Read input (held keys + accumulated edges for Mac SE) */
    newFacing = p->facing;
    if (Input_IsKeyDown(KEY_UP_ARROW) || Input_WasKeyPressed(KEY_UP_ARROW)) {
        dirY = -1;
        newFacing = DIR_UP;
    } else if (Input_IsKeyDown(KEY_DOWN_ARROW) || Input_WasKeyPressed(KEY_DOWN_ARROW)) {
        dirY = 1;
        newFacing = DIR_DOWN;
    } else if (Input_IsKeyDown(KEY_LEFT_ARROW) || Input_WasKeyPressed(KEY_LEFT_ARROW)) {
        dirX = -1;
        newFacing = DIR_LEFT;
    } else if (Input_IsKeyDown(KEY_RIGHT_ARROW) || Input_WasKeyPressed(KEY_RIGHT_ARROW)) {
        dirX = 1;
        newFacing = DIR_RIGHT;
    }

    p->facing = newFacing;

    /* No movement input */
    if (dirX == 0 && dirY == 0) {
        p->accumX = 0;
        p->accumY = 0;
        return;
    }

    /* Fractional accumulator: resolution-independent speed (R1) */
    if (dirX != 0) {
        p->accumX = (short)(p->accumX + ts * gGame.deltaTicks);
        movePixels = (short)(p->accumX / ticksPerTile);
        p->accumX = (short)(p->accumX % ticksPerTile);
        p->accumY = 0;
    } else {
        p->accumY = (short)(p->accumY + ts * gGame.deltaTicks);
        movePixels = (short)(p->accumY / ticksPerTile);
        p->accumY = (short)(p->accumY % ticksPerTile);
        p->accumX = 0;
    }

    if (movePixels <= 0) return;

    oldPX = p->pixelX;
    oldPY = p->pixelY;

    /* Axis-separated collision (R2): move along active axis */
    if (dirX != 0) {
        CollideAxis(p, (short)(dirX * movePixels), 0);
    } else {
        CollideAxis(p, 0, (short)(dirY * movePixels));
    }

    /* Corner sliding: if blocked, try nudging toward corridor alignment.
     * TryCornerSlide handles nudge, retry, and undo if no corridor exists. */
    if (p->pixelX == oldPX && p->pixelY == oldPY) {
        TryCornerSlide(p, dirX, dirY, movePixels);
    }

    /* Derive grid from center point (T006) */
    DeriveGrid(p);

    /* Update bomb pass-through state */
    UpdatePassThrough(p);

    if (p->pixelX != oldPX || p->pixelY != oldPY) {
        CLOG_DEBUG("P%d move px=(%d,%d)->(%d,%d) grid=(%d,%d)",
                   playerID, oldPX, oldPY, p->pixelX, p->pixelY,
                   p->gridCol, p->gridRow);
    }
}
