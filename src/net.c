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
#include "netcoord.h"
#include "net_wire.h"
#include <peertalk.h>
#include <clog.h>
#include <string.h>

static PT_Context *gPTCtx = NULL;
static short gExpectedPlayers = 0;
static int gVersionMismatch = FALSE;

/* Join-in-progress map sync (011-macosx-sdl2, Phase 2) */
static PT_Peer *Net_GetPeerByRank(int rank);
static void Net_SendMapStateTo(PT_Peer *peer);

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
                /* KI-006: snap the interpolation target onto the current
                 * position. The reactivation heuristic in Game_Update
                 * revives any inactive remote whose target diverges from
                 * its pixel position -- a quit peer caught mid-interpolation
                 * would otherwise be resurrected every frame, so the
                 * survivor's alive-count never drops and the game never
                 * ends. A genuine rejoin sends fresh positions, which move
                 * the target again and reactivate normally. */
                gGame.players[i].targetPixelX = gGame.players[i].pixelX;
                gGame.players[i].targetPixelY = gGame.players[i].pixelY;
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
    short localPX, localPY;
    short ts = gGame.tileSize;
    (void)peer;
    (void)user_data;

    if (len < NETWIRE_POSITION_LEN) return;
    /* Decode the big-endian wire form into an aligned local. net_wire reads
     * the multi-byte fields byte-by-byte, so there is no unaligned short
     * access (PeerTalk may deliver data at odd addresses, which would fault
     * a 68000) and the byte order is explicit rather than host-dependent —
     * a little-endian client (the Intel/Apple-Silicon .app slice) reads the
     * same positions as the big-endian classic Macs. (011 D3) */
    NetWire_UnpackPosition((const unsigned char *)data, &msg.playerID,
                           &msg.facing, &msg.pixelX, &msg.pixelY);

    if (msg.playerID < MAX_PLAYERS &&
        msg.playerID != (unsigned char)gGame.localPlayerID) {
        /* Join-in-progress: a position from a slot we are not yet tracking
         * means that player is in the game, filling a free spawn corner.
         * Seat it (Player_Init activates it and places it at its corner) and
         * grow the roster. Four corners => up to MAX_PLAYERS; a player can
         * join a game already running. This gates on the slot being inactive,
         * so it never disturbs players already in the game, and it only fires
         * in-game so lobby position chatter is unaffected. The KI-006 target
         * snapping still holds: Player_Init sets target == pixel. (011) */
        if (gGame.currentScreen == SCREEN_GAME &&
            !gGame.players[msg.playerID].active) {
            if (msg.playerID >= (unsigned char)gGame.numPlayers) {
                gGame.numPlayers = (short)(msg.playerID + 1);
            }
            Player_Init((short)msg.playerID,
                        TileMap_GetSpawnCol((short)msg.playerID),
                        TileMap_GetSpawnRow((short)msg.playerID));
            CLOG_INFO("P%d joined game in progress (numPlayers now %d)",
                      msg.playerID, gGame.numPlayers);

            /* Send our current (in-progress) map to the joiner so it inherits
             * already-destroyed blocks. Directed to the joiner only, so a
             * fresh map can never overwrite an existing player's. Every
             * seating peer sends; the joiner applies the first that differs
             * (Phase 2). */
            {
                PT_Peer *jp = Net_GetPeerByRank((int)msg.playerID);
                if (jp != NULL) Net_SendMapStateTo(jp);
            }
        }

        /* Convert tile-independent network coords back to local pixel coords.
         * Network coords use 256 units per tile, so multiply by local tileSize
         * and divide by 256 to get pixel position in our coordinate space. */
        localPX = NetCoord_ToLocal(msg.pixelX, ts);
        localPY = NetCoord_ToLocal(msg.pixelY, ts);

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

    if (gGame.currentScreen != SCREEN_GAME) return;
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

    if (gGame.currentScreen != SCREEN_GAME) return;
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

    if (gGame.currentScreen != SCREEN_GAME) return;
    if (len < sizeof(MsgBlockDestroyed)) return;
    msg = (const MsgBlockDestroyed *)data;

    /* Skip if tile already destroyed (duplicate from near-simultaneous
     * fuse expiry on multiple machines sending the same explosion) */
    if (TileMap_GetTile((short)msg->gridCol, (short)msg->gridRow) == TILE_FLOOR)
        return;

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

    if (gGame.currentScreen != SCREEN_GAME) return;
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
    gGame.gameOverTimeoutStart = TickCount();

    /* Cancel failsafe timer if active — authority's message arrived (005) */
    gGame.localGameOverDetected = FALSE;
    gGame.gameOverFailsafeStart = 0;
}

/*
 * on_map_state -- Apply a tilemap snapshot from an existing player (Phase 2).
 *
 * A late joiner receives this from each peer already in the game and applies
 * the first that actually differs from its (fresh) map, so it inherits blocks
 * destroyed before it joined. All-byte payload: read directly, no alignment
 * or byte-swap concerns. Only acted on in-game.
 */
static void on_map_state(PT_Peer *peer, const void *data, size_t len,
                         void *user_data)
{
    const MsgMapState *msg;
    short cols, rows;
    (void)peer;
    (void)user_data;

    if (gGame.currentScreen != SCREEN_GAME) return;
    if (len < 2) return;
    msg = (const MsgMapState *)data;
    cols = (short)msg->cols;
    rows = (short)msg->rows;
    if (cols < 1 || rows < 1) return;
    if (len < (size_t)(2 + (long)cols * rows)) return;   /* truncated */

    if (TileMap_SetState(cols, rows, msg->tiles)) {
        Renderer_RequestRebuildBackground();
        CLOG_INFO("Applied in-progress map snapshot (%dx%d)", cols, rows);
    }
}

/*
 * Net_GetPeerByRank -- Find the connected peer occupying a given rank/slot.
 * Used to direct a map snapshot at a specific late joiner.
 */
static PT_Peer *Net_GetPeerByRank(int rank)
{
    int count, i;

    if (!gPTCtx) return NULL;
    count = PT_GetPeerCount(gPTCtx);
    for (i = 0; i < count; i++) {
        PT_Peer *p = PT_GetPeer(gPTCtx, i);
        if (p != NULL &&
            PT_GetPeerState(p) == PT_PEER_CONNECTED &&
            PT_GetPeerRank(gPTCtx, p) == rank) {
            return p;
        }
    }
    return NULL;
}

/*
 * Net_SendMapStateTo -- Send our current tilemap to one peer (the late joiner).
 * Static buffer (not stack) to keep the 68k main stack small.
 */
static void Net_SendMapStateTo(PT_Peer *peer)
{
    static MsgMapState msg;
    TileMap *map;
    short cols, rows, r, c;
    long len;

    if (!gPTCtx || peer == NULL) return;
    map = TileMap_Get();
    cols = map->cols;
    rows = map->rows;
    if (cols < 1 || rows < 1 || cols > MAX_GRID_COLS || rows > MAX_GRID_ROWS) return;

    msg.cols = (unsigned char)cols;
    msg.rows = (unsigned char)rows;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            msg.tiles[r * cols + c] = map->tiles[r][c];
        }
    }

    len = 2 + (long)cols * rows;
    PT_Send(gPTCtx, peer, MSG_MAP_STATE, &msg, (size_t)len);
    CLOG_INFO("Sent map snapshot (%dx%d, %ld bytes) to joiner", cols, rows, len);
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
    PT_OnMessage(gPTCtx, MSG_MAP_STATE,       on_map_state, NULL);

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
        /* Only dial peers we are the designated initiator for (lower IP dials,
         * higher IP listens). PT_ShouldInitiate makes each pair connect from
         * exactly one side, so two machines never dial each other at once --
         * this is what eliminates the simultaneous-connect tiebreaker race that
         * left the MacTCP<->G5 link half-dead in the 011 cross-era game (a
         * player frozen at spawn, divergent maps). The higher-IP peer accepts
         * passively; PeerTalk's listener is always open. This makes the old
         * app-side mesh stagger (screen_lobby.c) redundant -- kept for now as a
         * harmless extra delay on higher ranks. */
        if (peer && PT_GetPeerState(peer) == PT_PEER_DISCOVERED &&
            PT_ShouldInitiate(gPTCtx, peer)) {
            PT_Connect(gPTCtx, peer);
            CLOG_INFO("Connecting to %s (initiator)", PT_PeerName(peer));
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
    unsigned char buf[NETWIRE_POSITION_LEN];
    short ts = gGame.tileSize;
    if (!gPTCtx) return;

    /* Two layers: netcoord.c converts to tile-independent coords (256 units =
     * 1 tile) so 16px-SE and 32px-PPC machines agree on positions, then
     * net_wire.c packs them big-endian so a little-endian client agrees too.
     * NetCoord_ToWire keeps the power-of-2 shift optimization (pixel*256/tile
     * as a shift, avoiding the 68k soft-divide); both are host unit tested. */
    NetWire_PackPosition((unsigned char)gGame.localPlayerID,
                         (unsigned char)facing,
                         NetCoord_ToWire(pixelX, ts),
                         NetCoord_ToWire(pixelY, ts), buf);

    CLOG_DEBUG("TX pos P%d px=(%d,%d) f=%d",
               gGame.localPlayerID, pixelX, pixelY, facing);
    PT_Broadcast(gPTCtx, MSG_POSITION, buf, NETWIRE_POSITION_LEN);
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

const char *Net_GetDiscoveredPeerName(int idx)
{
    int count, i, seen;
    PT_Peer *peer;
    if (!gPTCtx) return "";

    /* Index among the live (non-DISCONNECTED) peers, matching the filtered
     * Net_GetDiscoveredPeerCount so the lobby's count and name list agree. */
    count = PT_GetPeerCount(gPTCtx);
    seen = 0;
    for (i = 0; i < count; i++) {
        peer = PT_GetPeer(gPTCtx, i);
        if (peer && PT_GetPeerState(peer) != PT_PEER_DISCONNECTED) {
            if (seen == idx) return PT_PeerName(peer);
            seen++;
        }
    }
    return "";
}

const char *Net_GetDiscoveredPeerAddress(int idx)
{
    PT_Peer *peer;
    if (!gPTCtx) return "";

    peer = PT_GetPeer(gPTCtx, idx);
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

/*
 * Net_GetLocalRank -- Return local peer rank without side effects.
 * Safe to call during mesh formation (does not assign peer pointers).
 */
short Net_GetLocalRank(void)
{
    if (!gPTCtx) return 0;
    return (short)PT_GetPeerRank(gPTCtx, NULL);
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
