/*
 * player.h -- Player state and movement
 *
 * Source: BOMBERMAN_CLONE_PLAN.md Phase 3,
 *         Mac Game Programming (2002) collision patterns.
 */

#ifndef PLAYER_H
#define PLAYER_H

#include "game.h"

void Player_Init(short playerID, short spawnCol, short spawnRow);
void Player_Update(short playerID);
Player *Player_GetLocal(void);
void Player_SetPosition(short playerID, short pixelX, short pixelY, short facing);
void Player_GetHitbox(short playerID, Rect *outRect);
void Player_MarkDirtyTiles(short playerID);

#endif /* PLAYER_H */
