# Implementation Plan: Renderer Performance Optimizations

**Branch**: `006-renderer-optimization` | **Date**: 2026-04-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/006-renderer-optimization/spec.md`

## Summary

Reduce per-frame Toolbox trap overhead in the BomberTalk renderer by hoisting port save/restore out of sprite draw loops, eliminating redundant BeginFrame color state setup, replacing `transparent` CopyBits with srcCopy+maskRgn on color Macs, batching tile drawing by type in RebuildBackground, and adding an inline tilemap access macro for hot-path loops. Combined effect: ~35 fewer trap calls per gameplay frame on Mac SE, 2-3x faster sprite blits on color Macs, reduced rebuild hitch.

## Technical Context

**Language/Version**: C89/C90 (Retro68/RetroPPC cross-compiler)
**Primary Dependencies**: PeerTalk SDK v1.11.2, clog v1.4.1, Classic Mac Toolbox (QuickDraw, QDOffscreen, Resource Manager)
**Storage**: N/A (no persistent storage; all state in memory)
**Testing**: Manual hardware testing on Mac SE, Performa 6200, Performa 6400. Visual comparison + FPS observation.
**Target Platform**: Classic Mac OS (System 6 68k, System 7.5.3 PPC MacTCP, System 7.6.1 PPC OT)
**Project Type**: Desktop game (Classic Mac application)
**Performance Goals**: Measurable FPS improvement on Mac SE (currently 10-19fps gameplay); faster sprite blits on PPC (currently ~24fps)
**Constraints**: 4MB RAM / 8MHz 68000 floor (Mac SE); C89 only; pixel-identical rendering output
**Scale/Scope**: Single source tree, 6 files modified (renderer.c, renderer.h, screen_game.c, tilemap.h, bomb.c, player.c)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Prove PeerTalk Works | PASS | Performance improvements benefit PeerTalk demo quality |
| II. Run on All Three Macs | PASS | All changes build on 68k, PPC MacTCP, PPC OT; Mac SE fallback paths preserved |
| III. Ship Screens, Not Just a Loop | PASS | No screen changes; existing screen flow unaffected |
| IV. C89 Everywhere | PASS | All code C89/C90; macro is preprocessor (C89 compatible) |
| V. Mac SE Is the Floor | PASS | Primary beneficiary; mask regions only on color Macs |
| VI. Simple Graphics, Never Blocking | PASS | Fallback rectangles preserved; mask region failure falls back to transparent mode |
| VII. Fixed Frame Rate, Poll Everything | PASS | No changes to main loop structure or polling |
| VIII. Network State Is Authoritative | PASS | No network changes |
| IX. The Books Are Gospel | PASS | All optimizations derived from book research (Sex Lies, Black Art, Tricks of Gurus) |
| X. One Codebase, Three Builds | PASS | Single src/ tree; #ifdef paths for Mac SE vs color already exist |

No violations. All gates pass.

## Project Structure

### Documentation (this feature)

```text
specs/006-renderer-optimization/
├── plan.md              # This file
├── research.md          # Phase 0: technical research findings
├── data-model.md        # Phase 1: data structures (mask regions, macro)
├── contracts/
│   └── renderer-api.md  # Phase 1: changed renderer API surface
└── tasks.md             # Phase 2: implementation tasks (via /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── renderer.c           # Optimizations 1-4: port hoisting, color state, mask regions, tile batching
├── screen_game.c        # Optimization 1: Game_Draw() port save/restore wrapper
├── bomb.c               # Optimization 5: use TILEMAP_TILE macro in raycast
└── player.c             # Optimization 5: use TILEMAP_TILE macro in collision (via CheckTileSolid)

include/
├── renderer.h           # New API: Renderer_BeginSpriteDraw/EndSpriteDraw
└── tilemap.h            # New: TILEMAP_TILE(map, col, row) macro
```

**Structure Decision**: No new files or directories. All changes are modifications to existing source files within the established `src/` and `include/` layout.
