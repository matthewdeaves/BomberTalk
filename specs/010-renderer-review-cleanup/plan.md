# Implementation Plan: Renderer Review Cleanup

**Branch**: `010-renderer-review-cleanup` | **Date**: 2026-04-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/010-renderer-review-cleanup/spec.md`

## Summary

Book-grounded code review fixes and cppcheck static analysis cleanup for the 009-bomb-animation branch. Ten targeted changes across renderer memory management, Mac SE per-frame trap reduction, mask-building correctness, and static analysis warnings. All changes are local optimizations with no protocol or API impact.

## Technical Context

**Language/Version**: C89/C90 (Retro68/RetroPPC cross-compiler)  
**Primary Dependencies**: Classic Mac Toolbox (QuickDraw, QDOffscreen, Resource Manager, Memory Manager), PeerTalk SDK v1.11.2, clog v1.4.1  
**Storage**: N/A (all state in memory; resources from resource fork)  
**Testing**: Manual testing via QEMU (Quadra 800) and real Mac SE hardware; cppcheck static analysis  
**Target Platform**: Classic Mac: 68k MacTCP (Mac SE), PPC MacTCP (6200), PPC OT (6400)  
**Project Type**: Desktop game (Classic Mac application)  
**Performance Goals**: Mac SE 10+ fps gameplay with 3 active bombs  
**Constraints**: 4 MB RAM on Mac SE (~1 MB application heap); C89 only; no VLAs  
**Scale/Scope**: Single renderer source file (~1500 lines), one bomb source file, one header

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Prove PeerTalk Works | PASS | Frees 178 KB heap for PeerTalk TCP buffers |
| II. Run on All Three Macs | PASS | All fixes verified across 3 build targets |
| III. Ship Screens, Not Just a Loop | PASS | No screen changes; loading screen preserved |
| IV. C89 Everywhere | PASS | Stack arrays use fixed sizes, not VLAs; all C89 |
| V. Mac SE Is the Floor | PASS | Primary beneficiary: SE heap + SE trap reduction |
| VI. Simple Graphics, Never Blocking | PASS | Fallback paths preserved; no new blocking |
| VII. Fixed Frame Rate, Poll Everything | PASS | No timing changes |
| VIII. Network State Is Authoritative | PASS | No network changes |
| IX. The Books Are Gospel | PASS | All fixes grounded in book references |
| X. One Codebase, Three Builds | PASS | Single src/ tree, CMake selection unchanged |

No violations. No complexity tracking needed.

## Project Structure

### Documentation (this feature)

```text
specs/010-renderer-review-cleanup/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0: book references and decisions
├── data-model.md        # Phase 1: affected data structures
└── tasks.md             # Phase 2: implementation tasks
```

### Source Code (affected files)

```text
src/
├── renderer.c           # FR-001 through FR-009 (primary)
├── bomb.c               # FR-010 (ownerIdx condition)
include/
├── renderer.h           # FR-009 (remove BlitToWindow declaration if present)
```

**Structure Decision**: No new files. All changes are modifications to existing source files in the established single-project layout.

## Complexity Tracking

No constitution violations to justify.
