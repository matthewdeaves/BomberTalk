# Implementation Plan: Network Authority & Robustness Improvements

**Branch**: `005-network-authority` | **Date**: 2026-04-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/005-network-authority/spec.md`

## Summary

Transition BomberTalk from broadcast-all to owner-authoritative messaging for bomb explosions, block destruction, and game-over events. Add graceful shutdown coordination (30-tick grace period), mesh formation timing (rank-based stagger), heap monitoring, and document PeerTalk drain error log levels. Reduces TCP reliable message volume by ~67% with zero wire format changes. Protocol version bumped from 4 to 5.

## Technical Context

**Language/Version**: C89/C90 (Retro68/RetroPPC cross-compiler)
**Primary Dependencies**: PeerTalk SDK v1.11.0, clog v1.3.0, Classic Mac Toolbox
**Storage**: N/A (all state in memory)
**Testing**: Manual hardware testing across 3 Classic Macs + debug log analysis via `socat`
**Target Platform**: Mac SE (68k System 6), Performa 6200 (PPC System 7.5.3), Performa 6400 (PPC System 7.6.1)
**Project Type**: Desktop game (Classic Macintosh)
**Performance Goals**: No FPS regression on any platform. Mac SE: 10+ fps gameplay, 3+ fps lobby.
**Constraints**: 4MB RAM (Mac SE), C89 only, no threads, poll-based architecture
**Scale/Scope**: 2-4 players over LAN, single source tree, three build targets

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Prove PeerTalk Works | PASS | Directly improves PeerTalk usage efficiency — fewer redundant messages, cleaner connection lifecycle |
| II. Run on All Three Macs | PASS | All changes are platform-agnostic C89. Mesh stagger and grace period specifically help the Mac SE. |
| III. Ship Screens, Not Just a Loop | PASS | No screen changes. Lobby mesh timing improved, game-over transition smoother. |
| IV. C89 Everywhere | PASS | No C99 features needed. All new code uses C89 patterns. |
| V. Mac SE Is the Floor | PASS | Grace period (30 ticks) and stagger (30 ticks/rank) are well within Mac SE frame budget. Heap monitoring adds one FreeMem() call per 30 seconds. No new allocations. |
| VI. Simple Graphics, Never Blocking | PASS | No graphics changes. |
| VII. Fixed Frame Rate, Poll Everything | PASS | Grace period and stagger use tick-based timers decremented in the existing frame loop. No new polling or blocking. |
| VIII. Network State Is Authoritative | PASS | Strengthens this principle: authority is now explicit (bomb owner, lowest rank) instead of implicit (all broadcast, first processed wins). |
| IX. The Books Are Gospel | PASS | Marathon's "each machine tracks all state" model (Tricks of the Mac Game Programming Gurus, Ch. 10) is the direct inspiration for owner-authoritative messaging. |
| X. One Codebase, Three Builds | PASS | Single source tree. No platform-specific code paths added. |

**Post-design re-check**: All gates still PASS. No new dependencies, no new allocations, no platform-specific code.

## Project Structure

### Documentation (this feature)

```text
specs/005-network-authority/
├── plan.md              # This file
├── research.md          # Phase 0: 9 research decisions
├── data-model.md        # Phase 1: GameState changes, state transitions
├── quickstart.md        # Phase 1: Implementation guide
├── contracts/
│   └── network-protocol.md  # Phase 1: v5 protocol behavioral changes
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 output (via /speckit.tasks)
```

### Source Code (repository root)

```text
include/
├── game.h               # Protocol version, new constants, GameState fields
└── net.h                # Net_IsLowestRankConnected() declaration

src/
├── bomb.c               # Owner-authoritative ExplodeBomb broadcast
├── screen_game.c        # Authority-based game over, grace period
├── screen_lobby.c       # Mesh stagger delay
├── net.c                # Net_IsLowestRankConnected() implementation
└── main.c               # Heap monitoring, new field initialization
```

**Structure Decision**: Existing single-project layout. No new files. All changes are modifications to existing source files.

## Complexity Tracking

No constitution violations. No complexity justifications needed.
