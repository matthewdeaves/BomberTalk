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

/* ---- Round-end state machine (characterises the former inline flow) ---- */

static int gAuth;
static int gAuthCalls;
static int stub_auth(void) { gAuthCalls++; return gAuth; }

#define FS 120UL   /* failsafe ticks */
#define TO 180UL   /* pending timeout ticks */

static int step(RoundEnd *re, int dying, short alive, short last, short n,
                unsigned long now, unsigned char *w)
{
    return Win_RoundEndStep(re, dying, alive, last, n, now, FS, TO,
                            stub_auth, w);
}

static void test_re_clear_and_remote(void)
{
    RoundEnd re;
    printf("test_re_clear_and_remote\n");
    re.pending = 1; re.pendingWinner = 3; re.timeoutStart = 9;
    re.localDetected = 1; re.failsafeStart = 9;
    Win_RoundEndClear(&re);
    CHECK_EQ(re.pending, 0, "clear: pending");
    CHECK_EQ(re.pendingWinner, 0xFF, "clear: winner none");
    CHECK_EQ(re.timeoutStart, 0, "clear: timeout");
    CHECK_EQ(re.localDetected, 0, "clear: localDetected");
    CHECK_EQ(re.failsafeStart, 0, "clear: failsafe");

    re.localDetected = 1; re.failsafeStart = 500;
    Win_RoundEndOnRemote(&re, 2, 1000);
    CHECK_EQ(re.pending, 1, "remote: pending set");
    CHECK_EQ(re.pendingWinner, 2, "remote: winner stored");
    CHECK_EQ(re.timeoutStart, 1000, "remote: timeout started");
    CHECK_EQ(re.localDetected, 0, "remote: failsafe cancelled");
    CHECK_EQ(re.failsafeStart, 0, "remote: failsafe time cleared");
}

static void test_re_authority_decides(void)
{
    RoundEnd re; unsigned char w = 7; int a;
    printf("test_re_authority_decides\n");
    Win_RoundEndClear(&re); gAuth = 1; gAuthCalls = 0;
    a = step(&re, 0, 2, 1, 3, 100, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "two alive: nothing");
    CHECK_EQ(gAuthCalls, 0, "authority not asked while undecided");
    a = step(&re, 0, 1, 2, 3, 100, &w);
    CHECK_EQ(a, WIN_ACT_END_AUTHORITY, "sole survivor, authority ends");
    CHECK_EQ(w, 2, "winner is survivor");
    CHECK_EQ(re.localDetected, 0, "authority does not arm failsafe");
    a = step(&re, 0, 0, -1, 2, 100, &w);
    CHECK_EQ(a, WIN_ACT_END_AUTHORITY, "draw ends");
    CHECK_EQ(w, 0xFF, "draw winner is none");
    a = step(&re, 1, 1, 0, 3, 100, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "undecided while someone is dying");
}

static void test_re_nonauthority_failsafe(void)
{
    RoundEnd re; unsigned char w; int a;
    printf("test_re_nonauthority_failsafe\n");
    Win_RoundEndClear(&re); gAuth = 0;
    a = step(&re, 0, 1, 1, 2, 1000, &w);
    CHECK_EQ(a, WIN_ACT_WAIT_NONAUTH, "non-authority waits");
    CHECK_EQ(w, 1, "decided winner reported");
    CHECK_EQ(re.localDetected, 1, "failsafe armed");
    CHECK_EQ(re.failsafeStart, 0, "clock not started by Step");
    /* Armed failsafe with no clock never fires while authority is present. */
    a = step(&re, 0, 1, 1, 2, 999999, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "unarmed clock never times out");
    CHECK_EQ(re.localDetected, 1, "still waiting");
    Win_RoundEndArmFailsafe(&re, 1000);
    CHECK_EQ(re.failsafeStart, 1000, "caller starts the clock");

    /* Still waiting: authority present, timer not expired. */
    a = step(&re, 0, 1, 1, 2, 1000 + FS - 1, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "just before the timeout keeps waiting");
    CHECK_EQ(re.localDetected, 1, "still armed");

    /* Timer expires. */
    a = step(&re, 0, 1, 1, 2, 1000 + FS, &w);
    CHECK_EQ(a, WIN_ACT_END_FAILSAFE_TIME, "timeout sends the backup");
    CHECK_EQ(w, 1, "backup carries the survivor");
    CHECK_EQ(re.localDetected, 0, "failsafe disarmed");
    CHECK_EQ(re.failsafeStart, 1000, "failsafe time left set after firing");

    /* Authority asked exactly once per frame on the not-firing path. */
    Win_RoundEndClear(&re); gAuth = 0;
    step(&re, 0, 1, 1, 2, 1000, &w);
    Win_RoundEndArmFailsafe(&re, 1000);
    gAuthCalls = 0;
    step(&re, 0, 1, 1, 2, 1001, &w);
    CHECK_EQ(gAuthCalls, 1, "authority asked once per waiting frame");

    /* Authority disappears before the timeout; draw -> 0xFF. */
    Win_RoundEndClear(&re); gAuth = 0;
    step(&re, 0, 0, -1, 2, 50, &w);
    Win_RoundEndArmFailsafe(&re, 50);
    gAuth = 1;   /* we became lowest rank */
    a = step(&re, 0, 0, -1, 2, 60, &w);
    CHECK_EQ(a, WIN_ACT_END_FAILSAFE_GONE, "authority gone sends at once");
    CHECK_EQ(w, 0xFF, "draw backup has no winner");
}

static void test_re_remote_game_over(void)
{
    RoundEnd re; unsigned char w; int a;
    printf("test_re_remote_game_over\n");
    Win_RoundEndClear(&re); gAuth = 0; gAuthCalls = 0;
    Win_RoundEndOnRemote(&re, 1, 2000);
    a = step(&re, 1, 1, 1, 3, 2010, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "waits for death anims");
    CHECK_EQ(re.pending, 1, "still pending");
    CHECK_EQ(gAuthCalls, 0, "authority not asked (failsafe unarmed)");
    a = step(&re, 0, 1, 1, 3, 2020, &w);
    CHECK_EQ(a, WIN_ACT_END_REMOTE, "ends when anims finish");
    CHECK_EQ(w, 1, "reports the remote winner");
    CHECK_EQ(re.pending, 0, "pending cleared");

    Win_RoundEndClear(&re);
    Win_RoundEndOnRemote(&re, 0xFF, 2000);
    a = step(&re, 1, 1, 1, 3, 2000 + TO - 1, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "not yet timed out");
    a = step(&re, 1, 1, 1, 3, 2000 + TO, &w);
    CHECK_EQ(a, WIN_ACT_END_REMOTE_TIME, "timeout forces the end");
    CHECK_EQ(w, 0xFF, "winner none preserved");

    /* Timeout reported even when anims are also done (original warned). */
    Win_RoundEndClear(&re);
    Win_RoundEndOnRemote(&re, 0, 2000);
    a = step(&re, 0, 1, 0, 3, 2000 + TO, &w);
    CHECK_EQ(a, WIN_ACT_END_REMOTE_TIME, "timedOut wins the reason");

    /* timeoutStart == 0 means inactive: never times out. */
    Win_RoundEndClear(&re); re.pending = 1;
    a = step(&re, 1, 1, 1, 3, 99999, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "inactive timeout never fires");
}

static void test_re_pending_suppresses_failsafe(void)
{
    RoundEnd re; unsigned char w; int a;
    printf("test_re_pending_suppresses_failsafe\n");
    /* localDetected and pending both set (cannot normally coexist, but the
     * original guarded on !pending): the failsafe must not fire. */
    Win_RoundEndClear(&re);
    re.localDetected = 1; re.failsafeStart = 1; re.pending = 1;
    re.timeoutStart = 5000; re.pendingWinner = 2;
    gAuth = 1; gAuthCalls = 0;
    a = step(&re, 1, 1, 0, 3, 9000, &w);
    CHECK_EQ(gAuthCalls, 1, "authority asked whenever failsafe armed");
    CHECK_EQ(a, WIN_ACT_END_REMOTE_TIME, "pending path, not failsafe");
    CHECK_EQ(w, 2, "remote winner");
}

static void test_re_no_double_decision(void)
{
    RoundEnd re; unsigned char w; int a;
    printf("test_re_no_double_decision\n");
    Win_RoundEndClear(&re); gAuth = 0;
    step(&re, 0, 1, 0, 2, 10, &w);          /* arms failsafe */
    Win_RoundEndArmFailsafe(&re, 10);
    a = step(&re, 0, 1, 0, 2, 11, &w);
    CHECK_EQ(a, WIN_ACT_NONE, "armed failsafe is not re-decided each frame");
    CHECK_EQ(re.failsafeStart, 10, "failsafe start not reset");
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
    test_re_clear_and_remote();
    test_re_authority_decides();
    test_re_nonauthority_failsafe();
    test_re_remote_game_over();
    test_re_pending_suppresses_failsafe();
    test_re_no_double_decision();
    return TEST_RESULT();
}
