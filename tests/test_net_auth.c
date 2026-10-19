/*
 * test_net_auth.c -- Host tests for the who-may-say-what rules (KI-009).
 */
#include "net_auth.h"
#include "net_wire.h"
#include "test_util.h"

static int pa, pb, pc;   /* fake peers: only their addresses matter */

static void test_slot_of_peer(void)
{
    const void *peers[MAX_PLAYERS];
    int i;
    printf("test_slot_of_peer\n");
    for (i = 0; i < MAX_PLAYERS; i++) peers[i] = 0;
    peers[0] = &pa; peers[2] = &pc;

    CHECK_EQ(NetAuth_SlotOfPeer(peers, MAX_PLAYERS, &pa), 0, "peer a -> slot 0");
    CHECK_EQ(NetAuth_SlotOfPeer(peers, MAX_PLAYERS, &pc), 2, "peer c -> slot 2");
    CHECK_EQ(NetAuth_SlotOfPeer(peers, MAX_PLAYERS, &pb), NETAUTH_NO_SLOT,
             "unbound peer has no slot");
    CHECK_EQ(NetAuth_SlotOfPeer(peers, MAX_PLAYERS, 0), NETAUTH_NO_SLOT,
             "NULL peer has no slot (not a match for empty slots)");
    CHECK_EQ(NetAuth_SlotOfPeer(peers, 1, &pc), NETAUTH_NO_SLOT,
             "n bounds the search");
    CHECK_EQ(NetAuth_SlotOfPeer(peers, 99, &pa), 0, "n clamped to MAX_PLAYERS");
}

static void test_self_messages(void)
{
    int s, c;
    printf("test_self_messages\n");
    for (s = 0; s < MAX_PLAYERS; s++) {
        for (c = -1; c < 256; c++) {
            CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, s, c), (c == s),
                     "self: accepted iff claimed == sender");
        }
    }
}

static void test_spoofed_slot_rejected(void)
{
    printf("test_spoofed_slot_rejected\n");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, 1, 0), 0, "P1 claiming P0 dropped");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, 1, 2), 0, "P1 claiming P2 dropped");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, 1, 255), 0, "claim 0xFF dropped");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, 1, 1), 1, "P1 as P1 accepted");
}

static void test_unseated_sender_rejected(void)
{
    printf("test_unseated_sender_rejected\n");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, NETAUTH_NO_SLOT, 0), 0, "self");
    CHECK_EQ(NetAuth_Accept(NETAUTH_OWNER, NETAUTH_NO_SLOT, 0), 0, "owner");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SEATED, NETAUTH_NO_SLOT, 0), 0, "seated");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SEATED, MAX_PLAYERS, 0), 0,
             "out-of-range sender slot");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, NETAUTH_NO_SLOT, NETAUTH_NO_SLOT), 0,
             "no-slot == no-slot must not match");
}

static void test_owner_messages(void)
{
    printf("test_owner_messages\n");
    CHECK_EQ(NetAuth_Accept(NETAUTH_OWNER, 2, 2), 1, "owner detonates own bomb");
    CHECK_EQ(NetAuth_Accept(NETAUTH_OWNER, 2, 0), 0, "not owner: dropped");
    CHECK_EQ(NetAuth_Accept(NETAUTH_OWNER, 2, NETAUTH_NO_SLOT), 0,
             "no bomb at the cell: dropped");
}

static void test_seated_messages(void)
{
    int s;
    printf("test_seated_messages\n");
    for (s = 0; s < MAX_PLAYERS; s++) {
        CHECK_EQ(NetAuth_Accept(NETAUTH_SEATED, s, 0), 1,
                 "any seated peer may send block/map/game-over");
    }
}

static void test_late_joiner(void)
{
    printf("test_late_joiner\n");
    CHECK_EQ(NetAuth_AdmitLateJoiner(2, 2, 0, 0), 1, "free slot, rank matches");
    CHECK_EQ(NetAuth_AdmitLateJoiner(2, 1, 0, 0), 0,
             "claim differs from live rank: dropped");
    CHECK_EQ(NetAuth_AdmitLateJoiner(1, 1, 0, 1), 0,
             "slot already held by another peer (rank shuffle): dropped");
    CHECK_EQ(NetAuth_AdmitLateJoiner(0, 0, 0, 0), 0, "cannot take our own slot");
    CHECK_EQ(NetAuth_AdmitLateJoiner(MAX_PLAYERS, MAX_PLAYERS, 0, 0), 0,
             "no such slot");
    CHECK_EQ(NetAuth_AdmitLateJoiner(-1, -1, 0, 0), 0, "negative slot");
    CHECK_EQ(NetAuth_AdmitLateJoiner(255, 255, 0, 0), 0, "0xFF claim");
}

static void test_late_join_roster(void)
{
    printf("test_late_join_roster\n");
    CHECK_EQ(NetWire_SeatRoster(2, 2), 3, "roster grows to joiner slot + 1");
    CHECK_EQ(NetWire_SeatRoster(4, 2), 4, "roster never shrinks");
}

/* Roster agreement (KI-011): three seated machines (slots 0..2, local is 0).
 * Every machine applies the same rule to a newcomer, so they all admit it or
 * all ignore it. */
static void test_roster_scenarios(void)
{
    static int px, py, jn;
    const void *peers[MAX_PLAYERS];
    int num = 3, slot, held;
    printf("test_roster_scenarios\n");
    peers[0] = 0; peers[1] = &px; peers[2] = &py; peers[3] = 0;

    /* Joiner with the highest address: live rank 3, claims 3, slot free. */
    CHECK_EQ(NetAuth_SlotOfPeer(peers, MAX_PLAYERS, &jn), NETAUTH_NO_SLOT,
             "joiner starts unseated");
    held = (peers[3] != 0);
    CHECK_EQ(NetAuth_AdmitLateJoiner(3, 3, 0, held), 1, "top-rank joiner in");
    peers[3] = &jn;
    num = NetWire_SeatRoster(num, 3);
    CHECK_EQ(num, 4, "roster grew to 4");
    slot = NetAuth_SlotOfPeer(peers, MAX_PLAYERS, &jn);
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, slot, 3), 1, "joiner's own updates ok");
    CHECK_EQ(NetAuth_Accept(NETAUTH_SELF, slot, 1), 0, "joiner cannot be P1");

    /* Joiner whose address sorts into the middle: live rank 1 (existing
     * players' live ranks shift), but slot 1 is held, so it is ignored. */
    peers[3] = 0; num = 3;
    held = (peers[1] != 0);
    CHECK_EQ(NetAuth_AdmitLateJoiner(1, 1, 0, held), 0, "mid-rank joiner out");
    CHECK_EQ(NetAuth_AdmitLateJoiner(3, 1, 0, 0), 0, "cannot claim a free slot it does not rank into");
    CHECK_EQ(num, 3, "roster unchanged");

    /* A seated player drops and its slot frees; it (or a newcomer ranking
     * into it) is readmitted without moving anyone else. */
    peers[1] = 0;
    CHECK_EQ(NetAuth_AdmitLateJoiner(1, 1, 0, peers[1] != 0), 1, "freed slot reusable");
    CHECK_EQ(NetAuth_SlotOfPeer(peers, MAX_PLAYERS, &py), 2, "others keep slots");
}

int main(void)
{
    test_slot_of_peer();
    test_self_messages();
    test_spoofed_slot_rejected();
    test_unseated_sender_rejected();
    test_owner_messages();
    test_seated_messages();
    test_late_joiner();
    test_late_join_roster();
    test_roster_scenarios();
    return TEST_RESULT();
}
