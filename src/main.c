/*
 * main.c -- BomberTalk entry point
 *
 * InitToolbox, window creation, main event loop with screen dispatch.
 * Source: Black Art of Macintosh Game Programming (1996) Ch. 2-3,
 *         Tricks of the Mac Game Programming Gurus (1995) p.12-45.
 */

#include "game.h"
#include "screens.h"
#include "renderer.h"
#include "tilemap.h"
#include "input.h"
#include "net.h"
#include <clog.h>

/* The single global game state */
GameState gGame;

/* ---- Local state ---- */
static int      gQuitting = FALSE;

/*
 * Game_RequestQuit -- Set the quit flag so the main loop exits cleanly.
 *
 * All quit paths MUST go through this instead of calling ExitToShell()
 * directly, so the shutdown sequence in main() runs (Net_Shutdown,
 * Renderer_Shutdown port redirection, DisposeWindow, clog_shutdown).
 * Source: Macintosh Game Programming Techniques (1996) Ch. 4 — global
 * quit flag pattern: while (!bQuitting) ... then CleanupDesertTrek().
 */
void Game_RequestQuit(void)
{
    gQuitting = TRUE;
}

static long     gLastFrameTick = 0;
static short    gFPSFrameCount = 0;
static long     gFPSLastTick = 0;

/*
 * InitToolbox -- Initialize all Mac Toolbox managers
 *
 * MaxApplZone + MoreMasters MUST come first (CLAUDE.md gotcha).
 * Source: Tricks of the Mac Game Programming Gurus (1995) p.12,
 *         Black Art (1996) Ch. 2.
 */
static void InitToolbox(void)
{
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    MoreMasters();
    MoreMasters();

    InitGraf(&qd.thePort);
    InitFonts();
    FlushEvents(everyEvent, 0);
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0L);
    InitCursor();

    qd.randSeed = TickCount();
}

/*
 * SetupMenus -- Apple menu + File menu with Quit
 *
 * Source: Macintosh Game Programming Techniques (1996) Ch. 4.
 */
static void SetupMenus(void)
{
    MenuHandle appleMenu, fileMenu;

    appleMenu = NewMenu(rMenuApple, "\p\x14");
    AppendResMenu(appleMenu, 'DRVR');
    InsertMenu(appleMenu, 0);

    fileMenu = NewMenu(rMenuFile, "\pFile");
    AppendMenu(fileMenu, "\pQuit/Q");
    InsertMenu(fileMenu, 0);

    DrawMenuBar();
}

/*
 * DetectScreenSize -- Determine if we're on Mac SE (512x342)
 *                     or a color Mac (640x480+).
 */
static void DetectScreenSize(void)
{
    Rect screenRect;

    screenRect = qd.screenBits.bounds;

    if (screenRect.right - screenRect.left <= 512) {
        gGame.isMacSE = TRUE;
        gGame.tileSize = TILE_SIZE_SMALL;
    } else {
        gGame.isMacSE = FALSE;
        gGame.tileSize = TILE_SIZE_LARGE;
    }
    /* playWidth/playHeight set after TileMap_Init (dynamic grid dims) */
}

/*
 * CreateGameWindow -- Open the game window sized to play area
 *
 * Uses NewCWindow for color Macs, NewWindow for Mac SE.
 * Source: Black Art (1996) p.34, CLAUDE.md Mac SE gotcha.
 */
static void CreateGameWindow(void)
{
    Rect bounds;
    Rect screenRect;
    short screenW, screenH;
    short winLeft, winTop;

    screenRect = qd.screenBits.bounds;
    screenW = screenRect.right - screenRect.left;
    screenH = screenRect.bottom - screenRect.top;

    winLeft = (screenW - gGame.playWidth) / 2;
    winTop = (screenH - gGame.playHeight) / 2 + 20; /* menu bar offset */

    SetRect(&bounds, winLeft, winTop,
            winLeft + gGame.playWidth, winTop + gGame.playHeight);

    if (gGame.isMacSE) {
        /* Mac SE: no Color QuickDraw, use NewWindow */
        gGame.window = NewWindow(
            0L, &bounds, "\pBomberTalk", TRUE,
            noGrowDocProc, (WindowPtr)-1L, FALSE, 0L);
    } else {
        gGame.window = (WindowPtr)NewCWindow(
            0L, &bounds, "\pBomberTalk", TRUE,
            noGrowDocProc, (WindowPtr)-1L, FALSE, 0L);
    }

    if (gGame.window == NULL) {
        CLOG_ERR("Failed to create game window");
        SysBeep(30);
        ExitToShell();
    }

    SetPort(gGame.window);
}

/*
 * HandleMenuChoice -- Process menu selections
 */
static void HandleMenuChoice(long menuChoice)
{
    short menu, item;

    menu = (short)((menuChoice >> 16) & 0xFFFF);
    item = (short)(menuChoice & 0xFFFF);

    if (menu == rMenuFile && item == 1) {
        gQuitting = TRUE;
    }

    HiliteMenu(0);
}

/*
 * HandleEvent -- Process Mac OS events
 *
 * Keyboard input for movement uses GetKeys() polling (input.c),
 * NOT keyDown events. keyDown only used for Cmd-Q quit.
 * Source: Black Art (1996) Ch. 3, all six books agree.
 */
static void HandleEvent(EventRecord *event)
{
    WindowPtr whichWindow;
    short part;

    switch (event->what) {
    case mouseDown:
        part = FindWindow(event->where, &whichWindow);
        switch (part) {
        case inMenuBar:
            HandleMenuChoice(MenuSelect(event->where));
            break;
        case inDrag:
            DragWindow(whichWindow, event->where,
                       &qd.screenBits.bounds);
            break;
        case inContent:
            if (whichWindow != FrontWindow())
                SelectWindow(whichWindow);
            break;
        }
        break;

    case keyDown:
    case autoKey:
        if (event->modifiers & cmdKey) {
            HandleMenuChoice(MenuKey(
                (char)(event->message & charCodeMask)));
        }
        break;

    case updateEvt:
        whichWindow = (WindowPtr)event->message;
        BeginUpdate(whichWindow);
        Renderer_BlitToWindow(gGame.window);
        EndUpdate(whichWindow);
        break;
    }
}

/*
 * MainLoop -- WaitNextEvent + fixed-rate game update
 *
 * Source: Black Art (1996) Ch. 8, Tricks of the Gurus (1995).
 * WaitNextEvent(sleep=0) so we never yield CPU.
 * GetKeys() for input, PT_Poll() for networking, every iteration.
 */
static void MainLoop(void)
{
    EventRecord event;
    long currentTick;
    long elapsed;

    gLastFrameTick = TickCount();

    while (!gQuitting) {
        if (WaitNextEvent(everyEvent, &event, EVENT_TICKS, NULL)) {
            HandleEvent(&event);
        }

        Net_Poll();
        Input_Poll();  /* accumulate key edges every iteration, not just per frame */

        currentTick = TickCount();
        if (currentTick - gLastFrameTick >= FRAME_TICKS) {
            elapsed = currentTick - gLastFrameTick;
            if (elapsed > 10) elapsed = 10; /* cap to ~1/6 sec */
            gGame.deltaTicks = (short)elapsed;
            gLastFrameTick = currentTick;

            /* Toggle FPS display with F key */
            if (Input_WasKeyPressed(KEY_F)) {
                gGame.showFPS = !gGame.showFPS;
            }

            Screens_Update();
            Screens_Draw(gGame.window);

            /* FPS counting: update every second (60 ticks) */
            gFPSFrameCount++;
            if (currentTick - gFPSLastTick >= 60) {
                gGame.fpsValue = gFPSFrameCount;
                gFPSFrameCount = 0;
                gFPSLastTick = currentTick;
            }

            /* Draw FPS overlay directly on window */
            if (gGame.showFPS) {
                Renderer_DrawFPS(gGame.fpsValue);
            }

            Input_ConsumeFrame();  /* clear accumulated edges after frame */
        }
    }
}

/*
 * InitGameState -- Zero out the global game state
 */
static void InitGameState(void)
{
    short i;

    gGame.currentScreen = SCREEN_LOADING;
    gGame.numPlayers = 0;
    gGame.localPlayerID = -1;
    gGame.numActiveBombs = 0;
    gGame.gameRunning = FALSE;
    gGame.roundStartTick = 0;
    gGame.gameStartReceived = FALSE;
    gGame.deltaTicks = FRAME_TICKS;
    gGame.showFPS = FALSE;
    gGame.fpsValue = 0;
    gGame.pendingGameOver = FALSE;
    gGame.pendingWinner = 0xFF;
    gGame.gameOverTimeout = 0;

    for (i = 0; i < MAX_PLAYERS; i++) {
        gGame.players[i].active = FALSE;
        gGame.players[i].alive = FALSE;
        gGame.players[i].playerID = (unsigned char)i;
        gGame.players[i].bombsAvailable = 1;
        gGame.players[i].stats.bombsMax = 1;
        gGame.players[i].stats.bombRange = 1;
        gGame.players[i].stats.speedTicks = 12;
        gGame.players[i].peer = NULL;
    }

    for (i = 0; i < MAX_BOMBS; i++) {
        gGame.bombs[i].active = FALSE;
    }
}

/* ---- Entry Point ---- */
int main(void)
{
    InitToolbox();
    DetectScreenSize();

#ifndef CLOG_STRIP
    clog_set_file("BomberTalk Log");
    /* Flush errors/warnings to disk immediately so they survive crashes.
     * Mac SE: also flush all (INFO) since it only logs INFO level anyway
     * and we need crash diagnostics on the slowest machine. */
    clog_set_flush(gGame.isMacSE ? CLOG_FLUSH_ALL : CLOG_FLUSH_ERRORS);
    /* Mac SE: INFO only — DEBUG file writes add ~3ms each via File Manager,
     * and smooth movement generates many DBG calls per frame */
    clog_init("BomberTalk", gGame.isMacSE ? CLOG_LVL_INFO : CLOG_LVL_DBG);
#endif

    CLOG_INFO("BomberTalk starting");

    /* Load tilemap early so dimensions are known for window/buffer sizing */
    TileMap_Init();

    /* Update play dimensions from loaded tilemap (T027/T030) */
    gGame.playWidth = TileMap_GetCols() * gGame.tileSize;
    gGame.playHeight = TileMap_GetRows() * gGame.tileSize;

    SetupMenus();
    CreateGameWindow();
    InitGameState();

    Input_Init();
    Renderer_Init(gGame.window);
    Net_Init("BomberTalk");

    Screens_Init();

    /* Memory budget check (T038) */
    CLOG_INFO("Free heap after init: %ld bytes", FreeMem());

    CLOG_INFO("Entering main loop");
    MainLoop();

    CLOG_INFO("Shutting down");

    /* Shut down networking first: clears the clog UDP sink and tears
     * down MacTCP/OT streams before we free GWorlds and the window.
     * Prevents async MacTCP completions from referencing freed buffers
     * after ExitToShell reclaims the app heap.
     *
     * NOTE: We intentionally do NOT call Net_DisconnectAllPeers() here.
     * That sends goodbye frames via synchronous TCP, which requires the
     * remote side to ACK — per MacTCP guide: "the connection may remain
     * an arbitrary amount of time after TCPClose."  If two players quit
     * simultaneously, both block waiting for the other's ACK (deadlock).
     * PT_Shutdown uses TCPAbort/OTSndDisconnect (abortive disconnect)
     * which tears down immediately.  The game-over flow in screen_game.c
     * uses the graceful path because the game loop is still running. */
    Net_Shutdown();

    /* Renderer_Shutdown redirects QuickDraw to the window before
     * disposing GWorlds — required per Sex, Lies & Video Games (1996)
     * p.104 to avoid leaving stale Font Manager references in the
     * System heap (Bus Error in Finder's StdText after exit). */
    Renderer_Shutdown();

    if (gGame.window) {
        DisposeWindow(gGame.window);
        gGame.window = NULL;
    }

#ifndef CLOG_STRIP
    clog_shutdown();
#endif

    ExitToShell();
    return 0; /* not reached */
}
