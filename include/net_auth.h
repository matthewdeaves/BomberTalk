/*
 * net_auth.h -- Who may say what on the wire (portable core, KI-009).
 *
 * A message's claimed player slot is untrusted. The sender is known from the
 * transport (PeerTalk hands every callback the PT_Peer it arrived from), and
 * the game binds each peer to a player slot (gGame.players[i].peer) when the
 * roster is seated. These pure functions decide whether a message is allowed;
 * net.c supplies the facts. No Toolbox, PeerTalk or game.h dependency, so
 * they are host unit tested (tests/test_net_auth.c). Design note:
 * notes/net-identity.md.
 */
#ifndef NET_AUTH_H
#define NET_AUTH_H

#include "coredefs.h"   /* MAX_PLAYERS */

#define NETAUTH_NO_SLOT (-1)

/* Who a message class may come from. */
typedef enum {
    NETAUTH_SELF,    /* about the sender: claimed slot must equal sender slot
                        (position, bomb placed, player killed) */
    NETAUTH_OWNER,   /* about a thing the sender must own: claimed slot is the
                        owner of the referenced object, must equal sender
                        slot (bomb explode; claimed = owner of the bomb at
                        that cell, or NETAUTH_NO_SLOT if there is none) */
    NETAUTH_SEATED   /* any seated peer, no per-player claim (block destroyed,
                        map snapshot, game over) */
} NetAuthKind;

/* Slot bound to `peer` among peers[0..n-1] (peers[i] is the peer seated at
 * slot i, NULL if none), or NETAUTH_NO_SLOT for a NULL / unbound peer. */
int NetAuth_SlotOfPeer(const void *const *peers, int n, const void *peer);

/* 1 = accept, 0 = drop. senderSlot of NETAUTH_NO_SLOT (or out of range) is
 * always dropped: an unseated peer may not speak, whatever it claims. */
int NetAuth_Accept(NetAuthKind kind, int senderSlot, int claimedSlot);

/* Late join (a peer with no bound slot that starts talking in-game). Admit it
 * only into the slot PeerTalk's live rank gives it, if that is the slot it
 * claims, is a real slot, is not ours, and no other peer holds it. Anything
 * else is ignored by every machine alike, which keeps rosters consistent:
 * a joiner whose rank would shuffle existing players is simply not admitted.
 * `slotHeld` is non-zero if another peer is already bound to claimedSlot. */
int NetAuth_AdmitLateJoiner(int claimedSlot, int liveRank, int localSlot,
                            int slotHeld);

#endif /* NET_AUTH_H */
