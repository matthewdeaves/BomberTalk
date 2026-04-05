/*
 * screen_lobby.c -- Lobby screen (player discovery and connection)
 *
 * Shows local player, discovered peers, "Start" when 2+ connected.
 * Any player can press Start (no host concept).
 *
 * Draws to offscreen work buffer via Renderer helpers to avoid flicker.
 */

#include "screens.h"
#include "input.h"
#include "net.h"
#include "renderer.h"
#include <clog.h>

static int gConnecting = FALSE;
static int gWaitingForMesh = FALSE;
static long gConnectStartTick = 0;
static long gLastMeshRetryTick = 0;

void Lobby_Init(void)
{
    gConnecting = FALSE;
    gWaitingForMesh = FALSE;
    gConnectStartTick = 0;
    gLastMeshRetryTick = 0;
    Net_StartDiscovery();
    CLOG_INFO("Lobby entered, discovery started");
}

static void enter_game(int numPlayers)
{
    gGame.localPlayerID = Net_ComputeLocalPlayerID();
    gGame.numPlayers = (short)numPlayers;

    CLOG_INFO("Entering game: %d players, localID=%d",
              gGame.numPlayers, gGame.localPlayerID);
    Screens_TransitionTo(SCREEN_GAME);
}

void Lobby_Update(void)
{
    int peerCount;
    int connectedCount;
    short expected;

    if (Input_WasKeyPressed(KEY_ESCAPE)) {
        Net_StopDiscovery();
        gConnecting = FALSE;
        gWaitingForMesh = FALSE;
        Screens_TransitionTo(SCREEN_MENU);
        return;
    }

    peerCount = Net_GetDiscoveredPeerCount();
    connectedCount = Net_GetConnectedPeerCount();
    expected = Net_GetExpectedPlayers();

    /*
     * Waiting for full mesh: all players must be connected to each other.
     * Both the initiator and receivers end up here.
     */
    if (gWaitingForMesh) {
        if (connectedCount >= expected - 1) {
            CLOG_INFO("Full mesh: %d connections for %d players",
                      connectedCount, expected);
            enter_game(expected);
            return;
        }

        /* Retry connecting to unconnected peers every 2 seconds.
         * Handles tiebreaker race where both connections drop. */
        if (TickCount() - gLastMeshRetryTick > 120) {
            gLastMeshRetryTick = TickCount();
            CLOG_INFO("Mesh retry: %d/%d connected",
                      connectedCount, expected - 1);
            Net_ConnectToAllPeers();
        }

        /* Timeout after 15 seconds (was 10) */
        if (TickCount() - gConnectStartTick > 900) {
            CLOG_WARN("Mesh timeout: %d/%d connected, starting anyway",
                      connectedCount, expected);
            enter_game(connectedCount + 1);
            return;
        }
        return;
    }

    /*
     * Receiver: got MSG_GAME_START from another player.
     * Connect to any unconnected peers and wait for full mesh.
     */
    if (gGame.gameStartReceived && !gWaitingForMesh) {
        CLOG_INFO("Game start received, waiting for mesh (%d expected)",
                  expected);
        gWaitingForMesh = TRUE;
        gConnectStartTick = TickCount();
        return;
    }

    /* Initiator: connecting to all peers before sending game start */
    if (gConnecting) {
        if (connectedCount >= peerCount) {
            CLOG_INFO("All %d peers connected, sending game start",
                      connectedCount);
            expected = (short)(peerCount + 1);
            Net_SendGameStart((unsigned char)expected);

            gGame.gameStartReceived = TRUE;
            gWaitingForMesh = TRUE;
            /* connectStartTick already set */
            return;
        }

        /* Timeout after 10 seconds */
        if (TickCount() - gConnectStartTick > 600) {
            CLOG_ERR("Connection timeout");
            gConnecting = FALSE;
        }
        return;
    }

    if (Input_WasKeyPressed(KEY_RETURN) && peerCount >= 1) {
        gConnecting = TRUE;
        gConnectStartTick = TickCount();
        Net_ConnectToAllPeers();
        CLOG_INFO("Connecting to %d peers", peerCount);
    }

    if (Input_WasKeyPressed(KEY_SPACE)) {
        CLOG_INFO("Starting single-player test mode");
        gGame.localPlayerID = 0;
        gGame.numPlayers = 1;
        gGame.gameStartReceived = TRUE;
        Screens_TransitionTo(SCREEN_GAME);
    }
}

void Lobby_Draw(WindowPtr window)
{
    short centerX, y;
    int peerCount, i;

    Str255 titleStr, searchStr, startStr, spStr, connStr, foundStr;
    short strW;

    /* Title: "Lobby" */
    titleStr[0] = 5;
    titleStr[1] = 'L'; titleStr[2] = 'o'; titleStr[3] = 'b';
    titleStr[4] = 'b'; titleStr[5] = 'y';

    /* "Connecting..." */
    connStr[0] = 13;
    connStr[1] = 'C'; connStr[2] = 'o'; connStr[3] = 'n';
    connStr[4] = 'n'; connStr[5] = 'e'; connStr[6] = 'c';
    connStr[7] = 't'; connStr[8] = 'i'; connStr[9] = 'n';
    connStr[10] = 'g'; connStr[11] = '.'; connStr[12] = '.';
    connStr[13] = '.';

    /* "Found N player(s):" */
    foundStr[0] = 9;
    foundStr[1] = 'F'; foundStr[2] = 'o'; foundStr[3] = 'u';
    foundStr[4] = 'n'; foundStr[5] = 'd'; foundStr[6] = ' ';
    foundStr[7] = '0'; foundStr[8] = ':'; foundStr[9] = ' ';

    searchStr[0] = 15;
    searchStr[1] = 'S'; searchStr[2] = 'e'; searchStr[3] = 'a';
    searchStr[4] = 'r'; searchStr[5] = 'c'; searchStr[6] = 'h';
    searchStr[7] = 'i'; searchStr[8] = 'n'; searchStr[9] = 'g';
    searchStr[10] = '.'; searchStr[11] = '.'; searchStr[12] = '.';
    searchStr[13] = ' '; searchStr[14] = ' '; searchStr[15] = ' ';

    startStr[0] = 21;
    startStr[1] = 'P'; startStr[2] = 'r'; startStr[3] = 'e';
    startStr[4] = 's'; startStr[5] = 's'; startStr[6] = ' ';
    startStr[7] = 'R'; startStr[8] = 'e'; startStr[9] = 't';
    startStr[10] = 'u'; startStr[11] = 'r'; startStr[12] = 'n';
    startStr[13] = ' '; startStr[14] = 't'; startStr[15] = 'o';
    startStr[16] = ' '; startStr[17] = 'S'; startStr[18] = 't';
    startStr[19] = 'a'; startStr[20] = 'r'; startStr[21] = 't';

    spStr[0] = 21;
    spStr[1] = 'S'; spStr[2] = 'p'; spStr[3] = 'a'; spStr[4] = 'c';
    spStr[5] = 'e'; spStr[6] = ':'; spStr[7] = ' '; spStr[8] = 'S';
    spStr[9] = 'i'; spStr[10] = 'n'; spStr[11] = 'g'; spStr[12] = 'l';
    spStr[13] = 'e'; spStr[14] = '-'; spStr[15] = 'p'; spStr[16] = 'l';
    spStr[17] = 'a'; spStr[18] = 'y'; spStr[19] = 'e'; spStr[20] = 'r';
    spStr[21] = ' ';

    centerX = gGame.playWidth / 2;
    peerCount = Net_GetDiscoveredPeerCount();

    /* Draw to offscreen work buffer, then blit */
    Renderer_BeginScreenDraw();

    /* Title */
    TextSize(24);
    ForeColor(whiteColor);
    strW = StringWidth(titleStr);
    MoveTo(centerX - strW / 2, 40);
    DrawString(titleStr);

    /* Peer list */
    TextSize(14);
    y = 80;

    if (gConnecting) {
        strW = StringWidth(connStr);
        MoveTo(centerX - strW / 2, y);
        DrawString(connStr);
    } else if (peerCount == 0) {
        strW = StringWidth(searchStr);
        MoveTo(centerX - strW / 2, y);
        DrawString(searchStr);
    } else {
        /* Show "Found N:" header */
        foundStr[7] = (unsigned char)('0' + peerCount);
        strW = StringWidth(foundStr);
        MoveTo(centerX - strW / 2, y);
        DrawString(foundStr);
        y += 24;

        for (i = 0; i < peerCount && i < MAX_PLAYERS - 1; i++) {
            const char *name = Net_GetDiscoveredPeerName(i);
            const char *addr = Net_GetDiscoveredPeerAddress(i);
            Str255 peerStr;
            short len = 0;
            const char *src;

            if (!name) continue;
            /* Show "name (ip)" */
            src = name;
            while (*src && len < 240) {
                peerStr[len + 1] = *src;
                len++;
                src++;
            }
            peerStr[len + 1] = ' '; len++;
            peerStr[len + 1] = '('; len++;
            if (addr) {
                src = addr;
                while (*src && len < 252) {
                    peerStr[len + 1] = *src;
                    len++;
                    src++;
                }
            }
            peerStr[len + 1] = ')'; len++;
            peerStr[0] = (unsigned char)len;

            strW = StringWidth(peerStr);
            MoveTo(centerX - strW / 2, y);
            DrawString(peerStr);
            y += 20;
        }
    }

    /* Instructions */
    y = gGame.playHeight - 60;
    if (peerCount >= 1 && !gConnecting) {
        TextSize(12);
        strW = StringWidth(startStr);
        MoveTo(centerX - strW / 2, y);
        DrawString(startStr);
    }

    y += 20;
    if (!gConnecting) {
        TextSize(12);
        strW = StringWidth(spStr);
        MoveTo(centerX - strW / 2, y);
        DrawString(spStr);
    }

    Renderer_EndScreenDraw(window);
}
