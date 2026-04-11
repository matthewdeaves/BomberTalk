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

## Hardware Deployment

Deploy builds to real Classic Mac hardware using the [classic-mac-hardware-mcp](https://github.com/matthewdeaves/classic-mac-hardware-mcp) MCP server. See that repo's README for setup. Never use raw FTP scripts — the MCP server handles rate limiting and path normalization for RumpusFTP.

## Architecture

### Main Loop (`main.c`)

```
while (!gQuitting) {
    WaitNextEvent(sleep=0)  →  HandleEvent (menus, Cmd-Q only)
    Net_Poll()              →  PeerTalk callbacks fire (messages, connect/disconnect)
    Input_Poll()            →  GetKeys() every iteration, OR new edges into accumulator
    if (deltaTicks >= FRAME_TICKS) {
        gGame.deltaTicks = actual elapsed ticks (capped at 10)
        Screens_Update()    →  dispatches to current screen's Update()
        Screens_Draw()      →  dispatches to current screen's Draw()
        Input_ConsumeFrame() → clears accumulated edges for next frame
    }
}
```

### Timing Model — CRITICAL

**All game timers MUST be tick-based, not frame-based.** Mac SE runs ~6fps while PPC runs ~24fps. Frame-count timers cause gameplay to differ by 4-5x across machines.

- `gGame.deltaTicks` — actual elapsed ticks since last frame (set in main loop, capped at 10)
- Decrement timers by `gGame.deltaTicks`, never by 1
- Constants: `BOMB_FUSE_TICKS=180` (~3s), `EXPLOSION_DURATION_TICKS=20` (~0.33s), `DEATH_FLASH_TICKS=60` (~1s)
- **Movement**: Smooth pixel-by-pixel via fractional accumulator. Speed = `stats.speedTicks` (ticks to cross one tile, default 12). `accumX += tileSize * deltaTicks; movePixels = accumX / ticksPerTile; accumX %= ticksPerTile`. Resolution-independent: same crossing time on 16px and 32px tiles.
- **Positions**: `pixelX`/`pixelY` are authoritative. `gridCol`/`gridRow` derived via center point: `gridCol = (pixelX + tileSize/2) / tileSize`. Bomb placement uses derived grid position.
- **Collision**: Axis-separated AABB against tilemap. Hitbox inset: 2px (16px tiles) / 4px (32px tiles). Move X, clamp, then Y, clamp. Corner sliding nudges toward corridor alignment within threshold (5px/10px).
- **Explosions**: AABB overlap between player hitbox and explosion tile rects. Partial tile overlap = death. Per-frame kill check for players walking into active explosions.
- **Bomb walk-off**: `passThroughBombIdx` per player — set on bomb placement, cleared when hitbox fully leaves bomb tile.
- **Remote players**: Interpolation toward `targetPixelX`/`targetPixelY` via tick-based lerp (INTERP_TICKS=4).
- **Network coordinate normalization**: MsgPosition carries tile-independent fixed-point coords (256 units = 1 tile). Send: `netX = (pixelX << 8) / tileSize`. Receive: `pixelX = (netX * tileSize) >> 8`. This allows 16px-tile Mac SE and 32px-tile PPC to agree on positions. Without this, raw pixel coords cause false explosion kills (wrong grid mapping) and crashes (out-of-bounds grid indices on smaller-tiled machines).
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
1. `Renderer_BeginFrame()` — dirty rect CopyBits bg→work (partial or full), locks sprite PixMaps
2. Draw bombs, explosions, players into work buffer (using cached PixMap pointers)
3. `Renderer_EndFrame()` — unlocks sprites, unlocks work, dirty rect CopyBits work→window, clears dirty grid

**Menu/loading/lobby screens:** Use `Renderer_BeginScreenDraw()` / `Renderer_EndScreenDraw()` which clear work to black and draw text directly.

**Dirty Rectangle System** (002-perf-extensibility):
- Tile-granularity boolean grid `gDirtyGrid[MAX_GRID_ROWS][MAX_GRID_COLS]`
- `Renderer_MarkDirty(col, row)` — mark a tile for redraw next frame
- `Renderer_MarkAllDirty()` — full-screen mode (called after RebuildBackground)
- If >50% tiles dirty, falls back to full-screen CopyBits (avoids overhead of per-tile iteration)
- 32-bit aligned CopyBits rects via `AlignRect32()` — longword moves on both SE (32-pixel) and color (4-pixel)
- All sprite GWorlds locked once in BeginFrame, cached as `static BitMap *` pointers, unlocked in EndFrame

**Performance rules:**
- ForeColor(blackColor)/BackColor(whiteColor) MUST be set before every srcCopy CopyBits call on ALL platforms (per "Sex, Lies and Video Games" 1996 benchmarks — up to 2.5x penalty without)
- `Renderer_RebuildBackground()` redraws all tiles — only call directly for initialization (Renderer_Init, Game_Init)
- `Renderer_RequestRebuildBackground()` sets deferred flag — use for gameplay events (explosions, network block-destroy). Coalesced to one rebuild per frame in BeginFrame.
- Port save/lock is hoisted to `RebuildBackground`, not per-tile
- Pascal strings and `StringWidth()` results are cached as statics in screen draw functions
- `RGBColor` constants are `static const` at file scope, not stack-allocated per tile
- Mark tiles dirty in every code path that changes visible state: player move, bomb place/explode, block destroy, explosion expire, network position update, player disconnect
- `Player_MarkDirtyTiles(playerID)` marks 1-4 tiles for sub-tile positions (player straddling tile boundaries). Called before and after movement updates, and before disconnect deactivation.

### Network Layer (`net.c`)

Thin wrapper around PeerTalk SDK. All network I/O is callback-driven via `PT_Poll()`.

- **Discovery**: UDP broadcast, peers appear in lobby
- **Connection**: TCP mesh — any player can initiate, tiebreaker handles simultaneous connects
- **Player IDs**: Deterministic IP-sort (lowest IP = player 0). No host concept.
- **Messages**: 7 types registered at init. `PT_FAST` (UDP) for positions, `PT_RELIABLE` (TCP) for game events.
- **TCP Keepalive**: PeerTalk sends automatic keepalive frames (type 254) every 20s to prevent TCP timeout during gameplay. Positions go via UDP, so without keepalive the TCP connection starves if no game events (bombs, kills) happen for 60s.
- **MsgPosition v4**: 8 bytes — `{playerID: u8, facing: u8, pixelX: short, pixelY: short, pad: u8[2]}`. Coords are tile-independent fixed-point (256 units = 1 tile), not raw pixels. Sent via PT_FAST every frame the player moves. Remote machines convert to local pixel space and set interpolation targets.
- **Protocol Version**: `BT_PROTOCOL_VERSION 4` sent in MSG_GAME_START. Receivers reject mismatches and show warning in lobby. v3 sent raw pixel coords (broken across different tile sizes). Old v1.0-alpha clients send version 0 (the old `pad` byte).
- **Winner ID Validation**: MSG_GAME_OVER `winnerID` is bounds-checked against MAX_PLAYERS. Values >= MAX_PLAYERS (including 0xFF for draw) treated as no winner.
- **Logging**: `CLOG_LVL_DBG` level (Mac SE: `CLOG_LVL_INFO` to reduce File Manager overhead). All game events instrumented. UDP-broadcast to port 7356 (NOT 7355 — sharing PeerTalk's message UDP port floods MacTCP's 2KB receive buffer). Receive with `socat UDP-RECV:7356 -`. Mac SE has UDP log sink disabled (MacTCP send too slow).

### Global State (`game.h`)

Single `GameState gGame` struct holds all game state. Key fields:
- `isMacSE` / `tileSize` — detected at startup, drives renderer path selection
- `deltaTicks` — elapsed ticks this frame (for tick-based timers)
- `playWidth` / `playHeight` — computed from `TileMap_GetCols() * tileSize` after tilemap load
- `players[MAX_PLAYERS]` — all player state including `peer` pointer, `PlayerStats stats` (bombsMax, bombRange, speedTicks), pixel positions (pixelX/pixelY authoritative, gridCol/gridRow derived), interpolation targets (targetPixelX/Y), fractional accumulators (accumX/Y), bomb pass-through (passThroughBombIdx)
- `bombs[MAX_BOMBS]` — active bomb state with tick-based fuse timers
- Bomb module maintains `gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS]` for O(1) `Bomb_ExistsAt()` lookups. Grid set on placement, cleared on explosion.

### TileMap & TMAP Resource (`tilemap.c`)

- `TileMap_Init()` loads from `'TMAP'` resource 128, falls back to `level1.h` static data. Caches initial state for TileMap_Reset().
- `TileMap_Reset()` restores tilemap from cached initial state — use for round restarts instead of TileMap_Init(). No Resource Manager calls.
- Format: 2-byte cols + 2-byte rows + (cols*rows) tile bytes (big-endian, row-major)
- Dimensions clamped to [7-31] cols, [7-25] rows. Unknown tile values sanitized to TILE_FLOOR.
- `TileMap_ScanSpawns()` finds TILE_SPAWN tiles top-left to bottom-right, fills remaining with default corners
- All gameplay code uses `TileMap_GetCols()`/`TileMap_GetRows()` instead of compile-time GRID_COLS/GRID_ROWS

## Code Style

- C89/C90: no `//` comments, no mixed declarations, no VLAs, no stdint.h
- All memory allocated at init time — no malloc during gameplay
- GetKeys() for input polling — never use keyDown events for movement. Movement checks both `Input_IsKeyDown()` (held) and `Input_WasKeyPressed()` (accumulated edges) to catch quick taps between frames on slow machines.
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

**Mac SE performance**: Lobby ~3fps, gameplay 10-19fps (measured 2026-04-10). Minimize Toolbox trap calls in hot paths. Cache `StringWidth()`, avoid per-tile `SavePort`/`LockPixels`. Movement cooldown must fall through on expiry (not waste a frame), and direction input must use accumulated edges — at 3-10fps a quick tap can complete entirely between frames.

**CopyBits alignment for sub-tile sprites**: Sub-tile sprite positions will be misaligned for CopyBits (up to ~2x penalty per Sex Lies p.148). Accepted: Mac SE uses PaintRect (no penalty), PPC uses transparent mode (already slower). If PPC FPS drops measurably, consider pre-shifted sprite GWorlds (4 copies per sprite) as mitigation.

**BOMBERTALK_DEBUG**: CMake option (default ON). When OFF, adds `-DCLOG_STRIP` causing all `CLOG_*` macros to expand to `((void)0)`. clog library still linked (PeerTalk depends on it). Guard `clog_init`/`clog_set_file`/`clog_set_network_sink`/`clog_shutdown` with `#ifndef CLOG_STRIP`.

## Spec Artifacts

- `specs/001-v1-alpha/` — v1.0-alpha design: `data-model.md` (network message formats), `contracts/network-protocol.md` (IP-sort player IDs, message flow), `contracts/asset-pipeline.md` (PICT resource layout).
- `specs/002-perf-extensibility/` — Performance & extensibility upgrade: dirty rectangles, protocol versioning, TMAP resource loading, PlayerStats struct. Key refs: `data-model.md`, `contracts/network-protocol.md`, `research.md`.
- `specs/004-smooth-movement/` — Smooth sub-tile pixel movement: pixel-authoritative positions, AABB collision, bomb walk-off, corner sliding, MsgPosition v3, remote interpolation, disconnect cleanup, BOMBERTALK_DEBUG toggle. Key refs: `research.md` (10 decisions), `data-model.md`, `contracts/`, `tasks.md` (37 tasks).

## Reference Books

Six classic Mac game programming books in `books/` — consult before implementing any subsystem:
- **Black Art of Macintosh Game Programming (1996)** — primary reference
- **Tricks of the Mac Game Programming Gurus (1995)** — tile games, GWorld tricks, CopyBits optimization
- **Mac Game Programming (2002)** — keyboard polling, collision, dirty rectangles
- **Macintosh Game Programming Techniques (1996)** — menus, GWorlds
- **Sex, Lies, and Video Games (1996)** — buffered animation, CopyBits benchmarks
- **Macintosh Game Animation (1985)** — historical reference

## Active Technologies
- C89/C90 (Retro68 cross-compiler) + PeerTalk SDK (latest, commit 7e89304), clog (latest, commit e8d5da9), Retro68/RetroPPC toolchains (002-perf-extensibility)
- Classic Mac resource fork ('TMAP' resource type for map data) (002-perf-extensibility)
- C89/C90 (Retro68 cross-compiler) + PeerTalk SDK, clog, Retro68/RetroPPC toolchains, Classic Mac Toolbox (QuickDraw, Resource Manager) (003-optimize-correctness)
- Mac resource fork ('TMAP' resource type 128), static level data fallback (003-optimize-correctness)
- C89/C90 (Retro68 cross-compiler) + PeerTalk SDK (commit 7e89304), clog (commit e8d5da9), Retro68/RetroPPC toolchains, Classic Mac Toolbox (QuickDraw) (004-smooth-movement)

## Recent Changes
- 004-smooth-movement: Pixel-authoritative positions replace grid-locked movement. Fractional accumulator for resolution-independent speed. AABB collision with tilemap (axis-separated) and explosions (overlap = death). Bomb walk-off via passThroughBombIdx. Corner sliding. MsgPosition v4 with tile-independent network coords (256 units/tile) — fixes false kills and crashes when SE (16px) and PPC (32px) play together. Remote interpolation. Disconnect dirty rect fix. BOMBERTALK_DEBUG CMake toggle. Mac SE clog UDP sink disabled (MacTCP send too slow). All CLOG_STRIP-safe.
- Input responsiveness + TCP keepalive: Movement cooldown falls through on expiry instead of wasting a frame (critical at Mac SE 3-10fps). Direction input checks both held keys and accumulated edges to catch quick taps between frames. PeerTalk TCP keepalive (type 254, 20s interval) prevents connection timeout during gameplay — positions go via UDP, so TCP starved if no game events for 60s.
- 003-optimize-correctness: ForeColor/BackColor normalization on all platforms (not just Mac SE) for faster CopyBits. Deferred background rebuild via Renderer_RequestRebuildBackground() batching multiple block-destroys to one rebuild per frame. TileMap_Reset() for round restarts without Resource Manager calls. Spatial bomb grid (gBombGrid) for O(1) Bomb_ExistsAt(). Peer pointer NULLed on disconnect.
- 002-perf-extensibility: Dirty rectangle renderer optimization, LockPixels hoisting, static color constants, 32-bit CopyBits alignment. Protocol versioning (BT_PROTOCOL_VERSION 2) with lobby mismatch indicator. TMAP resource-based map loading with dynamic grid dimensions. PlayerStats struct replacing standalone bombRange. -std=c89 enforced in CMake.
