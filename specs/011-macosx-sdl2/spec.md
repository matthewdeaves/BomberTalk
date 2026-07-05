# Feature Specification: Mac OS X + Modern macOS Builds (Carbon/QuickDraw + SDL2)

**Feature Branch**: `011-macosx-sdl2`  
**Created**: 2026-07-05  
**Status**: Draft  
**Input**: User request: "I want a native Mac OS X application that runs on 10.3, 10.4, 10.5 (PPC) and 10.7 (Intel mini), AND a build for macOS 26+ on Apple Silicon (via SDL2). Do as much as possible on the Linux dev box and push it up so the M5 MacBook Air can pick up the modern packaging. PeerTalk v1.12.1 already provides the hardware-validated POSIX/BSD networking story — reuse it, don't reinvent it."

## Context & Strategy

BomberTalk currently ships three Classic Mac builds (68k MacTCP, PPC OT, PPC MacTCP) via Retro68/RetroPPC. This feature adds two new eras of target, all speaking the **same PeerTalk discovery/TCP/UDP wire protocol**, so a Mac SE over MacTCP can play against an Apple Silicon Mac:

| Target | Arch | GUI backend | Net backend | Build system | Where built |
|---|---|---|---|---|---|
| Classic Mac (SE / 6200 / 6400) | 68k / PPC CFM | QuickDraw Toolbox *(exists)* | MacTCP / OT | Retro68 CMake *(exists)* | Linux → toolchain |
| OS X 10.3.9 | PPC | Carbon + QuickDraw | PeerTalk POSIX | fat shell script (ppc-only) | Linux → `mini-intel` (10.7) |
| OS X 10.4 – 10.7 | PPC + i386 | Carbon + QuickDraw | PeerTalk POSIX | fat shell script | Linux → `mini-intel` (10.7) |
| macOS 26+ | arm64 | **SDL2** | PeerTalk POSIX | native CMake / clang | M5 MacBook Air |

Two facts shape the whole design:
1. **No single technology spans the range.** Carbon/QuickDraw was removed in macOS 10.15 — it does not exist on Apple Silicon. Modern frameworks do not exist on Panther. Therefore at least two new GUI backends are required behind one shared renderer seam.
2. **The game core is already portable.** Toolbox coupling is concentrated in `renderer.c` (QuickDraw), `input.c` (`GetKeys`), and the `main.c` event/timing shell. Game logic (`player.c`, `bomb.c`, `tilemap.c`, `screens.c`) and `net.c` include no Mac headers and compile as portable C89. PeerTalk's POSIX/BSD backend (validated v1.12.1) runs on OS X *and* Apple Silicon, so `net.c` does not change.

**Linux is a near-perfect proxy for the M5's SDL2 build** (same SDL2 API, same little-endian arch, same POSIX networking). The SDL2 game is therefore developed and played on the Linux dev box; the M5's remaining job is native packaging only.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Portable Game Core Off the Toolbox (Priority: P1)

As a developer, the game simulation and network layer must compile and run on a non-Toolbox platform (Linux/POSIX) with no Classic Mac headers, so that every non-classic target can reuse one shared core behind a platform seam.

**Why this priority**: This is the foundation. Every OS X and macOS build sits on top of a core that compiles without QuickDraw/Toolbox. Nothing else can proceed until the seam exists.

**Independent Test**: Compile the shared core (game logic + `net.c` + PeerTalk POSIX backend) on the Linux dev box with `-std=c89 -Wall -Wextra` against a stub/headless renderer, and run a non-graphical smoke test that steps the simulation and connects to a PeerTalk POSIX peer.

**Acceptance Scenarios**:

1. **Given** the shared core sources, **When** compiled on Linux with a non-Toolbox platform header, **Then** compilation succeeds with zero errors and no Classic Mac headers are included by any shared-core file.
2. **Given** a `TickCount()` call in game/network timing code, **When** built for a POSIX target, **Then** it resolves to a portable 60 Hz monotonic tick shim with identical semantics (ticks since start).
3. **Given** the `Rect` struct used for AABB collision in `player.c`/`bomb.c`, **When** built for a POSIX target, **Then** it resolves to a portable 4-field struct with identical field layout and no Toolbox dependency.
4. **Given** the classic Mac targets, **When** built after the seam is introduced, **Then** all three (68k MacTCP, PPC OT, PPC MacTCP) still compile clean and behave identically (Constitution II).

---

### User Story 2 - Playable SDL2 BomberTalk on the Dev Box (Priority: P1)

As a developer (and proxy for the M5), I can run a graphical, playable BomberTalk in an SDL2 window on the Linux dev box, connected to a PeerTalk POSIX peer, so that the modern game is proven before the M5 ever builds it.

**Why this priority**: SDL2 runs on Linux with the same API the M5 will use. Proving the SDL2 renderer, input, and main loop here reduces the M5's work to packaging. Delivers the largest, most visible slice of value.

**Independent Test**: Build the SDL2 target on Linux, launch two instances (or one instance + a headless POSIX peer), and play a full round: movement, bomb placement, explosions, block destruction, death, game-over — all rendered in the SDL2 window with held-key movement.

**Acceptance Scenarios**:

1. **Given** the SDL2 renderer backend implementing the `renderer.h` public surface, **When** a gameplay frame is drawn, **Then** tiles, players, bombs, and explosions render correctly in an SDL2 window that visually matches the classic color-Mac layout.
2. **Given** SDL2 keyboard input via held-key state, **When** a movement key is held, **Then** the player moves smoothly (continuous), and a quick tap registers a step (parity with the classic held-key + edge-accumulator model).
3. **Given** two SDL2 instances on the same network, **When** one places a bomb, **Then** the other renders the bomb, its explosion, and any resulting block destruction and kills.
4. **Given** the loading/menu/lobby/game screens, **When** each is shown in the SDL2 build, **Then** all screens render and transition correctly (Constitution III).

---

### User Story 3 - Cross-Era Play Across Endianness (Priority: P1)

As a player, a little-endian Mac (Intel OS X, Apple Silicon, or the Linux dev peer) must correctly interoperate with a big-endian Classic/PPC Mac over the shared wire, so that a Mac SE can genuinely play an Apple Silicon Mac.

**Why this priority**: Cross-era interop is the entire point of the feature (Constitution I — prove PeerTalk works). BomberTalk's own message payloads are multi-byte structs; without byte-order handling on little-endian builds, positions and events corrupt across eras.

**Independent Test**: Byte-order conversion is unit-testable on Linux (encode → decode round-trips, plus known big-endian byte vectors decoded on a little-endian build). Full LE↔BE interop is confirmed on hardware against a Classic/PPC Mac.

**Acceptance Scenarios**:

1. **Given** a BomberTalk network message (e.g. `MsgPosition`, MSG_GAME_START, MSG_BOMB_EXPLODE) with multi-byte fields, **When** encoded on a little-endian build, **Then** the bytes on the wire are big-endian (network/Classic-Mac order), matching what a 68k/PPC Mac produces.
2. **Given** a big-endian message arriving on a little-endian build, **When** decoded, **Then** field values match the sender's intent (positions map to the same tile, protocol version reads correctly).
3. **Given** the Classic Mac builds (big-endian), **When** the conversion layer is added, **Then** their on-wire bytes are unchanged and no per-message swapping cost is added on big-endian targets.
4. **Given** protocol version negotiation, **When** an OS X/modern client and a Classic client connect, **Then** version checks succeed and mismatches are reported in the lobby exactly as today.

---

### User Story 4 - Native OS X Application for 10.3–10.7 (Priority: P2)

As an owner of a PPC/Intel Mac fleet (G3 10.3.9, G4 10.4, G5 10.5, Intel mini 10.7), I can launch a native windowed BomberTalk that uses Carbon + QuickDraw for graphics and PeerTalk's POSIX/BSD backend for networking, so I can play on real vintage OS X hardware.

**Why this priority**: Highly wanted, but gated on an unproven capability (QuickDraw + Carbon windowing on OS X, especially 10.7 Intel), which PeerTalk never validated (its OS X apps are console-only). It also requires the old fleet powered on and builds driven to the 10.7 host. P2 reflects risk and hardware dependency, not desirability.

**Independent Test**: A viability spike (a minimal Carbon Mach-O that opens a window, `CopyBits` a colored rect, reads a key, quits) built fat on `mini-intel` and run on the Intel mini (10.7) and one PPC Mac, launched in the desktop session via the on-screen mechanism. If green, build the full fat `.app` and validate a round on hardware.

**Acceptance Scenarios**:

1. **Given** the Carbon viability spike, **When** run on the Intel mini (10.7, i386) and a PPC Mac, **Then** a window opens and QuickDraw drawing appears on-screen. *(Gate for the rest of this story.)*
2. **Given** the existing QuickDraw renderer (`renderer.c`, color path), **When** compiled into a Carbon Mach-O app, **Then** it draws the game without a QuickDraw rewrite.
3. **Given** the fat (ppc+i386, min 10.4) build, **When** run on the G4, G5, and Intel mini, **Then** the same binary launches and plays on each via dyld slice selection.
4. **Given** the ppc-only (min 10.3.9) build, **When** run on the G3, **Then** it launches and plays (10.3.9 is below the fat binary's 10.4 floor).
5. **Given** a native OS X BomberTalk and a Classic Mac (e.g. Mac SE over MacTCP) on the same network, **When** they join a game, **Then** they play across the wire (cross-era interop, US3).

---

### User Story 5 - Native macOS 26+ App on Apple Silicon (Priority: P2)

As an M5 MacBook Air owner, I can build and run a native arm64 BomberTalk on macOS 26+ from the pushed SDL2 sources with minimal platform-specific work, and play it against the rest of the fleet.

**Why this priority**: Depends on US2 (the SDL2 game proven on Linux). Once US2 is pushed, this is mostly a native build + packaging step, so it is sequenced after the SDL2 core lands but is a distinct, hardware-specific deliverable owned by the M5.

**Independent Test**: On the M5, `brew install sdl2` (or vendored SDL2), configure/build the SDL2 target with native CMake/clang for arm64, and run a round against a Linux POSIX peer and against a Classic Mac.

**Acceptance Scenarios**:

1. **Given** the pushed SDL2 sources, **When** built natively on macOS 26 arm64, **Then** compilation succeeds and produces a runnable native app with no source changes to the shared core.
2. **Given** the native arm64 app, **When** launched, **Then** it opens an SDL2 window, plays a full round, and honors Retina/high-DPI scaling.
3. **Given** the native macOS app and a Classic Mac, **When** they connect, **Then** cross-era play works (US3).
4. **Given** the packaged app, **When** distributed on macOS 26, **Then** it launches as a proper `.app` bundle (packaging/signing details captured as M5-side tasks).

---

### Edge Cases

- **QuickDraw absent on 10.7 Intel**: If the US4 spike shows QuickDraw is unusable on Lion i386, the Carbon fat plan is re-scoped (e.g. PPC-only native Carbon for 10.3–10.5, plus an alternative for the Intel mini) — the spike decides before any large build investment.
- **GUI over raw SSH**: A Carbon windowed app launched via bare `ssh host ./app` may fail to reach the logged-in Aqua session's WindowServer; on-screen runs use the desktop-session launcher (osascript), not headless SSH.
- **Endianness on mixed OS X fat binary**: The OS X fat app spans big-endian (PPC 10.3–10.5) and little-endian (Intel 10.7); byte-order handling (US3) must be arch-driven, not target-driven.
- **SDL2 minimum OS**: SDL2 does not support OS X 10.3–10.7, so it is a modern-only backend; the legacy fleet uses Carbon/QuickDraw. There is no shared SDL backend across both eras.
- **Asset format mismatch**: Classic assets are PICT resources; SDL2 needs PNG (or embedded raw). A conversion step derives SDL assets from the existing PICT sources (`scripts/embed-gfx.py` pipeline as reference).
- **Missing SDL2 at build time**: If SDL2 is not found, the SDL2 target is skipped with a clear message; classic and Carbon targets are unaffected.
- **Debug logging**: clog's file/network sinks must behave on POSIX (no MacTCP UDP sink); `BOMBERTALK_DEBUG`/`CLOG_STRIP` semantics preserved.

## Requirements *(mandatory)*

### Functional Requirements

**Portable core & platform seam**
- **FR-001**: The shared game core (game logic + `net.c`) MUST compile for a POSIX target with no Classic Mac Toolbox headers included by any shared-core source or header.
- **FR-002**: A platform abstraction MUST provide portable equivalents for `TickCount()` (60 Hz monotonic ticks-since-start) and the `Rect` type, selected at build time without `#ifdef` scattered through shared logic.
- **FR-003**: The renderer MUST be split into a backend-agnostic public surface (`renderer.h` calls the screens use) and swappable backends: QuickDraw (existing) and SDL2 (new). No SDL or Toolbox type may leak into shared headers.
- **FR-004**: Input polling MUST be abstracted so the classic build uses `GetKeys()` and the SDL2 build uses SDL held-key state, both presenting the same held-key + edge-accumulator semantics to game code.
- **FR-005**: The main loop / event / timing shell MUST have a POSIX/SDL2 variant selected by build, leaving the classic `WaitNextEvent` loop unchanged.

**SDL2 backend**
- **FR-006**: An SDL2 renderer backend MUST implement the public renderer surface (screen begin/end, gameplay frame, draw player/bomb/explosion, tile/background, text) with output visually matching the classic color-Mac layout.
- **FR-007**: The SDL2 build MUST render all four screens (loading, menu, lobby, game) and their transitions.
- **FR-008**: SDL2 assets MUST be derived from the existing sprite sources (PICT → PNG/raw), with a documented conversion step; the SDL2 renderer MUST fall back to colored rectangles if assets are missing (Constitution VI).
- **FR-009**: The SDL2 target MUST build and run on the Linux dev box for development and testing, and build natively on macOS 26 arm64 with no shared-core changes.

**Cross-era interoperability**
- **FR-010**: All BomberTalk network message payloads with multi-byte fields MUST be serialized in big-endian (Classic-Mac/network) order regardless of host endianness.
- **FR-011**: Little-endian builds (Intel OS X, Apple Silicon, Linux) MUST convert message fields on encode/decode; big-endian builds (68k/PPC) MUST incur no conversion cost and produce byte-identical output to today.
- **FR-012**: Protocol version negotiation and winner-ID/bounds validation MUST behave identically across all eras; no protocol version bump is introduced unless a wire-format change is required (to be confirmed during planning).

**Carbon / OS X native app (gated on spike)**
- **FR-013**: A Carbon viability spike MUST confirm QuickDraw windowing on OS X — specifically 10.7 i386 and at least one PPC version — before the full OS X app is built.
- **FR-014**: The OS X native app MUST reuse the existing QuickDraw renderer (color path) under Carbon Mach-O without a graphics rewrite.
- **FR-015**: The OS X build MUST produce a fat ppc+i386 app (min 10.4) covering G4/G5/Intel-mini and a ppc-only app (min 10.3.9) covering the G3, both linking PeerTalk's POSIX backend.
- **FR-016**: The OS X build MUST be driven from the Linux dev box over rsync+SSH to the 10.7 Intel host (`mini-intel`), adapting PeerTalk's `tools/build-macosx-fat.sh` (extended to produce a windowed app bundle).

**Build system, quality & compatibility**
- **FR-017**: The PeerTalk dependency consumed by the new builds MUST be v1.12.1 (the `~/peertalk` checkout is already v1.12.1); the CMake FetchContent fallback SHOULD be pinnable to a tag for reproducibility.
- **FR-018**: All new shared-core and backend code MUST build warning-clean; only the documented third-party warnings (clog `fsync`, Apple `crt1.o -mlong-branch`) are acceptable, each explicitly justified.
- **FR-019**: The three existing Classic Mac builds MUST remain green and behaviorally unchanged after all seam/refactor work (Constitution II).
- **FR-020**: Shared-core code MUST remain strict C89 (`-std=c89 -Wall -Wextra`, no `//`, no mixed declarations, no VLAs, no `stdint.h`); platform backends MAY use their platform's natural C dialect but MUST NOT leak platform types into shared headers (Constitution IV).
- **FR-021**: No OS X- or SDL-specific hacks may leak into shared/SDK-facing code; platform concessions live in platform backends only.

### Key Entities

- **Platform seam**: A small build-selected boundary providing portable `TickCount`/`Rect`/time and the renderer/input/loop backend selection. Classic → Toolbox backend; OS X → Carbon/QuickDraw backend; modern → SDL2 backend.
- **Renderer backend**: An implementation of the public renderer surface. Three exist after this feature (QuickDraw shared by classic+Carbon; SDL2 for modern).
- **Network message payload**: BomberTalk's own message structs (`MsgPosition`, game-start, bomb-explode, block-destroyed, player-killed, game-over). Wire order is big-endian; little-endian builds convert.
- **OS X fat artifact**: One universal Mach-O `.app` (ppc+i386, min 10.4) plus a ppc-only (min 10.3.9) build.
- **SDL2 asset set**: PNG/raw sprites and tiles derived from the existing PICT sources.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The shared core compiles on Linux with `-std=c89 -Wall -Wextra` with zero errors and zero warnings, and no shared-core file includes a Classic Mac header.
- **SC-002**: A full round of BomberTalk is playable in an SDL2 window on the Linux dev box against a PeerTalk POSIX peer (movement, bombs, explosions, block destruction, death, game-over).
- **SC-003**: Message byte-order unit tests pass on the Linux (little-endian) build: encode→decode round-trips and fixed big-endian byte vectors decode to expected values.
- **SC-004**: All three Classic Mac builds compile clean and produce byte-identical network output to the pre-feature build (SC verified by diffing on-wire bytes for a fixed message set).
- **SC-005**: The Carbon viability spike returns a definitive on-screen result on the Intel mini (10.7) and one PPC Mac; the go/no-go decision for the OS X app is recorded.
- **SC-006**: The OS X fat app runs a round on at least one PPC Mac and the Intel mini; the ppc-only build runs on the G3 (subject to SC-005 being green).
- **SC-007**: The SDL2 sources build natively on macOS 26 arm64 on the M5 with no shared-core edits, producing a runnable app.
- **SC-008**: Cross-era play is demonstrated: a little-endian macOS/OS X/Linux client and a big-endian Classic Mac (e.g. Mac SE over MacTCP) play the same game.
- **SC-009**: New code builds warning-clean except the two documented third-party warnings, each justified in-tree.

## Assumptions

- The `~/peertalk` v1.12.1 checkout, its `tools/build-macosx-fat.sh`, `osx-screen-run.sh`, and the SSH config for the four OS X Macs are present and working as described in `~/peertalk/tools/build-macosx-fat.md`.
- QuickDraw + Carbon windowing on OS X 10.3–10.6 PPC is very likely available; the 10.7 Intel case is the primary risk and is resolved by the US4 spike before large investment.
- SDL2 on Linux is a faithful development proxy for SDL2 on macOS 26 arm64 (same API, both little-endian); only native packaging (bundle, signing/notarization, Retina) is M5-specific.
- PeerTalk's own protocol framing already handles its wire byte order; only BomberTalk's application message payloads need the conversion layer.
- The M5 MacBook Air (macOS 26, arm64) is available to pick up the native build/packaging once the SDL2 core is pushed; it is not in the current SSH fleet table.
- No persistent storage is added; all state remains in memory.

## Dependencies

- PeerTalk SDK **v1.12.1** (POSIX/BSD backend; hardware-validated on the four OS X Macs per `~/peertalk/tools/build-macosx-fat.md`).
- clog v1.4.1 (POSIX sink behavior; documented `fsync` third-party warning).
- Build host `mini-intel` (OS X 10.7, gcc-4.0, `/Developer/SDKs/MacOSX10.4u.sdk` + `MacOSX10.3.9.sdk`, no CMake/git) reachable over SSH for the OS X builds.
- SDL2 (dev on Linux; native on macOS 26 arm64 for the M5).
- The four OS X Macs (`mini-intel`, `imac-g5`, `quicksilver`, `yosemite`) and at least one Classic Mac powered on for hardware validation (via SSH for OS X, classic-mac-hardware MCP for Classic).
- Constitution principles I (prove PeerTalk), II (do not break the three Classic builds), III (ship screens), IV (C89 shared core), VI (graphics fallback), IX (books/SDK discipline).
