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
static int gVersionMismatch = FALSE;

/* ---- Callbacks ---- */

static void on_peer_discovered(PT_Peer *peer, void *user_data)
{
    (void)user_data;
    CLOG_INFO("Peer discovered: %s (%s)",
              PT_PeerName(peer), PT_PeerAddress(peer));
    (void)peer;
}

static void on_peer_lost(PT_Peer *peer, void *user_data)
{
    (void)user_data;
    CLOG_INFO("Peer lost: %s", PT_PeerName(peer));
    (void)peer;
}

static void on_connected(PT_Peer *peer, void *user_data)
{
    (void)user_data;
    CLOG_INFO("Connected to: %s", PT_PeerName(peer));
    (void)peer;
}

static void on_disconnected(PT_Peer *peer, PT_DisconnectReason reason,
                            void *user_data)
{
    short i;
    (void)user_data;
    (void)reason;
    CLOG_INFO("Disconnected from: %s (reason=%d, screen=%d)",
              PT_PeerName(peer), reason, gGame.currentScreen);

    /* Always clear stale peer pointers to avoid dangling references.
     * Only do gameplay cleanup (dirty tiles, deactivation) in-game.
     * During mesh formation, disconnects are normal (tiebreaker). */
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (gGame.players[i].peer == peer) {
            if (gGame.currentScreen == SCREEN_GAME && gGame.players[i].active) {
                /* Mark tiles dirty BEFORE deactivation (T028) */
                Player_MarkDirtyTiles(i);
                gGame.players[i].active = FALSE;
                CLOG_INFO("P%d marked inactive (disconnect)", i);
            }
            gGame.players[i].peer = NULL;
            CLOG_INFO("P%d peer pointer cleared", i);
            break;
        }
    }
}

static void on_error(PT_Peer *peer, PT_Status error,
                     const char *description, void *user_data)
{
    (void)peer;
    (void)user_data;
    (void)error;
    (void)description;
    CLOG_ERR("PeerTalk error %d: %s", error,
             description ? description : "(null)");
}

static void on_position(PT_Peer *peer, const void *data, size_t len,
                        void *user_data)
{
    MsgPosition msg;
    Player *p;
    short localPX, localPY;
    short ts = gGame.tileSize;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgPosition)) return;
    /* Copy to aligned local — PeerTalk may deliver data at odd addresses,
     * causing 68000 address errors when accessing short fields directly */
    memcpy(&msg, data, sizeof(MsgPosition));

    if (msg.playerID < MAX_PLAYERS &&
        msg.playerID < (unsigned char)gGame.numPlayers &&
        msg.playerID != (unsigned char)gGame.localPlayerID) {
        p = &gGame.players[msg.playerID];

        /* Reactivate player if we receive position data from them.
         * Handles transient disconnect/reconnect during gameplay. */
        if (!p->active && gGame.currentScreen == SCREEN_GAME) {
            p->active = TRUE;
            p->alive = TRUE;
            p->deathTimer = 0;
            CLOG_INFO("P%d reactivated via position msg", msg.playerID);
        }

        /* Convert tile-independent network coords back to local pixel coords.
         * Network coords use 256 units per tile, so multiply by local tileSize
         * and divide by 256 to get pixel position in our coordinate space. */
        localPX = (short)(((long)msg.pixelX * ts) >> 8);
        localPY = (short)(((long)msg.pixelY * ts) >> 8);

        /* Mark old position dirty (multi-tile aware) */
        Player_MarkDirtyTiles(msg.playerID);
        /* Set interpolation target (not direct position) */
        Player_SetPosition(msg.playerID,
                          localPX, localPY,
                          (short)msg.facing);
        /* New position marked dirty in next frame update */
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

    if (msg->playerID >= (unsigned char)gGame.numPlayers) return;

    CLOG_INFO("RX bomb placed P%d (%d,%d) range=%d",
              msg->playerID, msg->gridCol, msg->gridRow, msg->range);
    Renderer_MarkDirty((short)msg->gridCol, (short)msg->gridRow);
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
    CLOG_INFO("RX bomb explode (%d,%d)", msg->gridCol, msg->gridRow);
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

    CLOG_INFO("RX block destroyed (%d,%d)", msg->gridCol, msg->gridRow);
    TileMap_SetTile((short)msg->gridCol, (short)msg->gridRow, TILE_FLOOR);
    Renderer_MarkDirty((short)msg->gridCol, (short)msg->gridRow);
    Renderer_RequestRebuildBackground();
}

static void on_player_killed(PT_Peer *peer, const void *data, size_t len,
                             void *user_data)
{
    const MsgPlayerKilled *msg;
    (void)peer;
    (void)user_data;

    if (len < sizeof(MsgPlayerKilled)) return;
    msg = (const MsgPlayerKilled *)data;

    if (msg->playerID < MAX_PLAYERS &&
        msg->playerID < (unsigned char)gGame.numPlayers) {
        gGame.players[msg->playerID].deathTimer = DEATH_FLASH_TICKS;
        CLOG_INFO("RX player killed P%d by P%d", msg->playerID, msg->killerID);
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

    /* Version check (T023) */
    if (msg->version != BT_PROTOCOL_VERSION) {
        CLOG_WARN("Version mismatch: got %d, expected %d",
                   msg->version, BT_PROTOCOL_VERSION);
        gVersionMismatch = TRUE;
        return;
    }

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

    CLOG_INFO("Game over received: winner=%d (screen=%d)", msg->winnerID,
              gGame.currentScreen);

    /* Ignore game-over if we're not in-game (e.g. already transitioned to lobby).
     * Duplicate MSG_GAME_OVER can arrive after transition due to network latency. */
    if (gGame.currentScreen != SCREEN_GAME) return;

    /* Bounds check winnerID (T024) */
    if (msg->winnerID < MAX_PLAYERS) {
        CLOG_INFO("Winner is P%d", msg->winnerID);
    } else {
        CLOG_INFO("No winner (draw or invalid ID: 0x%02X)", msg->winnerID);
    }

    /* Defer transition: let death animations finish before going to lobby.
     * Game_Update will handle the actual transition. */
    gGame.pendingGameOver = TRUE;
    gGame.pendingWinner = msg->winnerID;
    gGame.gameOverTimeout = 180; /* 3 second safety timeout */
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

void Net_DisconnectAllPeers(void)
{
    if (!gPTCtx) return;
    PT_DisconnectAll(gPTCtx);
}

void Net_SendPosition(short pixelX, short pixelY, short facing)
{
    MsgPosition msg;
    short ts = gGame.tileSize;
    /* tileSize is always power of 2 (16 or 32): use shift instead of
     * 32-bit division. 16: <<8/16 = <<4.  32: <<8/32 = <<3.
     * Saves ~200 cycles per send on 68k (avoids __divsi3). */
    short shift = (ts == 16) ? 4 : 3;
    if (!gPTCtx) return;

    msg.playerID = (unsigned char)gGame.localPlayerID;
    msg.facing = (unsigned char)facing;
    /* Convert to tile-independent network coords (256 units = 1 tile).
     * This allows machines with different tile sizes (16px SE vs 32px PPC)
     * to agree on player positions. */
    msg.pixelX = (short)((long)pixelX << shift);
    msg.pixelY = (short)((long)pixelY << shift);
    msg.pad[0] = 0;
    msg.pad[1] = 0;

    CLOG_DEBUG("TX pos P%d px=(%d,%d) f=%d", msg.playerID, pixelX, pixelY, facing);
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

    CLOG_INFO("TX bomb placed (%d,%d) range=%d", col, row, range);
    PT_Broadcast(gPTCtx, MSG_BOMB_PLACED, &msg, sizeof(msg));
}

void Net_SendBombExplode(short col, short row, short range)
{
    MsgBombExplode msg;
    if (!gPTCtx) return;

    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;
    msg.range = (unsigned char)range;

    CLOG_INFO("TX bomb explode (%d,%d) range=%d", col, row, range);
    PT_Broadcast(gPTCtx, MSG_BOMB_EXPLODE, &msg, sizeof(msg));
}

void Net_SendBlockDestroyed(short col, short row)
{
    MsgBlockDestroyed msg;
    if (!gPTCtx) return;

    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;

    CLOG_INFO("TX block destroyed (%d,%d)", col, row);
    PT_Broadcast(gPTCtx, MSG_BLOCK_DESTROYED, &msg, sizeof(msg));
}

void Net_SendPlayerKilled(unsigned char playerID, unsigned char killerID)
{
    MsgPlayerKilled msg;
    if (!gPTCtx) return;

    msg.playerID = playerID;
    msg.killerID = killerID;

    CLOG_INFO("TX player killed: P%d by P%d", playerID, killerID);
    PT_Broadcast(gPTCtx, MSG_PLAYER_KILLED, &msg, sizeof(msg));
}

void Net_SendGameStart(unsigned char numPlayers)
{
    MsgGameStart msg;
    if (!gPTCtx) return;

    msg.numPlayers = numPlayers;
    msg.version = BT_PROTOCOL_VERSION;

    gExpectedPlayers = (short)numPlayers;
    CLOG_INFO("TX game start: %d players, proto v%d", numPlayers, BT_PROTOCOL_VERSION);
    PT_Broadcast(gPTCtx, MSG_GAME_START, &msg, sizeof(msg));
}

void Net_SendGameOver(unsigned char winnerID)
{
    MsgGameOver msg;
    if (!gPTCtx) return;

    msg.winnerID = winnerID;
    msg.pad = 0;

    CLOG_INFO("TX game over: winner=%d", winnerID);
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
                strncpy(gGame.players[pid].name, PT_PeerName(peer), 31);
                gGame.players[pid].name[31] = '\0';
                CLOG_INFO("Peer %s -> player %d",
                          PT_PeerAddress(peer), pid);
            }
        }
    }

    return localID;
}
