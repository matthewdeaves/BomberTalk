/*
 * wincond.c -- Last-player-standing decision + reactivation predicate.
 * See wincond.h.
 *
 * Extracted (behaviour-preserving) from screen_game.c (011-macosx-sdl2).
 */
#include "wincond.h"

int Win_Decide(int anyDying, short aliveCount, short lastAlive,
               short numPlayers, int *winnerOut)
{
    if (!anyDying && aliveCount <= 1 && numPlayers > 1) {
        *winnerOut = (aliveCount == 1) ? lastAlive : -1;
        return 1;
    }
    *winnerOut = -1;
    return 0;
}

int Win_ShouldReactivate(int active, short targetPX, short targetPY,
                         short pixelX, short pixelY)
{
    return (!active && (targetPX != pixelX || targetPY != pixelY));
}
