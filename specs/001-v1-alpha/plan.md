# Implementation Plan: BomberTalk v1.0-alpha

**Branch**: `001-v1-alpha` | **Date**: 2026-04-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-v1-alpha/spec.md`

## Summary

Build a networked Bomberman clone for Classic Macintosh that proves the PeerTalk SDK
enables real-time multiplayer gaming across 68k MacTCP, PPC MacTCP, and PPC Open
Transport machines. The alpha delivers loading screen, main menu, lobby with peer
discovery, and gameplay with movement, bombs, and explosions — all synchronized over
LAN via PeerTalk.

## Technical Context

**Language/Version**: C89/C90 (Retro68/RetroPPC cross-compilation)
**Primary Dependencies**: PeerTalk SDK (~/peertalk), clog (~/clog), Mac Toolbox
**Storage**: N/A (all state in memory, level data compiled in)
**Testing**: Manual on real hardware (Mac SE, Performa 6200, Performa 6400)
**Target Platform**: Classic Mac OS (System 6 through 7.6.1)
**Project Type**: Desktop game application
**Performance Goals**: 10+ fps on 68000 @ 8 MHz, position sync within 100ms
**Constraints**: 1 MB application heap, 4 MB total RAM (Mac SE), C89 only, no malloc after init
**Scale/Scope**: 2-4 players, single arena, ~2000-3000 LOC estimated

## Constitution Check

*GATE: Must pass before implementation begins.*

| Principle | Check | Status |
|-----------|-------|--------|
| I. Prove PeerTalk Works | Game uses PT_Init, PT_StartDiscovery, PT_Connect, PT_Send, PT_Broadcast, PT_Poll, all callbacks | PASS |
| II. Run on All Three Macs | Three build targets: 68k MacTCP, PPC MacTCP, PPC OT | PASS |
| III. Ship Screens, Not Just a Loop | Loading, menu, lobby, gameplay screens planned | PASS |
| IV. C89 Everywhere | All code C89/C90, same as PeerTalk | PASS |
| V. Mac SE Is the Floor | Memory budget fits 1 MB, GWorld sizing accounts for SE | PASS |
| VI. Simple Graphics First | Colored rectangles for all tiles and sprites | PASS |
| VII. Fixed Frame Rate, Poll Everything | WaitNextEvent(sleep=0), GetKeys(), PT_Poll() in main loop | PASS |
| VIII. Network State Is Authoritative | MSG_POSITION via PT_FAST, events via PT_RELIABLE | PASS |
| IX. The Books Are Gospel | Game code follows patterns from the 6 classic Mac game programming books in books/. BOMBERMAN_CLONE_PLAN.md extracts specific references. | PASS |
| X. One Codebase, Three Builds | Single src/ tree, CMake toolchain selection | PASS |

## Project Structure

### Documentation (this feature)

```text
specs/001-v1-alpha/
├── spec.md              # User stories and requirements
├── plan.md              # This file
├── research.md          # Platform research and decisions
├── data-model.md        # Game entities and network messages
├── quickstart.md        # How to build and run
├── contracts/           # Network protocol and module APIs
│   └── network-protocol.md
├── checklists/
│   └── build-verification.md
└── tasks.md             # Implementation task list
```

### Source Code (repository root)

```text
bombertalk/
├── CMakeLists.txt              # Build configuration (3 targets)
├── CLAUDE.md                   # Agent development context
├── .specify/                   # Speckit templates and scripts
│   ├── templates/              # Constitution, spec, plan, tasks templates
│   ├── scripts/bash/           # Feature creation, setup scripts
│   └── memory/
│       └── constitution.md     # BomberTalk constitution
│
├── include/
│   ├── game.h                  # Master header: types, constants, resource IDs
│   ├── screens.h               # Screen state machine (loading/menu/lobby/game)
│   ├── tilemap.h               # Tile map data structures and queries
│   ├── player.h                # Player state, movement, bomb placement
│   ├── bomb.h                  # Bomb state, fuse timer, explosion logic
│   ├── renderer.h              # GWorld management, tile/sprite drawing
│   ├── input.h                 # Keyboard polling via GetKeys()
│   └── net.h                   # PeerTalk wrapper (thin layer)
│
├── src/
│   ├── main.c                  # Entry point, toolbox init, screen dispatch
│   ├── screens.c               # Screen state machine implementation
│   ├── screen_loading.c        # Loading screen (title text, brief delay)
│   ├── screen_menu.c           # Main menu (New Game, Join Game, Quit)
│   ├── screen_lobby.c          # Lobby (player discovery, connect, start)
│   ├── screen_game.c           # Gameplay (movement, bombs, rendering)
│   ├── tilemap.c               # Map loading, tile queries
│   ├── player.c                # Player movement, collision, multiplayer state
│   ├── bomb.c                  # Bomb placement, fuse countdown, explosion
│   ├── renderer.c              # Offscreen buffer management, blitting
│   ├── input.c                 # GetKeys() polling, key state tracking
│   └── net.c                   # PT_Init/PT_Shutdown/PT_Poll/message handling
│
├── maps/
│   └── level1.h                # Hardcoded level data (C array)
│
├── resources/
│   ├── bombertalk.r            # Rez source: WIND, MENU, DLOG, SIZE
│   └── bombertalk_size.r       # SIZE resource (memory partition)
│
├── books/                      # Classic Mac game programming references
│   ├── Black_Art_of_Macintosh_Game_Programming_1996.txt
│   ├── Tricks_Of_The_Mac_Game_Programming__Gurus_1995.txt
│   ├── Mac_Game_Programming_2002.txt
│   ├── Macintosh_Game_Programming_Techniques_1996.txt
│   ├── Sex_Lies_and_Video_Games_How_to_Write_a_Macintosh_Arcade_Game_1996.txt
│   └── Macintosh_Game_Animation_1985.txt
│
└── BOMBERMAN_CLONE_PLAN.md     # Detailed architecture reference
```

**Structure Decision**: Single project with screen-based source organization. Each game
screen (loading, menu, lobby, game) gets its own source file for clean separation.
Follows the pattern from BOMBERMAN_CLONE_PLAN.md but adds the screen system and bomb
module that the original plan deferred to "future phases."

## Key Design Decisions

### Screen State Machine

The game uses a simple state machine in `screens.c`:

```c
typedef enum {
    SCREEN_LOADING,
    SCREEN_MENU,
    SCREEN_LOBBY,
    SCREEN_GAME
} ScreenState;
```

Each screen implements three functions: `Screen_XXX_Init()`, `Screen_XXX_Update()`,
`Screen_XXX_Draw()`. The main loop dispatches to the current screen. Transitions
call shutdown on the old screen and init on the new screen.

### Network Architecture

**Pure peer-to-peer — no host**: All players run identical code with no special roles.
The menu has "Play" and "Quit." Any player can press Start in the lobby once 2+ peers
are connected. Player IDs are assigned deterministically by sorting all connected peer
IPs (including local IP) — lowest IP = player 0. This mirrors PeerTalk's own tiebreaker
and requires no coordination message. MSG_PLAYER_INFO was removed — 7 message types total.

**State sync**: Each player is authoritative over their own position and actions.
Players broadcast their state; other players render it. This is simple but allows
cheating — acceptable for a LAN party game.

**Message flow**:
1. Any player presses Start -> broadcast MSG_GAME_START (PT_RELIABLE)
2. All clients compute player IDs locally (IP sort) -> assign spawn corners
3. Player moves locally -> broadcast MSG_POSITION (PT_FAST)
4. Player places bomb locally -> broadcast MSG_BOMB_PLACED (PT_RELIABLE)
5. Bomb timer expires locally -> check explosion locally -> broadcast results (PT_RELIABLE)
6. Each client processes received messages and updates remote player/bomb state
7. Duplicate MSG_GAME_START messages are ignored (first one wins)

### Rendering Strategy

Two GWorld offscreen buffers (from BOMBERMAN_CLONE_PLAN.md):
1. **gBackground**: Pre-rendered tile map, redrawn only when blocks are destroyed
2. **gWorkBuffer**: Per-frame working copy — background + players + bombs

Each frame: CopyBits(background -> work), draw sprites to work, CopyBits(work -> window).

For v1.0-alpha, graphics are loaded from PICT resources: a tile sheet (4 tiles in a
row), player sprite sheet (4 players in a row), bomb, explosion, and title graphic.
Color assets (PICT 128-199) use 32x32 tiles at 8-bit. Mac SE assets (PICT 200-255)
use 16x16 tiles at 1-bit. If PICT resources fail to load, the renderer falls back to
colored rectangles — graphics never block gameplay. See contracts/asset-pipeline.md.

## Complexity Tracking

No constitution violations. The architecture is straightforward: state machine screens,
poll-based game loop, PeerTalk for networking, GWorld for rendering, GetKeys for input.
