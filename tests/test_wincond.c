/*
 * test_wincond.c -- Host unit tests for the game-over decision (wincond.c).
 *
 * KI-006 was a game that would not end when opponents quit: array logic over
 * players, found by hand on three Macs. These tests pin the decision and the
 * reactivation guard so that regression is caught on the host.
 */
#include "wincond.h"
#include "test_util.h"

static void test_sole_survivor_wins(void)
{
    int over, winner;
    printf("test_sole_survivor_wins\n");
    /* 3-player match, one left standing, nobody dying -> that player wins. */
    over = Win_Decide(0 /*anyDying*/, 1 /*aliveCount*/, 2 /*lastAlive*/,
                      3 /*numPlayers*/, &winner);
    CHECK_EQ(over, 1, "round over with one survivor");
    CHECK_EQ(winner, 2, "the last-alive index wins");
}

static void test_draw_when_none_left(void)
{
    int over, winner;
    printf("test_draw_when_none_left\n");
    /* Mutual destruction: zero alive, nobody dying -> draw (winner -1). */
    over = Win_Decide(0, 0, -1, 2, &winner);
    CHECK_EQ(over, 1, "round over with no survivors");
    CHECK_EQ(winner, -1, "no survivors is a draw");
}

static void test_not_over_while_dying(void)
{
    int over, winner;
    printf("test_not_over_while_dying\n");
    /* Someone still mid death-flash: undecided even at one alive. */
    over = Win_Decide(1 /*anyDying*/, 1, 0, 3, &winner);
    CHECK_EQ(over, 0, "not over while a death animation plays");
    CHECK_EQ(winner, -1, "no winner reported while undecided");
}

static void test_not_over_with_multiple_alive(void)
{
    int over, winner;
    printf("test_not_over_with_multiple_alive\n");
    over = Win_Decide(0, 2, 3, 4, &winner);
    CHECK_EQ(over, 0, "two still standing -> keep playing");
    CHECK_EQ(winner, -1, "no winner with two alive");
}

static void test_single_player_never_ends(void)
{
    int over, winner;
    printf("test_single_player_never_ends\n");
    /* numPlayers <= 1 must never trigger game over (solo lobby / warmup). */
    over = Win_Decide(0, 1, 0, 1, &winner);
    CHECK_EQ(over, 0, "one-player match never ends");
    over = Win_Decide(0, 0, -1, 1, &winner);
    CHECK_EQ(over, 0, "zero survivors in a solo match still not over");
}

static void test_reactivation_guard(void)
{
    printf("test_reactivation_guard\n");
    /* KI-006: a genuinely gone peer has target snapped to position -> no
     * phantom reactivation. */
    CHECK_EQ(Win_ShouldReactivate(0, 100, 200, 100, 200), 0,
             "inactive with target==position stays inactive");
    /* Fresh network data diverges the target -> reactivate. */
    CHECK_EQ(Win_ShouldReactivate(0, 132, 200, 100, 200), 1,
             "inactive with diverged X target reactivates");
    CHECK_EQ(Win_ShouldReactivate(0, 100, 240, 100, 200), 1,
             "inactive with diverged Y target reactivates");
    /* Already-active players are never re-touched here. */
    CHECK_EQ(Win_ShouldReactivate(1, 132, 240, 100, 200), 0,
             "active player is not a reactivation candidate");
}

int main(void)
{
    printf("== test_wincond ==\n");
    test_sole_survivor_wins();
    test_draw_when_none_left();
    test_not_over_while_dying();
    test_not_over_with_multiple_alive();
    test_single_player_never_ends();
    test_reactivation_guard();
    return TEST_RESULT();
}
