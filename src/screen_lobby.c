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

/* Pre-built Pascal strings */
static const unsigned char kLobbyTitle[]  = {5, 'L','o','b','b','y'};
static const unsigned char kLobbyConn[]   = {13, 'C','o','n','n','e','c','t','i','n','g','.','.','.'};
static const unsigned char kLobbySearch[] = {15, 'S','e','a','r','c','h','i','n','g','.',' ',' ',' ',' ',' '};
static const unsigned char kLobbyStart[]  = {21, 'P','r','e','s','s',' ','R','e','t','u','r','n',' ','t','o',' ','S','t','a','r','t'};
static const unsigned char kLobbySP[]     = {21, 'S','p','a','c','e',':',' ','S','i','n','g','l','e','-','p','l','a','y','e','r',' '};
static const unsigned char kLobbyVMis[]  = {18, 'V','e','r','s','i','o','n',' ','m','i','s','m','a','t','c','h','!',' '};

/* Cached StringWidth values */
static short gLobbyTitleW = 0, gLobbyConnW = 0, gLobbySearchW = 0;
static short gLobbyStartW = 0, gLobbySPW = 0, gLobbyVMisW = 0;
static int gLobbyWidthsCached = FALSE;

void Lobby_Init(void)
{
    gConnecting = FALSE;
    gWaitingForMesh = FALSE;
    gConnectStartTick = 0;
    gLastMeshRetryTick = 0;
    Net_ResetVersionMismatch();
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
        CLOG_INFO("Lobby: ESC pressed, returning to menu");
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
    short strW;

    centerX = gGame.playWidth / 2;
    peerCount = Net_GetDiscoveredPeerCount();

    /* Draw to offscreen work buffer, then blit */
    Renderer_BeginScreenDraw();

    /* Cache StringWidth on first draw (needs valid port) */
    if (!gLobbyWidthsCached) {
        TextSize(24);
        gLobbyTitleW = StringWidth((ConstStr255Param)kLobbyTitle);
        TextSize(14);
        gLobbyConnW = StringWidth((ConstStr255Param)kLobbyConn);
        gLobbySearchW = StringWidth((ConstStr255Param)kLobbySearch);
        TextSize(12);
        gLobbyStartW = StringWidth((ConstStr255Param)kLobbyStart);
        gLobbySPW = StringWidth((ConstStr255Param)kLobbySP);
        gLobbyVMisW = StringWidth((ConstStr255Param)kLobbyVMis);
        gLobbyWidthsCached = TRUE;
    }

    /* Title */
    TextSize(24);
    ForeColor(whiteColor);
    MoveTo(centerX - gLobbyTitleW / 2, 40);
    DrawString((ConstStr255Param)kLobbyTitle);

    /* Peer list */
    TextSize(14);
    y = 80;

    if (gConnecting) {
        MoveTo(centerX - gLobbyConnW / 2, y);
        DrawString((ConstStr255Param)kLobbyConn);
    } else if (peerCount == 0) {
        MoveTo(centerX - gLobbySearchW / 2, y);
        DrawString((ConstStr255Param)kLobbySearch);
    } else {
        /* Show "Found N:" header -- digit is dynamic */
        Str255 foundStr;
        foundStr[0] = 9;
        foundStr[1] = 'F'; foundStr[2] = 'o'; foundStr[3] = 'u';
        foundStr[4] = 'n'; foundStr[5] = 'd'; foundStr[6] = ' ';
        foundStr[7] = (unsigned char)('0' + peerCount);
        foundStr[8] = ':'; foundStr[9] = ' ';

        strW = StringWidth(foundStr);
        MoveTo(centerX - strW / 2, y);
        DrawString(foundStr);
        y += 24;

        /* Peer names (dynamic, must be built each frame) */
        for (i = 0; i < peerCount && i < MAX_PLAYERS - 1; i++) {
            const char *name = Net_GetDiscoveredPeerName(i);
            const char *addr = Net_GetDiscoveredPeerAddress(i);
            Str255 peerStr;
            short len = 0;
            const char *src;

            if (!name) continue;
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

    /* Version mismatch warning (T025) */
    if (Net_HasVersionMismatch()) {
        TextSize(12);
        ForeColor(whiteColor);
        MoveTo(centerX - gLobbyVMisW / 2, y + 10);
        DrawString((ConstStr255Param)kLobbyVMis);
    }

    /* Instructions */
    y = gGame.playHeight - 60;
    if (peerCount >= 1 && !gConnecting) {
        TextSize(12);
        MoveTo(centerX - gLobbyStartW / 2, y);
        DrawString((ConstStr255Param)kLobbyStart);
    }

    y += 20;
    if (!gConnecting) {
        TextSize(12);
        MoveTo(centerX - gLobbySPW / 2, y);
        DrawString((ConstStr255Param)kLobbySP);
    }

    Renderer_EndScreenDraw(window);
}
