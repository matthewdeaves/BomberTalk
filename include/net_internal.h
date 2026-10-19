/*
 * net_internal.h -- Shared internals of the net_*.c files.
 *
 * net.c owns the PeerTalk context and the small bits of cross-file network
 * state (expected player count, version-mismatch flag). net_handlers.c
 * (incoming) and net_send.c (outgoing) reach them through these accessors.
 * Not for use outside src/net*.c: game code goes through net.h, which keeps
 * PeerTalk out of the game headers.
 */
#ifndef NET_INTERNAL_H
#define NET_INTERNAL_H

#include <peertalk.h>

/* The live PeerTalk context, or NULL before Net_Init / after Net_Shutdown. */
PT_Context *Net_Ctx(void);

void Net_SetExpectedPlayers(short n);
void Net_SetVersionMismatch(void);

/* net_send.c: send our current tilemap to one peer (the late joiner). */
void Net_SendMapStateTo(PT_Peer *peer);

/* net_handlers.c: register the PeerTalk event callbacks and the per-message
 * handlers. Message types themselves are registered by Net_Init. */
void NetHandlers_Register(PT_Context *ctx);

#endif /* NET_INTERNAL_H */
