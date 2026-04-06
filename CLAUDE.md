# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Constitution (Binding)

These 10 principles govern ALL implementation decisions. Full text: `.specify/memory/constitution.md`

1. **Prove PeerTalk Works** — Every design decision MUST prioritize demonstrating PeerTalk SDK capabilities.
2. **Run on All Three Macs** — 68k MacTCP (Mac SE), PPC MacTCP (6200), PPC OT (6400). No feature ships if it breaks any target.
3. **Ship Screens, Not Just a Loop** — Loading, menu, lobby, gameplay screens required.
4. **C89 Everywhere** — All game code MUST be C89/C90 for Retro68 compatibility.
5. **Mac SE Is the Floor** — 4 MB RAM, 8 MHz 68000 sets the minimum bar.
6. **Simple Graphics, Never Blocking** — Basic PICT resource graphics. Renderer falls back to colored rectangles if PICTs missing.
7. **Fixed Frame Rate, Poll Everything** — WaitNextEvent(sleep=0), GetKeys(), PT_Poll(). No threads.
8. **Network State Is Authoritative** — PT_FAST for positions, PT_RELIABLE for game events.
9. **The Books Are Gospel** — The 6 game programming books in `books/` are authoritative. Consult them before implementing any subsystem.
10. **One Codebase, Three Builds** — Single src/ tree, CMake toolchain selection.

## Before Every Change

- [ ] Does this help prove PeerTalk works? (I) — if no, lower priority
- [ ] Does this build on all three targets? (II) — if no, fix it
- [ ] Is this C89-clean? (IV) — no `//` comments, no mixed declarations, no VLAs
- [ ] Will this fit in 1 MB on the Mac SE? (V) — if unsure, check FreeMem()

## Build Commands

Environment variables (defaults shown):
- `RETRO68_TOOLCHAIN` — `~/Retro68-build/toolchain`
- `PEERTALK_DIR` — `~/peertalk` (must be built first per target)
- `CLOG_DIR` — `~/clog` (must be built first per target)

```bash
# 68k MacTCP (build-68k/) — Mac SE, System 6
mkdir -p build-68k && cd build-68k && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC Open Transport (build-ppc-ot/) — Performa 6400, System 7.6.1
mkdir -p build-ppc-ot && cd build-ppc-ot && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC MacTCP (build-ppc-mactcp/) — Performa 6200, System 7.5.3
mkdir -p build-ppc-mactcp && cd build-ppc-mactcp && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make
```

After changes, always build and verify all three targets compile clean before committing.

## Architecture

### Main Loop (`main.c`)

```
while (!gQuitting) {
    WaitNextEvent(sleep=0)  →  HandleEvent (menus, Cmd-Q only)
    Net_Poll()              →  PeerTalk callbacks fire (messages, connect/disconnect)
    if (deltaTicks >= FRAME_TICKS) {
        gGame.deltaTicks = actual elapsed ticks (capped at 10)
        Input_Poll()        →  GetKeys() once, compute pressed/released edges
        Screens_Update()    →  dispatches to current screen's Update()
        Screens_Draw()      →  dispatches to current screen's Draw()
    }
}
```

### Timing Model — CRITICAL

**All game timers MUST be tick-based, not frame-based.** Mac SE runs ~6fps while PPC runs ~24fps. Frame-count timers cause gameplay to differ by 4-5x across machines.

- `gGame.deltaTicks` — actual elapsed ticks since last frame (set in main loop, capped at 10)
- Decrement timers by `gGame.deltaTicks`, never by 1
- Constants: `BOMB_FUSE_TICKS=180` (~3s), `EXPLOSION_DURATION_TICKS=20` (~0.33s), `DEATH_FLASH_TICKS=60` (~1s), `MOVE_COOLDOWN_TICKS=12` (~0.2s)
- Network sync: `MSG_BOMB_EXPLODE` forces remote machines to explode immediately if their local fuse hasn't expired yet

### Screen State Machine (`screens.c`)

Each screen implements `Init()`, `Update()`, `Draw()`. Transition via `Screens_TransitionTo()`.

```
SCREEN_LOADING → SCREEN_MENU → SCREEN_LOBBY → SCREEN_GAME
                      ↑              ↑              │
                      └──────────────┴──────────────┘
```

### Renderer Pipeline (`renderer.c`)

Two offscreen buffers: **background** (static tilemap) and **work** (per-frame compositing).

- Color Macs: GWorld-based (`NewGWorld`, `LockPixels`/`UnlockPixels`)
- Mac SE: BitMap + GrafPort (manual allocation, 1-bit monochrome)

**Gameplay frame:**
1. `Renderer_BeginFrame()` — CopyBits bg→work, keeps work locked
2. Draw bombs, explosions, players into work buffer
3. `Renderer_EndFrame()` — unlocks work, CopyBits work→window

**Menu/loading/lobby screens:** Use `Renderer_BeginScreenDraw()` / `Renderer_EndScreenDraw()` which clear work to black and draw text directly.

**Performance rules:**
- `Renderer_RebuildBackground()` redraws all 195 tiles — call sparingly (once after explosion, not per-block)
- Port save/lock is hoisted to `RebuildBackground`, not per-tile
- Pascal strings and `StringWidth()` results are cached as statics in screen draw functions
- `RGBColor` constants are `static const` at file scope, not stack-allocated per tile

### Network Layer (`net.c`)

Thin wrapper around PeerTalk SDK. All network I/O is callback-driven via `PT_Poll()`.

- **Discovery**: UDP broadcast, peers appear in lobby
- **Connection**: TCP mesh — any player can initiate, tiebreaker handles simultaneous connects
- **Player IDs**: Deterministic IP-sort (lowest IP = player 0). No host concept.
- **Messages**: 7 types registered at init. `PT_FAST` (UDP) for positions, `PT_RELIABLE` (TCP) for game events.
- **Logging**: clog messages are UDP-broadcast to port 7355 for remote monitoring. Receive with `socat UDP-RECV:7355 -`

### Global State (`game.h`)

Single `GameState gGame` struct holds all game state. Key fields:
- `isMacSE` / `tileSize` — detected at startup, drives renderer path selection
- `deltaTicks` — elapsed ticks this frame (for tick-based timers)
- `players[MAX_PLAYERS]` — all player state including `peer` pointer for network mapping
- `bombs[MAX_BOMBS]` — active bomb state with tick-based fuse timers

## Code Style

- C89/C90: no `//` comments, no mixed declarations, no VLAs, no stdint.h
- All memory allocated at init time — no malloc during gameplay
- GetKeys() for input polling — never use keyDown events for movement
- WaitNextEvent(sleep=0) — never yield CPU in game loop
- GWorld double-buffering — draw to offscreen, CopyBits to window
- PeerTalk poll-based — call PT_Poll() every frame

## Known Platform Gotchas

**Mac SE (System 6)**: No Color QuickDraw. `NewCWindow` unavailable — use `NewWindow` fallback. GWorlds are 1-bit (monochrome). System 6 has no Gestalt — use `SysEnvirons` if needed.

**PPC toolchain**: File is `retroppc.toolchain.cmake`, NOT `retro68.toolchain.cmake`. `CMAKE_SYSTEM_NAME` is `RetroPPC`.

**MaxApplZone/MoreMasters**: MUST be called before ANY Memory Manager calls. First thing in `InitToolbox()`.

**C89 + clog**: clog uses variadic macros (C99). Do NOT use `-pedantic`. Use `-Wall -Wextra` only.

**OT linker**: PPC OT builds link `OpenTransportAppPPC` + `OpenTransportLib` + `OpenTptInternetLib`. Handled by PeerTalk's static library — BomberTalk just links `libpeertalk.a`.

**Big-endian**: Both 68k and PPC are big-endian. Network message structs need no byte swapping on Classic Mac. Will need conversion if POSIX build is added later.

**Mac SE performance**: ~6fps. Minimize Toolbox trap calls in hot paths. Cache `StringWidth()`, avoid per-tile `SavePort`/`LockPixels`. Books recommend dirty rectangle tracking for further optimization.

## Spec Artifacts

Design docs in `specs/001-v1-alpha/`. Key references: `data-model.md` (network message formats), `contracts/network-protocol.md` (IP-sort player IDs, message flow), `contracts/asset-pipeline.md` (PICT resource layout).

## Reference Books

Six classic Mac game programming books in `books/` — consult before implementing any subsystem:
- **Black Art of Macintosh Game Programming (1996)** — primary reference
- **Tricks of the Mac Game Programming Gurus (1995)** — tile games, GWorld tricks, CopyBits optimization
- **Mac Game Programming (2002)** — keyboard polling, collision, dirty rectangles
- **Macintosh Game Programming Techniques (1996)** — menus, GWorlds
- **Sex, Lies, and Video Games (1996)** — buffered animation, CopyBits benchmarks
- **Macintosh Game Animation (1985)** — historical reference
