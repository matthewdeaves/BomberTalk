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

/* Launch handshake. PeerTalk auto-mesh forms the full mesh in the background
 * (see Net_Init), so the lobby no longer dials, staggers, or retries. It only
 * decides WHEN to start: gStartRequested = this player pressed Start;
 * gGame.gameStartReceived = another player sent MSG_GAME_START. Either way we
 * wait until the mesh is complete (or MESH_FORM_TIMEOUT_TICKS elapses) and
 * then enter the game together. gMeshWaitStart timestamps the wait. */
static int gStartRequested = FALSE;
static long gMeshWaitStart = 0;

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
    short i;

    gStartRequested = FALSE;
    gMeshWaitStart = 0;

    /* Clear leftover game state from previous round.
     * pendingGameOver can be set by duplicate MSG_GAME_OVER arriving
     * after we already transitioned to lobby. gameStartReceived should
     * already be FALSE but belt-and-suspenders. */
    gGame.pendingGameOver = FALSE;
    gGame.gameStartReceived = FALSE;
    gGame.gameOverTimeoutStart = 0;
    gGame.disconnectGraceStart = 0;
    gGame.localGameOverDetected = FALSE;
    gGame.gameOverFailsafeStart = 0;

    /* Clear stale peer pointers from previous game */
    for (i = 0; i < MAX_PLAYERS; i++) {
        gGame.players[i].peer = NULL;
    }

    Net_ResetVersionMismatch();
    Net_StartDiscovery();
    CLOG_INFO("Lobby entered");
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
        gStartRequested = FALSE;
        gMeshWaitStart = 0;
        Screens_TransitionTo(SCREEN_MENU);
        return;
    }

    peerCount = Net_GetDiscoveredPeerCount();
    connectedCount = Net_GetConnectedPeerCount();

    /*
     * Launch handshake. PeerTalk auto-mesh keeps the full mesh connected in
     * the background, so there is nothing to dial here -- we only wait for the
     * mesh to be complete before entering the game together. We are launching
     * if we pressed Start (gStartRequested) or received MSG_GAME_START.
     */
    if (gStartRequested || gGame.gameStartReceived) {
        int timedOut;
        if (gMeshWaitStart == 0) gMeshWaitStart = TickCount();
        timedOut = (TickCount() - gMeshWaitStart > MESH_FORM_TIMEOUT_TICKS);

        if (gGame.gameStartReceived) {
            /* The roster size is fixed by the GAME_START we saw or sent.
             * Enter once every announced peer is connected; on timeout enter
             * with whoever is actually connected (a lagging peer can still
             * join in progress once its link comes up). */
            expected = Net_GetExpectedPlayers();
            if (connectedCount >= expected - 1) {
                CLOG_INFO("Full mesh: %d connections for %d players",
                          connectedCount, expected);
                enter_game(expected);
            } else if (timedOut) {
                CLOG_WARN("Mesh incomplete (%d/%d), starting anyway",
                          connectedCount, expected - 1);
                enter_game(connectedCount + 1);
            }
            return;
        }

        /* Initiator: wait for the mesh to every discovered peer, then announce
         * the roster and launch. On timeout start with whoever connected. */
        if (connectedCount >= peerCount || timedOut) {
            expected = (short)((timedOut ? connectedCount : peerCount) + 1);
            if (timedOut && connectedCount < peerCount) {
                CLOG_WARN("Mesh form timeout: starting with %d players",
                          expected);
            } else {
                CLOG_INFO("Full mesh (%d peers), sending game start",
                          connectedCount);
            }
            Net_SendGameStart((unsigned char)expected);
            gGame.gameStartReceived = TRUE;
            enter_game(expected);
        }
        return;
    }

    if (Input_WasKeyPressed(KEY_RETURN) && peerCount >= 1) {
        gStartRequested = TRUE;
        gMeshWaitStart = TickCount();
        CLOG_INFO("Start requested; waiting for full mesh (%d peers)",
                  peerCount);
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
    int launching;

    centerX = gGame.playWidth / 2;
    peerCount = Net_GetDiscoveredPeerCount();
    launching = gStartRequested || gGame.gameStartReceived;

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

    if (launching) {
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
    if (peerCount >= 1 && !launching) {
        TextSize(12);
        MoveTo(centerX - gLobbyStartW / 2, y);
        DrawString((ConstStr255Param)kLobbyStart);
    }

    y += 20;
    if (!launching) {
        TextSize(12);
        MoveTo(centerX - gLobbySPW / 2, y);
        DrawString((ConstStr255Param)kLobbySP);
    }

    Renderer_EndScreenDraw(window);
}
