# Implementation Plan: Smooth Sub-Tile Player Movement

**Branch**: `004-smooth-movement` | **Date**: 2026-04-10 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/004-smooth-movement/spec.md`

## Summary

Replace grid-locked tile-snap movement with true sub-tile smooth pixel movement, matching classic Bomberman feel. Players move continuously at pixel granularity using tick-based speed, collision uses AABB checks against tilemap and explosions, bombs support walk-off after placement, and network protocol is updated to transmit pixel coordinates. Additionally fixes a disconnect bug where ghost sprites remain on screen.

## Technical Context

**Language/Version**: C89/C90 (Retro68 cross-compiler)
**Primary Dependencies**: PeerTalk SDK (commit 7e89304), clog (commit e8d5da9), Retro68/RetroPPC toolchains, Classic Mac Toolbox (QuickDraw)
**Storage**: Mac resource fork ('TMAP' resource type 128), static level data fallback
**Testing**: Manual hardware testing on 3 target Macs + in-game FPS counter + clog UDP logging
**Target Platform**: Mac SE (68000, System 6), Performa 6200 (PPC, System 7.5.3), Performa 6400 (PPC, System 7.6.1)
**Project Type**: Desktop game (Classic Macintosh)
**Performance Goals**: Maintain current fps: Mac SE ~10fps, 6200 ~24fps, 6400 ~26fps. No regression.
**Constraints**: 4 MB RAM on Mac SE (~1 MB application heap), C89 strict, no runtime malloc, all three targets must build clean
**Scale/Scope**: ~3,500 LOC across 15 source files. Changes touch 6-7 files.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Prove PeerTalk Works | PASS | Protocol v3 with pixel coords exercises PeerTalk's PT_FAST path with larger payloads. Disconnect cleanup directly proves robust networking. |
| II. Run on All Three Macs | PASS | Resolution-independent speed model (ticks-per-tile) works across 16px and 32px tile sizes. All collision math uses tileSize variable. |
| III. Ship Screens, Not Just a Loop | PASS | No screen structure changes. Gameplay screen enhanced. |
| IV. C89 Everywhere | PASS | All new code is C89-compliant. No new language features. Integer-only math (no floating point). |
| V. Mac SE Is the Floor | PASS | New static memory: ~16 bytes per player for interpolation state + 1 byte per player for bomb pass-through. AABB collision adds ~10 integer comparisons per frame per axis — negligible vs CopyBits cost. |
| VI. Simple Graphics, Never Blocking | PASS | Renderer_DrawPlayer gains pixel-position rect instead of grid-position rect. No new graphics resources. |
| VII. Fixed Frame Rate, Poll Everything | PASS | Movement integrated into existing frame loop. Speed uses deltaTicks multiplication. |
| VIII. Network State Is Authoritative | PASS | Pixel positions sent via PT_FAST. Remote machines interpolate toward received positions. Local collision is authoritative for local player only. |
| IX. The Books Are Gospel | PASS | AABB collision approach from Mac Game Programming (2002) Ch. 9 (tile-based collision). Dirty rectangle expansion from Tricks of the Mac Game Programming Gurus (1995). |
| X. One Codebase, Three Builds | PASS | Single source tree. tileSize-relative hitbox inset adapts automatically. |

## Project Structure

### Documentation (this feature)

```text
specs/004-smooth-movement/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── contracts/           # Phase 1 output (network protocol, movement API)
│   ├── network-protocol.md
│   └── movement-api.md
├── checklists/          # Spec quality checklist
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── player.c             # Core rewrite: pixel movement, AABB collision, corner sliding
├── bomb.c               # AABB explosion kill check, bomb walk-off pass-through
├── net.c                # MsgPosition v3 (pixel coords), disconnect dirty rect fix, interpolation
├── screen_game.c        # Multi-tile dirty rect marking, pixel-position drawing calls
├── renderer.c           # Renderer_DrawPlayer takes pixel coords, Renderer_MarkDirtyRect helper

include/
├── game.h               # Player struct changes, MsgPosition v3, protocol version bump
├── player.h             # New function signatures
├── bomb.h               # Updated Bomb_ExistsAt for AABB, pass-through API
```

**Structure Decision**: Existing single-project structure. No new files needed — all changes modify existing source files and headers.

## Book Verification (Principle IX)

Verified implementation approach against the six reference books. Key findings:

| Topic | Book | Finding | Plan Impact |
|-------|------|---------|-------------|
| CopyBits alignment | Sex, Lies & Video Games (1996) pp.143-148 | Long-word aligned CopyBits is ~2x faster. Sub-tile sprite positions will be misaligned. | Accepted: Mac SE uses PaintRect (no penalty), PPC uses transparent mode (already slower). Monitor in T036. See research.md R9. |
| AABB tile collision | Mac Game Programming (2002) Ch. 9 | Tile lookup via `playerX / kTileWidth` — standard approach matches our axis-separated collision. | Confirmed: T008 approach matches book pattern. |
| Collision tunneling | Macintosh Game Animation (1985) | Fast sprites can jump over targets. Recommend figures >= 2x combined speeds. | Covered: T008 intermediate tile sweep handles this. Walls are 16-32px wide, max movement ~20px/frame. |
| Dirty rect animation | Mac Game Programming (2002), Sex Lies (1996) | Standard 4-step: move, erase old, draw new, blit to screen. Grid-based dirty flags optimal for largely-static screens. | Confirmed: existing dirty rect system matches book pattern. T011 extends to multi-tile marking. |
| Sprite movement timing | Sex, Lies & Video Games (1996) | Millisecond delay or per-frame pixel advance. | Confirmed: our tick-based fractional accumulator is the Classic Mac equivalent of the book's millisecond delay model. |
| Offscreen buffering | All books | Background + work buffer dual-buffer pattern. Draw to work, blit to screen. | Confirmed: existing renderer matches exactly. No changes needed to buffer architecture. |

## Complexity Tracking

> No constitution violations to justify.
