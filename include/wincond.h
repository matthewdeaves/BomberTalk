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

#endif /* WINCOND_H */
