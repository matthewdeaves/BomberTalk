/*
 * net_send.c -- Outgoing MSG_* messages.
 *
 * Packs each game message and hands it to PeerTalk (broadcast, or direct for
 * the late-join map snapshot). Wire layouts are in game.h / net_wire.h and are
 * unchanged. Split out of net.c (structure refactor).
 */

#include "net.h"
#include "net_internal.h"
#include "game.h"
#include "tilemap.h"
#include "netcoord.h"
#include "net_wire.h"
#include <peertalk.h>
#include <clog.h>

/*
 * Net_SendMapStateTo -- Send our current tilemap to one peer (the late joiner).
 * Static buffer (not stack) to keep the 68k main stack small.
 */
void Net_SendMapStateTo(PT_Peer *peer)
{
    static MsgMapState msg;
    TileMap *map;
    short cols, rows, r, c;
    long len;

    if (!Net_Ctx() || peer == NULL) return;
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
    PT_Send(Net_Ctx(), peer, MSG_MAP_STATE, &msg, (size_t)len);
    CLOG_INFO("Sent map snapshot (%dx%d, %ld bytes) to joiner", cols, rows, len);
}


void Net_SendPosition(short pixelX, short pixelY, short facing)
{
    unsigned char buf[NETWIRE_POSITION_LEN];
    short ts = gGame.tileSize;
    if (!Net_Ctx()) return;

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
    PT_Broadcast(Net_Ctx(), MSG_POSITION, buf, NETWIRE_POSITION_LEN);
}

void Net_SendBombPlaced(short col, short row, short range)
{
    MsgBombPlaced msg;
    if (!Net_Ctx()) return;

    msg.playerID = (unsigned char)gGame.localPlayerID;
    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;
    msg.range = (unsigned char)range;
    msg.fuseTicks = (unsigned char)BOMB_FUSE_TICKS;

    CLOG_INFO("TX bomb placed (%d,%d) range=%d", col, row, range);
    PT_Broadcast(Net_Ctx(), MSG_BOMB_PLACED, &msg, sizeof(msg));
}

void Net_SendBombExplode(short col, short row, short range)
{
    MsgBombExplode msg;
    if (!Net_Ctx()) return;

    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;
    msg.range = (unsigned char)range;

    CLOG_INFO("TX bomb explode (%d,%d) range=%d", col, row, range);
    PT_Broadcast(Net_Ctx(), MSG_BOMB_EXPLODE, &msg, sizeof(msg));
}

void Net_SendBlockDestroyed(short col, short row)
{
    MsgBlockDestroyed msg;
    if (!Net_Ctx()) return;

    msg.gridCol = (unsigned char)col;
    msg.gridRow = (unsigned char)row;

    CLOG_INFO("TX block destroyed (%d,%d)", col, row);
    PT_Broadcast(Net_Ctx(), MSG_BLOCK_DESTROYED, &msg, sizeof(msg));
}

void Net_SendPlayerKilled(unsigned char playerID, unsigned char killerID)
{
    MsgPlayerKilled msg;
    if (!Net_Ctx()) return;

    msg.playerID = playerID;
    msg.killerID = killerID;

    CLOG_INFO("TX player killed: P%d by P%d", playerID, killerID);
    PT_Broadcast(Net_Ctx(), MSG_PLAYER_KILLED, &msg, sizeof(msg));
}

void Net_SendGameStart(unsigned char numPlayers)
{
    MsgGameStart msg;
    if (!Net_Ctx()) return;

    msg.numPlayers = numPlayers;
    msg.version = BT_PROTOCOL_VERSION;

    Net_SetExpectedPlayers((short)numPlayers);
    CLOG_INFO("TX game start: %d players, proto v%d", numPlayers, BT_PROTOCOL_VERSION);
    PT_Broadcast(Net_Ctx(), MSG_GAME_START, &msg, sizeof(msg));
}

void Net_SendGameOver(unsigned char winnerID)
{
    MsgGameOver msg;
    if (!Net_Ctx()) return;

    msg.winnerID = winnerID;
    msg.pad = 0;

    CLOG_INFO("TX game over: winner=%d", winnerID);
    PT_Broadcast(Net_Ctx(), MSG_GAME_OVER, &msg, sizeof(msg));
}
