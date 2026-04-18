# Implementation Plan: Hot-Path Performance & Memory Optimizations (008)

**Branch**: `008-perf-hotpath` | **Date**: 2026-04-18 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/008-perf-hotpath/spec.md`

## Summary

Seven book-verified and cppcheck-verified local optimizations to the game hot paths, with two optional extensions. No network protocol, public API, or game-logic behaviour changes. All changes are local-only and target the Mac SE frame budget (Principle V) and color-Mac debug-noise budget. Each FR is independently mergeable so a hardware regression on one item does not block the rest.

Primary technical approach:

- **Cache-hoist + inline (FR-001, FR-002)** — move `TileMap_GetCols`/`GetRows` reads out of collision inner loops; inline `Bomb_ExistsAt` to a direct `gBombGrid[row][col]` read inside `CheckTileSolid` (caller's bounds clamp is the precondition). Replace in-loop `SetRect` trap calls with direct four-field assignments inside `ExplodeBomb` and `Bomb_Update` per-frame AABB loops and the player `CollideAxis` / `TryCornerSlide` paths.
- **Batch colour state (FR-003)** — refactor `Renderer_DrawPlayer`'s fallback path to mirror the pass-based structure already used in `Renderer_RebuildBackground` (one colour set per tile-type pass). The PICT path stays untouched.
- **Edge-trigger debug logs (FR-004)** — the simplest behavioural change that keeps diagnostics alive: downgrade per-frame `CLOG_DEBUG` in `Player_Update`, `Player_SetPosition`, and any similar per-packet sites to fire only on meaningful state change (direction change, target ≥ 1 tile from last logged target). No new runtime flag introduced.
- **Dead-code removal (FR-005, FR-006)** — delete `TileMap_IsSolid` from source + header; delete the unreachable `if (gGame.isMacSE)` branch in `LoadPICTResources` with a comment pointer to the SE resource IDs still in `game.h`.
- **`FreeMem` → `PurgeSpace` (FR-007)** — two-line call-site change in `main.c`.
- **Optional** — packed dirty-list coords (FR-008) and title-sprite disposal on menu → lobby transition (FR-009, gated on explicit user sign-off).

## Technical Context

**Language/Version**: C89 / C90 only. Retro68 (m68k-apple-macos) and RetroPPC (powerpc-apple-macos) cross-compilers. `-std=c89 -Wall -Wextra`. No `//` comments, no mixed declarations, no VLAs, no `stdint.h`, no C99 features. clog uses variadic macros (C99) — do NOT add `-pedantic`.
**Primary Dependencies**: Classic Mac Toolbox (QuickDraw, QDOffscreen, Memory Manager, Resource Manager); PeerTalk SDK v1.11.2; clog v1.4.1.
**Storage**: N/A. All game state is in-memory; tilemap comes from `kLevel1` static data or `'TMAP'` resource 128.
**Testing**: Manual on-target verification via `run-68k` (Quadra 800 VM with System 7.6.1) and QEMU / real hardware via classic-mac-hardware-mcp. F-key FPS overlay for Mac SE perf check; `socat -u UDP-RECV:7356,reuseaddr -` for debug-channel capture on color Macs. No automated test harness — Principle VII poll-based architecture + Principle II three-target constraint rules out unit-test scaffolding for this change.
**Target Platform**: Three Classic Mac targets — 68k MacTCP (Mac SE, System 6), PPC MacTCP (Performa 6200, System 7.5.3), PPC Open Transport (Performa 6400, System 7.6.1). All three must build and run after the change (Principle II).
**Project Type**: Cross-compiled native desktop game. Single source tree + CMake toolchain selection (Principle X). Not a library, not a service.
**Performance Goals**: Maintain or improve Mac SE gameplay fps (baseline ~10–19 fps per CLAUDE.md 2026-04-10 measurement). Zero per-frame movement log lines on color Macs. Zero new compiler warnings vs 007 baseline.
**Constraints**: Mac SE approximate 1 MB application heap budget (Principle V). No network protocol changes (spec FR-N2). No public header API additions beyond at most one inline helper (FR-N3). No background threads, no completion routines (Principle VII).
**Scale/Scope**: ~4256 LOC across 12 `.c` files and 8 `.h` files today. Change touches a subset of: `player.c`, `bomb.c`, `renderer.c`, `tilemap.c`, `tilemap.h`, `main.c`. Plus optional renderer.c storage packing (FR-008) and one transition hook in `screen_lobby.c` (FR-009).

No `NEEDS CLARIFICATION` items. The spec, the fact-check, and the cppcheck run all resolved prior to this phase.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked post-design at end of plan.*

| Principle | Check | Verdict |
|---|---|---|
| I. Prove PeerTalk Works | Does this feature help demonstrate PeerTalk? | PASS — indirect. Improving Mac SE fps keeps the three-target demo alive on the oldest hardware, which is *the* differentiating PeerTalk story. No network-layer changes. |
| II. Run on All Three Macs | Builds on 68k MacTCP, PPC MacTCP, PPC OT? | PASS — explicit FR-N1 and SC-003 require clean builds on all three. Each FR is independently mergeable so a target-specific regression blocks only the offending item. |
| III. Ship Screens, Not Just a Loop | Preserves loading / menu / lobby / gameplay screen set? | PASS — no screen added or removed. Optional FR-009 adds a one-shot dispose hook at menu → lobby transition, nothing else. |
| IV. C89 Everywhere | All changes C89-clean? | PASS — FR-N1 makes this explicit. Cppcheck variable-scope suggestions are rejected because they conflict with C89 top-of-block declarations. |
| V. Mac SE Is the Floor | Fits 4 MB / 8 MHz budget? | PASS — this is *the* driving motivation. FR-001 / FR-002 / FR-003 directly target SE frame budget. No memory-growth change. FR-008 shrinks renderer state by ~1.5 KB. FR-009 reclaims ~40 KB on color Macs (SE unaffected — SE never loads the title PICT). |
| VI. Simple Graphics, Never Blocking | Keeps renderer fallback path? | PASS — FR-003 only batches colour state; fallback still falls back. FR-006 removes a dead branch that would never have run anyway. No draw-correctness change. |
| VII. Fixed Frame Rate, Poll Everything | No threads, no CR? | PASS — timing model untouched. FR-004 affects only log volume, not scheduling. |
| VIII. Network State Authoritative | No protocol change? | PASS — FR-N2 forbids it. Change is local-only. |
| IX. The Books Are Gospel | Book refs cited? | PASS — every FR traces to Tricks of the Gurus, Inside Macintosh IV, Sex Lies, or cppcheck. One originally-proposed item (CopyMask) was rejected specifically because *Tricks* p.6239 contradicts it; one (P6 memset) was withdrawn on closer code inspection. |
| X. One Codebase, Three Builds | CMake / toolchain neutral? | PASS — no CMakeLists.txt changes anticipated. Pure source edits in `src/` and `include/`. |

**Constitution gate: PASS (10/10).**

No violations to track under Complexity Tracking.

## Project Structure

### Documentation (this feature)

```text
specs/008-perf-hotpath/
├── plan.md              # This file (/speckit.plan output)
├── spec.md              # /speckit.specify output (already written)
├── research.md          # Phase 0 output — resolves per-FR approach questions
├── data-model.md        # Phase 1 output — N/A content but present for workflow completeness
├── contracts/           # Phase 1 output — internal header diff (TileMap_IsSolid removal)
│   └── tilemap-api.md
├── quickstart.md        # Phase 1 output — build + verify recipe per FR
├── checklists/
│   └── requirements.md  # Written during /speckit.specify
└── tasks.md             # Phase 2 output (/speckit.tasks — NOT created by this plan)
```

### Source Code (repository root — unchanged layout)

```text
BomberTalk/
├── CMakeLists.txt                    # unchanged
├── include/
│   ├── game.h                        # unchanged
│   ├── tilemap.h                     # FR-005: remove TileMap_IsSolid declaration
│   ├── player.h                      # unchanged
│   ├── bomb.h                        # unchanged
│   ├── renderer.h                    # unchanged
│   ├── net.h                         # unchanged
│   ├── input.h                       # unchanged
│   └── screens.h                     # unchanged
├── src/
│   ├── main.c                        # FR-007: FreeMem → PurgeSpace (2 sites)
│   ├── tilemap.c                     # FR-005: remove TileMap_IsSolid body
│   ├── player.c                      # FR-001: cache map bounds in CollideAxis; inline bomb grid read; FR-002: inline SetRect in hot AABB paths; FR-004: edge-trigger movement log
│   ├── bomb.c                        # FR-001: cache map bounds in ExplodeBomb; FR-002: inline SetRect in per-frame kill-check loops (bomb.c:164-167 and 247-250)
│   ├── renderer.c                    # FR-003: batch ForeColor in fallback Renderer_DrawPlayer; FR-006: delete dead SE branch in LoadPICTResources; FR-008 (optional): pack dirty list
│   ├── screen_lobby.c                # FR-009 (optional, gated): one-shot title-sprite dispose on menu-to-lobby transition
│   ├── screen_game.c                 # unchanged
│   ├── screen_menu.c                 # unchanged
│   ├── screen_loading.c              # unchanged
│   ├── screens.c                     # unchanged
│   ├── net.c                         # FR-004: edge-trigger Player_SetPosition log if touched
│   └── input.c                       # unchanged
├── build-68k/                        # regenerated by CMake
├── build-ppc-mactcp/                 # regenerated by CMake
├── build-ppc-ot/                     # regenerated by CMake
├── books/                            # reference only
└── specs/008-perf-hotpath/           # this feature
```

**Structure Decision**: Single-project C89 cross-compiled layout (Constitution Principle X). No new directories, no new translation units. Each FR lands in one or two existing files. Optional FRs (008, 009) each touch one additional file. Feature touches ~6 of 12 `.c` files; 1 of 8 `.h` files.

## Phase 0: Research

Research notes live in `research.md`. Key topics resolved:

- **R1** — For FR-001, does removing `Bomb_ExistsAt` bounds check create any risk? **Decision**: No — `CollideAxis` already clamps `minCol/maxCol/minRow/maxRow` to `[0, cols-1] × [0, rows-1]` before entering the loop (player.c:201-205). Direct `gBombGrid[row][col]` is safe there. Bomb module still owns the grid; we add a `Bomb_GridCell(col,row)` *inline accessor* or expose the grid via a `const unsigned char (*)[MAX_GRID_COLS]` getter — preferred inline accessor to avoid exposing the grid pointer.
- **R2** — For FR-002, how aggressively to inline `SetRect`? **Decision**: Only inside per-frame inner loops identified in the spec. Keep `SetRect` at window creation, screen layout, PICT loading bounds, and other one-time sites — cost is negligible there and readability matters.
- **R3** — For FR-003, fallback draw batching — does it interact with `Renderer_BeginSpriteDraw`/`EndSpriteDraw` bracket? **Decision**: The bracket already sets colour once per frame (renderer.c:835-848). The fallback path currently still re-sets ForeColor/BackColor per player inside the bracket. Refactor: pass 1 draws all player bodies with one colour set (local) per player in one pass; pass 2 draws markers. Concretely: iterate players twice inside `Game_Draw`, or push the two-pass loop inside a new `Renderer_DrawPlayersFallback` that Game_Draw calls. Chosen approach: add a private static two-pass helper inside `renderer.c` invoked by `Renderer_DrawPlayer` only when fallback applies — keep `Game_Draw`'s loop shape unchanged. (This avoids FR-N3 public-API growth.)
- **R4** — For FR-004, edge-trigger vs verbose flag? **Decision**: Edge-trigger. Simpler, no new runtime state, cost is near-zero per frame (comparison of four shorts). Verbose flag would need a menu/key and settings persistence, out of scope.
- **R5** — For FR-006, is there *any* reachable path into the dead branch? **Decision**: No. Grep for `LoadPICTResources` shows one caller at `renderer.c:531` guarded by `if (!gGame.isMacSE)`. `gGame.isMacSE` is set once in `DetectScreenSize` at startup (`main.c:92-106`) and never mutated thereafter. The dead branch is provably unreachable.
- **R6** — For FR-007, is `PurgeSpace` available on Mac SE / System 6? **Decision**: Yes — introduced System 4.1 per Inside Macintosh IV. Retro68's universal headers expose it. No compatibility guard needed.
- **R7** — For FR-008 (optional), is a packed layout equally fast on 68k? **Decision**: Indifferent at the instruction level — one `short` load per iteration vs two in parallel arrays, both are word-size loads on 68k. Net: +locality, -1.5 KB BSS. Safe.
- **R8** — For FR-009 (optional), is there any update-event path that would draw the title sprite after menu? **Decision**: Lobby_Draw and subsequent screens' `Draw` routines do not touch `gTitleSprite`; only `screen_loading.c` / `screen_menu.c` reference the title. Once past menu, the pointer is unused. NULL-after-dispose + existing NULL-checks make it safe. Spec FR-009 still requires explicit user sign-off before enabling.
- **R9** — Interaction with 007 `TickCount` timers? **None.** This feature does not touch timing code at all.
- **R10** — Interaction with 005 authority / mesh stagger code? **None.** Network layer unchanged apart from optional per-packet log edge-trigger in FR-004.

**Output**: `research.md`.

## Phase 1: Design & Contracts

### Data model

No new data types, no existing type changes. The `TileMap` struct is unchanged (we only remove `TileMap_IsSolid`'s body and declaration; the function touches no struct fields that aren't reachable via the remaining accessors). `gDirtyListCol` / `gDirtyListRow` may become a single packed `short[]` under FR-008 but that is a pure storage-shape change with no observable semantics. Captured in `data-model.md` as "no changes".

### Contracts

Internal-API change only:

- **FR-005 contract change**: `include/tilemap.h` loses one declaration: `int TileMap_IsSolid(short col, short row);`. No implementation swap — the function is *removed*, not renamed. Replacement pattern for future callers documented in the contract file: use `TileMap_GetTile(col,row) == TILE_WALL || == TILE_BLOCK`. The grep at `specs/008-perf-hotpath/contracts/tilemap-api.md` capture confirms zero current callers in `src/`.

Everything else is private to single translation units and so outside the contracts scope.

### Quickstart

`quickstart.md` covers the three build recipes from CLAUDE.md plus per-FR verification steps (F-key FPS read, `socat` capture, startup log inspection, `cppcheck` re-run).

### Agent context

Run after plan is written:

```bash
.specify/scripts/bash/update-agent-context.sh claude
```

This updates `CLAUDE.md` "Active Technologies" / "Recent Changes" sections with the 008-perf-hotpath entry.

## Constitution Check (post-design)

Re-running the gate after research + contracts:

- All ten principles still pass.
- No scope drift introduced during Phase 0/1 — `Bomb_GridCell` inline accessor (from R1) is considered part of FR-001 and does not count as a new public header API under FR-N3 because it lives in `bomb.h` as a `static inline` helper AND, critically, the spec explicitly allows "at most one inline helper" in FR-N3. Budget is exactly exhausted, which is acceptable.
- No complexity violations to log.

**Constitution gate (post-design): PASS.**

## Complexity Tracking

No violations. Table omitted.
