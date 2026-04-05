# Research: BomberTalk v1.0-alpha

**Feature**: `001-v1-alpha`
**Date**: 2026-04-05

## R1: Target Platform Specifications

| Machine | CPU | Clock | RAM | System | Network | Display |
|---------|-----|-------|-----|--------|---------|---------|
| Mac SE | 68000 | 8 MHz | 4 MB | System 6 | MacTCP | 9" mono 512x342 |
| Performa 6200 | PPC 603 | 75 MHz | 40 MB | 7.5.3 | MacTCP | 640x480 color |
| Performa 6400 | PPC 603e | 200 MHz | 48 MB | 7.6.1 | Open Transport | 640x480 color |

**Decision**: Target 640x480 color displays for the primary experience. The Mac SE
with its 512x342 monochrome screen needs special consideration — may need 16x16 tiles
instead of 32x32, or run in a scrolling viewport. For v1.0-alpha, we target color Macs
first and add Mac SE display adaptation as a stretch goal. The Mac SE MUST still build
and run (networking works) but may have reduced visuals.

**Update**: Mac SE runs System 6, not System 7. Verify all Toolbox calls are System 6
compatible. Key concern: NewCWindow may not be available on System 6 without color
QuickDraw. May need to fall back to NewWindow (B&W) on the SE.

## R2: PeerTalk SDK Integration

PeerTalk provides exactly what BomberTalk needs:

- **PT_Init/PT_Shutdown**: Lifecycle — auto-sizes buffers based on FreeMem()
- **PT_StartDiscovery**: UDP broadcast on port 7353, fires PT_OnPeerDiscovered
- **PT_Connect/PT_Disconnect**: TCP connection on port 7354
- **PT_RegisterMessage**: Declare MSG_POSITION as PT_FAST, MSG_BOMB as PT_RELIABLE
- **PT_Send/PT_Broadcast**: Send to one peer or all peers
- **PT_Poll**: Drive all I/O — call from game loop every iteration
- **PT_OnMessage**: Receive callback per message type

PeerTalk allocates all memory at PT_Init time (principle V). On a 4 MB Mac SE,
PeerTalk uses approximately 50-200 KB depending on FreeMem(). BomberTalk must
budget for this.

**Decision**: BomberTalk wraps PeerTalk in a thin net.c/net.h layer (as shown in
the plan) to keep PeerTalk includes out of game headers.

## R3: Memory Budget (Mac SE)

```
Application partition:   1000 KB (SIZE resource minimum)
- Toolbox overhead:       ~100 KB (estimated)
- PeerTalk allocation:    ~100 KB (4 MB Mac, small tier)
- Available for game:     ~800 KB

Game memory:
- TileMap:                   195 bytes (15x13)
- Player array (4 players):  ~128 bytes
- Bomb array (16 max):       ~256 bytes
- Game state/globals:         ~1 KB
- GWorld (work buffer):    ~200 KB (480x416 @ 8-bit)
- GWorld (background):     ~200 KB (480x416 @ 8-bit)
- Stack:                    ~32 KB
Total game:                ~435 KB

Margin:                    ~365 KB (comfortable)
```

**Decision**: 1 MB minimum partition size is sufficient. Set preferred to 1.5 MB
for headroom. Two GWorlds at 480x416 @ 8-bit is the largest allocation; if this
fails on Mac SE, fall back to a single buffer (slight flicker acceptable).

**Note**: Mac SE is monochrome (1-bit), so GWorlds would be much smaller there
(480x416 @ 1-bit = ~25 KB each). This actually helps memory significantly.

## R4: Build System

Three build targets using Retro68 CMake toolchains:

```bash
# 68k (Mac SE) — MacTCP
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake

# PPC (6400) — Open Transport
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT

# PPC (6200) — MacTCP
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=MACTCP
```

PeerTalk and clog must be pre-built for each target. The game links against
static libraries from PeerTalk's build directories.

**Decision**: Follow PeerTalk's CMake pattern exactly. Use the same CLOG_DIR,
PEERTALK_DIR, and platform detection variables.

## R5: Game Programming Book References

Six classic Mac game programming books are available in `books/`:

1. **Black Art of Macintosh Game Programming (1996)** — Primary reference for
   Toolbox init, event loop, GWorld rendering, keyboard input
2. **Tricks of the Mac Game Programming Gurus (1995)** — Tile-based games,
   offscreen rendering tricks, color table seed optimization
3. **Mac Game Programming (2002)** — Latest reference, C++ but patterns apply.
   Keyboard polling (WasKeyPressed), physics/collision
4. **Macintosh Game Programming Techniques (1996)** — Menu system, GWorld patterns
5. **Sex, Lies, and Video Games (1996)** — Buffered animation, sprite techniques
6. **Macintosh Game Animation (1985)** — Historical, pre-Color QuickDraw

**Decision**: Primary architecture follows Black Art + Tricks of Gurus patterns
as documented in BOMBERMAN_CLONE_PLAN.md. The plan already extracts the relevant
code patterns from all six books.

## R6: Screen System Architecture

The game needs four distinct screens:
1. **Loading**: Static screen with title text, brief delay
2. **Menu**: Three options rendered with QuickDraw text
3. **Lobby**: Player list, discovery status, start button
4. **Game**: Tile map, players, bombs, explosions

**Decision**: Implement as a state machine in main.c. Each screen has Init/Update/Draw
functions. Screen transitions are clean — dispose old screen resources, init new screen.
No complex UI framework needed; QuickDraw text and rectangles suffice for alpha.

## R7: Network Message Types

| Type ID | Name | Transport | Size | Frequency |
|---------|------|-----------|------|-----------|
| 0x01 | MSG_POSITION | PT_FAST | 5 bytes | 5/sec per player |
| 0x02 | MSG_BOMB_PLACED | PT_RELIABLE | 5 bytes | On action |
| 0x03 | MSG_BOMB_EXPLODE | PT_RELIABLE | 5 bytes | On timer |
| 0x04 | MSG_BLOCK_DESTROYED | PT_RELIABLE | 3 bytes | On explosion |
| 0x05 | MSG_PLAYER_KILLED | PT_RELIABLE | 2 bytes | On explosion |
| 0x06 | MSG_GAME_START | PT_RELIABLE | 2 bytes | Once |
| 0x07 | MSG_GAME_OVER | PT_RELIABLE | 2 bytes | Once |
| 0x08 | MSG_PLAYER_INFO | PT_RELIABLE | ~20 bytes | On join |

**Decision**: Keep messages small. Position update is just (playerID, col, row, facing,
frame) = 5 bytes. This fits easily in PeerTalk's UDP packet (max 1400 bytes). At 5
updates/sec with 4 players = 20 messages/sec = ~100 bytes/sec — trivial for Ethernet.
