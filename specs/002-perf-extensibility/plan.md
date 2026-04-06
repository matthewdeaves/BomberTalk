# Implementation Plan: Performance & Extensibility Upgrade

**Branch**: `002-perf-extensibility` | **Date**: 2026-04-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/002-perf-extensibility/spec.md`

## Summary

Optimize BomberTalk's renderer for Classic Mac hardware (especially Mac SE) by implementing dirty rectangle tracking, hoisting LockPixels/UnlockPixels to frame boundaries, promoting stack-allocated colors to static constants, and aligning CopyBits rectangles. Add protocol versioning to MSG_GAME_START and bounds-checking to MSG_GAME_OVER. Lay extensibility groundwork with resource-based map loading ('TMAP' format), parameterized grid dimensions, externalized spawn points, and a PlayerStats struct for future power-ups/character editor.

## Technical Context

**Language/Version**: C89/C90 (Retro68 cross-compiler)
**Primary Dependencies**: PeerTalk SDK (latest, commit 7e89304), clog (latest, commit e8d5da9), Retro68/RetroPPC toolchains
**Storage**: Classic Mac resource fork ('TMAP' resource type for map data)
**Testing**: Manual hardware testing on Mac SE, Performa 6200, Performa 6400; TickCount() frame timing instrumentation
**Target Platform**: Classic Macintosh — 68k (System 6) and PPC (System 7.5.3+)
**Project Type**: Desktop game (Classic Mac application)
**Performance Goals**: 10+ fps on Mac SE during 4-player gameplay; <25% pixel copies on low-change frames
**Constraints**: 1 MB app heap on Mac SE; no malloc during gameplay; no C99/C11; big-endian only
**Scale/Scope**: 12 source files, ~3000 LOC; changes touch renderer.c, game.h, tilemap.c/.h, bomb.c, net.c, screen_game.c, screen_lobby.c, player.c, CMakeLists.txt

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| # | Principle | Status | Notes |
|---|-----------|--------|-------|
| I | Prove PeerTalk Works | PASS | Renderer optimization enables playable frame rates for multiplayer on Mac SE. Protocol versioning hardens the PeerTalk message layer. |
| II | Run on All Three Macs | PASS | All changes are C89, use standard Toolbox calls available on all targets. Dirty rect and LockPixels changes affect both GWorld (color) and BitMap (SE) paths. |
| III | Ship Screens, Not Just a Loop | PASS | No screen changes — all four screens remain. Lobby gains version mismatch feedback. |
| IV | C89 Everywhere | PASS | Adding -std=c89 flag actively enforces this. No C99 constructs introduced. |
| V | Mac SE Is the Floor | PASS | Every optimization primarily targets Mac SE. Memory budget validated: DirtyGrid adds 775 bytes max, PlayerStats adds ~24 bytes. |
| VI | Simple Graphics, Never Blocking | PASS | No graphics changes. Colored rectangle fallback preserved. |
| VII | Fixed Frame Rate, Poll Everything | PASS | Main loop unchanged. Dirty rect tracking is a per-frame bookkeeping step, not a new event source. |
| VIII | Network State Is Authoritative | PASS | Protocol version in MSG_GAME_START ensures all peers agree on message format. Winner ID validation prevents corruption. |
| IX | The Books Are Gospel | PASS | Dirty rect technique from Mac Game Programming (2002) Ch.6. CopyBits alignment from Tricks of the Gurus (1995) p.183. LockPixels hoisting from Mac Game Programming Techniques (1996) Ch.7. |
| X | One Codebase, Three Builds | PASS | Single src/ tree. Platform-specific paths (Mac SE BitMap vs GWorld) already exist and are preserved. |

**Gate result: ALL PASS. No violations.**

## Project Structure

### Documentation (this feature)

```text
specs/002-perf-extensibility/
├── plan.md              # This file
├── research.md          # Phase 0: research findings
├── data-model.md        # Phase 1: updated data model
├── quickstart.md        # Phase 1: build & test guide
├── contracts/
│   └── network-protocol.md  # Updated MSG_GAME_START/GAME_OVER
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 output (speckit.tasks)
```

### Source Code (repository root)

```text
include/
├── game.h               # MODIFIED: PlayerStats struct, DirtyGrid, TMAP constants,
│                         #   protocol version, parameterized grid dims
├── tilemap.h            # MODIFIED: TileMap struct with dimensions, spawn scan API
├── renderer.h           # MODIFIED: dirty rect API (MarkDirty, MarkAllDirty)
├── player.h             # UNCHANGED
├── bomb.h               # UNCHANGED
├── net.h                # MODIFIED: version check API
├── screens.h            # UNCHANGED
└── input.h              # UNCHANGED

src/
├── main.c               # UNCHANGED
├── renderer.c           # MODIFIED: dirty rect system, LockPixels hoisting,
│                         #   static colors, PixMap caching, 32-bit alignment
├── tilemap.c            # MODIFIED: resource loading, dimension storage, spawn scan
├── game.h (inline)      # PlayerStats added to Player struct
├── net.c                # MODIFIED: protocol version in MSG_GAME_START,
│                         #   winner ID bounds check in MSG_GAME_OVER
├── screen_game.c        # MODIFIED: spawn from map, stats references, dirty marking
├── screen_lobby.c       # MODIFIED: version mismatch display
├── player.c             # MODIFIED: read from stats struct
├── bomb.c               # MODIFIED: read range from stats, mark dirty on explode
└── CMakeLists.txt       # MODIFIED: add -std=c89

maps/
└── level1.h             # UNCHANGED (used as static fallback)

resources/
├── bombertalk.r         # MODIFIED: add 'TMAP' resource include (optional)
└── bombertalk_size.r    # UNCHANGED
```

**Structure Decision**: Existing single-project structure preserved. All changes are modifications to existing files. No new source files needed — dirty rect tracking is integrated into renderer.c, TMAP loading into tilemap.c.

## Key Design Decisions

### D1: Tile-Granularity Dirty Tracking (not pixel-level)

Use a boolean grid matching GRID_ROWS x GRID_COLS (max 31x25 = 775 bytes). Each cell tracks whether that tile needs redrawing. This matches the game's tile-based nature and avoids complex region calculations. When a sprite enters or leaves a tile, both the old and new tile are marked dirty. When a block is destroyed, that tile is marked dirty. The full-screen fallback (FR-006) triggers when dirty count exceeds a threshold (e.g., >50% of tiles) or after RebuildBackground.

Source: Mac Game Programming (2002) Ch.6 pp.188-192.

### D2: Frame-Level Sprite Lock Batching

Lock all sprite GWorlds at the start of Renderer_BeginFrame(), cache PixMap pointers in file-scope statics, unlock all in Renderer_EndFrame(). Individual DrawPlayer/DrawBomb/DrawExplosion functions use the cached pointers directly. The Mac SE path (BitMap, no GWorlds for sprites) is unaffected.

Source: Macintosh Game Programming Techniques (1996) Ch.7.

### D3: Protocol Version as Replacement for Pad Byte

MSG_GAME_START currently has a `pad` byte. Replace it with `version` (value: BT_PROTOCOL_VERSION = 2). This is backwards-compatible in size (still 2 bytes) and lets receivers detect old clients that sent version 0 (the old pad value). MSG_GAME_OVER's `pad` byte is preserved — only winnerID validation is added, no format change.

### D4: TMAP Resource Format

Simple binary format in resource fork: 2-byte cols (big-endian short), 2-byte rows (big-endian short), followed by (cols * rows) bytes of tile data. Resource type 'TMAP', ID 128. Loaded via GetResource(). If absent, fall back to static level1.h array. Validated on load: clamp dims to 7-31 cols, 7-25 rows; unknown tile types become TILE_FLOOR.

### D5: Spawn Point Discovery

After loading a map (from resource or static), scan all tiles for TILE_SPAWN. Store up to MAX_PLAYERS spawn positions in order found (top-left to bottom-right scan). If fewer than MAX_PLAYERS spawns found, fill remaining from hardcoded default corners. This means existing level1.h (which has TILE_SPAWN at corners) works unchanged.

### D6: PlayerStats Struct Migration

Add `PlayerStats` struct to `Player` with fields: `bombsMax`, `bombRange`, `speedTicks` (movement cooldown in ticks). Initialize to current defaults (1, 1, 12). Update `player.c` to read `p->stats.speedTicks` instead of MOVE_COOLDOWN_TICKS constant. Update `bomb.c` to read `p->stats.bombRange`. Update `screen_game.c` to read `p->stats.bombsMax`. No network message changes — stats are local only.

## Complexity Tracking

> No constitution violations. No complexity justifications needed.
