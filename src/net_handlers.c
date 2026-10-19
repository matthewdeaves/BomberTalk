/*
 * net_handlers.c -- PeerTalk event and message callbacks.
 *
 * Everything that reacts to traffic from other peers: sender identity
 * (KI-009), peer lifecycle callbacks, and the per-message handlers that
 * validate and apply MSG_* payloads to game state. The acceptance rules are
 * pure and live in net_auth.c / net_wire.c; outgoing traffic is net_send.c;
 * PeerTalk lifecycle, discovery and roster queries are net.c.
 * Split out of net.c (structure refactor); behaviour unchanged.
 */

#include "net.h"
#include "net_internal.h"
#include "game.h"
#include "screens.h"
#include "player.h"
#include "bomb.h"
#include "tilemap.h"
#include "renderer.h"
#include "netcoord.h"
#include "net_wire.h"
#include "net_auth.h"
#include <peertalk.h>
#include <clog.h>
#include <string.h>

/* ---- Sender identity (KI-009) ----
 * The claimed player in a message is untrusted; the sender is the peer the
 * transport delivered it from. Each peer is bound to a slot in
 * gGame.players[i].peer when the roster is seated (Net_ComputeLocalPlayerID,
 * or a late join below) and the binding is stored, not recomputed from the
 * live rank, so ranks shifting when someone joins or leaves cannot re-assign
 * who is who. Rules live in net_auth.c (host tested). */
static int Net_SenderSlot(const PT_Peer *peer)
{
    const void *peers[MAX_PLAYERS];
    short i;
    for (i = 0; i < MAX_PLAYERS; i++) peers[i] = gGame.players[i].peer;
    return NetAuth_SlotOfPeer(peers, MAX_PLAYERS, peer);
}

/*
 * Net_ResolveSender -- Sender slot for a per-player message, admitting a late
 * joiner (join-in-progress, 011) if the peer is not bound yet. An unbound
 * peer is admitted only into the free slot its live rank gives it, and only
 * if that is the slot it claims; otherwise it stays unseated and every
 * machine ignores it alike (KI-009 / KI-011). On admission the peer is bound,
 * the roster grows to cover its slot, a never-seated slot is spawned, and
 * our current map is sent to it so it inherits destroyed blocks.
 * Returns the sender's slot or NETAUTH_NO_SLOT.
 */
static int Net_ResolveSender(PT_Peer *peer, unsigned char claimed)
{
    int slot = Net_SenderSlot(peer);
    int rank;

    if (slot != NETAUTH_NO_SLOT) return slot;
    if (!Net_Ctx() || gGame.currentScreen != SCREEN_GAME) return NETAUTH_NO_SLOT;

    rank = PT_GetPeerRank(Net_Ctx(), peer);
    if (!NetAuth_AdmitLateJoiner((int)claimed, rank, gGame.localPlayerID,
                                 NetWire_ValidSlot((int)claimed) &&
                                 gGame.players[claimed].peer != NULL)) {
        CLOG_DEBUG("Unseated peer claiming P%d (rank %d) ignored",
                   claimed, rank);
        return NETAUTH_NO_SLOT;
    }

    gGame.players[claimed].peer = peer;
    strncpy(gGame.players[claimed].name, PT_PeerName(peer), PLAYER_NAME_MAX);
    gGame.players[claimed].name[PLAYER_NAME_MAX] = '\0';
    gGame.numPlayers = (short)NetWire_SeatRoster(gGame.numPlayers, (int)claimed);

    /* A slot we are not yet tracking means that player is filling a free
     * spawn corner. Player_Init activates it and places it at its corner;
     * it only touches never-seated slots, so players already in the game are
     * undisturbed. The KI-006 target snapping still holds: Player_Init sets
     * target == pixel. */
    if (!gGame.players[claimed].active) {
        Player_Init((short)claimed,
                    TileMap_GetSpawnCol((short)claimed),
                    TileMap_GetSpawnRow((short)claimed));
        Net_SendMapStateTo(peer);
    }
    CLOG_INFO("P%d joined game in progress (numPlayers now %d)",
              claimed, gGame.numPlayers);
    return (int)claimed;
}

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

    /* Only in-game, and only about the sender itself (KI-009). */
    if (gGame.currentScreen != SCREEN_GAME) return;
    if (!NetAuth_Accept(NETAUTH_SELF, Net_ResolveSender(peer, msg.playerID),
                        msg.playerID)) {
        CLOG_DEBUG("Position for P%d from wrong sender dropped", msg.playerID);
        return;
    }

    {
        /* Convert tile-independent network coords back to local pixel coords.
         * Network coords use 256 units per tile, so multiply by local tileSize
         * and divide by 256 to get pixel position in our coordinate space,
         * clamped into the play area (the wire value is untrusted). */
        localPX = NetCoord_ToLocalClamped(msg.pixelX, ts, gGame.playWidth);
        localPY = NetCoord_ToLocalClamped(msg.pixelY, ts, gGame.playHeight);

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
    (void)user_data;

    if (gGame.currentScreen != SCREEN_GAME) return;
    if (len < sizeof(MsgBombPlaced)) return;
    msg = (const MsgBombPlaced *)data;

    if (!NetAuth_Accept(NETAUTH_SELF, Net_ResolveSender(peer, msg->playerID),
                        msg->playerID)) {
        CLOG_DEBUG("Bomb for P%d from wrong sender dropped", msg->playerID);
        return;
    }
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
    int owner = NETAUTH_NO_SLOT;
    short i;
    (void)user_data;

    if (gGame.currentScreen != SCREEN_GAME) return;
    if (len < sizeof(MsgBombExplode)) return;
    msg = (const MsgBombExplode *)data;

    /* Owner-authoritative (005): only the bomb's owner announces its fuse
     * expiry, so the sender must own the bomb at that cell. */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (gGame.bombs[i].active &&
            gGame.bombs[i].gridCol == msg->gridCol &&
            gGame.bombs[i].gridRow == msg->gridRow) {
            owner = (int)gGame.bombs[i].ownerID;
            break;
        }
    }
    if (!NetAuth_Accept(NETAUTH_OWNER, Net_SenderSlot(peer), owner)) {
        CLOG_DEBUG("Bomb explode (%d,%d) from non-owner dropped",
                   msg->gridCol, msg->gridRow);
        return;
    }

    /* Force-explode the bomb if it hasn't exploded locally yet.
     * This keeps slow machines in sync with fast ones. */
    CLOG_INFO("RX bomb explode (%d,%d)", msg->gridCol, msg->gridRow);
    Bomb_ForceExplodeAt((short)msg->gridCol, (short)msg->gridRow);
}

static void on_block_destroyed(PT_Peer *peer, const void *data, size_t len,
                               void *user_data)
{
    const MsgBlockDestroyed *msg;
    (void)user_data;

    if (gGame.currentScreen != SCREEN_GAME) return;
    if (len < sizeof(MsgBlockDestroyed)) return;
    msg = (const MsgBlockDestroyed *)data;

    if (!NetAuth_Accept(NETAUTH_SEATED, Net_SenderSlot(peer), 0)) return;

    /* Only an existing block can be destroyed. Skips duplicates (near-
     * simultaneous fuse expiry on several machines sending the same
     * explosion) and refuses to open walls/spawns or touch cells outside
     * the map. */
    if (!TileMap_DestroyBlock((short)msg->gridCol, (short)msg->gridRow))
        return;

    CLOG_INFO("RX block destroyed (%d,%d)", msg->gridCol, msg->gridRow);
    Renderer_MarkDirty((short)msg->gridCol, (short)msg->gridRow);
    Renderer_RequestRebuildBackground();
}

static void on_player_killed(PT_Peer *peer, const void *data, size_t len,
                             void *user_data)
{
    const MsgPlayerKilled *msg;
    (void)user_data;

    if (gGame.currentScreen != SCREEN_GAME) return;
    if (len < sizeof(MsgPlayerKilled)) return;
    msg = (const MsgPlayerKilled *)data;

    /* Each machine is authoritative for its own player's death only (see
     * Bomb_Update), so a death report must come from the victim itself. */
    if (!NetAuth_Accept(NETAUTH_SELF, Net_SenderSlot(peer), msg->playerID)) {
        CLOG_DEBUG("Kill of P%d from wrong sender dropped", msg->playerID);
        return;
    }
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

    /* Any player may press Start (no host concept), so there is no single
     * authorised sender -- but a launch announcement only means something in
     * the lobby. In-game or elsewhere it is dropped: a hostile or late
     * GAME_START cannot re-roster a running game (KI-009/KI-011). */
    if (gGame.currentScreen != SCREEN_LOBBY) return;

    /* Ignore duplicate MSG_GAME_START */
    if (gGame.gameStartReceived) return;

    if (len < sizeof(MsgGameStart)) return;
    msg = (const MsgGameStart *)data;

    /* numPlayers bounds gGame.players[] once it reaches gGame.numPlayers:
     * reject anything outside 1..MAX_PLAYERS rather than trust the wire. */
    if (!NetWire_ValidRoster(msg->numPlayers)) {
        CLOG_WARN("Game start with invalid player count %d ignored",
                  msg->numPlayers);
        return;
    }

    /* Version check (T023) */
    if (msg->version != BT_PROTOCOL_VERSION) {
        CLOG_WARN("Version mismatch: got %d, expected %d",
                   msg->version, BT_PROTOCOL_VERSION);
        Net_SetVersionMismatch();
        return;
    }

    gGame.gameStartReceived = TRUE;
    Net_SetExpectedPlayers((short)msg->numPlayers);

    CLOG_INFO("Game start received, expecting %d players",
              Net_GetExpectedPlayers());
    /* No manual dialing: PeerTalk auto-mesh already holds the connections. */
}

static void on_game_over(PT_Peer *peer, const void *data, size_t len,
                         void *user_data)
{
    const MsgGameOver *msg;
    (void)user_data;

    if (len < sizeof(MsgGameOver)) return;
    msg = (const MsgGameOver *)data;

    CLOG_INFO("Game over received: winner=%d (screen=%d)", msg->winnerID,
              gGame.currentScreen);

    /* Ignore game-over if we're not in-game (e.g. already transitioned to lobby).
     * Duplicate MSG_GAME_OVER can arrive after transition due to network latency. */
    if (gGame.currentScreen != SCREEN_GAME) return;

    /* Round end must come from a seated peer. Not restricted to the lowest
     * rank: the failsafe (005) has a non-authority machine send it when the
     * authority is gone or silent, so every seated peer is a legitimate
     * sender. */
    if (!NetAuth_Accept(NETAUTH_SEATED, Net_SenderSlot(peer), 0)) {
        CLOG_DEBUG("Game over from unseated peer dropped");
        return;
    }

    /* Bounds check winnerID (T024) */
    if (msg->winnerID < MAX_PLAYERS) {
        CLOG_INFO("Winner is P%d", msg->winnerID);
    } else {
        CLOG_INFO("No winner (draw or invalid ID: 0x%02X)", msg->winnerID);
    }

    /* Defer transition: let death animations finish before going to lobby.
     * Game_Update will handle the actual transition. */
    Win_RoundEndOnRemote(&gGame.roundEnd, msg->winnerID, TickCount());
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
    (void)user_data;

    if (gGame.currentScreen != SCREEN_GAME) return;
    if (len < 2) return;
    if (!NetAuth_Accept(NETAUTH_SEATED, Net_SenderSlot(peer), 0)) return;
    msg = (const MsgMapState *)data;
    cols = (short)msg->cols;
    rows = (short)msg->rows;

    /* TileMap_SetState rejects out-of-range dims, dims that differ from the
     * local map (playWidth/Height are fixed at startup) and a payload shorter
     * than cols*rows without modifying the map (no clamp-then-overread). */
    if (TileMap_SetState(cols, rows, msg->tiles, (long)(len - 2))) {
        Renderer_RequestRebuildBackground();
        CLOG_INFO("Applied in-progress map snapshot (%dx%d)", cols, rows);
    }
}

/* ---- Registration ---- */

void NetHandlers_Register(PT_Context *ctx)
{
    /* Register callbacks */
    PT_OnPeerDiscovered(ctx, on_peer_discovered, NULL);
    PT_OnPeerLost(ctx, on_peer_lost, NULL);
    PT_OnConnected(ctx, on_connected, NULL);
    PT_OnDisconnected(ctx, on_disconnected, NULL);
    PT_OnError(ctx, on_error, NULL);

    PT_OnMessage(ctx, MSG_POSITION,        on_position, NULL);
    PT_OnMessage(ctx, MSG_BOMB_PLACED,     on_bomb_placed, NULL);
    PT_OnMessage(ctx, MSG_BOMB_EXPLODE,    on_bomb_explode, NULL);
    PT_OnMessage(ctx, MSG_BLOCK_DESTROYED, on_block_destroyed, NULL);
    PT_OnMessage(ctx, MSG_PLAYER_KILLED,   on_player_killed, NULL);
    PT_OnMessage(ctx, MSG_GAME_START,      on_game_start, NULL);
    PT_OnMessage(ctx, MSG_GAME_OVER,       on_game_over, NULL);
    PT_OnMessage(ctx, MSG_MAP_STATE,       on_map_state, NULL);
}
