/*
 * net.c -- PeerTalk wrapper
 *
 * Thin layer keeping PeerTalk includes out of game headers.
 * Source: contracts/network-protocol.md
 */

#include "net.h"
#include "game.h"
#include "screens.h"
#include "player.h"
#include "bomb.h"
#include "tilemap.h"
#include "renderer.h"
#include <peertalk.h>
#include <clog.h>
#include <string.h>

static PT_Context *gPTCtx = NULL;
static short gExpectedPlayers = 0;

/* ---- Callbacks ---- */

static void on_peer_discovered(PT_Peer *peer, void *user_data)
{
    (void)user_data;
    CLOG_INFO("Peer discovered: %s (%s)",
              PT_PeerName(peer), PT_PeerAddress(peer));
}

static void on_peer_lost(PT_Peer *peer, void *user_data)
{
    (void)user_data;
    CLOG_INFO("Peer lost: %s", PT_PeerName(peer));
}

static void on_connected(PT_Peer *peer, void *user_data)
{
    (void)user_data;
    CLOG_INFO("Connected to: %s", PT_PeerName(peer));
}

static void on_disconnected(PT_Peer *peer, PT_DisconnectReason reason,
                            void *user_data)
{
    short i;
    (void)user_data;
    CLOG_INFO("Disconnected from: %s (reason=%d)", PT_PeerName(peer), reason);

    /* Only mark players inactive if we're in-game.
     * During mesh formation, disconnects are normal (tiebreaker). */
    if (gGame.currentScreen != SCREEN_GAME) return;

    for (i = 0; i < MAX_PLAYERS; i++) {
        if (gGame.players[i].active && gGame.players[i].peer == peer) {
            gGame.players[i].active = FALSE;
            CLOG_INFO("Player %d marked inactive (disconnect)", i);
            break;
        }
    }
}

static void on_error(PT_Peer *peer, PT_Status error,
                     const char *description, void *user_data)
{
    (void)peer;
    (void)user_data;
    CLOG_ERR("PeerTalk error %d: %s", error,
             description ? description : "(null)");
}

static void on_position(PT_Peer *peer, const void *data, size_t len,
                        void *user_data)
{
    const MsgPosition *msg;
    Player *p;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgPosition)) return;
    msg = (const MsgPosition *)data;

    if (msg->playerID < MAX_PLAYERS &&
        msg->playerID != (unsigned char)gGame.localPlayerID) {
        p = &gGame.players[msg->playerID];

        /* Reactivate player if we receive position data from them.
         * Handles transient disconnect/reconnect during gameplay. */
        if (!p->active && gGame.currentScreen == SCREEN_GAME) {
            p->active = TRUE;
            p->alive = TRUE;
            p->deathTimer = 0;
            CLOG_INFO("Player %d reactivated via position msg", msg->playerID);
        }

        Player_SetPosition(msg->playerID,
                          (short)msg->gridCol, (short)msg->gridRow,
                          (short)msg->facing);
    }
}

static void on_bomb_placed(PT_Peer *peer, const void *data, size_t len,
                           void *user_data)
{
    const MsgBombPlaced *msg;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgBombPlaced)) return;
    msg = (const MsgBombPlaced *)data;

    Bomb_PlaceAt((short)msg->gridCol, (short)msg->gridRow,
                 (short)msg->range, msg->playerID);
}

static void on_bomb_explode(PT_Peer *peer, const void *data, size_t len,
                            void *user_data)
{
    const MsgBombExplode *msg;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgBombExplode)) return;
    msg = (const MsgBombExplode *)data;

    /* Force-explode the bomb if it hasn't exploded locally yet.
     * This keeps slow machines in sync with fast ones. */
    Bomb_ForceExplodeAt((short)msg->gridCol, (short)msg->gridRow);
}

static void on_block_destroyed(PT_Peer *peer, const void *data, size_t len,
                               void *user_data)
{
    const MsgBlockDestroyed *msg;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgBlockDestroyed)) return;
    msg = (const MsgBlockDestroyed *)data;

    TileMap_SetTile((short)msg->gridCol, (short)msg->gridRow, TILE_FLOOR);
    Renderer_RebuildBackground();
}

static void on_player_killed(PT_Peer *peer, const void *data, size_t len,
                             void *user_data)
{
    const MsgPlayerKilled *msg;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgPlayerKilled)) return;
    msg = (const MsgPlayerKilled *)data;

    if (msg->playerID < MAX_PLAYERS) {
        gGame.players[msg->playerID].deathTimer = DEATH_FLASH_TICKS;
        CLOG_INFO("Player %d killed by %d (remote)", msg->playerID, msg->killerID);
    }
}

static void on_game_start(PT_Peer *peer, const void *data, size_t len,
                          void *user_data)
{
    const MsgGameStart *msg;
    (void)peer;
    (void)user_data;

    /* Ignore duplicate MSG_GAME_START */
    if (gGame.gameStartReceived) return;

    if (len < sizeof(MsgGameStart)) return;
    msg = (const MsgGameStart *)data;

    gGame.gameStartReceived = TRUE;
    gExpectedPlayers = (short)msg->numPlayers;

    CLOG_INFO("Game start received, expecting %d players", gExpectedPlayers);

    /* Connect to any discovered peers we haven't connected to yet (mesh) */
    Net_ConnectToAllPeers();
}

static void on_game_over(PT_Peer *peer, const void *data, size_t len,
                         void *user_data)
{
    const MsgGameOver *msg;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgGameOver)) return;
    msg = (const MsgGameOver *)data;

    CLOG_INFO("Game over! Winner: %d", msg->winnerID);
    gGame.gameRunning = FALSE;

    /* Transition back to lobby, preserving connections */
    gGame.gameStartReceived = FALSE;
    Screens_TransitionTo(SCREEN_LOBBY);
}

/* ---- UDP Log Broadcast ---- */

#define CLOG_UDP_PORT 7355

static char gLogPrefix[20];
static int  gLogPrefixLen = 0;

static void udp_log_sink(const char *msg, int len, void *user_data)
{
    static char udp_buf[220];
    int total;

    (void)user_data;
    if (!gPTCtx || len <= 0) return;

    /* Prefix with IP, add newline */
    memcpy(udp_buf, gLogPrefix, (size_t)gLogPrefixLen);
    total = gLogPrefixLen;
    if (total + len > 216) len = 216 - total;
    memcpy(udp_buf + total, msg, (size_t)len);
    total += len;
    udp_buf[total++] = '\n';

    PT_SendUDPBroadcast(gPTCtx, CLOG_UDP_PORT, udp_buf, (size_t)total);
}

/* ---- Public API ---- */

void Net_Init(const char *playerName)
{
    PT_Status status;

    status = PT_Init(&gPTCtx, playerName);
    if (status != PT_OK) {
        CLOG_ERR("PT_Init failed: %d", status);
        return;
    }

    /* Enable UDP broadcast logging (survives crashes) */
    {
        const char *lip = PT_LocalAddress(gPTCtx);
        const char *s = lip;
        int i = 0;
        gLogPrefix[i++] = '[';
        while (*s && i < 17) {
            gLogPrefix[i++] = *s++;
        }
        gLogPrefix[i++] = ']';
        gLogPrefix[i++] = ' ';
        gLogPrefixLen = i;
    }
    clog_set_network_sink(udp_log_sink, NULL);

    /* Register message types */
    PT_RegisterMessage(gPTCtx, MSG_POSITION,        PT_FAST);
    PT_RegisterMessage(gPTCtx, MSG_BOMB_PLACED,     PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_BOMB_EXPLODE,    PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_BLOCK_DESTROYED, PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_PLAYER_KILLED,   PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_GAME_START,      PT_RELIABLE);
    PT_RegisterMessage(gPTCtx, MSG_GAME_OVER,       PT_RELIABLE);

    /* Register callbacks */
    PT_OnPeerDiscovered(gPTCtx, on_peer_discovered, NULL);
    PT_OnPeerLost(gPTCtx, on_peer_lost, NULL);
    PT_OnConnected(gPTCtx, on_connected, NULL);
    PT_OnDisconnected(gPTCtx, on_disconnected, NULL);
    PT_OnError(gPTCtx, on_error, NULL);

    PT_OnMessage(gPTCtx, MSG_POSITION,        on_position, NULL);
    PT_OnMessage(gPTCtx, MSG_BOMB_PLACED,     on_bomb_placed, NULL);
    PT_OnMessage(gPTCtx, MSG_BOMB_EXPLODE,    on_bomb_explode, NULL);
    PT_OnMessage(gPTCtx, MSG_BLOCK_DESTROYED, on_block_destroyed, NULL);
    PT_OnMessage(gPTCtx, MSG_PLAYER_KILLED,   on_player_killed, NULL);
    PT_OnMessage(gPTCtx, MSG_GAME_START,      on_game_start, NULL);
    PT_OnMessage(gPTCtx, MSG_GAME_OVER,       on_game_over, NULL);

    CLOG_INFO("Net initialized");
}

void Net_Shutdown(void)
{
    if (gPTCtx) {
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
        CLOG_INFO("Discovery started");
    }
}

void Net_StopDiscovery(void)
{
    if (gPTCtx) {
        PT_StopDiscovery(gPTCtx);
    }
}

void Net_ConnectToAllPeers(void)
{
    int count, i;
    PT_Peer *peer;

    if (!gPTCtx) return;

    count = PT_GetPeerCount(gPTCtx);
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) == PT_PEER_DISCOVERED) {
            PT_Connect(gPTCtx, peer);
            CLOG_INFO("Connecting to %s", PT_PeerName(peer));
        }
    }
}

void Net_SendPosition(short col, short row, short facing)
{
    MsgPosition msg;
    if (!gPTCtx) return;

    msg.playerID = (unsigned char)gGame.localPlayerID;
    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;
    msg.facing = (unsigned char)facing;
    msg.animFrame = 0;

    PT_Broadcast(gPTCtx, MSG_POSITION, &msg, sizeof(msg));
}

void Net_SendBombPlaced(short col, short row, short range)
{
    MsgBombPlaced msg;
    if (!gPTCtx) return;

    msg.playerID = (unsigned char)gGame.localPlayerID;
    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;
    msg.range = (unsigned char)range;
    msg.fuseTicks = (unsigned char)BOMB_FUSE_TICKS;

    PT_Broadcast(gPTCtx, MSG_BOMB_PLACED, &msg, sizeof(msg));
}

void Net_SendBombExplode(short col, short row, short range)
{
    MsgBombExplode msg;
    if (!gPTCtx) return;

    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;
    msg.range = (unsigned char)range;

    PT_Broadcast(gPTCtx, MSG_BOMB_EXPLODE, &msg, sizeof(msg));
}

void Net_SendBlockDestroyed(short col, short row)
{
    MsgBlockDestroyed msg;
    if (!gPTCtx) return;

    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;

    PT_Broadcast(gPTCtx, MSG_BLOCK_DESTROYED, &msg, sizeof(msg));
}

void Net_SendPlayerKilled(unsigned char playerID, unsigned char killerID)
{
    MsgPlayerKilled msg;
    if (!gPTCtx) return;

    msg.playerID = playerID;
    msg.killerID = killerID;

    PT_Broadcast(gPTCtx, MSG_PLAYER_KILLED, &msg, sizeof(msg));
}

void Net_SendGameStart(unsigned char numPlayers)
{
    MsgGameStart msg;
    if (!gPTCtx) return;

    msg.numPlayers = numPlayers;
    msg.pad = 0;

    gExpectedPlayers = (short)numPlayers;
    PT_Broadcast(gPTCtx, MSG_GAME_START, &msg, sizeof(msg));
}

void Net_SendGameOver(unsigned char winnerID)
{
    MsgGameOver msg;
    if (!gPTCtx) return;

    msg.winnerID = winnerID;
    msg.pad = 0;

    PT_Broadcast(gPTCtx, MSG_GAME_OVER, &msg, sizeof(msg));
}

int Net_GetDiscoveredPeerCount(void)
{
    int count, i, discovered;
    PT_Peer *peer;

    if (!gPTCtx) return 0;

    count = PT_GetPeerCount(gPTCtx);
    discovered = 0;
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer) discovered++;
    }
    return discovered;
}

const char *Net_GetDiscoveredPeerName(int index)
{
    PT_Peer *peer;
    if (!gPTCtx) return "";

    peer = PT_GetPeer(gPTCtx, index);
    if (peer) return PT_PeerName(peer);
    return "";
}

const char *Net_GetDiscoveredPeerAddress(int index)
{
    PT_Peer *peer;
    if (!gPTCtx) return "";

    peer = PT_GetPeer(gPTCtx, index);
    if (peer) return PT_PeerAddress(peer);
    return "";
}

int Net_IsConnected(void)
{
    int count, i;
    PT_Peer *peer;

    if (!gPTCtx) return FALSE;

    count = PT_GetPeerCount(gPTCtx);
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) == PT_PEER_CONNECTED) {
            return TRUE;
        }
    }
    return FALSE;
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
 * ip_to_ulong -- Convert dotted-quad string to unsigned long for comparison
 *
 * Returns 0 on invalid input. Result is a comparable value (NOT network order).
 */
static unsigned long ip_to_ulong(const char *ip)
{
    unsigned long result = 0;
    unsigned long octet = 0;
    int dots = 0;

    if (!ip || !*ip) return 0;

    while (*ip) {
        if (*ip >= '0' && *ip <= '9') {
            octet = octet * 10 + (unsigned long)(*ip - '0');
        } else if (*ip == '.') {
            result = (result << 8) | (octet & 0xFF);
            octet = 0;
            dots++;
        } else {
            return 0;
        }
        ip++;
    }
    result = (result << 8) | (octet & 0xFF);

    if (dots != 3) return 0;
    return result;
}

/*
 * Net_ComputeLocalPlayerID -- Assign player IDs by sorting IP addresses
 *
 * All connected peer IPs + local IP sorted ascending.
 * Lowest IP = player 0. This is deterministic across all clients.
 * Source: contracts/network-protocol.md
 */
short Net_ComputeLocalPlayerID(void)
{
    int count, i;
    PT_Peer *peer;
    short localID = 0;
    unsigned long localIP;
    const char *localAddr;

    /* Collected connected peers (max MAX_PLAYERS-1 peers + local) */
    PT_Peer *connPeers[MAX_PLAYERS];
    unsigned long connIPs[MAX_PLAYERS];
    int numConn = 0;

    if (!gPTCtx) return 0;

    localAddr = PT_LocalAddress(gPTCtx);
    localIP = ip_to_ulong(localAddr);

    CLOG_INFO("Local IP: %s (0x%08lX)", localAddr, localIP);

    /* Gather connected peers and their IPs */
    count = PT_GetPeerCount(gPTCtx);
    for (i = 0; i < count && numConn < MAX_PLAYERS - 1; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) == PT_PEER_CONNECTED) {
            connPeers[numConn] = peer;
            connIPs[numConn] = ip_to_ulong(PT_PeerAddress(peer));
            CLOG_INFO("Peer %d IP: %s (0x%08lX)",
                      numConn, PT_PeerAddress(peer), connIPs[numConn]);
            numConn++;
        }
    }

    /* Count how many connected peers have a lower IP than us */
    localID = 0;
    for (i = 0; i < numConn; i++) {
        if (connIPs[i] < localIP) {
            localID++;
        }
    }

    CLOG_INFO("Player ID assignment: local=%d (of %d total)",
              localID, numConn + 1);

    /* Assign peer pointers to player slots */
    for (i = 0; i < numConn; i++) {
        /* Peer's player ID: count how many IPs are below this peer's IP */
        short pid = 0;
        int j;
        /* Count local IP if below this peer */
        if (localIP < connIPs[i]) pid++;
        /* Count other peers below this one */
        for (j = 0; j < numConn; j++) {
            if (j != i && connIPs[j] < connIPs[i]) pid++;
        }

        if (pid < MAX_PLAYERS) {
            gGame.players[pid].peer = connPeers[i];
            gGame.players[pid].active = TRUE;
            strncpy(gGame.players[pid].name, PT_PeerName(connPeers[i]), 31);
            gGame.players[pid].name[31] = '\0';
            CLOG_INFO("Peer %s -> player %d",
                      PT_PeerAddress(connPeers[i]), pid);
        }
    }

    return localID;
}
