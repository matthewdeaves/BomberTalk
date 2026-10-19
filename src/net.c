/*
 * net.c -- PeerTalk wrapper: lifecycle, discovery, peer/roster queries
 *
 * Thin layer keeping PeerTalk includes out of game headers.
 * Source: contracts/network-protocol.md
 *
 * Split by concern: incoming traffic is net_handlers.c, outgoing is
 * net_send.c (both share the context and state below via net_internal.h);
 * the pure acceptance/wire rules are net_auth.c, net_wire.c, netcoord.c.
 */

#include "net.h"
#include "net_internal.h"
#include "game.h"
#include <peertalk.h>
#include <clog.h>
#include <string.h>

static PT_Context *gPTCtx = NULL;
static short gExpectedPlayers = 0;
static int gVersionMismatch = FALSE;

PT_Context *Net_Ctx(void)
{
    return gPTCtx;
}

void Net_SetExpectedPlayers(short n)
{
    gExpectedPlayers = n;
}

void Net_SetVersionMismatch(void)
{
    gVersionMismatch = TRUE;
}

/* ---- Debug Broadcast (via PeerTalk debug channel) ---- */

#ifndef CLOG_STRIP
/* Bridge clog output into PeerTalk's debug broadcast channel.
 * PeerTalk handles prefixing and UDP broadcast; clog stays file-only. */
static void log_to_debug(const char *msg, int len, void *user_data)
{
    PT_DebugSend((PT_Context *)user_data, msg, (size_t)len);
}
#endif

/* ---- Public API ---- */

void Net_Init(const char *playerName)
{
    PT_Status status;

    status = PT_Init(&gPTCtx, playerName);
    if (status != PT_OK) {
        CLOG_ERR("PT_Init failed: %d", status);
        return;
    }

#ifndef CLOG_STRIP
    PT_EnableDebugBroadcast(gPTCtx, 0);
    clog_set_network_sink(log_to_debug, gPTCtx);
#endif

    /* Register message types */
    PT_RegisterMessage(gPTCtx, MSG_POSITION,        PT_FAST);
    PT_RegisterMessage(gPTCtx, MSG_BOMB_PLACED,     PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_BOMB_EXPLODE,    PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_BLOCK_DESTROYED, PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_PLAYER_KILLED,   PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_GAME_START,      PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_GAME_OVER,       PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_MAP_STATE,       PT_RELIABLE);

    NetHandlers_Register(gPTCtx);

    /* Full-mesh topology is PeerTalk's job: once discovery finds peers it
     * keeps a TCP connection open to every one of them (dialing only the
     * pairs we are the designated initiator for, so the simultaneous-connect
     * race cannot arise, and re-dialing after any drop). The lobby no longer
     * dials, staggers, or retries by hand -- by the time a player presses
     * Start the mesh is already formed, and GAME_START launches over it. */
    PT_EnableAutoMesh(gPTCtx, 1);

    CLOG_INFO("Net initialized");
}

void Net_Shutdown(void)
{
    if (gPTCtx) {
#ifndef CLOG_STRIP
        /* Clear UDP log sink BEFORE shutdown: PT_Shutdown logs via CLOG
         * during MacTCP teardown phases.  If the sink is still active,
         * those CLOG calls try to PT_SendUDPBroadcast through the very
         * UDP streams being released, corrupting MacTCP driver state in
         * the System heap (causes Finder crash after ExitToShell). */
        clog_set_network_sink(NULL, NULL);
#endif
        PT_StopDiscovery(gPTCtx);
        PT_Shutdown(gPTCtx);
        gPTCtx = NULL;
    }
}

void Net_Poll(void)
{
    if (gPTCtx) {
        PT_Poll(gPTCtx);
    }
}

void Net_StartDiscovery(void)
{
    if (gPTCtx) {
        PT_StartDiscovery(gPTCtx);
    }
}

void Net_StopDiscovery(void)
{
    if (gPTCtx) {
        PT_StopDiscovery(gPTCtx);
    }
}

void Net_DisconnectAllPeers(void)
{
    if (!gPTCtx) return;
    PT_DisconnectAll(gPTCtx);
}

int Net_GetDiscoveredPeerCount(void)
{
    int count, i, discovered;
    const PT_Peer *peer;

    if (!gPTCtx) return 0;

    count = PT_GetPeerCount(gPTCtx);
    discovered = 0;
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        /* Skip peers PeerTalk has flagged gone. A peer that quit (clean Cmd-Q
         * sends a discovery leave; a crash/close tears down TCP) is marked
         * DISCONNECTED but kept in the table -- counting it made the lobby
         * show quit players as still present until they aged out (011). */
        if (peer && PT_GetPeerState(peer) != PT_PEER_DISCONNECTED)
            discovered++;
    }
    return discovered;
}

/* idx-th live (non-DISCONNECTED) peer, matching the filtered
 * Net_GetDiscoveredPeerCount so the lobby's count, name list and address list
 * all index the same peers. NULL if idx is out of range. */
static PT_Peer *Net_GetLivePeer(int idx)
{
    int count, i, seen;
    PT_Peer *peer;
    if (!gPTCtx || idx < 0) return NULL;

    count = PT_GetPeerCount(gPTCtx);
    seen = 0;
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) != PT_PEER_DISCONNECTED) {
            if (seen == idx) return peer;
            seen++;
        }
    }
    return NULL;
}

const char *Net_GetDiscoveredPeerName(int idx)
{
    PT_Peer *peer = Net_GetLivePeer(idx);
    return peer ? PT_PeerName(peer) : "";
}

const char *Net_GetDiscoveredPeerAddress(int idx)
{
    PT_Peer *peer = Net_GetLivePeer(idx);
    return peer ? PT_PeerAddress(peer) : "";
}

int Net_GetConnectedPeerCount(void)
{
    int count, i, connected;
    PT_Peer *peer;

    if (!gPTCtx) return 0;

    connected = 0;
    count = PT_GetPeerCount(gPTCtx);
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) == PT_PEER_CONNECTED)
            connected++;
    }
    return connected;
}

short Net_GetExpectedPlayers(void)
{
    return gExpectedPlayers;
}

/*
 * Net_IsLowestRankConnected -- TRUE if local machine has the lowest
 * rank among all currently connected peers.
 * Used for game-over authority: only lowest rank sends MSG_GAME_OVER.
 * Returns TRUE if no peers are connected (single player fallback).
 */
int Net_IsLowestRankConnected(void)
{
    int count, i;
    short localRank, peerRank;
    PT_Peer *peer;

    if (!gPTCtx) return TRUE;

    localRank = (short)PT_GetPeerRank(gPTCtx, NULL);

    count = PT_GetPeerCount(gPTCtx);
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) == PT_PEER_CONNECTED) {
            peerRank = (short)PT_GetPeerRank(gPTCtx, peer);
            if (peerRank < localRank) {
                CLOG_DEBUG("Not authority: peer rank %d < local %d",
                           peerRank, localRank);
                return FALSE;
            }
        }
    }

    CLOG_DEBUG("Authority: local rank %d is lowest", localRank);
    return TRUE;
}

int Net_HasVersionMismatch(void)
{
    return gVersionMismatch;
}

void Net_ResetVersionMismatch(void)
{
    gVersionMismatch = FALSE;
}

/*
 * Net_ComputeLocalPlayerID -- Assign player IDs by sorting IP addresses
 *
 * Uses PT_GetPeerRank() for deterministic IP-sort ranking.
 * Lowest IP = player 0. Identical result across all clients.
 * Source: contracts/network-protocol.md
 */
short Net_ComputeLocalPlayerID(void)
{
    int count, i;
    PT_Peer *peer;
    short localID;

    if (!gPTCtx) return 0;

    localID = (short)PT_GetPeerRank(gPTCtx, NULL);
    CLOG_INFO("Local IP: %s -> player %d",
              PT_LocalAddress(gPTCtx), localID);

    /* Assign peer pointers to player slots */
    count = PT_GetPeerCount(gPTCtx);
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) == PT_PEER_CONNECTED) {
            int pid = PT_GetPeerRank(gPTCtx, peer);
            if (pid >= 0 && pid < MAX_PLAYERS) {
                gGame.players[pid].peer = peer;
                gGame.players[pid].active = TRUE;
                strncpy(gGame.players[pid].name, PT_PeerName(peer), PLAYER_NAME_MAX);
                gGame.players[pid].name[PLAYER_NAME_MAX] = '\0';
                CLOG_INFO("Peer %s -> player %d",
                          PT_PeerAddress(peer), pid);
            }
        }
    }

    return localID;
}
