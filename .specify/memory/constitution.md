# BomberTalk Constitution

## Project Overview

### What BomberTalk Is

A networked Bomberman clone for Classic Macintosh, built as the
flagship demo application proving the PeerTalk SDK works for
real-time multiplayer gaming across 68k and PowerPC hardware.

### What It's For

BomberTalk exists to prove one thing: the PeerTalk SDK enables
real-time multiplayer games on Classic Macintosh hardware spanning
three decades of Apple networking stacks.

**Target hardware for v1.0-alpha:**

| Machine | CPU | System | Networking | RAM |
|---------|-----|--------|------------|-----|
| Mac SE | 68000 8MHz | System 6 | MacTCP | 4 MB |
| Performa 6200 | PPC 603 75MHz | System 7.5.3 | MacTCP | 40 MB |
| Performa 6400 | PPC 603e 200MHz | System 7.6.1 | Open Transport | 48 MB |

All three machines must play the same game together over Ethernet.

### What the Player Sees

A Bomberman-style grid arena where 2-4 players move, place bombs,
and try to be the last one standing. The game discovers other
players on the LAN automatically, connects them, and synchronizes
game state in real-time using PeerTalk.

## Core Principles

### I. Prove PeerTalk Works

Every design decision MUST prioritize demonstrating PeerTalk SDK
capabilities. The game is a vehicle for the SDK, not the other
way around. If a feature doesn't exercise or showcase PeerTalk
networking, it is lower priority than one that does.

### II. Run on All Three Macs

The game MUST build and run on all three target machines: 68k
MacTCP, PPC MacTCP, and PPC Open Transport. No feature ships
if it breaks any target. The Mac SE (4 MB, 8 MHz) is the
constraining platform — if it works there, it works everywhere.

### III. Ship Screens, Not Just a Loop

v1.0-alpha MUST have a loading screen, main menu, lobby/player
discovery, and gameplay. The game must feel like a real product,
not a tech demo. Polish comes later, but structure comes first.

### IV. C89 Everywhere

All game code MUST be C89/C90 for Classic Mac compatibility via
Retro68/RetroPPC cross-compilation. No C99, no C11, no C++.
Same constraint as PeerTalk itself.

### V. Mac SE Is the Floor

The Mac SE with 4 MB RAM and 8 MHz 68000 sets the minimum bar.
All memory budgets, frame rates, and feature decisions must
account for this machine. GWorld buffers, tile maps, network
buffers — everything must fit in approximately 1 MB of
application heap alongside PeerTalk's allocation.

### VI. Simple Graphics, Never Blocking

v1.0-alpha uses basic PICT resource graphics: tile sheets,
player sprites, bomb, explosion, and title graphic. AI-generated
artwork scaled to exact pixel sizes and converted to PICT format.
The renderer MUST fall back to colored rectangles if PICT
resources fail to load — graphics never block gameplay. Art
assets can be improved incrementally without changing game logic.

### VII. Fixed Frame Rate, Poll Everything

Game loop runs at a fixed tick rate using WaitNextEvent with
sleep=0. Input via GetKeys() polling. Network via PT_Poll().
No threads, no completion routines, no interrupt-time game
logic. Same poll-based architecture as PeerTalk itself.

### VIII. Network State Is Authoritative

Player positions and game events are synchronized via PeerTalk
messages. Use PT_FAST (UDP) for position updates (Bomberman
pattern from PeerTalk spec). Use PT_RELIABLE (TCP) for game
events (bomb placed, block destroyed, player killed). The
network protocol must handle disconnects without crashing
remaining clients. Late joining is out of scope for v1.0-alpha.

### IX. The Books Are Gospel

The six classic Mac game programming books in `books/` are the
authoritative reference for how to write game code. Before
implementing any subsystem, consult the relevant book sections.
BOMBERMAN_CLONE_PLAN.md extracts specific patterns and page
references from these books — follow them. When in doubt about
Toolbox init, GWorld rendering, input handling, animation, or
memory management, the books have the tested answer. Modern
assumptions do not apply to Classic Mac. Deviate from book
patterns only when real hardware testing reveals problems.

### X. One Codebase, Three Builds

A single source tree produces three binaries via CMake toolchain
files: 68k (retro68.toolchain.cmake), PPC OT
(retroppc.toolchain.cmake with -DPT_PLATFORM=OT), and PPC
MacTCP (retroppc.toolchain.cmake with -DPT_PLATFORM=MACTCP).
Platform differences are handled by PeerTalk, not by the game.

## Scope & Deliverables

### v1.0-alpha Delivers

- Loading screen with BomberTalk title
- Main menu (Play, Quit)
- Lobby screen showing discovered LAN players via PeerTalk
  (pure peer-to-peer — no host, any player can start)
- Gameplay: 15x13 grid, player movement, bomb placement,
  explosions, block destruction
- 2-4 player multiplayer over LAN
- Colored rectangle graphics (placeholder)
- Builds for 68k MacTCP, PPC MacTCP, PPC Open Transport

### v1.0-alpha Does NOT Deliver

- Sprite-based graphics or animations
- Sound effects or music
- Power-ups (speed, extra bombs, fire range)
- AI opponents
- POSIX/modern Mac build
- Score tracking, win/loss statistics, or results UI
  (basic last-player-standing detection IS in scope — needed
  to trigger lobby transition per FR-016)

### Future Versions

- v1.1: Real sprite graphics, win/lose/respawn
- v1.2: Power-ups, sound effects
- v1.3: POSIX build for Linux/modern macOS
- v2.0: AI opponents, multiple levels

## Definition of Done

BomberTalk v1.0-alpha is done when:

- Three Macs on the same LAN can discover each other, connect,
  and play a game of Bomberman together
- The Mac SE runs at a playable frame rate (10+ fps minimum)
- The game has loading, menu, lobby, and gameplay screens
- A player can move, place bombs, destroy blocks, and
  eliminate other players
- Disconnecting a player is handled gracefully (removed from game)

## Governance

This constitution is the highest-authority document for
BomberTalk development decisions. All implementation work MUST
comply with these principles.

- **Amendments** require documenting the change rationale,
  updating this file, and propagating changes to dependent
  templates (plan, spec, tasks).
- **Versioning** follows semantic versioning:
  - MAJOR: Principle removals or backward-incompatible
    redefinitions
  - MINOR: New principles added or existing guidance
    materially expanded
  - PATCH: Clarifications, wording fixes, non-semantic
    refinements
- **Compliance review**: Every feature plan MUST include a
  Constitution Check gate verifying alignment with these
  principles before implementation begins.
- **Scope disputes**: When uncertain whether a feature belongs,
  apply Principle I — does it help prove PeerTalk works?

**Version**: 1.0.0 | **Ratified**: 2026-04-05 | **Last Amended**: 2026-04-05
