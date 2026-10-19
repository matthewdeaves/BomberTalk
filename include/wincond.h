/*
 * wincond.h -- Last-player-standing decision + reactivation predicate (core).
 *
 * The two pure decisions behind the game-over flow (screen_game.c), carved out
 * (011-macosx-sdl2) so the logic that decides a round -- and the KI-006
 * reactivation guard -- is host unit tested rather than only ever run by a
 * human quitting a peer on real hardware. No Toolbox, no globals; the caller
 * does the player-array tally and I/O, these just judge the tallied result.
 * See notes/deepening-and-testability.md.
 */
#ifndef WINCOND_H
#define WINCOND_H

/*
 * Win_Decide -- Is the round over, and who won?
 *
 *   anyDying     TRUE if any active+alive player is still in its death flash
 *   aliveCount   number of active+alive players NOT dying
 *   lastAlive    index of the last such player seen (valid when aliveCount==1)
 *   numPlayers   players in the match
 *   winnerOut    receives the sole survivor's index, or -1 for a draw
 *
 * Returns TRUE when the round is decided: nobody is mid-death and at most one
 * player is left standing in a match of more than one. A draw (aliveCount 0)
 * reports winner -1. While players are still dying, the round is undecided.
 */
int Win_Decide(int anyDying, short aliveCount, short lastAlive,
               short numPlayers, int *winnerOut);

/*
 * Win_ShouldReactivate -- Should an inactive remote player be brought back?
 *
 * A transient disconnect/reconnect delivers fresh network coordinates to an
 * inactive player, moving its interpolation target away from its held
 * position. That divergence -- and only that -- means the peer is live again.
 * KI-006's fix snaps target==position on disconnect so this stays FALSE for a
 * genuinely gone peer, letting the round end.
 *
 * Returns TRUE iff the player is inactive AND its target diverges from its
 * current pixel position.
 */
int Win_ShouldReactivate(int active, short targetPX, short targetPY,
                         short pixelX, short pixelY);

/*
 * ---- Round-end state machine ----
 *
 * The game-over flow used to be spread over screen_game.c (decide + failsafe +
 * pending), net.c (on_game_over), screen_lobby.c / main*.c (resets). It is now
 * one small struct and four functions here, host tested in test_wincond.c.
 * Callers do the I/O (Net_SendGameOver, logging, grace period) from the
 * action Win_RoundEndStep returns. Time is passed in (TickCount() on the Mac)
 * and the authority question is a callback so it is only asked when the
 * original code asked it.
 */
typedef struct {
    int           pending;       /* remote MSG_GAME_OVER seen, awaiting death anims */
    unsigned char pendingWinner; /* winner byte from that message (0xFF = none) */
    unsigned long timeoutStart;  /* now when pending began, 0 = inactive */
    int           localDetected; /* we decided the round is over but are not authority */
    unsigned long failsafeStart; /* now when localDetected began, 0 = inactive */
} RoundEnd;

/* Reset every field to the idle state. */
void Win_RoundEndClear(RoundEnd *re);

/* A validated remote MSG_GAME_OVER arrived: start the pending wait and cancel
 * any local-failsafe wait (the authority has spoken). */
void Win_RoundEndOnRemote(RoundEnd *re, unsigned char winner,
                          unsigned long now);

/* Start the failsafe clock. The caller does this after WIN_ACT_WAIT_NONAUTH,
 * once its logging is done, so the timer starts where the original inline code
 * started it. Until then localDetected is set with failsafeStart == 0, which
 * never times out. */
void Win_RoundEndArmFailsafe(RoundEnd *re, unsigned long now);

/* What the caller must do after Win_RoundEndStep. */
#define WIN_ACT_NONE              0 /* nothing: round carries on (or still waiting) */
#define WIN_ACT_WAIT_NONAUTH      1 /* decided locally, not authority: log player dump, then Win_RoundEndArmFailsafe */
#define WIN_ACT_END_AUTHORITY     2 /* decided locally as authority: send winner, end; log player dump */
#define WIN_ACT_END_FAILSAFE_GONE 3 /* non-authority backup: authority gone: send winner, end */
#define WIN_ACT_END_FAILSAFE_TIME 4 /* non-authority backup: authority silent: send winner, end */
#define WIN_ACT_END_REMOTE        5 /* remote game over, death anims done: end (no send) */
#define WIN_ACT_END_REMOTE_TIME   6 /* remote game over, death anims timed out: end (no send) */

/*
 * Win_RoundEndStep -- Advance the round-end flow by one frame.
 *
 *   anyDying, aliveCount, lastAlive, numPlayers   the caller's tally (see Win_Decide)
 *   now                     current tick count
 *   failsafeTicks           wait before a non-authority sends the backup
 *   timeoutTicks            wait for death anims after a remote game over
 *   isAuthority             returns TRUE if this machine is the round-end
 *                           authority; called only when the original code
 *                           called it (it logs)
 *   winnerOut               winner byte to send / that was decided (0xFF = none);
 *                           valid for every action except NONE
 *
 * Order of precedence, as in the original inline code: local failsafe, then a
 * pending remote game over, then the local decision.
 */
int Win_RoundEndStep(RoundEnd *re, int anyDying, short aliveCount,
                     short lastAlive, short numPlayers, unsigned long now,
                     unsigned long failsafeTicks, unsigned long timeoutTicks,
                     int (*isAuthority)(void), unsigned char *winnerOut);

#endif /* WINCOND_H */
