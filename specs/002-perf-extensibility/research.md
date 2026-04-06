# Research: Performance & Extensibility Upgrade

**Feature**: `002-perf-extensibility`
**Date**: 2026-04-06

## R1: Dirty Rectangle Implementation Strategy

**Decision**: Tile-granularity boolean grid, stored as `unsigned char dirtyGrid[MAX_GRID_ROWS][MAX_GRID_COLS]`. Mark tiles dirty when sprites enter/leave, bombs placed/exploded, blocks destroyed. Full-screen fallback when dirty count exceeds 50% of total tiles.

**Rationale**: Mac Game Programming (2002) Ch.6 pp.188-192 prescribes exactly this approach for tile-based games. The grid matches our existing tile coordinate system — no coordinate translation needed. A per-pixel dirty rect system (union of rectangles) would add complexity and offer no advantage since all game objects snap to tile boundaries.

**Alternatives considered**:
- Per-pixel dirty rectangles (union/merge approach): More complex, requires rectangle coalescing, no benefit for grid-locked game objects.
- QuickDraw region-based tracking (NewRgn/UnionRgn): Heavy Toolbox overhead on Mac SE, regions are opaque heap objects requiring Memory Manager calls.
- No dirty tracking (status quo): Full-screen CopyBits every frame. Works but wastes ~80% of copy bandwidth on static frames.

**Key implementation detail**: The dirty grid must be checked in both BeginFrame (bg→work copy) and EndFrame (work→window blit). When dirty count equals total tiles, skip per-tile iteration and do a single full-screen CopyBits (FR-006).

## R2: LockPixels Hoisting Feasibility

**Decision**: Lock all sprite GWorlds (player sprites, bomb sprite, explosion sprite) at the top of Renderer_BeginFrame(). Cache PixMapHandle dereferences as file-scope `static BitMap *` pointers. Unlock all in Renderer_EndFrame().

**Rationale**: Macintosh Game Programming Techniques (1996) Ch.7 recommends batching lock/unlock around drawing sequences. Currently each DrawPlayer/DrawBomb/DrawExplosion call does its own LockPixels + GetGWorldPixMap. With up to 16 draw calls per frame (4 players + 4 bombs + 8 explosions), that's 32 lock/unlock calls reduced to ~6 (one per unique GWorld).

**Alternatives considered**:
- Lock once at init, never unlock: Prevents heap compaction for the entire game session. On Mac SE with 1 MB heap, this could fragment memory fatally.
- Lock per-sprite (status quo): Safe but redundant. Each lock involves a Toolbox trap call.

**Risk**: If a GWorld is purged between BeginFrame and EndFrame, the cached pointer becomes invalid. Mitigation: sprite GWorlds are small (~4KB each) and allocated at init — they should not be purged during a single frame. Add a NULL check on the cached pointer as safety.

## R3: 32-Bit CopyBits Alignment

**Decision**: Align dirty-tile CopyBits rectangles to longword boundaries. For 8-bit displays: align left edge down to nearest 4-pixel boundary, right edge up to nearest 4-pixel boundary. For 1-bit displays: align to 32-pixel boundaries.

**Rationale**: Tricks of the Mac Game Programming Gurus (1995) p.183 documents that CopyBits uses longword moves when source and destination rectangles are 32-bit aligned. Misaligned copies require byte-by-byte shifting.

**Formula**:
```
For 8-bit (1 byte/pixel): left = left & ~3; right = (right + 3) & ~3;
For 1-bit (8 pixels/byte, 32 pixels/long): left = left & ~31; right = (right + 31) & ~31;
```

**Alternatives considered**:
- No alignment (copy exact tile rects): Simpler, but CopyBits may use slower per-byte path.
- Align to tile boundaries only: Tiles are 32x32 (color) or 16x16 (SE). 32px tiles are already longword-aligned on 1-bit displays. 16px tiles on SE are only half-aligned. But since we copy whole tiles, 32px tiles are naturally aligned on 8-bit displays (32 pixels * 1 byte = 32 bytes = 8 longs). The main benefit is for 16px tiles on Mac SE.

**Conclusion**: Alignment is trivially cheap (two bitwise ANDs per rect). Apply it universally.

## R4: Protocol Version Placement

**Decision**: Replace the `pad` byte in MsgGameStart with `version` byte. Value: `BT_PROTOCOL_VERSION = 2`. Receivers check `msg->version == BT_PROTOCOL_VERSION` before honoring the game start.

**Rationale**: The pad byte in MsgGameStart is currently unused (sent as 0). Old clients (v1.0-alpha, no version field) will send 0 in this position. New clients send 2. This provides natural backwards-incompatibility detection: version 0 (old) != version 2 (new) → reject.

**Alternatives considered**:
- Add a new MSG_VERSION message type: Adds network complexity, requires handshake before game start.
- Include version in discovery broadcast: Would require PeerTalk SDK changes (discovery payload is SDK-controlled).
- Bump to version 1 instead of 2: Version 0 (old padding) vs 1 is a smaller gap, but version 2 clearly signals "second protocol revision" which is semantically accurate.

**Wire format change**:
```
Before: { numPlayers: u8, pad: u8 }       /* pad always 0 */
After:  { numPlayers: u8, version: u8 }    /* version = 2 */
```

## R5: TMAP Resource Format Design

**Decision**: Custom resource type 'TMAP', ID 128. Binary format: 2-byte cols (big-endian short) + 2-byte rows (big-endian short) + (cols * rows) bytes of tile data. Loaded via `GetResource('TMAP', 128)`.

**Rationale**: Classic Mac convention is to store game data as custom resources. This matches the asset pipeline already used for PICT resources. The format is dead simple — no headers, no compression, no versioning (the resource type + ID is the version). Big-endian is native to both 68k and PPC.

**Alternatives considered**:
- ResEdit TMPL template: More discoverable in ResEdit but adds complexity. The TMAP format is simple enough to hex-edit.
- Separate data fork file: Not Classic Mac convention. Resource fork is the standard location for game data.
- JSON/text format: Not C89-friendly to parse. Binary is simpler and faster.

**Validation rules**:
- Cols: clamp to [7, 31]. Default: 15.
- Rows: clamp to [7, 25]. Default: 13.
- Tile values: anything > TILE_SPAWN (3) treated as TILE_FLOOR (0).
- Total data size check: resource size must equal 4 + (cols * rows). If mismatch, fall back to default.

## R6: Spawn Point Scan Order

**Decision**: Scan tiles top-left to bottom-right (row-major order). First TILE_SPAWN found = spawn 0, second = spawn 1, etc. Store up to MAX_PLAYERS (4) spawn positions. If fewer found, fill remaining with default corners: (1,1), (13,1), (1,11), (13,11).

**Rationale**: Top-left to bottom-right is deterministic and intuitive for a map editor. The existing level1.h has spawns at the four corners; scanning order produces player 0 at top-left (1,1), which matches the current hardcoded assignment.

**Alternatives considered**:
- Encode spawn order in tile data (TILE_SPAWN_0 through TILE_SPAWN_3): Uses 4 tile types instead of 1. Complicates the tile type enum.
- Store spawns as separate data after tile grid: Adds format complexity.
- Always use corners regardless of map: Breaks custom maps where corners are walls.

## R7: PeerTalk and clog Compatibility

**Decision**: No changes needed. Both libraries are fully compatible with BomberTalk's current usage.

**Rationale**: Verified against latest releases:
- PeerTalk (commit 7e89304, 2026-04-06): BomberTalk already uses PT_LocalAddress, PT_SendUDPBroadcast, updated PT_ErrorCallback signature with peer parameter. All 24 API functions available, 15 used by BomberTalk.
- clog (commit e8d5da9, 2026-04-06): BomberTalk already uses clog_set_network_sink. All macros (CLOG_ERR/WARN/INFO/DEBUG) and lifecycle functions used correctly.

**New clog feature available but not needed**: `clog_set_level()` for runtime log level changes. Could be useful for a future debug menu but out of scope for this feature.
