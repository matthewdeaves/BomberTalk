# Classic Mac Bomberman Clone - Complete Build Plan

## Project: "BomberTalk" - A Networked Bomberman Clone for Classic Macintosh

**Target Platforms:** 68k Macintosh (System 7+), PowerPC Macintosh (System 7.5+)
**Language:** C89 (for Retro68/RetroPPC cross-compilation)
**Networking:** PeerTalk SDK (LAN multiplayer via MacTCP or Open Transport)
**Build System:** CMake with Retro68 toolchain
**Graphics:** Color QuickDraw with GWorld double-buffering (8-bit, 256 colors)

---

## TABLE OF CONTENTS

1. [Architecture Overview](#1-architecture-overview)
2. [Project File Structure](#2-project-file-structure)
3. [Build System & Cross-Compilation](#3-build-system--cross-compilation)
4. [Phase 1: Skeleton App - Window & Event Loop](#4-phase-1-skeleton-app---window--event-loop)
5. [Phase 2: Tile Map System](#5-phase-2-tile-map-system)
6. [Phase 3: Player Movement & Collision](#6-phase-3-player-movement--collision)
7. [Phase 4: Double-Buffered Rendering](#7-phase-4-double-buffered-rendering)
8. [Phase 5: Graphics Assets & Resource File](#8-phase-5-graphics-assets--resource-file)
9. [Phase 6: PeerTalk Integration (Multiplayer Stub)](#9-phase-6-peertalk-integration-multiplayer-stub)
10. [Resource Creation Guide (Photoshop 3 on Classic Mac)](#10-resource-creation-guide-photoshop-3-on-classic-mac)
11. [API Reference: Every Toolbox Call We Use](#11-api-reference-every-toolbox-call-we-use)
12. [Code Patterns from the Books](#12-code-patterns-from-the-books)
13. [Known Gotchas & Platform Differences](#13-known-gotchas--platform-differences)
14. [Future Phases (Beyond This Starter)](#14-future-phases-beyond-this-starter)

---

## 1. ARCHITECTURE OVERVIEW

### What This Starter Project Does

- Opens a color window sized to fit a Bomberman-style grid
- Displays a tile-based map with floor tiles and indestructible wall blocks
- Renders a player character sprite on the grid
- Accepts arrow key input to move the player up/down/left/right
- Prevents the player from walking through wall blocks
- Uses double-buffered offscreen rendering (GWorld) for flicker-free display
- Integrates PeerTalk SDK headers and init/shutdown (ready for multiplayer)
- Builds for both 68k (MacTCP) and PPC (Open Transport) via Retro68

### What This Starter Project Does NOT Do (Yet)

- Bombs, explosions, destructible blocks
- Enemy AI
- Multiplayer game state sync
- Sound effects
- Score/lives/game over
- Menu system beyond Quit

### High-Level Architecture

```
main()
  |
  +-- InitToolbox()          Toolbox managers
  +-- InitGame()             Load resources, create map, create GWorlds
  +-- PT_Init()              Initialize PeerTalk (networking ready)
  +-- MainEventLoop()        WaitNextEvent + game frame updates
  |     |
  |     +-- HandleEvents()   Keyboard (GetKeys), mouse, update, quit
  |     +-- UpdateGame()     Move player, check collisions
  |     +-- RenderFrame()    Draw map + player to offscreen, blit to window
  |     +-- PT_Poll()        Drive PeerTalk I/O (even in single-player)
  |
  +-- PT_Shutdown()          Clean up networking
  +-- CleanupGame()          Dispose GWorlds, release resources
```

### Memory Layout

```
Application Heap (after MaxApplZone + MoreMasters):
  +-- Game globals (small, fixed)
  +-- Tile map array: 15x13 bytes = 195 bytes
  +-- Offscreen GWorld: 480x416 pixels @ 8-bit = ~200 KB
  +-- Background GWorld: 480x416 pixels @ 8-bit = ~200 KB
  +-- Tile sheet GWorld: loaded from PICT resource ~10-50 KB
  +-- Player sprite GWorld: loaded from PICT resource ~2-5 KB
  +-- PeerTalk context: ~50-200 KB (auto-sized by PT_Init)
  Total: ~500 KB - 700 KB (fits comfortably in 4 MB Mac)
```

---

## 2. PROJECT FILE STRUCTURE

```
bombertalk/
|-- CMakeLists.txt                  # Build configuration
|-- CLAUDE.md                       # Project conventions
|-- README.md                       # How to build and run
|
|-- include/
|   |-- game.h                      # Main game types, constants, globals
|   |-- tilemap.h                   # Tile map data structures and functions
|   |-- player.h                    # Player state and movement
|   |-- renderer.h                  # GWorld management, drawing
|   |-- input.h                     # Keyboard polling
|   `-- net.h                       # PeerTalk wrapper (thin layer)
|
|-- src/
|   |-- main.c                      # Entry point, toolbox init, event loop
|   |-- tilemap.c                   # Map loading, tile queries
|   |-- player.c                    # Player movement, collision with map
|   |-- renderer.c                  # Offscreen buffer management, blitting
|   |-- input.c                     # GetKeys polling, key state tracking
|   `-- net.c                       # PT_Init/PT_Shutdown/PT_Poll wrapper
|
|-- resources/
|   |-- bombertalk.r                # Rez source: WIND, MENU, DLOG, SIZE
|   |-- bombertalk_size.r           # SIZE resource (memory partition)
|   |-- tiles.pict                  # Tile sheet (created in Photoshop 3)
|   `-- player.pict                 # Player sprite sheet
|
|-- maps/
|   `-- level1.h                    # Hardcoded level data (C array)
|
`-- build/                          # Build output (gitignored)
```

### Why This Structure

- **Separate .h/.c per concern**: Each file is small, fits in a single compilation unit, easy to understand
- **Resources in .r files**: Rez format compiles to resource fork via Retro68
- **Maps as C headers**: Simplest possible approach - no file I/O needed, just a const array
- **PeerTalk as external dependency**: Linked from /home/matt/Desktop/peertalk via CMake

---

## 3. BUILD SYSTEM & CROSS-COMPILATION

### CMakeLists.txt Design

```cmake
cmake_minimum_required(VERSION 3.15)
project(BomberTalk C)

# PeerTalk SDK location
set(PEERTALK_DIR "$ENV{HOME}/Desktop/peertalk" CACHE PATH "PeerTalk SDK")
set(PEERTALK_INCLUDE "${PEERTALK_DIR}/include")

# clog location (PeerTalk dependency)
set(CLOG_DIR "$ENV{HOME}/Desktop/clog" CACHE PATH "clog library")

# Detect platform (same pattern as PeerTalk)
if(CMAKE_SYSTEM_NAME MATCHES "Retro68")
    set(BT_PLATFORM "68K")
    set(PT_PLATFORM "MACTCP")
    set(CLOG_LIB_DIR "${CLOG_DIR}/build-m68k")
    set(PT_LIB_DIR "${PEERTALK_DIR}/build-68k")
elseif(CMAKE_SYSTEM_NAME MATCHES "RetroPPC")
    set(BT_PLATFORM "PPC")
    set(PT_PLATFORM "OT")
    set(CLOG_LIB_DIR "${CLOG_DIR}/build-ppc")
    set(PT_LIB_DIR "${PEERTALK_DIR}/build-ppc-ot")
else()
    message(FATAL_ERROR "BomberTalk targets Classic Mac only")
endif()

# Game sources
set(GAME_SOURCES
    src/main.c
    src/tilemap.c
    src/player.c
    src/renderer.c
    src/input.c
    src/net.c
)

# Resource files
set(GAME_RESOURCES
    resources/bombertalk.r
    resources/bombertalk_size.r
)

# Build the application
add_application(BomberTalk ${GAME_SOURCES} ${GAME_RESOURCES})

target_include_directories(BomberTalk PRIVATE
    include
    ${PEERTALK_INCLUDE}
    ${CLOG_DIR}/include
)

# Link PeerTalk and clog
target_link_libraries(BomberTalk
    ${PT_LIB_DIR}/libpeertalk.a
    ${CLOG_LIB_DIR}/libclog.a
)

# Platform-specific linker flags
if(BT_PLATFORM STREQUAL "PPC")
    # Open Transport libraries for PPC
    target_link_libraries(BomberTalk
        -lOpenTransportAppPPC
        -lOpenTransportLib
    )
endif()
```

### Build Commands

**68k (for Mac SE, Mac II, LC series, etc.):**
```bash
cd ~/path/to/bombertalk
mkdir -p build-68k && cd build-68k
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

**PPC (for Performa, Power Mac, etc.):**
```bash
cd ~/path/to/bombertalk
mkdir -p build-ppc && cd build-ppc
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
make
```

### SIZE Resource (Memory Partition)

```c
/* bombertalk_size.r */
#include "Retro68APPL.r"

resource 'SIZE' (-1) {
    dontSaveScreen,
    acceptSuspendResumeEvents,
    enableOptionSwitch,
    canBackground,
    multiFinderAware,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    not32BitCompatible,
    reserved, reserved, reserved, reserved, reserved, reserved, reserved,
    1500 * 1024,    /* preferred size: 1.5 MB */
    1000 * 1024     /* minimum size: 1 MB */
};
```

---

## 4. PHASE 1: SKELETON APP - WINDOW & EVENT LOOP

### Goal
A window appears, responds to events, quits cleanly with Cmd-Q.

### game.h - Core Types and Constants

```c
/* game.h - Master header for BomberTalk */
#ifndef GAME_H
#define GAME_H

/* ---- Includes ---- */
/* Classic Mac headers come via Retro68 universal headers */
#include <Quickdraw.h>
#include <Windows.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Resources.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <GestaltEqu.h>

/* ---- Grid Constants ---- */
/*
 * Classic Bomberman uses a 15x13 grid (15 wide, 13 tall).
 * Each tile is 32x32 pixels.
 * Total play area: 480 x 416 pixels.
 *
 * This fits comfortably on a 640x480 screen (13" monitor)
 * with room for a title bar and status area.
 *
 * On a 512x342 screen (9" compact Mac), we could use
 * 16x16 tiles for a 240x208 play area, but for now we
 * target 13"+ color Macs.
 */
#define GRID_COLS       15
#define GRID_ROWS       13
#define TILE_SIZE       32      /* pixels per tile edge */

#define PLAY_WIDTH      (GRID_COLS * TILE_SIZE)   /* 480 */
#define PLAY_HEIGHT     (GRID_ROWS * TILE_SIZE)   /* 416 */

/* Window position (centered on 640x480 screen) */
#define WIN_LEFT        ((640 - PLAY_WIDTH) / 2)      /* 80 */
#define WIN_TOP         ((480 - PLAY_HEIGHT) / 2 + 20) /* 52 (menu bar offset) */

/* ---- Tile Types ---- */
#define TILE_FLOOR      0   /* Walkable empty space */
#define TILE_WALL       1   /* Indestructible wall (permanent grid) */
#define TILE_BLOCK      2   /* Destructible block (future: bombs destroy these) */
#define TILE_SPAWN      3   /* Player spawn point (walkable) */

/* ---- Directions ---- */
#define DIR_NONE        0
#define DIR_UP          1
#define DIR_DOWN        2
#define DIR_LEFT        3
#define DIR_RIGHT       4

/* ---- Timing ---- */
#define FRAME_TICKS     2   /* Game updates every 2 ticks = 30 fps */
#define EVENT_TICKS     0   /* WaitNextEvent sleep (0 = don't yield) */

/* ---- Resource IDs ---- */
#define rWindGame       128
#define rMenuApple      128
#define rMenuFile       129
#define rPictTiles      128     /* PICT resource: tile sheet */
#define rPictPlayer     129     /* PICT resource: player sprite sheet */

/* ---- Key Codes (from Mac keyboard hardware) ---- */
/*
 * Source: Black Art of Macintosh Game Programming, p.87
 * GetKeys() returns a KeyMap of 128 bits.
 * These are hardware key codes, NOT character codes.
 */
#define KEY_UP_ARROW    0x7E    /* 126 */
#define KEY_DOWN_ARROW  0x7D    /* 125 */
#define KEY_LEFT_ARROW  0x7B    /* 123 */
#define KEY_RIGHT_ARROW 0x7C    /* 124 */
#define KEY_SPACE       0x31    /* 49 - future: place bomb */
#define KEY_ESCAPE      0x35    /* 53 - future: pause */
#define KEY_Q           0x0C    /* 12 */

/* ---- Boolean (C89 has no bool) ---- */
#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

/* ---- Forward Declarations ---- */
/* (defined in their respective headers) */

#endif /* GAME_H */
```

### main.c - Entry Point and Event Loop

```c
/* main.c - BomberTalk entry point */
#include "game.h"
#include "tilemap.h"
#include "player.h"
#include "renderer.h"
#include "input.h"
#include "net.h"

/* ---- Globals ---- */
static WindowPtr    gWindow = NULL;
static int          gQuitting = FALSE;
static long         gLastFrameTick = 0;

/* ---- Toolbox Initialization ----
 *
 * Source: Tricks of the Mac Game Programming Gurus (1995), p.12
 * and Black Art of Macintosh Game Programming (1996), Chapter 2.
 *
 * Every classic Mac app MUST call these in order before doing
 * anything else. MaxApplZone() expands the application heap zone
 * to its limit (the boundary between stack and heap), ensuring
 * maximum contiguous memory is available. MoreMasters()
 * pre-allocates master pointer blocks so handle operations
 * don't stall later.
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
    FlushEvents(everyEvent, 0);  /* Clear stray events (per Inside Mac Vol I) */
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0L);
    InitCursor();

    /* Seed random number generator with tick count */
    qd.randSeed = TickCount();
}

/* ---- Menu Setup ----
 *
 * Minimal menu bar: Apple menu and File menu with Quit.
 * Source: Macintosh Game Programming Techniques (1996), Chapter 4.
 */
static void SetupMenus(void)
{
    MenuHandle appleMenu, fileMenu;

    appleMenu = NewMenu(rMenuApple, "\p\x14");  /* Apple symbol */
    AppendResMenu(appleMenu, 'DRVR');
    InsertMenu(appleMenu, 0);

    fileMenu = NewMenu(rMenuFile, "\pFile");
    AppendMenu(fileMenu, "\pQuit/Q");
    InsertMenu(fileMenu, 0);

    DrawMenuBar();
}

/* ---- Window Creation ----
 *
 * Source: Black Art of Macintosh Game Programming (1996), p.34
 *
 * We create a plain window (no grow box, no zoom) sized exactly
 * to our play area. Using NewCWindow for color support.
 * Window type noGrowDocProc (4) gives us a simple titled window.
 */
static void CreateGameWindow(void)
{
    Rect bounds;

    SetRect(&bounds, WIN_LEFT, WIN_TOP,
            WIN_LEFT + PLAY_WIDTH, WIN_TOP + PLAY_HEIGHT);

    gWindow = NewCWindow(
        0L,                     /* let system allocate storage */
        &bounds,                /* window rectangle */
        "\pBomberTalk",         /* title (Pascal string) */
        TRUE,                   /* visible immediately */
        noGrowDocProc,          /* window type: no grow, no zoom */
        (WindowPtr)-1L,         /* in front of all windows */
        FALSE,                  /* no close box (quit via menu) */
        0L                      /* refCon */
    );

    if (gWindow == NULL) {
        SysBeep(30);
        ExitToShell();
    }

    SetPort(gWindow);
}

/* ---- Event Handling ----
 *
 * Source: Black Art (1996) Chapter 3, Tricks of the Gurus (1995) p.45
 *
 * We use WaitNextEvent with sleep=0 so we never yield CPU time.
 * This is standard for action games - we want every cycle.
 * Keyboard input for movement uses GetKeys() polling (in input.c),
 * NOT keyDown events, because keyDown events go through the
 * OS repeat-rate delay which is way too slow for games.
 */
static void HandleMenuChoice(long menuChoice)
{
    short menu = (menuChoice >> 16) & 0xFFFF;
    short item = menuChoice & 0xFFFF;

    if (menu == rMenuFile && item == 1) {
        gQuitting = TRUE;
    }

    HiliteMenu(0);
}

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
        /* Redraw from offscreen buffer */
        Renderer_BlitToWindow(gWindow);
        EndUpdate(whichWindow);
        break;
    }
}

/* ---- Main Game Loop ----
 *
 * Source: Black Art (1996) Chapter 8 (Invaders), Tricks of Gurus (1995)
 *
 * The loop runs as fast as possible but only updates game state
 * every FRAME_TICKS (2 ticks = 30fps). This means:
 *   - Events are always processed promptly
 *   - Game logic runs at a fixed rate
 *   - Rendering happens after each game update
 *   - PeerTalk is polled every iteration for network responsiveness
 *
 * GetKeys() is called in UpdateGame() for real-time keyboard polling.
 * This is the standard approach from ALL the game programming books -
 * keyDown events are too slow for arcade-style input.
 */
static void MainLoop(void)
{
    EventRecord event;
    long currentTick;

    while (!gQuitting) {
        /* Always process system events */
        if (WaitNextEvent(everyEvent, &event, EVENT_TICKS, NULL)) {
            HandleEvent(&event);
        }

        /* Poll PeerTalk for network I/O */
        Net_Poll();

        /* Fixed-rate game update */
        currentTick = TickCount();
        if (currentTick - gLastFrameTick >= FRAME_TICKS) {
            gLastFrameTick = currentTick;

            /* Read keyboard, move player, check collisions */
            Input_Poll();
            Player_Update();

            /* Draw everything to offscreen, then blit to window */
            Renderer_DrawFrame(gWindow);
        }
    }
}

/* ---- Entry Point ---- */
int main(void)
{
    InitToolbox();
    SetupMenus();
    CreateGameWindow();

    /* Initialize subsystems */
    TileMap_Init();
    Player_Init();
    Renderer_Init(gWindow);
    Input_Init();
    Net_Init();

    /* Run the game */
    MainLoop();

    /* Clean up */
    Net_Shutdown();
    Renderer_Shutdown();
    DisposeWindow(gWindow);

    return 0;
}
```

### Key Decisions Explained

**Why WaitNextEvent with sleep=0?**
Source: Every book agrees. Black Art (1996) says: "Call WaitNextEvent() occasionally to keep the system responsive, but use sleep=0 for action games so you never surrender CPU time." Tricks of the Gurus (1995) uses the same pattern. For Bomberman-style gameplay, we need responsive controls.

**Why GetKeys() instead of keyDown events?**
Source: Black Art (1996) Chapter 4: "Events are queued and may be delayed up to 500ms... GetKeys returns 128 bits representing the state of all keys on the keyboard." Mac Game Programming (2002) Chapter 7 provides the complete `WasKeyPressed()` implementation we adapt. All books recommend GetKeys for real-time games.

**Why noGrowDocProc for the window?**
A game window shouldn't be resizable. The grid is fixed at 15x13 tiles. noGrowDocProc gives us a clean titled window with no grow box and no zoom box. Players can still drag it.

**Why NewCWindow instead of GetNewCWindow?**
We create the window programmatically rather than from a WIND resource so the constants are in one place (game.h). Either approach works, but this way we don't need a separate WIND resource.

---

## 5. PHASE 2: TILE MAP SYSTEM

### Goal
A 15x13 grid with floor tiles and wall blocks arranged in the classic Bomberman pattern.

### Classic Bomberman Grid Pattern

The standard Bomberman layout has:
- Outer border: all walls
- Interior: alternating wall pillars on even row/col intersections
- Spawn corners: guaranteed clear spaces for player starts
- Remaining spaces: mix of destructible blocks and floor

```
W W W W W W W W W W W W W W W
W . . B B B B B B B B B . . W
W . W B W B W B W B W B W . W
W B B B B B B B B B B B B B W
W B W B W B W B W B W B W B W
W B B B B B B B B B B B B B W
W B W B W B W B W B W B W B W
W B B B B B B B B B B B B B W
W B W B W B W B W B W B W B W
W B B B B B B B B B B B B B W
W . W B W B W B W B W B W . W
W . . B B B B B B B B B . . W
W W W W W W W W W W W W W W W

W = Indestructible wall (TILE_WALL)
B = Destructible block (TILE_BLOCK) - future: destroyed by bombs
. = Floor (TILE_FLOOR) - always walkable
```

Spawn corners (top-left, top-right, bottom-left, bottom-right) have guaranteed floor tiles so players can always move at spawn.

### maps/level1.h - Hardcoded Level Data

```c
/* maps/level1.h - Classic Bomberman level layout
 *
 * 15 columns x 13 rows.
 * 0 = floor, 1 = wall (permanent), 2 = block (destructible), 3 = spawn
 *
 * The classic pattern: walls form a grid of pillars at every
 * even-row/even-column intersection. Border is all walls.
 * Corners have spawn points with guaranteed clear space.
 */
#ifndef LEVEL1_H
#define LEVEL1_H

static const unsigned char kLevel1[GRID_ROWS][GRID_COLS] = {
/*       0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  */
/* 0 */ {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
/* 1 */ {1, 3, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 3, 1},
/* 2 */ {1, 0, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1},
/* 3 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/* 4 */ {1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
/* 5 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/* 6 */ {1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
/* 7 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/* 8 */ {1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
/* 9 */ {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1},
/*10 */ {1, 0, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 0, 1},
/*11 */ {1, 3, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 3, 1},
/*12 */ {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
};

#endif /* LEVEL1_H */
```

### tilemap.h

```c
/* tilemap.h - Tile map data structures and queries */
#ifndef TILEMAP_H
#define TILEMAP_H

#include "game.h"

/* The map is a simple 2D array of tile types */
typedef struct {
    unsigned char tiles[GRID_ROWS][GRID_COLS];
} TileMap;

/* Initialize the map (loads level1 data) */
void TileMap_Init(void);

/* Query tile at grid coordinates */
unsigned char TileMap_GetTile(short col, short row);

/* Set tile at grid coordinates (future: for destroying blocks) */
void TileMap_SetTile(short col, short row, unsigned char type);

/* Is the tile at (col, row) solid? (walls and blocks are solid) */
int TileMap_IsSolid(short col, short row);

/* Convert pixel coordinates to grid coordinates */
short TileMap_PixelToCol(short pixelX);
short TileMap_PixelToRow(short pixelY);

/* Convert grid coordinates to pixel coordinates (top-left of tile) */
short TileMap_ColToPixel(short col);
short TileMap_RowToPixel(short row);

/* Get pointer to map data (for renderer) */
const TileMap* TileMap_GetMap(void);

#endif /* TILEMAP_H */
```

### tilemap.c

```c
/* tilemap.c - Tile map implementation
 *
 * Source: Tricks of the Mac Game Programming Gurus (1995)
 *   Chapter "Dungeon" - tile enum (empty, wall, player, enemy...)
 *   with tileArray[kArraySizeH][kArraySizeV] pattern.
 *
 * Source: Mac Game Programming (2002) Chapter 10
 *   GameLevel class with levelMap as flat array indexed by
 *   mapIndex = (row * levelWidth) + column
 *   Tile types: kWallTile, kFloorTile, kDoorTile, etc.
 *
 * We use a simple 2D array (C89 style) instead of classes.
 * The map is copied from the const level data at init time
 * so we can modify it later (destroying blocks).
 */
#include "tilemap.h"
#include "maps/level1.h"

static TileMap gMap;

void TileMap_Init(void)
{
    short row, col;
    for (row = 0; row < GRID_ROWS; row++) {
        for (col = 0; col < GRID_COLS; col++) {
            gMap.tiles[row][col] = kLevel1[row][col];
        }
    }
}

unsigned char TileMap_GetTile(short col, short row)
{
    if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS)
        return TILE_WALL;  /* out of bounds = solid */
    return gMap.tiles[row][col];
}

void TileMap_SetTile(short col, short row, unsigned char type)
{
    if (col >= 0 && col < GRID_COLS && row >= 0 && row < GRID_ROWS)
        gMap.tiles[row][col] = type;
}

int TileMap_IsSolid(short col, short row)
{
    unsigned char t = TileMap_GetTile(col, row);
    return (t == TILE_WALL || t == TILE_BLOCK);
}

short TileMap_PixelToCol(short pixelX)
{
    return pixelX / TILE_SIZE;
}

short TileMap_PixelToRow(short pixelY)
{
    return pixelY / TILE_SIZE;
}

short TileMap_ColToPixel(short col)
{
    return col * TILE_SIZE;
}

short TileMap_RowToPixel(short row)
{
    return row * TILE_SIZE;
}

const TileMap* TileMap_GetMap(void)
{
    return &gMap;
}
```

### Design Notes

**Why copy the level data instead of using it directly?**
The const array is read-only. When we later add bombs that destroy blocks, we need to modify the map at runtime. Copying at init gives us a mutable working copy.

**Why return TILE_WALL for out-of-bounds?**
This is a safety net. If any movement code accidentally queries outside the grid, it gets "solid" back, preventing the player from escaping the map. Source: Mac Game Programming (2002) uses the same pattern in its CanMoveUp/CanMoveDown functions.

**Why store as [row][col] but expose as (col, row)?**
The internal array is [row][col] for cache-friendly row iteration during rendering (drawing left-to-right, top-to-bottom). The API uses (col, row) which maps to (x, y) for the callers, which is more intuitive for game logic.

---

## 6. PHASE 3: PLAYER MOVEMENT & COLLISION

### Goal
Player moves tile-by-tile on arrow key press. Cannot walk through walls or blocks.

### Movement Model: Grid-Locked vs Smooth

**We use grid-locked movement for the starter.** The player occupies exactly one tile at a time and moves to an adjacent tile. This is:
- Simplest to implement
- Exactly how classic Bomberman works
- Collision detection is trivial: check the destination tile
- No sub-tile alignment issues

Future enhancement: smooth interpolated movement (animate between tiles over several frames) can be added later by tracking a "moving" state and interpolating pixel position.

### player.h

```c
/* player.h - Player state and movement */
#ifndef PLAYER_H
#define PLAYER_H

#include "game.h"

typedef struct {
    short gridCol;      /* Current tile column (0-14) */
    short gridRow;      /* Current tile row (0-12) */
    short pixelX;       /* Top-left pixel X (= gridCol * TILE_SIZE) */
    short pixelY;       /* Top-left pixel Y (= gridRow * TILE_SIZE) */
    short facing;       /* DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT */
    short animFrame;    /* Current animation frame (future use) */
    int   alive;        /* Is this player active? */
} Player;

/* Initialize player at spawn point */
void Player_Init(void);

/* Process movement based on current input state */
void Player_Update(void);

/* Get player state (for renderer) */
const Player* Player_GetState(void);

#endif /* PLAYER_H */
```

### player.c

```c
/* player.c - Player movement and tile collision
 *
 * Source: Mac Game Programming (2002) Chapter 12
 *   PhysicsController::CanMoveUp/Down/Left/Right pattern:
 *   - Calculate which tile the player would enter
 *   - Look up tile type in the level map
 *   - If kWallTile, deny movement
 *   - Check BOTH edges of the player (left+right for vertical,
 *     top+bottom for horizontal) to handle sprites wider than 1 tile
 *
 * Source: Tricks of the Gurus (1995) "Dungeon" chapter:
 *   Grid-based movement: check tileArray[newH][newV] before moving.
 *   switch(tileArray[clickedTile.h][clickedTile.v]) {
 *       case wall: SysBeep(1); break;
 *       case empty: // move player
 *   }
 *
 * For grid-locked Bomberman movement, collision is simple:
 * just check if the destination tile is solid.
 */
#include "player.h"
#include "tilemap.h"
#include "input.h"

static Player gPlayer;

/* Movement cooldown to prevent moving every single frame.
 * At 30fps, moving every frame would be way too fast.
 * We allow one move every MOVE_COOLDOWN frames.
 * 6 frames at 30fps = 5 moves/second, which feels right
 * for Bomberman-style grid movement.
 */
#define MOVE_COOLDOWN   6

static short gMoveCooldown = 0;

void Player_Init(void)
{
    /* Spawn at top-left spawn point (grid position 1,1) */
    gPlayer.gridCol = 1;
    gPlayer.gridRow = 1;
    gPlayer.pixelX = TileMap_ColToPixel(1);
    gPlayer.pixelY = TileMap_RowToPixel(1);
    gPlayer.facing = DIR_DOWN;
    gPlayer.animFrame = 0;
    gPlayer.alive = TRUE;
}

/* Try to move the player in a direction.
 * Returns TRUE if the move succeeded.
 *
 * This is the core collision check. We calculate the
 * destination tile and check if it's solid.
 *
 * Source: Mac Game Programming (2002) CanMoveUp():
 *   "Test tile distance pixels above... if kWallTile return false"
 * We simplify because the player is exactly 1 tile in size
 * and always grid-aligned, so we only need to check one tile.
 */
static int TryMove(short dcol, short drow)
{
    short newCol = gPlayer.gridCol + dcol;
    short newRow = gPlayer.gridRow + drow;

    /* Check if destination tile is walkable */
    if (TileMap_IsSolid(newCol, newRow))
        return FALSE;

    /* Move the player */
    gPlayer.gridCol = newCol;
    gPlayer.gridRow = newRow;
    gPlayer.pixelX = TileMap_ColToPixel(newCol);
    gPlayer.pixelY = TileMap_RowToPixel(newRow);

    return TRUE;
}

void Player_Update(void)
{
    if (!gPlayer.alive)
        return;

    /* Cooldown timer */
    if (gMoveCooldown > 0) {
        gMoveCooldown--;
        return;
    }

    /* Check input and try to move
     *
     * Priority: up > down > left > right (arbitrary).
     * Only one direction per update - no diagonal movement
     * in Bomberman.
     *
     * Source: Mac Game Programming (2002) Chapter 7:
     *   "if (WasKeyPressed(kUpArrow, currentKeyboardState))
     *       movedUp = true;"
     *   We check key state, not events, for responsive controls.
     */
    if (Input_IsKeyDown(KEY_UP_ARROW)) {
        gPlayer.facing = DIR_UP;
        if (TryMove(0, -1))
            gMoveCooldown = MOVE_COOLDOWN;
    }
    else if (Input_IsKeyDown(KEY_DOWN_ARROW)) {
        gPlayer.facing = DIR_DOWN;
        if (TryMove(0, 1))
            gMoveCooldown = MOVE_COOLDOWN;
    }
    else if (Input_IsKeyDown(KEY_LEFT_ARROW)) {
        gPlayer.facing = DIR_LEFT;
        if (TryMove(-1, 0))
            gMoveCooldown = MOVE_COOLDOWN;
    }
    else if (Input_IsKeyDown(KEY_RIGHT_ARROW)) {
        gPlayer.facing = DIR_RIGHT;
        if (TryMove(1, 0))
            gMoveCooldown = MOVE_COOLDOWN;
    }
}

const Player* Player_GetState(void)
{
    return &gPlayer;
}
```

### input.h

```c
/* input.h - Keyboard polling */
#ifndef INPUT_H
#define INPUT_H

#include "game.h"

/* Initialize input system */
void Input_Init(void);

/* Poll keyboard state (call once per frame before Player_Update) */
void Input_Poll(void);

/* Check if a specific key is currently held down.
 * keyCode is a hardware key code (KEY_UP_ARROW, etc.)
 */
int Input_IsKeyDown(unsigned char keyCode);

#endif /* INPUT_H */
```

### input.c

```c
/* input.c - Keyboard polling with GetKeys()
 *
 * Source: Mac Game Programming (2002) Chapter 7, p.186
 *   Boolean WasKeyPressed(short keyCode, KeyMap theKeyboard) {
 *       short keyMapIndex = keyCode / 8;
 *       short bitInIndex = keyCode % 8;
 *       short keyTestValue = 1 << bitInIndex;
 *       char *startOfKeymap = (char *)&theKeyboard[0];
 *       char keyMapEntryValue = *(startOfKeymap + keyMapIndex);
 *       return ((keyMapEntryValue & keyTestValue) != 0);
 *   }
 *
 * Source: Tricks of the Gurus (1995) p.52:
 *   GetKeys(myKeyMap);
 *   thePointer = (char *)&myKeyMap[0];
 *   if(thePointer[6] & 0x40) { // space key }
 *
 * Source: Black Art (1996) Chapter 4:
 *   "GetKeys returns 128 bits representing the state of all keys"
 *
 * Inside Macintosh Vol I defines KeyMap as:
 *   TYPE KeyMap = PACKED ARRAY[0..127] OF BOOLEAN;
 * This is 128 bits (16 bytes). In C, it's typically declared as
 * an array of 4 unsigned longs. Each key has one bit.
 *
 * GetKeys() is the ONLY way to get real-time keyboard state.
 * It reads the keyboard hardware directly, bypassing the event
 * queue entirely. This means:
 *   - No key repeat delay
 *   - Multiple simultaneous keys detected
 *   - Zero latency
 *
 * To test key N: check byte (N/8), bit (N%8).
 * The bit-test formula: (keyBytes[N >> 3] & (1 << (N & 7)))
 * This is equivalent to the Mac Game Programming 2002 approach
 * of (keyMapEntry >> bitInIndex) & 0x01, and to the Tricks of
 * the Gurus approach of direct byte/bitmask testing.
 */
#include "input.h"

static KeyMap gKeyMap;

void Input_Init(void)
{
    /* Clear the key map */
    gKeyMap[0] = 0;
    gKeyMap[1] = 0;
    gKeyMap[2] = 0;
    gKeyMap[3] = 0;
}

void Input_Poll(void)
{
    GetKeys(gKeyMap);
}

int Input_IsKeyDown(unsigned char keyCode)
{
    unsigned char *keyBytes = (unsigned char *)gKeyMap;
    short byteIndex = keyCode >> 3;      /* keyCode / 8 */
    short bitIndex = keyCode & 0x07;     /* keyCode % 8 */

    return (keyBytes[byteIndex] & (1 << bitIndex)) != 0;
}
```

### Design Notes

**Why grid-locked movement instead of smooth pixel movement?**
Classic Bomberman uses grid-locked movement. The player is always aligned to the grid. This makes bomb placement trivial (always on a grid cell), collision with blocks trivial (check one tile), and multiplayer sync trivial (just send grid positions). Smooth movement can be layered on top later as visual interpolation.

**Why a cooldown timer instead of move-on-keypress?**
With GetKeys() polling at 30fps, a held arrow key would move the player every frame (30 tiles/second) which is way too fast. The cooldown of 6 frames means ~5 moves per second, matching classic Bomberman feel. The player holds the key and the character moves at a steady pace.

**Why not diagonal movement?**
Bomberman never has diagonal movement. The grid corridors are exactly 1 tile wide, so diagonal movement would clip through walls. We enforce one-axis-at-a-time with if/else if.

**Why priority ordering (up > down > left > right)?**
When multiple arrow keys are pressed simultaneously, we need a deterministic choice. The priority is arbitrary but consistent. Classic Bomberman typically uses the most recently pressed key, which requires tracking key-down events - a future enhancement.

---

## 7. PHASE 4: DOUBLE-BUFFERED RENDERING

### Goal
Flicker-free drawing of the tile map and player sprite using offscreen GWorlds.

### renderer.h

```c
/* renderer.h - Offscreen buffer management and drawing */
#ifndef RENDERER_H
#define RENDERER_H

#include "game.h"

/* Initialize offscreen buffers */
void Renderer_Init(WindowPtr window);

/* Shut down and dispose GWorlds */
void Renderer_Shutdown(void);

/* Draw a complete frame: map + player to offscreen, then blit */
void Renderer_DrawFrame(WindowPtr window);

/* Blit current offscreen buffer to window (for update events) */
void Renderer_BlitToWindow(WindowPtr window);

#endif /* RENDERER_H */
```

### renderer.c

```c
/* renderer.c - Double-buffered rendering with GWorlds
 *
 * Source: Black Art of Macintosh Game Programming (1996)
 *   Chapter 8 (Invaders! game loop):
 *   "1. Clear offscreen buffer with background
 *    2. Draw all sprites to offscreen buffer
 *    3. Copy entire offscreen buffer to screen (one CopyBits call)
 *    This prevents visible flicker"
 *
 * Source: Tricks of the Gurus (1995) p.67:
 *   Three-step offscreen animation cycle:
 *   1. Restore Background
 *   2. Draw Objects
 *   3. Copy to Screen (single CopyBits)
 *
 * Source: Sex, Lies, and Video Games (1996):
 *   "Buffered Animation (Preferred for Games)"
 *   Uses GetGWorld/SetGWorld to switch drawing target
 *
 * Source: Macintosh Game Programming Techniques (1996):
 *   NewGWorld(&pGWorldScores, sPixelDepth, &rectScores, nil, nil, 0);
 *   bitmapScores = ((GrafPtr)pGWorldScores)->portBits;
 *
 * We use TWO offscreen buffers:
 *   1. gBackground: Contains the fully drawn tile map.
 *      Redrawn only when the map changes (block destroyed).
 *   2. gWorkBuffer: Working buffer for each frame.
 *      Background is copied here, then sprites drawn on top,
 *      then the whole thing is blitted to the window.
 *
 * This means drawing the map every frame is just one CopyBits
 * (background -> work buffer), not 195 individual tile draws.
 *
 * OPTIMIZATION: Color table seed synchronization.
 * Source: Tricks of the Gurus (1995) p.73 (undocumented in Inside Macintosh):
 *   "Copy the screen's color table seed into the source pixmap.
 *    This will minimize CopyBits()' setup time."
 *   (*(*gOffscreenPixels)->pmTable)->ctSeed =
 *       (*(*(*GetGDevice())->gdPMap)->pmTable)->ctSeed;
 *
 * This trick is not in Inside Macintosh but is verified in
 * Tricks of the Gurus (exact code on p.119-120) and widely
 * used. Without it, CopyBits does expensive color mapping.
 * With matched seeds, it does a fast raw copy.
 * NOTE: Only relevant for indexed color modes (8-bit).
 * At 16-bit or 32-bit depth there are no color tables.
 */
#include "renderer.h"
#include "tilemap.h"
#include "player.h"

static GWorldPtr    gBackground = NULL;   /* Pre-rendered tile map */
static GWorldPtr    gWorkBuffer = NULL;   /* Per-frame work buffer */
static PixMapHandle gBackPix = NULL;
static PixMapHandle gWorkPix = NULL;
static Rect         gPlayRect;            /* {0,0,PLAY_HEIGHT,PLAY_WIDTH} */

/* Placeholder colors for tiles until real graphics are loaded.
 * These draw colored rectangles:
 *   Floor = dark green (classic Bomberman grass)
 *   Wall  = dark gray (stone pillars)
 *   Block = brown (destructible soft blocks)
 *   Spawn = lighter green (same as floor but marked)
 */
static RGBColor kColorFloor  = {0x2000, 0x6000, 0x2000};  /* dark green */
static RGBColor kColorWall   = {0x4000, 0x4000, 0x4000};  /* gray */
static RGBColor kColorBlock  = {0x8000, 0x5000, 0x2000};  /* brown */
static RGBColor kColorSpawn  = {0x3000, 0x7000, 0x3000};  /* light green */
static RGBColor kColorPlayer = {0xFFFF, 0xFFFF, 0xFFFF};  /* white */

/* ---- GWorld Creation ----
 *
 * Source: Black Art (1996) p.112:
 *   err = NewGWorld(&offscreenBuffer, 8, &bufferBounds,
 *                   NULL, NULL, 0);
 *
 * Source: Mac Game Programming (2002) Chapter 4:
 *   error = NewGWorld(&bufferStorage, colorDepth, &bufferRect,
 *                     nil, nil, flags);
 *
 * We use 0 for depth (auto-detect from screen), which the
 * Mac Game Programming book recommends for compatibility.
 * On a 256-color Mac, this gives us 8-bit.
 * On a thousands-color Mac, this gives us 16-bit.
 */
void Renderer_Init(WindowPtr window)
{
    QDErr err;

    SetRect(&gPlayRect, 0, 0, PLAY_WIDTH, PLAY_HEIGHT);

    /* Create background buffer */
    err = NewGWorld(&gBackground, 0, &gPlayRect, NULL, NULL, 0);
    if (err != noErr || gBackground == NULL) {
        SysBeep(30);
        ExitToShell();
    }
    gBackPix = GetGWorldPixMap(gBackground);

    /* Create work buffer */
    err = NewGWorld(&gWorkBuffer, 0, &gPlayRect, NULL, NULL, 0);
    if (err != noErr || gWorkBuffer == NULL) {
        SysBeep(30);
        ExitToShell();
    }
    gWorkPix = GetGWorldPixMap(gWorkBuffer);

    /* Draw the initial tile map to the background buffer */
    Renderer_RebuildBackground();
}

void Renderer_Shutdown(void)
{
    if (gWorkBuffer) {
        DisposeGWorld(gWorkBuffer);
        gWorkBuffer = NULL;
    }
    if (gBackground) {
        DisposeGWorld(gBackground);
        gBackground = NULL;
    }
}

/* ---- Draw Tile Map to Background Buffer ----
 *
 * Source: Tricks of the Gurus (1995) DrawTile():
 *   SetRect(&tileRectangle, h * kTileSizeH, v * kTileSizeV,
 *           (h+1) * kTileSizeH, (v+1) * kTileSizeV);
 *   switch(tileArray[h][v]) {
 *       case wall:  DrawPicture(wallTile, &tileRectangle); break;
 *       case empty: DrawPicture(floorTile, &tileRectangle); break;
 *   }
 *
 * For now we draw colored rectangles. When PICT resources
 * are added, this switches to CopyBits from a tile sheet.
 *
 * Future enhancement: Load a tile sheet PICT into its own
 * GWorld, then CopyBits each 32x32 region to the background.
 */
static void DrawTileRect(short col, short row, RGBColor *color)
{
    Rect tileRect;
    SetRect(&tileRect,
            col * TILE_SIZE, row * TILE_SIZE,
            (col + 1) * TILE_SIZE, (row + 1) * TILE_SIZE);
    RGBForeColor(color);
    PaintRect(&tileRect);
}

static void Renderer_RebuildBackground(void)
{
    CGrafPtr oldPort;
    GDHandle oldDevice;
    short row, col;
    unsigned char tile;

    GetGWorld(&oldPort, &oldDevice);
    SetGWorld(gBackground, NULL);
    LockPixels(gBackPix);

    for (row = 0; row < GRID_ROWS; row++) {
        for (col = 0; col < GRID_COLS; col++) {
            tile = TileMap_GetTile(col, row);
            switch (tile) {
            case TILE_FLOOR:
                DrawTileRect(col, row, &kColorFloor);
                break;
            case TILE_WALL:
                DrawTileRect(col, row, &kColorWall);
                break;
            case TILE_BLOCK:
                DrawTileRect(col, row, &kColorBlock);
                break;
            case TILE_SPAWN:
                DrawTileRect(col, row, &kColorSpawn);
                break;
            }
        }
    }

    UnlockPixels(gBackPix);
    SetGWorld(oldPort, oldDevice);
}

/* ---- Draw One Frame ----
 *
 * Source: Black Art (1996) Chapter 8:
 *   // Clear offscreen buffer with background
 *   CopyBits(...gBackgroundGWorld..., ...offscreenBuffer...,
 *            srcCopy, NULL);
 *   // Draw all sprites to offscreen
 *   UpdateAllSprites(offscreenBuffer);
 *   // Copy offscreen buffer to screen
 *   CopyBits(...offscreenBuffer..., ...mainWindow->portBits...,
 *            srcCopy, NULL);
 *
 * Steps:
 *   1. Copy background -> work buffer (restores clean map)
 *   2. Draw player sprite onto work buffer
 *   3. Copy work buffer -> window (single
 *


































blit)
 */
void Renderer_DrawFrame(WindowPtr window)
{
    CGrafPtr oldPort;
    GDHandle oldDevice;
    const Player *player;
    Rect playerRect;

    /* Step 1: Copy background to work buffer */
    GetGWorld(&oldPort, &oldDevice);

    LockPixels(gBackPix);
    LockPixels(gWorkPix);

    SetGWorld(gWorkBuffer, NULL);

    CopyBits((BitMap *)*gBackPix,
             (BitMap *)*gWorkPix,
             &gPlayRect, &gPlayRect,
             srcCopy, NULL);

    /* Step 2: Draw player onto work buffer */
    player = Player_GetState();
    if (player->alive) {
        SetRect(&playerRect,
                player->pixelX + 2, player->pixelY + 2,
                player->pixelX + TILE_SIZE - 2,
                player->pixelY + TILE_SIZE - 2);

        /* Placeholder: white rectangle with 2px inset
         * Future: CopyMask() from player sprite sheet GWorld
         * with a separate mask GWorld for transparency.
         *
         * NOTE: The game books reference a "transparent" transfer
         * mode for CopyBits, but this is NOT documented in Inside
         * Macintosh. Use CopyMask() instead for reliable sprite
         * transparency across all systems. CopyMask is documented
         * in Inside Macintosh Vol V (Color QuickDraw).
         */
        RGBForeColor(&kColorPlayer);
        PaintRect(&playerRect);
    }

    UnlockPixels(gWorkPix);
    UnlockPixels(gBackPix);

    /* Step 3: Blit work buffer to window */
    SetGWorld(oldPort, oldDevice);
    Renderer_BlitToWindow(window);
}

/* ---- Blit to Window ----
 *
 * Source: Tricks of the Gurus (1995) blitToScreen():
 *   ForeColor(blackColor);
 *   BackColor(whiteColor);
 *   (*(*gOffscreenPixels)->pmTable)->ctSeed =
 *       (*(*(*GetGDevice())->gdPMap)->pmTable)->ctSeed;
 *   CopyBits(...gOffscreenPixels..., ...gMainWindow->portBits...,
 *            srcCopy, NULL);
 *
 * CRITICAL: Set fore/back color to black/white before CopyBits.
 * If they're set to other colors, CopyBits will "colorize" the
 * output, producing wrong colors.
 *
 * CRITICAL: Sync color table seeds for fast path.
 */
void Renderer_BlitToWindow(WindowPtr window)
{
    CGrafPtr oldPort;
    GDHandle oldDevice;

    GetGWorld(&oldPort, &oldDevice);
    SetPort(window);

    ForeColor(blackColor);
    BackColor(whiteColor);

    LockPixels(gWorkPix);

    CopyBits((BitMap *)*gWorkPix,
             &window->portBits,
             &gPlayRect, &gPlayRect,
             srcCopy, NULL);

    UnlockPixels(gWorkPix);

    SetGWorld(oldPort, oldDevice);
}
```

### Design Notes

**Why two GWorlds instead of one?**
The background buffer contains the pre-rendered tile map. We only rebuild it when the map changes (future: block destruction). Each frame, we copy the background to the work buffer, draw sprites on top, then blit to screen. Without this, we'd need to redraw all 195 tiles every frame, which is slow. Source: Black Art (1996) uses exactly this pattern in Invaders!.

**Why 0 for pixel depth in NewGWorld?**
Source: Mac Game Programming (2002) says "0 = use device depth". This auto-detects the screen's color depth. On a 256-color Mac it's 8-bit, on thousands it's 16-bit. This avoids CopyBits having to convert between depths on every blit.

**Why ForeColor(blackColor)/BackColor(whiteColor) before CopyBits?**
Source: Tricks of the Gurus (1995) and Sex, Lies, and Video Games (1996) both warn about this. CopyBits uses the port's fore/back colors in its transfer calculations. If they're not black/white, the output gets colorized incorrectly. This is one of the most common classic Mac graphics bugs.

**Why colored rectangles instead of real graphics?**
This is a starter project. The colored rectangles let us verify the entire pipeline (map data -> rendering -> collision) before worrying about art assets. Real PICT-based tiles are Phase 5.

**Future: Sprite drawing with transparency**
The game books reference a "transparent" transfer mode for CopyBits, but this mode is NOT documented in Inside Macintosh and may not work on all systems. Instead, use CopyMask() (documented in Inside Macintosh Vol V) with a separate mask bitmap for reliable sprite transparency. CopyMask takes separate source, mask, and destination bitmaps. The mask bitmap has white pixels where the sprite should be drawn and black where it should be transparent. This is the officially documented approach and works on all Color QuickDraw systems.

---

## 8. PHASE 5: GRAPHICS ASSETS & RESOURCE FILE

### Goal
Replace colored rectangles with real tile graphics loaded from PICT resources.

### Tile Sheet Design

A single PICT resource containing all tiles arranged in a strip:

```
+--------+--------+--------+--------+
| Floor  | Wall   | Block  | Spawn  |
| 32x32  | 32x32  | 32x32  | 32x32  |
| Tile 0  | Tile 1  | Tile 2  | Tile 3  |
+--------+--------+--------+--------+

Total: 128 x 32 pixels
```

### Player Sprite Sheet Design

A single PICT with the player facing each direction:

```
+--------+--------+--------+--------+
| Down   | Up     | Left   | Right  |
| 32x32  | 32x32  | 32x32  | 32x32  |
| Frame 0 | Frame 1 | Frame 2 | Frame 3 |
+--------+--------+--------+--------+

Total: 128 x 32 pixels
```

Future: Multiple frames per direction for walk animation (3 frames each = 12 frames total = 384 x 32 strip).

### Loading Tile Sheet into GWorld

```c
/* Pattern from Black Art (1996) and all other books:
 *
 *   PicHandle myPict = GetPicture(rPictTiles);
 *   NewGWorld(&tileSheetGWorld, 0, &pictBounds, NULL, NULL, 0);
 *   SetGWorld(tileSheetGWorld, NULL);
 *   LockPixels(GetGWorldPixMap(tileSheetGWorld));
 *   DrawPicture(myPict, &pictBounds);
 *   UnlockPixels(...)
 *   ReleaseResource((Handle)myPict);
 *
 * The PICT is drawn into the GWorld once at load time.
 * Then we CopyBits from the tile sheet GWorld to the
 * background buffer for each tile.
 */
static GWorldPtr    gTileSheet = NULL;
static PixMapHandle gTileSheetPix = NULL;

static void LoadTileSheet(void)
{
    PicHandle pic;
    Rect picBounds;
    CGrafPtr oldPort;
    GDHandle oldDevice;
    QDErr err;

    pic = GetPicture(rPictTiles);
    if (pic == NULL) return;  /* fall back to colored rects */

    picBounds = (**pic).picFrame;
    OffsetRect(&picBounds, -picBounds.left, -picBounds.top);

    err = NewGWorld(&gTileSheet, 0, &picBounds, NULL, NULL, 0);
    if (err != noErr) {
        ReleaseResource((Handle)pic);
        return;
    }

    gTileSheetPix = GetGWorldPixMap(gTileSheet);

    GetGWorld(&oldPort, &oldDevice);
    SetGWorld(gTileSheet, NULL);
    LockPixels(gTileSheetPix);
    EraseRect(&picBounds);
    DrawPicture(pic, &picBounds);
    UnlockPixels(gTileSheetPix);
    SetGWorld(oldPort, oldDevice);

    ReleaseResource((Handle)pic);
}

/* Draw one tile from the tile sheet to the background buffer.
 *
 * Source: Mac Game Programming (2002) DrawTileFromLevelMap():
 *   Point tileGridLocation = GetGridLocation(tileNumber);
 *   Rect tileRect = GetRectFromGridLocation(tileGridLocation);
 *   tileBlitter.Setup(background, tileRect, backgroundRect);
 *   tileBlitter.DrawImageToOffscreenBuffer();
 */
static void DrawTileFromSheet(short tileIndex, short destCol, short destRow)
{
    Rect srcRect, destRect;

    SetRect(&srcRect,
            tileIndex * TILE_SIZE, 0,
            (tileIndex + 1) * TILE_SIZE, TILE_SIZE);

    SetRect(&destRect,
            destCol * TILE_SIZE, destRow * TILE_SIZE,
            (destCol + 1) * TILE_SIZE, (destRow + 1) * TILE_SIZE);

    LockPixels(gTileSheetPix);

    CopyBits((BitMap *)*gTileSheetPix,
             (BitMap *)*gBackPix,
             &srcRect, &destRect,
             srcCopy, NULL);

    UnlockPixels(gTileSheetPix);
}
```

### Resource File (Rez Source)

```c
/* resources/bombertalk.r */
#include "Retro68APPL.r"

/* Menu bar */
resource 'MBAR' (128) {
    { 128, 129 }
};

resource 'MENU' (128) {
    128, textMenuProc, 0x7FFFFFFD, enabled, apple,
    { "About BomberTalk...", noIcon, noKey, noMark, plain }
};

resource 'MENU' (129) {
    129, textMenuProc, 0x7FFFFFFD, enabled, "File",
    { "Quit", noIcon, "Q", noMark, plain }
};

/* PICT resources would be added via ResEdit or Resourcerer
 * after creating them in Photoshop 3 on the classic Mac.
 * They can also be added by converting from modern formats
 * using a tool on the build machine.
 *
 * resource 'PICT' (128, "Tiles") { ... };
 * resource 'PICT' (129, "Player") { ... };
 */
```

### Creating Graphics in Photoshop 3 on Classic Mac

See Section 10 for detailed instructions.

---

## 9. PHASE 6: PEERTALK INTEGRATION (MULTIPLAYER STUB)

### Goal
Initialize PeerTalk, discover peers, prepare for future multiplayer game state sync.

### net.h

```c
/* net.h - PeerTalk networking wrapper */
#ifndef NET_H
#define NET_H

#include "game.h"
#include "peertalk.h"

/* Initialize PeerTalk with game name */
void Net_Init(void);

/* Shut down PeerTalk cleanly */
void Net_Shutdown(void);

/* Poll PeerTalk for I/O (call every main loop iteration) */
void Net_Poll(void);

/* Is a remote peer connected? */
int Net_IsConnected(void);

#endif /* NET_H */
```

### net.c

```c
/* net.c - PeerTalk integration
 *
 * PeerTalk SDK API (from /home/matt/Desktop/peertalk/include/peertalk.h):
 *
 *   PT_Init(ctx, name)         - Initialize with peer name
 *   PT_Shutdown(ctx)           - Clean shutdown, send goodbyes
 *   PT_StartDiscovery(ctx)     - Begin UDP broadcast/listen (port 7353)
 *   PT_Poll(ctx)               - Drive all I/O, fire callbacks
 *   PT_OnPeerDiscovered(ctx, cb, ud) - Peer found callback
 *   PT_OnConnected(ctx, cb, ud)      - TCP connection established
 *   PT_OnMessage(ctx, type, cb, ud)  - Message received callback
 *   PT_Connect(ctx, peer)            - Initiate TCP connection
 *   PT_Send(ctx, peer, type, data, len) - Send to one peer
 *   PT_Broadcast(ctx, type, data, len)  - Send to all peers
 *   PT_RegisterMessage(ctx, type, transport) - Set message transport
 *
 * PeerTalk discovery uses UDP broadcast on port 7353.
 * TCP reliable messages on port 7354.
 * UDP fast messages on port 7355.
 *
 * For Bomberman, we'll use:
 *   PT_RELIABLE for game state changes (player moves, bomb placed)
 *   PT_FAST for position updates (future: smooth interpolation)
 *
 * Memory: PeerTalk pre-allocates everything at PT_Init() based on
 * available memory (FreeMem() on Classic Mac). On a 4MB Mac it
 * allocates ~50KB for 2-8 peer slots. Zero malloc after init.
 *
 * IMPORTANT: PT_Init() calls MaxApplZone() and MoreMasters()
 * internally on Classic Mac. Since we also call them in
 * InitToolbox(), calling them twice is harmless.
 */
#include "net.h"

static PT_Context *gPTCtx = NULL;
static int gConnected = FALSE;

/* ---- Callbacks ---- */

static void OnPeerDiscovered(PT_Peer *peer, void *ud)
{
    /* Future: show peer name in lobby UI */
    (void)ud;
    (void)peer;
}

static void OnConnected(PT_Peer *peer, void *ud)
{
    (void)ud;
    (void)peer;
    gConnected = TRUE;
}

static void OnDisconnected(PT_Peer *peer, int reason, void *ud)
{
    (void)ud;
    (void)peer;
    (void)reason;
    gConnected = FALSE;
}

/* ---- Message Types ----
 *
 * Define game-specific message types.
 * PeerTalk message types are 0-254 (255 is reserved for goodbye).
 */
#define MSG_PLAYER_MOVE     1   /* Player moved to new grid position */
#define MSG_PLAYER_BOMB     2   /* Player placed a bomb (future) */
#define MSG_GAME_STATE      3   /* Full game state sync (future) */

/* Player move message: 4 bytes */
typedef struct {
    unsigned char playerID;     /* 0-3 */
    unsigned char gridCol;      /* 0-14 */
    unsigned char gridRow;      /* 0-12 */
    unsigned char facing;       /* DIR_UP/DOWN/LEFT/RIGHT */
} MoveMessage;

/* ---- Init / Shutdown ---- */

void Net_Init(void)
{
    PT_Status status;

    status = PT_Init(&gPTCtx, "BomberTalk");
    if (status != PT_OK) {
        /* Networking unavailable - game still works in single player */
        gPTCtx = NULL;
        return;
    }

    /* Register callbacks */
    PT_OnPeerDiscovered(gPTCtx, OnPeerDiscovered, NULL);
    PT_OnConnected(gPTCtx, OnConnected, NULL);
    PT_OnDisconnected(gPTCtx, OnDisconnected, NULL);

    /* Register message types */
    PT_RegisterMessage(gPTCtx, MSG_PLAYER_MOVE, PT_RELIABLE);

    /* Start discovery - begin broadcasting presence on LAN */
    PT_StartDiscovery(gPTCtx);
}

void Net_Shutdown(void)
{
    if (gPTCtx) {
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

int Net_IsConnected(void)
{
    return gConnected;
}
```

### Future Multiplayer Architecture

When multiplayer is added, the flow will be:

```
Player 1 presses Right arrow
  |
  +-- Player_Update() moves local player to (5, 3)
  +-- Net_SendMove(playerID=0, col=5, row=3, facing=DIR_RIGHT)
  |     +-- PT_Broadcast(ctx, MSG_PLAYER_MOVE, &moveMsg, 4)
  |
Player 2 receives MSG_PLAYER_MOVE
  +-- OnMoveReceived(peer, data, len)
  +-- RemotePlayer_SetPosition(playerID=0, col=5, row=3)
  +-- Renderer draws remote player at new position
```

The PeerTalk SDK handles all the networking complexity:
- Discovery (finding other players on LAN)
- Connection (establishing TCP)
- Message framing (headers, chunking, reassembly)
- Cross-platform wire protocol (68k <-> PPC <-> Linux)

We just call PT_Send/PT_Broadcast with our 4-byte move messages.

---

## 10. RESOURCE CREATION GUIDE (PHOTOSHOP 3 ON CLASSIC MAC)

### Creating the Tile Sheet (128 x 32 pixels)

1. **Open Photoshop 3** on your classic Mac
2. **File > New**: Width=128, Height=32, Resolution=72, Mode=Indexed Color (256 colors)
3. **Draw 4 tiles** side by side, each 32x32:

   **Tile 0 - Floor (position 0-31):**
   - Fill with dark green (#336633 or similar)
   - Add subtle grass texture or grid lines
   - This is the walkable ground

   **Tile 1 - Wall (position 32-63):**
   - Fill with dark gray (#666666)
   - Add 3D shading: lighter top-left edges, darker bottom-right
   - Draw stone/brick pattern
   - These are the permanent indestructible pillars

   **Tile 2 - Block (position 64-95):**
   - Fill with brown/tan (#996633)
   - Add brick/crate texture
   - Slightly different from walls so player can tell them apart
   - These will be destructible in future

   **Tile 3 - Spawn (position 96-127):**
   - Same as floor but slightly lighter green
   - Or identical to floor (spawn points look the same)

4. **File > Save As**: Save as Photoshop format for editing
5. **To get into resource fork**: Use ResEdit or Resourcerer
   - Select All, Copy
   - Open ResEdit, create new resource file
   - Create new PICT resource (ID 128, name "Tiles")
   - Paste

### Creating the Player Sprite (128 x 32 pixels)

1. **File > New**: Width=128, Height=32, Resolution=72, Mode=Indexed Color
2. **Draw 4 directional sprites**, each 32x32:

   **Frame 0 - Facing Down (position 0-31):**
   - White character body (classic Bomberman white)
   - Simple body: head, body, arms, legs
   - Eyes looking down

   **Frame 1 - Facing Up (position 32-63):**
   - Back of character
   - Same body shape, no face visible

   **Frame 2 - Facing Left (position 64-95):**
   - Side view, facing left

   **Frame 3 - Facing Right (position 96-127):**
   - Side view, facing right (can mirror left)

3. **Transparency**: Use a specific background color (e.g., magenta #FF00FF or white) that will be treated as transparent. When drawing the sprite with CopyBits, use CopyMask with a mask GWorld where background=black, character=white.

4. **Save and import to resource fork** same as tiles.

### Alternative: Creating Resources on the Build Machine

If you prefer, you can:
1. Create PNG files on a modern machine
2. Convert to PICT format using a conversion tool
3. Embed in the resource fork during the build process

Retro68 supports adding resources via .r files using the Rez compiler.

### Creating a Mask for Sprite Transparency

For each sprite frame, create a matching mask where:
- White pixels = draw the sprite pixel
- Black pixels = transparent (show background)

Store masks as a separate PICT (rPictPlayerMask = 130) or generate them at load time by scanning the sprite sheet for the background color.

---

## 11. API REFERENCE: EVERY TOOLBOX CALL WE USE

### Initialization (Called Once at Startup)

| Call | Purpose | Source |
|------|---------|--------|
| `MaxApplZone()` | Expand application heap zone to its limit | Inside Mac Vol I, Tricks of Gurus p.12 |
| `MoreMasters()` | Pre-allocate master pointer blocks | Tricks of Gurus p.12 |
| `InitGraf(&qd.thePort)` | Initialize QuickDraw globals | All books |
| `InitFonts()` | Initialize Font Manager | All books |
| `InitWindows()` | Initialize Window Manager | All books |
| `InitMenus()` | Initialize Menu Manager | All books |
| `TEInit()` | Initialize TextEdit | All books |
| `InitDialogs(0L)` | Initialize Dialog Manager | All books |
| `InitCursor()` | Set cursor to arrow | All books |
| `FlushEvents(everyEvent, 0)` | Clear event queue | Tricks of Gurus |

### Window Management

| Call | Purpose | Source |
|------|---------|--------|
| `NewCWindow(...)` | Create color window | Black Art p.34 |
| `SetPort(window)` | Set current drawing port | All books |
| `DisposeWindow(window)` | Free window memory | All books |
| `DragWindow(window, pt, bounds)` | Handle window dragging | Mac Game Techniques |
| `SelectWindow(window)` | Bring window to front | Mac Game Techniques |
| `FrontWindow()` | Get frontmost window | Mac Game Techniques |
| `FindWindow(pt, &window)` | Determine window part clicked | Mac Game Techniques |
| `BeginUpdate(window)` | Start update drawing | All books |
| `EndUpdate(window)` | End update drawing | All books |

### Event Handling

| Call | Purpose | Source |
|------|---------|--------|
| `WaitNextEvent(mask, &event, sleep, rgn)` | Get next event | All books |
| `GetKeys(keyMap)` | Poll keyboard hardware state | Black Art p.87, Mac Game 2002 p.186 |
| `MenuSelect(where)` | Handle menu bar click | Mac Game Techniques |
| `MenuKey(char)` | Handle Cmd-key shortcut | Mac Game Techniques |
| `HiliteMenu(0)` | De-highlight menu title | Mac Game Techniques |

### GWorld (Offscreen Buffers)

| Call | Purpose | Source |
|------|---------|--------|
| `NewGWorld(&gw, depth, &rect, ctab, gd, flags)` | Create offscreen buffer | All books |
| `DisposeGWorld(gw)` | Free offscreen buffer | All books |
| `GetGWorldPixMap(gw)` | Get PixMapHandle from GWorld | All books |
| `LockPixels(pixMap)` | Lock pixels for drawing/reading | All books |
| `UnlockPixels(pixMap)` | Unlock pixels | All books |
| `SetGWorld(gw, gd)` | Set drawing target to GWorld | All books |
| `GetGWorld(&port, &gd)` | Save current drawing target | All books |

### Drawing

| Call | Purpose | Source |
|------|---------|--------|
| `CopyBits(src, dst, &srcR, &dstR, mode, mask)` | Blit pixels between bitmaps | All books (THE workhorse) |
| `CopyMask(src, mask, dst, &srcR, &maskR, &dstR)` | Blit with transparency mask | Mac Game Techniques |
| `DrawPicture(picH, &rect)` | Draw PICT into current port | All books |
| `SetRect(&rect, l, t, r, b)` | Set rectangle coordinates | All books |
| `OffsetRect(&rect, dh, dv)` | Move rectangle | All books |
| `SectRect(&r1, &r2, &result)` | Intersect rectangles | Tricks of Gurus |
| `PaintRect(&rect)` | Fill rect with current color | All books |
| `EraseRect(&rect)` | Erase rect to background color | All books |
| `RGBForeColor(&color)` | Set drawing foreground color | Mac Game Techniques |
| `ForeColor(blackColor)` | Set fore color (indexed) | Tricks of Gurus p.73 |
| `BackColor(whiteColor)` | Set back color (indexed) | Tricks of Gurus p.73 |

### Resource Management

| Call | Purpose | Source |
|------|---------|--------|
| `GetPicture(resID)` | Load PICT resource | All books |
| `ReleaseResource(handle)` | Mark resource as purgeable (system reclaims when needed, not immediate free) | Inside Mac Vol I, All books |
| `GetResource(type, id)` | Load any resource by type/ID | All books |

### Memory Management

| Call | Purpose | Source |
|------|---------|--------|
| `NewPtr(size)` | Allocate non-relocatable block | All books |
| `DisposePtr(ptr)` | Free non-relocatable block | All books |
| `NewHandle(size)` | Allocate relocatable block | All books |
| `DisposeHandle(handle)` | Free relocatable block | All books |
| `HLock(handle)` | Lock handle in place | Black Art p.75 |
| `HUnlock(handle)` | Unlock handle | Black Art p.75 |

### Menu System

| Call | Purpose | Source |
|------|---------|--------|
| `NewMenu(id, title)` | Create menu | Black Art p.142 |
| `AppendMenu(menu, items)` | Add items to menu | Black Art p.142 |
| `AppendResMenu(menu, type)` | Add DA items to Apple menu | Mac Game Techniques |
| `InsertMenu(menu, before)` | Insert menu in menu bar | All books |
| `DrawMenuBar()` | Redraw menu bar | All books |

### Timing

| Call | Purpose | Source |
|------|---------|--------|
| `TickCount()` | Get tick count (60ths of second) | All books |

### System

| Call | Purpose | Source |
|------|---------|--------|
| `SysBeep(duration)` | Play system alert sound | All books |
| `ExitToShell()` | Quit to Finder | All books |
| `Random()` | Generate random number | Tricks of Gurus |

---

## 12. CODE PATTERNS FROM THE BOOKS

### Pattern 1: The Save/Restore Port Dance

Every time you draw to a GWorld, you MUST save and restore the previous port:

```c
CGrafPtr oldPort;
GDHandle oldDevice;

GetGWorld(&oldPort, &oldDevice);    /* Save */
SetGWorld(myGWorld, NULL);          /* Switch to offscreen */
LockPixels(myPixMap);

/* ... draw stuff ... */

UnlockPixels(myPixMap);
SetGWorld(oldPort, oldDevice);      /* Restore */
```

Source: Every single book. This is the most repeated pattern in classic Mac graphics programming.

### Pattern 2: CopyBits Between GWorlds

```c
/* Both pixmaps must be locked */
LockPixels(srcPix);
LockPixels(dstPix);

CopyBits((BitMap *)*srcPix,     /* cast PixMapHandle to BitMap* */
         (BitMap *)*dstPix,
         &srcRect, &dstRect,
         srcCopy,               /* fast, opaque copy */
         NULL);                 /* no clipping region */

UnlockPixels(srcPix);
UnlockPixels(dstPix);
```

Source: Black Art p.112, Tricks of Gurus p.67. The `(BitMap *)` cast is required because CopyBits takes BitMap* but PixMapHandle dereferences to PixMap*. The Toolbox handles the difference internally.

### Pattern 3: Loading a PICT into a GWorld

```c
PicHandle pic = GetPicture(resourceID);
Rect bounds = (**pic).picFrame;
OffsetRect(&bounds, -bounds.left, -bounds.top);  /* normalize to 0,0 */

GWorldPtr gw;
NewGWorld(&gw, 0, &bounds, NULL, NULL, 0);
PixMapHandle pm = GetGWorldPixMap(gw);

CGrafPtr oldPort;
GDHandle oldDev;
GetGWorld(&oldPort, &oldDev);
SetGWorld(gw, NULL);
LockPixels(pm);
EraseRect(&bounds);
DrawPicture(pic, &bounds);
UnlockPixels(pm);
SetGWorld(oldPort, oldDev);

ReleaseResource((Handle)pic);  /* mark PICT as purgeable (not immediate free) */
```

Source: Black Art, Tricks of Gurus, Sex Lies. This is the standard way to prepare sprite/tile graphics.

### Pattern 4: GetKeys Keyboard Polling

```c
KeyMap keyMap;
unsigned char *keys;

GetKeys(keyMap);
keys = (unsigned char *)keyMap;

/* Test specific key by code */
if (keys[keyCode >> 3] & (1 << (keyCode & 7))) {
    /* Key is pressed */
}
```

Source: Mac Game Programming (2002) p.186, Tricks of Gurus p.52, Black Art p.87.

### Pattern 5: Fixed-Rate Game Loop

```c
long lastTick = TickCount();

while (!quit) {
    if (WaitNextEvent(everyEvent, &event, 0, NULL))
        HandleEvent(&event);

    long now = TickCount();
    if (now - lastTick >= FRAME_TICKS) {
        lastTick = now;
        UpdateGame();
        DrawFrame();
    }
}
```

Source: Black Art Chapter 8 (Invaders). All books use TickCount() for frame timing.

---

## 13. KNOWN GOTCHAS & PLATFORM DIFFERENCES

### 68k vs PPC Differences

| Issue | 68k | PPC |
|-------|-----|-----|
| Byte order | Big-endian | Big-endian |
| Alignment | 2-byte | 4-byte |
| Calling convention | Pascal (A-trap) | Mixed-mode (PPC native) |
| Networking | MacTCP driver | Open Transport |
| Toolbox | A-trap dispatched | Code Fragment Manager |
| Memory | 24-bit clean needed? | 32-bit clean |

**Retro68 handles most of this.** The cross-compiler generates correct code for each target. The main concern is:

1. **Structure alignment**: Use `#pragma pack` or don't rely on struct sizes for wire formats. Our MoveMessage struct is all unsigned chars, so no alignment issues.

2. **Networking**: PeerTalk SDK abstracts this entirely. We never touch MacTCP or OT directly.

3. **Memory**: 68k Macs typically have 4-8 MB. Our game uses ~700 KB. Plenty of room.

### Common Classic Mac Programming Bugs

1. **Forgetting to lock pixels before CopyBits**: Unlocked PixMaps can be moved by the Memory Manager during CopyBits, causing crashes or garbled graphics. ALWAYS LockPixels before drawing.

2. **Not restoring the port after GWorld operations**: If you SetGWorld and forget to restore, all subsequent QuickDraw calls go to the wrong destination. Use the save/restore dance religiously.

3. **Wrong fore/back colors before CopyBits**: If ForeColor isn't blackColor and BackColor isn't whiteColor, CopyBits will produce wrong colors. Set them explicitly before every blit to screen.

4. **Calling DisposeGWorld on the current port**: Never dispose the GWorld you're currently drawing into. Always SetGWorld back to the window first.

5. **Not calling MaxApplZone()**: Without this, the heap stays at its default size (not expanded to the zone limit). NewGWorld will fail silently or cause memory fragmentation. Per Inside Mac Vol I, MaxApplZone expands the application heap zone to its limit — the boundary between the heap and the stack.

6. **KeyMap bit numbering**: Key code N is at byte (N/8), bit (N%8). NOT bit (7 - N%8). Some books get this wrong. Test on real hardware.

7. **Forgetting FlushEvents at startup**: Old events in the queue can cause unexpected behavior. Clear them.

8. **PixMap rowBytes masking**: When accessing rowBytes directly, mask with 0x3FFF to clear flag bits: `rowBytes = (**pm).rowBytes & 0x3FFF;` Note: Inside Mac Vol V only documents the high bit (bit 15) as a flag distinguishing PixMap from BitMap. The 0x3FFF mask (clearing top 2 bits) is common practice from game programming books but is more conservative than strictly necessary.

### PeerTalk Gotchas

1. **PT_Init must be called after Toolbox init**: On Classic Mac, PeerTalk calls MaxApplZone/MoreMasters internally, but the Toolbox must be initialized first.

2. **PT_Poll must be called regularly**: Even in single-player mode, call PT_Poll() every loop iteration. It drives discovery and connection management. Without it, the SDK's state machines stall.

3. **Fixed ports**: PeerTalk uses ports 7353 (discovery), 7354 (TCP), 7355 (UDP). Only one PeerTalk app per machine.

4. **Message type 255 is reserved**: Don't use it. PeerTalk uses 255 for internal goodbye messages.

5. **Single send buffer per peer**: On MacTCP, a second send fails if the first hasn't completed. PeerTalk handles this internally, but if you flood sends, messages may be delayed.

---

## 14. FUTURE PHASES (BEYOND THIS STARTER)

### Phase 7: Smooth Movement Animation
- Add `Player.moving` state and `Player.moveProgress` (0.0 to 1.0)
- Interpolate pixel position between grid cells over 4-6 frames
- Movement input queues next direction while current move completes
- Source: Mac Game Programming (2002) physics controller with velocity

### Phase 8: Bomb Placement & Explosions
- Space bar places bomb at current grid cell
- Bomb timer: 3 seconds (180 ticks)
- Explosion: clear blocks in 4 directions (up to blast radius)
- TileMap_SetTile(col, row, TILE_FLOOR) when block destroyed
- Renderer_RebuildBackground() after map changes
- Explosion animation: 4-frame sequence in explosion GWorld

### Phase 9: Multiplayer Game State
- NET_MSG_PLAYER_MOVE: broadcast grid position on every move
- NET_MSG_BOMB_PLACED: broadcast bomb position
- NET_MSG_GAME_START: synchronize game start
- 2-4 player support using PeerTalk peer discovery
- Lobby screen: show discovered peers, connect, start game

### Phase 10: Power-ups
- Speed Up, Extra Bomb, Blast Radius, Bomb Pass, Block Pass
- Dropped by destroyed blocks (random chance)
- New tile types or separate sprite layer

### Phase 11: Sound Effects
- Mac Sound Manager: `SndPlay()`
- Load 'snd ' resources for: bomb place, explosion, pickup, death
- Source: Tricks of Gurus p.89, Black Art Chapter 7

### Phase 12: AI Opponents
- Simple: random movement avoiding walls
- Medium: pathfinding toward nearest player
- Advanced: bomb avoidance, strategic block destruction
- Source: Tricks of Gurus "Dungeon" enemy AI

### Phase 13: Multiple Levels
- Level data in resource fork ('LEVL' custom resources)
- Or embedded as C arrays per level
- Varying block density, spawn positions, power-up distribution

### Phase 14: Title Screen & Game Over
- PICT resource for title screen
- Dialog for game setup (number of players, lives, etc.)
- Score display between rounds

---

## APPENDIX A: COMPLETE FILE LISTING WITH LINE COUNTS (ESTIMATED)

```
include/game.h          ~80 lines     Constants, types, includes
include/tilemap.h       ~30 lines     Map API
include/player.h        ~25 lines     Player API
include/renderer.h      ~20 lines     Renderer API
include/input.h         ~15 lines     Input API
include/net.h           ~20 lines     Network API

src/main.c              ~180 lines    Entry, toolbox init, event loop
src/tilemap.c           ~60 lines     Map data, tile queries
src/player.c            ~80 lines     Movement, collision
src/renderer.c          ~200 lines    GWorlds, drawing, blitting
src/input.c             ~30 lines     GetKeys polling
src/net.c               ~80 lines     PeerTalk wrapper

maps/level1.h           ~25 lines     Level data

resources/bombertalk.r  ~30 lines     Rez source
resources/bombertalk_size.r ~20 lines SIZE resource

CMakeLists.txt          ~50 lines     Build configuration

TOTAL:                  ~925 lines of C code + ~100 lines support files
```

This is intentionally small. A starter project should be readable in one sitting and buildable in minutes. Every line has a purpose.

---

## APPENDIX B: QUICK REFERENCE - BOMBERMAN GRID LAYOUT

```
     Col: 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14
Row 0:    W  W  W  W  W  W  W  W  W  W  W  W  W  W  W
Row 1:    W  S  .  B  B  B  B  B  B  B  B  B  .  S  W
Row 2:    W  .  W  B  W  B  W  B  W  B  W  B  W  .  W
Row 3:    W  B  B  B  B  B  B  B  B  B  B  B  B  B  W
Row 4:    W  B  W  B  W  B  W  B  W  B  W  B  W  B  W
Row 5:    W  B  B  B  B  B  B  B  B  B  B  B  B  B  W
Row 6:    W  B  W  B  W  B  W  B  W  B  W  B  W  B  W
Row 7:    W  B  B  B  B  B  B  B  B  B  B  B  B  B  W
Row 8:    W  B  W  B  W  B  W  B  W  B  W  B  W  B  W
Row 9:    W  B  B  B  B  B  B  B  B  B  B  B  B  B  W
Row 10:   W  .  W  B  W  B  W  B  W  B  W  B  W  .  W
Row 11:   W  S  .  B  B  B  B  B  B  B  B  B  .  S  W
Row 12:   W  W  W  W  W  W  W  W  W  W  W  W  W  W  W

W = Wall (TILE_WALL=1)   - Indestructible, permanent grid pillars
B = Block (TILE_BLOCK=2) - Destructible, future: destroyed by bombs
. = Floor (TILE_FLOOR=0) - Always walkable
S = Spawn (TILE_SPAWN=3) - Player start positions (walkable)

Spawn points: (1,1) (13,1) (1,11) (13,11) = 4 corners
Each spawn has guaranteed 2-tile clear space for escape routes
```

---

## APPENDIX C: PEERTALK QUICK REFERENCE

```c
/* Lifecycle */
PT_Init(&ctx, "BomberTalk");    /* allocates everything */
PT_Shutdown(ctx);                /* sends goodbyes, frees all */

/* Discovery */
PT_StartDiscovery(ctx);          /* broadcast on UDP 7353 every 2s */
PT_StopDiscovery(ctx);           /* stop broadcasting (keep listening) */

/* Connections */
PT_Connect(ctx, peer);           /* initiate TCP to discovered peer */
PT_Disconnect(ctx, peer);        /* graceful close */

/* Messaging */
PT_RegisterMessage(ctx, type, PT_RELIABLE); /* or PT_FAST */
PT_Send(ctx, peer, type, data, len);        /* to one peer */
PT_Broadcast(ctx, type, data, len);         /* to all connected */

/* Event Loop */
PT_Poll(ctx);                    /* MUST call regularly - drives everything */

/* Callbacks */
PT_OnPeerDiscovered(ctx, cb, userdata);
PT_OnPeerLost(ctx, cb, userdata);
PT_OnConnected(ctx, cb, userdata);
PT_OnDisconnected(ctx, cb, userdata);
PT_OnMessage(ctx, type, cb, userdata);
PT_OnError(ctx, cb, userdata);

/* Peer Info */
PT_GetPeerCount(ctx);
PT_GetPeer(ctx, index);
PT_PeerName(peer);
PT_GetPeerState(peer);           /* DISCOVERED / CONNECTED / DISCONNECTED */
```

---

## APPENDIX D: BOOK-TO-CODE CROSS-REFERENCE

| Book | Key Contribution to This Project |
|------|----------------------------------|
| **Black Art of Macintosh Game Programming (1996)** | Toolbox init sequence, GWorld creation, CopyBits double-buffering, sprite system architecture, complete Invaders! game loop pattern, GetKeys keyboard polling, key code reference |
| **Mac Game Programming (2002)** | Tile map system (GameLevel, MapElement), tile-based collision detection (CanMoveUp/Down/Left/Right checking wall tiles), PhysicsController pattern, WasKeyPressed() implementation, Carbon accessor functions |
| **Tricks of the Mac Game Programming Gurus (1995)** | Tile-based dungeon game (tileArray, DrawTile, MovePlayer), sprite loading from PICT resources, color table seed synchronization, fixed-point movement math, procedural level generation |
| **Sex, Lies, and Video Games (1996)** | Offscreen buffer creation (CreateOffscreen), CopyBits performance optimization guide, sprite blitting techniques (logical masked, RLE, compiled), CSprite/CSpriteGroup/CPlayField architecture, Pong game implementation |
| **Macintosh Game Animation (1985)** | Boundary checking with boolean algebra, collision detection grid concept, animation frame sequencing (CEL/sequence arrays), GET/PUT image patterns |
| **Macintosh Game Programming Techniques (1996)** | Event loop architecture (CheckEvent pattern), WaitNextEvent vs GetNextEvent compatibility, NiceDelay timing, resource file management, window reference constants |

---

*Plan created by synthesizing 6 classic Macintosh game programming books (~6 MB of text) and the PeerTalk SDK source code (~3,700 lines). Every code pattern and API call is sourced from these references.*
