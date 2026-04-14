# Implementation Plan: Wall-Clock Timers & Log Cleanup

**Branch**: `007-timer-fixes` | **Date**: 2026-04-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/007-timer-fixes/spec.md`

## Summary

Convert four network/coordination timers from `deltaTicks` decrement to `TickCount()` wall-clock comparison, fixing frame-rate-dependent drift caused by the deltaTicks cap at 10 (main.c:241). On the Mac SE at ~1-3fps, these timers currently undercount real time by 2-20x. The fix extends the existing lobby timer pattern (screen_lobby.c:120, 128) to four more timers. Also downgrades one noisy log message and documents another as expected PeerTalk behavior.

## Technical Context

**Language/Version**: C89/C90 (Retro68/RetroPPC cross-compiler)
**Primary Dependencies**: PeerTalk SDK v1.11.2, clog v1.4.1, Classic Mac Toolbox (TickCount)
**Storage**: N/A (no persistent storage; all state in memory)
**Testing**: Manual hardware testing on Mac SE, Performa 6200, Performa 6400. Log timestamp analysis for timer accuracy.
**Target Platform**: Classic Mac OS (System 6 68k, System 7.5.3 PPC MacTCP, System 7.6.1 PPC OT)
**Project Type**: Desktop game (Classic Mac application)
**Performance Goals**: Timer accuracy within ±1 frame duration regardless of fps
**Constraints**: 4MB RAM / 8MHz 68000 floor (Mac SE); C89 only; no gameplay behavior changes
**Scale/Scope**: Single source tree, 5 source files modified (game.h, screen_game.c, screen_lobby.c, main.c, net.c), 1 file log-level change (bomb.c), 1 documentation update (known-issues.md)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Prove PeerTalk Works | PASS | Fixes grace period race that causes spurious disconnect logs during PeerTalk teardown |
| II. Run on All Three Macs | PASS | TickCount() available on all targets since System 1; no platform-specific code |
| III. Ship Screens, Not Just a Loop | PASS | No screen changes; existing screen flow unaffected |
| IV. C89 Everywhere | PASS | unsigned long, TickCount() — pure C89 |
| V. Mac SE Is the Floor | PASS | Primary beneficiary; fixes timer drift caused by SE's low fps |
| VI. Simple Graphics, Never Blocking | PASS | No renderer changes |
| VII. Fixed Frame Rate, Poll Everything | PASS | No changes to main loop structure or polling; TickCount() is non-blocking |
| VIII. Network State Is Authoritative | PASS | No wire protocol changes; timers are local coordination only |
| IX. The Books Are Gospel | PASS | Sex, Lies and Video Games (lines 11787-11823) explicitly recommends wall-clock timing for speed-independent behavior |
| X. One Codebase, Three Builds | PASS | Single src/ tree; no #ifdef needed |

No violations. All gates pass.

## Project Structure

### Documentation (this feature)

```text
specs/007-timer-fixes/
├── plan.md              # This file
├── research.md          # Phase 0: timer pattern analysis
├── data-model.md        # Phase 1: GameState field changes
├── quickstart.md        # Phase 1: quick implementation reference
├── contracts/           # Phase 1: (none — no API surface changes)
└── tasks.md             # Phase 2: implementation tasks (via /speckit.tasks)
```

### Source Code (repository root)

```text
include/
└── game.h               # Timer field type changes (short → unsigned long), field renames

src/
├── main.c               # Timer field reset assignments (game reset)
├── net.c                # Timer start assignments for failsafe and timeout (MSG_GAME_OVER handler)
├── screen_game.c        # Convert 3 timers: disconnectGraceTimer, gameOverFailsafeTimer, gameOverTimeout
├── screen_lobby.c       # Convert 1 timer: meshStaggerTimer; timer field resets (lobby init)
└── bomb.c               # Downgrade CLOG_INFO → CLOG_DEBUG for fuse-expiry log

notes/
└── known-issues.md      # Document KI-003 as expected PeerTalk behavior
```

**Structure Decision**: No new files or directories. All changes are modifications to existing source files within the established `src/`, `include/`, and `notes/` layout.
