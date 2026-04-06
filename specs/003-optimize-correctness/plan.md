# Implementation Plan: Performance & Correctness Optimizations

**Branch**: `003-optimize-correctness` | **Date**: 2026-04-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/003-optimize-correctness/spec.md`

## Summary

Five targeted optimizations and correctness fixes identified from a full code review cross-referenced with the Classic Mac game programming books. The highest-impact change (ForeColor/BackColor normalization before CopyBits) is a 2-line fix per call site that could yield up to 2.5x faster blitting on color Macs. The remaining changes batch background rebuilds, eliminate redundant resource loads, add O(1) bomb collision lookup, and clean up stale pointers.

## Technical Context

**Language/Version**: C89/C90 (Retro68 cross-compiler)
**Primary Dependencies**: PeerTalk SDK, clog, Retro68/RetroPPC toolchains, Classic Mac Toolbox (QuickDraw, Resource Manager)
**Storage**: Mac resource fork ('TMAP' resource type 128), static level data fallback
**Testing**: Manual hardware testing on 3 target Macs + in-game FPS counter + clog UDP logging
**Target Platform**: Mac SE (68000, System 6), Performa 6200 (PPC, System 7.5.3), Performa 6400 (PPC, System 7.6.1)
**Project Type**: Desktop game (Classic Macintosh)
**Performance Goals**: 15+ fps on Mac SE, 24+ fps on PPC Macs. No regression from current build.
**Constraints**: 4 MB RAM on Mac SE (~1 MB application heap), C89 strict, no runtime malloc, all three targets must build clean
**Scale/Scope**: ~3,500 LOC across 15 source files. 5 changes touching 6 files.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Prove PeerTalk Works | PASS | Deferred rebuild fix (P2) improves network explosion rendering. Other changes maintain PeerTalk integration. |
| II. Run on All Three Macs | PASS | All changes target all three platforms. ForeColor/BackColor fix specifically helps color Macs while maintaining Mac SE behavior. |
| III. Ship Screens, Not Just a Loop | PASS | No screen changes. Existing screen structure preserved. |
| IV. C89 Everywhere | PASS | All changes are C89-compliant. No new language features needed. |
| V. Mac SE Is the Floor | PASS | Spatial bomb grid adds 775 bytes (well within budget). Tilemap cache adds ~800 bytes. Total ~1.6 KB new static memory. |
| VI. Simple Graphics, Never Blocking | PASS | No graphics changes. Renderer pipeline preserved. |
| VII. Fixed Frame Rate, Poll Everything | PASS | No loop changes. Deferred rebuild integrates with existing BeginFrame. |
| VIII. Network State Is Authoritative | PASS | Network message handling preserved. Block destroy batching is a rendering optimization only. |
| IX. The Books Are Gospel | PASS | ForeColor/BackColor fix directly from "Sex, Lies and Video Games" (1996) CopyBits benchmarks. |
| X. One Codebase, Three Builds | PASS | Single source tree, no platform ifdefs added. |

## Project Structure

### Documentation (this feature)

```text
specs/003-optimize-correctness/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── contracts/           # Phase 1 output (renderer internal API)
├── checklists/          # Spec quality checklist
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── renderer.c           # ForeColor/BackColor fix, deferred rebuild flag
├── bomb.c               # Spatial bomb grid, deferred rebuild calls
├── tilemap.c            # Tilemap cache + Reset function
├── net.c                # Deferred rebuild call, peer pointer cleanup
├── screen_game.c        # TileMap_Reset instead of TileMap_Init
└── main.c               # No changes expected

include/
├── renderer.h           # New: Renderer_RequestRebuildBackground()
├── tilemap.h            # New: TileMap_Reset()
└── bomb.h               # No API changes (Bomb_ExistsAt stays, internals change)
```

**Structure Decision**: Existing single-project C structure. All changes are modifications to existing files. No new files created.

## Complexity Tracking

No constitution violations. No complexity justifications needed.
