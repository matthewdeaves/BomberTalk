# BomberTalk Development Guidelines

## Constitution (Binding)

These 10 principles govern ALL implementation decisions. Full text: `.specify/memory/constitution.md`

1. **Prove PeerTalk Works** — Every design decision MUST prioritize demonstrating PeerTalk SDK capabilities.
2. **Run on All Three Macs** — 68k MacTCP (Mac SE), PPC MacTCP (6200), PPC OT (6400). No feature ships if it breaks any target.
3. **Ship Screens, Not Just a Loop** — Loading, menu, lobby, gameplay screens required.
4. **C89 Everywhere** — All game code MUST be C89/C90 for Retro68 compatibility.
5. **Mac SE Is the Floor** — 4 MB RAM, 8 MHz 68000 sets the minimum bar.
6. **Simple Graphics, Never Blocking** — Basic PICT resource graphics (AI-generated). Renderer falls back to colored rectangles if PICTs missing. Never block on art.
7. **Fixed Frame Rate, Poll Everything** — WaitNextEvent(sleep=0), GetKeys(), PT_Poll(). No threads.
8. **Network State Is Authoritative** — PT_FAST for positions, PT_RELIABLE for game events.
9. **The Books Are Gospel** — The 6 game programming books in `books/` are authoritative. Consult them before implementing any subsystem. Follow BOMBERMAN_CLONE_PLAN.md which references specific book patterns.
10. **One Codebase, Three Builds** — Single src/ tree, CMake toolchain selection.

## Before Every Change

- [ ] Does this help prove PeerTalk works? (I) — if no, lower priority
- [ ] Does this build on all three targets? (II) — if no, fix it
- [ ] Is this C89-clean? (IV) — no `//` comments, no mixed declarations, no VLAs
- [ ] Will this fit in 1 MB on the Mac SE? (V) — if unsure, check FreeMem()

## Project Structure

```
include/                    # Headers (.h)
  game.h                    # Master: constants, types, resource IDs
  screens.h                 # Screen state machine
  tilemap.h, player.h       # Game entities
  bomb.h                    # Bombs and explosions
  renderer.h                # GWorld double-buffering
  input.h                   # Keyboard polling
  net.h                     # PeerTalk wrapper

src/                        # Implementation (.c)
  main.c                    # Entry point, toolbox init, main loop
  screens.c                 # Screen dispatch
  screen_loading.c          # Loading screen
  screen_menu.c             # Main menu
  screen_lobby.c            # Player discovery and connection
  screen_game.c             # Gameplay
  tilemap.c, player.c       # Game logic
  bomb.c                    # Bomb mechanics
  renderer.c                # Rendering
  input.c                   # Input
  net.c                     # PeerTalk integration

maps/level1.h               # Hardcoded level data
resources/                  # Rez files (MENU, SIZE)
books/                      # Classic Mac game programming references
specs/001-v1-alpha/         # Speckit artifacts (spec, plan, tasks, contracts)
```

## Build Commands

PeerTalk SDK: `$PEERTALK_DIR` (defaults to `~/peertalk`) — must be built first for each target.
clog: `$CLOG_DIR` (defaults to `~/clog`) — must be built first for each target.

```bash
# 68k MacTCP (build-68k/) — for Mac SE (System 6)
mkdir -p build-68k && cd build-68k && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC Open Transport (build-ppc-ot/) — for Performa 6400 (System 7.6.1)
mkdir -p build-ppc-ot && cd build-ppc-ot && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make

# PPC MacTCP (build-ppc-mactcp/) — for Performa 6200 (System 7.5.3)
mkdir -p build-ppc-mactcp && cd build-ppc-mactcp && \
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DPEERTALK_DIR=$PEERTALK_DIR -DCLOG_DIR=$CLOG_DIR && make
```

## Code Style

- C89/C90: no `//` comments, no mixed declarations, no VLAs, no stdint.h
- All memory allocated at init time — no malloc during gameplay
- GetKeys() for input polling — never use keyDown events for movement
- WaitNextEvent(sleep=0) — never yield CPU in game loop
- GWorld double-buffering — draw to offscreen, CopyBits to window
- PeerTalk poll-based — call PT_Poll() every frame

## Known Platform Gotchas

**Mac SE (System 6)**: No Color QuickDraw. NewCWindow may not be available — use NewWindow fallback. GWorlds are 1-bit (monochrome), much smaller memory footprint. System 6 has no Gestalt — use SysEnvirons instead if needed.

**PPC toolchain**: File is `retroppc.toolchain.cmake`, NOT `retro68.toolchain.cmake`. CMAKE_SYSTEM_NAME is `RetroPPC`.

**MaxApplZone/MoreMasters**: MUST be called before ANY Memory Manager calls. First thing in InitToolbox().

**C89 + clog**: clog uses variadic macros (C99). Do NOT use `-pedantic`. Use `-Wall -Wextra` only.

**OT linker**: PPC builds link `OpenTransportAppPPC` + `OpenTransportLib` + `OpenTptInternetLib`. This is handled by PeerTalk's static library — BomberTalk just links libpeertalk.a.

**Big-endian**: Both 68k and PPC are big-endian. Network message structs need no byte swapping on Classic Mac. Will need conversion if POSIX build is added later.

## Spec Artifacts

All design docs live in `specs/001-v1-alpha/`:
- `spec.md` — 5 user stories with acceptance scenarios
- `plan.md` — technical context, constitution check, project structure
- `tasks.md` — 55 tasks across 8 phases
- `research.md` — platform specs, memory budget, book references
- `data-model.md` — game entities and 7 network message formats (no host, pure peer-to-peer)
- `contracts/network-protocol.md` — PeerTalk integration, message flow, IP-sort player ID assignment
- `contracts/asset-pipeline.md` — PICT resource formats, loading pattern, tile/sprite sheet layout
- `checklists/build-verification.md` — 46-item verification checklist
- `quickstart.md` — build and run instructions

## Reference Books

Six classic Mac game programming books in `books/`:
- Black Art of Macintosh Game Programming (1996) — primary reference
- Tricks of the Mac Game Programming Gurus (1995) — tile games, GWorld tricks
- Mac Game Programming (2002) — keyboard polling, collision
- Macintosh Game Programming Techniques (1996) — menus, GWorlds
- Sex, Lies, and Video Games (1996) — buffered animation
- Macintosh Game Animation (1985) — historical reference

<!-- MANUAL ADDITIONS START -->
<!-- MANUAL ADDITIONS END -->

## Active Technologies
- C89/C90 + PeerTalk SDK, clog, Mac Toolbox (Color QuickDraw, GWorld), Retro68/RetroPPC (001-v1-alpha)

## Recent Changes
- 001-v1-alpha: Initial project setup with constitution, spec, plan, and task breakdown
