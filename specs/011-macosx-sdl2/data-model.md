# Phase 1 Data Model: Mac OS X + Modern macOS Builds

This feature adds no persistent storage and no new gameplay state. The "data model" here is (a) the platform seam abstractions, (b) the network message wire layout with its endianness surface, and (c) the build artifacts.

## 1. Platform seam (`include/platform.h`)

Build-selected boundary. Selected by a single define per target (e.g. `BT_PLATFORM_POSIX`, `BT_PLATFORM_CARBON`, or the existing classic path when neither is set).

| Abstraction | Classic / Carbon | POSIX (SDL2) | Notes |
|---|---|---|---|
| Tick source | Toolbox `TickCount()` (60 Hz) | `platform_posix.c` monotonic → 60 Hz ticks-since-start | Same semantics: `unsigned long`, wraps like TickCount. Used by 007 wall-clock timers + bomb anim. |
| `Rect` type | Toolbox `Rect` | portable `{short left, top, right, bottom;}` | Same field order/layout; used by AABB collision (`player.c`, `bomb.c`). |
| Renderer backend | `renderer.c` (QuickDraw) | `renderer_sdl.c` (SDL2) | Both implement `renderer.h` public surface. |
| Input backend | `input.c` (`GetKeys`) | `input_sdl.c` (`SDL_GetKeyboardState`) | Same held-key + edge-accumulator semantics. |
| Main loop | `main.c` (`WaitNextEvent`) | `main_posix.c` (SDL poll + `PT_Poll`) | Poll-based, no threads (Principle VII). |

**Rule**: No SDL or Toolbox type appears in any shared-core header. Selection is at build time (compile the right backend `.c`), not `#ifdef` scattered through logic.

## 2. Network message wire layout & endianness surface

BomberTalk's seven messages. **Endianness surface = the two shaded multi-byte fields only.** Everything else is single-byte and identical on every architecture.

| Message | Transport | Fields | Multi-byte fields (need swap on LE) |
|---|---|---|---|
| `MSG_POSITION` (`MsgPosition` v4, 8 B) | PT_FAST | playerID u8, facing u8, **pixelX i16**, **pixelY i16**, pad u8[2] | **pixelX, pixelY** |
| `MSG_BOMB_PLACED` | PT_RELIABLE | playerID u8, gridCol u8, gridRow u8, range u8, fuseTicks u8 | none |
| `MSG_BOMB_EXPLODE` | PT_RELIABLE | gridCol u8, gridRow u8, range u8 | none |
| `MSG_BLOCK_DESTROYED` | PT_RELIABLE | gridCol u8, gridRow u8 | none |
| `MSG_PLAYER_KILLED` | PT_RELIABLE | playerID u8, killerID u8 | none |
| `MSG_GAME_START` | PT_RELIABLE | version u8, numPlayers u8 | none |
| `MSG_GAME_OVER` | PT_RELIABLE | winnerID u8 | none |

**Consequence**: The entire cross-era byte-order layer reduces to converting `pixelX` and `pixelY` in `MsgPosition` on send/receive. `version` occupies the old v1.0-alpha `pad` byte (single byte) — endian-neutral, so protocol negotiation is unaffected. Single-byte fields (playerID, facing, grid coords, range) are architecture-independent by definition.

**Canonical order**: big-endian (network / Classic-Mac order). Big-endian builds: conversions are no-ops → classic wire bytes are byte-identical (SC-004). Little-endian builds: swap the two shorts.

**Protocol version**: no bump. The byte *layout* on the wire is unchanged; only host-side interpretation on little-endian builds changes. `BT_PROTOCOL_VERSION` stays 4/5 as-is.

**Note on the fixed-point coords**: `pixelX`/`pixelY` already carry tile-independent fixed-point values (256 units = 1 tile) per the existing normalization. The endianness swap is orthogonal — it only reorders the bytes of each `short`; the fixed-point math is unchanged.

## 3. Build artifacts

| Artifact | Arch/OS | Built where | How |
|---|---|---|---|
| Classic Mac binaries ×3 | 68k/PPC CFM | Linux (Retro68/RetroPPC) | existing CMake toolchains (unchanged) |
| `BomberTalk.app` (fat) | ppc+i386, min 10.4 | `mini-intel` (10.7) via SSH | `tools/build-macosx-fat.sh` (Carbon .app) |
| `BomberTalk.app` (ppc-only) | ppc, min 10.3.9 | `mini-intel` via SSH | same script, `SDK=…10.3.9 MIN=10.3.9 ARCHS=ppc` |
| SDL2 binary (dev) | x86-64 Linux | Linux dev box | CMake `find_package(SDL2)` |
| SDL2 app (native) | arm64 | M5 (macOS 26) | native CMake/clang; packaging M5-side |

All link PeerTalk v1.12.1 (POSIX/BSD backend for the OS X/SDL2 rows) + clog v1.4.1.

## 4. SDL2 asset set

Derived from existing PICT sprite sources via the `scripts/embed-gfx.py` pipeline. PNG or embedded raw RGBA under `resources/gfx/`. Renderer falls back to colored rectangles if absent (Constitution VI). No new artwork — same visual identity as the classic color-Mac look.

## State transitions

None added. Screen state machine, bomb/explosion timers, and network authority model are unchanged. The only behavioral change is transport-layer byte order on little-endian builds, which is transparent to game logic.
