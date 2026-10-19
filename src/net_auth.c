/*
 * net_auth.c -- Who may say what on the wire (portable core, KI-009).
 * See include/net_auth.h and notes/net-identity.md.
 */
#include "net_auth.h"
#include "net_wire.h"

int NetAuth_SlotOfPeer(const void *const *peers, int n, const void *peer)
{
    int i;
    if (peer == 0) return NETAUTH_NO_SLOT;
    if (n > MAX_PLAYERS) n = MAX_PLAYERS;
    for (i = 0; i < n; i++) {
        if (peers[i] == peer) return i;
    }
    return NETAUTH_NO_SLOT;
}

int NetAuth_Accept(NetAuthKind kind, int senderSlot, int claimedSlot)
{
    if (!NetWire_ValidSlot(senderSlot)) return 0;
    switch (kind) {
    case NETAUTH_SELF:
    case NETAUTH_OWNER:
        return claimedSlot == senderSlot;
    case NETAUTH_SEATED:
        return 1;
    }
    return 0;
}

int NetAuth_AdmitLateJoiner(int claimedSlot, int liveRank, int localSlot,
                            int slotHeld)
{
    if (!NetWire_ValidSlot(claimedSlot)) return 0;
    if (claimedSlot != liveRank) return 0;
    if (claimedSlot == localSlot) return 0;
    if (slotHeld) return 0;
    return 1;
}
