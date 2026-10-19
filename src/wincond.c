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

void Win_RoundEndClear(RoundEnd *re)
{
    re->pending = 0;
    re->pendingWinner = 0xFF;
    re->timeoutStart = 0;
    re->localDetected = 0;
    re->failsafeStart = 0;
}

void Win_RoundEndArmFailsafe(RoundEnd *re, unsigned long now)
{
    re->failsafeStart = now;
}

void Win_RoundEndOnRemote(RoundEnd *re, unsigned char winner,
                          unsigned long now)
{
    re->pending = 1;
    re->pendingWinner = winner;
    re->timeoutStart = now;
    /* Cancel the local failsafe: the authority's message arrived (005). */
    re->localDetected = 0;
    re->failsafeStart = 0;
}

int Win_RoundEndStep(RoundEnd *re, int anyDying, short aliveCount,
                     short lastAlive, short numPlayers, unsigned long now,
                     unsigned long failsafeTicks, unsigned long timeoutTicks,
                     int (*isAuthority)(void), unsigned char *winnerOut)
{
    int winnerIdx;

    *winnerOut = 0xFF;

    /* Failsafe: we detected game over locally but are not authority and no
     * MSG_GAME_OVER has arrived. Fires if the authority is gone or silent. */
    if (re->localDetected) {
        int authorityGone = isAuthority();
        int timedOut = (re->failsafeStart != 0 &&
                        now - re->failsafeStart >= failsafeTicks);
        if (!re->pending && (authorityGone || timedOut)) {
            *winnerOut = (aliveCount == 1) ? (unsigned char)lastAlive : 0xFF;
            re->localDetected = 0;
            return authorityGone ? WIN_ACT_END_FAILSAFE_GONE
                                 : WIN_ACT_END_FAILSAFE_TIME;
        }
    }

    /* Pending remote game over: wait for death anims or the safety timeout. */
    if (re->pending) {
        int timedOut = (re->timeoutStart != 0 &&
                        now - re->timeoutStart >= timeoutTicks);
        if (!anyDying || timedOut) {
            *winnerOut = re->pendingWinner;
            re->pending = 0;
            return timedOut ? WIN_ACT_END_REMOTE_TIME : WIN_ACT_END_REMOTE;
        }
        return WIN_ACT_NONE;
    }

    /* Local decision. */
    if (!re->localDetected &&
        Win_Decide(anyDying, aliveCount, lastAlive, numPlayers, &winnerIdx)) {
        *winnerOut = (winnerIdx < 0) ? 0xFF : (unsigned char)winnerIdx;
        if (isAuthority())
            return WIN_ACT_END_AUTHORITY;
        re->localDetected = 1;
        return WIN_ACT_WAIT_NONAUTH;
    }
    return WIN_ACT_NONE;
}
