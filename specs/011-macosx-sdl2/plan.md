# Implementation Plan: Mac OS X + Modern macOS Builds (Carbon/QuickDraw + SDL2)

**Branch**: `011-macosx-sdl2` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/011-macosx-sdl2/spec.md`

## Summary

Add two new eras of BomberTalk target — native OS X (10.3–10.7, PPC+Intel, via Carbon/QuickDraw) and modern macOS 26+ (Apple Silicon, via SDL2) — while preserving the three existing Classic Mac builds unchanged. All targets speak the same PeerTalk discovery/TCP/UDP wire protocol, so a Mac SE over MacTCP can play an Apple Silicon Mac (the core PeerTalk proof, Principle I).

Technical approach: carve a **portable game core** (game logic + `net.c`) that compiles without the Toolbox, sitting behind a small build-selected **platform seam** (portable `TickCount`/`Rect`/time; swappable renderer/input/loop backends). The existing QuickDraw renderer serves both Classic Mac *and* OS X Carbon (QuickDraw exists on OS X); a new **SDL2 backend** serves modern macOS and is developed+played on the Linux dev box (a faithful proxy — same API, same little-endian arch). A small **byte-order layer** makes little-endian builds interoperate with big-endian Classic/PPC Macs. Networking reuses PeerTalk v1.12.1's hardware-validated POSIX/BSD backend — `net.c` is unchanged. The OS X Carbon path is gated on a **viability spike** (QuickDraw windowing on 10.7 i386 is the one thing PeerTalk never proved).

## Technical Context

**Language/Version**: C89/C90 for shared core (`-std=c89 -Wall -Wextra`); platform backends target C89 where practical (SDL2 and Carbon are C-callable).  
**Primary Dependencies**: PeerTalk SDK v1.12.1 (POSIX/BSD backend), clog v1.4.1, SDL2 (modern target only), Carbon + QuickDraw (OS X 10.3–10.7 target only), Classic Mac Toolbox (existing classic targets).  
**Storage**: N/A — all state in memory; tilemap from `kLevel1` or `'TMAP'` resource 128 (classic) / equivalent embedded data (POSIX).  
**Testing**: Byte-order unit tests + headless simulation smoke test on Linux; hardware validation on the four OS X Macs (SSH), the M5 (native), and ≥1 Classic Mac (classic-mac-hardware MCP).  
**Target Platform**: Classic Mac (68k/PPC, existing) + OS X 10.3.9 PPC + OS X 10.4–10.7 PPC/i386 + macOS 26+ arm64.  
**Project Type**: Single C codebase, multiple build targets (desktop game).  
**Performance Goals**: Classic budgets unchanged (Mac SE 10+ fps floor). SDL2/Carbon targets vastly exceed classic hardware; no new perf constraint beyond smooth 60 fps-capable play.  
**Constraints**: Shared core must not include Classic Mac headers; no SDL/Toolbox types leak into shared headers; three classic builds stay green and byte-identical on the wire; warning-clean except two documented third-party warnings.  
**Scale/Scope**: ~5.2k LOC existing; adds a platform seam, one new renderer backend (SDL2), an input/loop POSIX shell, a byte-order layer, and two build pipelines (Carbon fat via `mini-intel`; SDL2 native CMake).

## Constitution Check

*GATE evaluated against constitution v1.0.0.*

| Principle | Status | Notes |
|---|---|---|
| I. Prove PeerTalk Works | ✅ **Strongly aligned** | Cross-era Mac-SE-↔-Apple-Silicon play is the strongest possible PeerTalk demonstration. |
| II. Run on All Three Macs | ✅ Pass (guarded) | FR-019: all three classic builds stay green + byte-identical on wire (SC-004). This feature only *adds* targets. |
| III. Ship Screens | ✅ Pass | SDL2/Carbon builds render loading/menu/lobby/game (FR-007). |
| IV. C89 Everywhere | ⚠️ Justified deviation | Shared core stays strict C89. Platform backends (SDL2, Carbon) target C89 where practical but may use platform-natural C. See Complexity Tracking. |
| V. Mac SE Is the Floor | ✅ Pass | Classic memory/fps budgets untouched; new targets have far more RAM. |
| VI. Simple Graphics, Never Blocking | ✅ Pass | SDL2 renderer keeps the colored-rect fallback (FR-008). |
| VII. Fixed Frame Rate, Poll Everything | ✅ Pass | POSIX/SDL2 main loop is poll-based (SDL event poll + `PT_Poll`), no threads. |
| VIII. Network State Authoritative | ✅ Pass | Authority model unchanged; byte-order layer preserves semantics (US3). |
| IX. The Books Are Gospel | ✅ Pass (extended) | Books remain authority for Classic/QuickDraw. For the OS X path, PeerTalk's `build-macosx-fat.md` (hardware-validated) is the governing reference (Principle I spirit: reuse PeerTalk's proven approach). |
| X. One Codebase, Three Builds | ⚠️ Justified deviation | Feature expands beyond three builds and adds platform-specific *game* code (GUI backends), which Principle X assigns to PeerTalk. PeerTalk handles *networking* differences only; GUI differences cannot be delegated to it. See Complexity Tracking. |

**Roadmap note**: The constitution's own roadmap lists **"v1.3: POSIX build for Linux/modern macOS"** as a future version, and "POSIX/modern Mac build" is explicitly deferred (not forbidden) in v1.0-alpha scope. This feature realizes that roadmap item. A minor constitution amendment to bless multi-era GUI backends is recommended as a follow-up governance action (surfaced, not blocking).

**Gate result**: PASS with two justified deviations recorded in Complexity Tracking. No unjustified violations.

## Project Structure

### Documentation (this feature)

```text
specs/011-macosx-sdl2/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── renderer-backend.md      # Public renderer surface the backends implement
│   ├── network-wire-endianness.md  # Byte-order contract for message payloads
│   └── build-targets.md         # Build recipes: SDL2 (CMake) + Carbon fat (shell)
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
include/
├── platform.h           # NEW: build-selected seam — Tick/Rect/time shims, backend selection macros
├── renderer.h           # EXISTING: public renderer surface (backend-agnostic calls the screens use)
├── net_wire.h           # NEW: byte-order encode/decode for message payloads (no-op on big-endian)
└── ...                  # existing headers unchanged

src/
├── player.c bomb.c tilemap.c screens.c screen_*.c   # SHARED CORE — carve to compile off-Toolbox
├── net.c                # SHARED — unchanged logic; message pack/unpack routed through net_wire
├── renderer.c           # QuickDraw backend — serves Classic Mac AND OS X Carbon
├── renderer_sdl.c       # NEW: SDL2 backend (implements renderer.h public surface)
├── input.c              # EXISTING: GetKeys backend (classic + Carbon)
├── input_sdl.c          # NEW: SDL held-key backend
├── main.c               # EXISTING: WaitNextEvent loop (classic + Carbon)
├── main_posix.c         # NEW: SDL2/POSIX poll loop
└── platform_posix.c     # NEW: Tick/time shim implementation for POSIX

tools/
├── build-macosx-fat.sh  # NEW: adapted from ~/peertalk (Carbon .app; fat + ppc-only 10.3.9)
└── osx-screen-run.sh     # reused from ~/peertalk pattern (on-screen launch)

CMakeLists.txt           # EXTENDED: SDL2 native target (find SDL2; skip cleanly if absent)

resources/gfx/           # + PNG/raw derivatives for SDL2 (converted from existing PICTs)
tests/                   # NEW: byte-order unit tests + headless sim smoke (POSIX/Linux)
```

**Structure Decision**: Single source tree, build-selected backends. The seam is `include/platform.h` (types/time/backend selection) + the renderer/input/loop backend split. Classic builds keep their existing files verbatim; new files are compiled only into the POSIX/SDL2 and Carbon targets. This honors "one codebase" while acknowledging that GUI backends are irreducibly platform-specific (Complexity Tracking).

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| **Principle X**: >3 builds + platform-specific GUI game code (renderer/input/loop backends) | No single GUI technology spans OS 10.3 → macOS 26 (Carbon/QuickDraw removed in 10.15; modern frameworks absent on Panther). PeerTalk delegates *networking* portability but has no graphics; GUI portability must live in the game. | "Let PeerTalk handle it" is impossible — PeerTalk is networking-only. A single shared GUI backend cannot exist across the era span. The seam is the minimal structure that keeps the *core* single while isolating unavoidable per-era GUI code. |
| **Principle IV**: platform backends may use platform-natural C rather than strict C89 | SDL2/Carbon toolchains and idioms are not Retro68; forcing strict C89 on backend glue adds friction for no Classic-Mac benefit (backends never run on classic hardware). | Strict C89 everywhere buys nothing here — backend files are excluded from classic builds. The *shared core* (which classic builds compile) stays strict C89, preserving the actual constraint. Backends target C89 where practical for consistency. |
