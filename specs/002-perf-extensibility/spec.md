# Feature Specification: Performance & Extensibility Upgrade

**Feature Branch**: `002-perf-extensibility`  
**Created**: 2026-04-06  
**Status**: Draft  
**Input**: User description: "BomberTalk v1.1 Performance & Extensibility Upgrade"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Smoother Gameplay on Mac SE (Priority: P1)

A player on the Mac SE (8 MHz 68000, 1-bit monochrome) experiences noticeably smoother gameplay after the renderer is optimized. Where previously the entire screen was redrawn every frame, now only tiles that have changed are updated. Sprite drawing no longer performs redundant per-sprite setup work. The game feels more responsive during 4-player matches with multiple bombs and explosions on screen.

**Why this priority**: The Mac SE is the constraining platform (Constitution V). Every optimization that improves Mac SE frame rate directly proves PeerTalk can deliver playable real-time multiplayer on the weakest target hardware (Constitution I). Without acceptable performance, the game fails its core mission.

**Independent Test**: Build the 68k target, transfer to Mac SE, run a 4-player game. Measure frame time using TickCount() delta. Compare before/after frame rates with identical gameplay scenarios (4 players moving, 3 active bombs, 2 explosions).

**Acceptance Scenarios**:

1. **Given** a 4-player game on Mac SE with 3 active bombs and 2 explosions, **When** all players are moving simultaneously, **Then** frame rate remains at or above 10 fps (no frame exceeds 6 ticks).
2. **Given** a frame where only 2 players moved and nothing else changed, **When** the renderer draws the frame, **Then** only the tiles containing those 2 players (current and previous positions) are copied from background to work buffer and from work buffer to window — not the full 195-tile grid.
3. **Given** a frame with 4 player sprites, 3 bomb sprites, and 8 explosion tiles, **When** sprites are drawn, **Then** sprite GWorld pixel maps are locked exactly once at frame start and unlocked once at frame end, not per-sprite.

---

### User Story 2 - Stable Cross-Version Multiplayer (Priority: P2)

When a player running an older version of BomberTalk joins a lobby with newer clients, the game detects the version mismatch and prevents them from starting a game together. Players see a clear indication that versions don't match. Game-over messages with invalid data no longer risk corrupting game state on receiving machines.

**Why this priority**: Network stability directly proves PeerTalk works (Constitution I, VIII). Protocol versioning prevents silent corruption as the game evolves. Without this, future updates risk breaking existing multiplayer sessions.

**Independent Test**: Build two copies of BomberTalk with different protocol version numbers. Connect both to the same LAN. Verify the lobby prevents game start and displays a version mismatch indicator.

**Acceptance Scenarios**:

1. **Given** two players on the same LAN with different protocol versions, **When** either player presses Start, **Then** the game does not begin and the initiator is informed of the mismatch.
2. **Given** a game-over message arrives with an invalid winner ID (outside 0-3 range), **When** the receiving client processes the message, **Then** it ignores the invalid winner ID and does not index out of bounds.
3. **Given** BomberTalk is compiled, **When** the build system runs, **Then** the compiler enforces C89 mode and rejects C99/C11 constructs (mixed declarations, VLAs, `//` comments).

---

### User Story 3 - Loading Custom Maps (Priority: P3)

A future map editor can save level data as a resource in the game's resource fork. When BomberTalk launches, it loads map data from the resource fork if available, falling back to the built-in default level if no custom map resource is found. Maps can have varying dimensions (within platform memory limits) and define their own spawn point locations. This groundwork enables a future MacPaint-style map editor without requiring changes to the game's core loading logic.

**Why this priority**: Extensibility groundwork does not directly prove PeerTalk works (Constitution I), but it enables future features (map editor, character editor) that will make BomberTalk a richer demo application. The work is small and non-disruptive — it restructures existing data flow without changing gameplay.

**Independent Test**: Create a custom 'TMAP' resource with a different grid layout and embed it in the resource fork. Launch BomberTalk and verify it loads the custom map instead of the default. Remove the resource and verify fallback to the default level.

**Acceptance Scenarios**:

1. **Given** a 'TMAP' resource exists in the resource fork, **When** the game initializes a new round, **Then** it loads the map from the resource including dimensions and tile data.
2. **Given** no 'TMAP' resource exists, **When** the game initializes a new round, **Then** it falls back to the built-in 15x13 default level.
3. **Given** a custom map with TILE_SPAWN markers placed at non-standard positions, **When** the game assigns spawn points, **Then** players spawn at the TILE_SPAWN positions found in the map, not at hardcoded corner positions.
4. **Given** a custom map with dimensions different from 15x13, **When** the renderer initializes, **Then** it adapts play area dimensions to match the loaded map.

---

### User Story 4 - Player Stats Groundwork (Priority: P4)

Player attributes (maximum bombs, bomb range, movement speed) are stored in a dedicated stats structure rather than as loose fields on the player. This change is invisible to the current player but enables a future character editor or power-up system to modify these values without touching movement or bomb placement logic.

**Why this priority**: Lowest priority because it is purely structural — no user-visible change. However, it establishes the data model for power-ups (v1.2 scope per constitution) and character customization, reducing future refactoring cost.

**Independent Test**: Play a game and verify bomb range, bomb count, and movement cooldown behave identically to v1.0-alpha. Inspect the code to confirm attributes are read from the stats structure.

**Acceptance Scenarios**:

1. **Given** a player with default stats, **When** they place a bomb, **Then** the bomb range matches the value from the player's stats structure (default: 1 tile).
2. **Given** a player with default stats, **When** they move, **Then** the movement cooldown matches the value derived from the player's stats structure (default: 12 ticks).

---

### Edge Cases

- What happens when a 'TMAP' resource contains corrupted data (dimensions out of range, tile values > max type)? Game clamps dimensions to safe limits and treats unknown tile values as TILE_FLOOR.
- What happens when a custom map has fewer than 2 TILE_SPAWN markers? Game falls back to hardcoded corner spawns for any players without a spawn marker.
- What happens when all tiles on screen are dirty in a single frame (e.g., after Renderer_RebuildBackground)? The dirty rect system falls back to a full-screen copy, no worse than the current behavior.
- What happens when a peer sends a MSG_GAME_START with a version the local client doesn't recognize? The receiver rejects the game start and remains in the lobby.

## Requirements *(mandatory)*

### Functional Requirements

**Renderer Performance:**

- **FR-001**: Renderer MUST track which tiles have changed each frame (dirty rectangle grid) and only copy dirty tiles during background-to-work and work-to-window transfers.
- **FR-002**: Renderer MUST lock all sprite GWorld pixel maps once at frame start and unlock once at frame end, not per-sprite.
- **FR-003**: Renderer MUST store all fixed color values (player colors, explosion color, tile colors) as file-scope constants, not stack-allocated per-call.
- **FR-004**: Renderer MUST cache GWorld PixMap handle dereferences to avoid repeated handle lookups during sprite drawing.
- **FR-005**: Renderer MUST align CopyBits rectangle boundaries to 32-bit-aligned pixel positions when performing partial screen updates.
- **FR-006**: When all tiles are dirty (e.g., after a full background rebuild), the dirty rect system MUST perform a single full-screen CopyBits rather than 195 individual tile copies.

**Stability & Build:**

- **FR-007**: Build system MUST enforce C89 compilation mode via compiler flags.
- **FR-008**: MSG_GAME_START MUST include a protocol version identifier. Receivers MUST reject game start messages with a different protocol version and remain in the lobby.
- **FR-009**: MSG_GAME_OVER receiver MUST bounds-check the winner ID before using it to index any player data. Invalid winner IDs MUST be ignored.

**Extensibility - Map Loading:**

- **FR-010**: Game MUST support loading map data from a 'TMAP' resource in the resource fork, including grid dimensions and tile data.
- **FR-011**: When no 'TMAP' resource is available, the game MUST fall back to the built-in default level (15x13 standard Bomberman layout).
- **FR-012**: Grid dimensions MUST be stored in the map data structure rather than as compile-time constants. The renderer and game logic MUST derive play area size from the loaded map.
- **FR-013**: Spawn points MUST be determined by scanning the loaded map for TILE_SPAWN markers. If insufficient spawn markers exist, the game MUST fall back to default corner positions.
- **FR-014**: Map loading MUST validate dimensions against safe limits (minimum 7x7, maximum 31x25) and clamp out-of-range values. Unknown tile type values MUST be treated as TILE_FLOOR.

**Extensibility - Player Stats:**

- **FR-015**: Player attributes (maximum bomb count, bomb range, movement speed modifier) MUST be grouped into a dedicated stats structure on each player.
- **FR-016**: Bomb placement and movement logic MUST read attributes from the player's stats structure, not from standalone fields.

### Key Entities

- **DirtyGrid**: A per-tile boolean grid tracking which screen tiles need redrawing. Cleared after each frame blit. Marked dirty when sprites enter/leave a tile, bombs appear/explode, or blocks are destroyed.
- **TileMap (extended)**: Now includes grid dimensions (columns, rows) alongside tile data. Can be loaded from a resource or initialized from a static default. Contains spawn point locations derived from TILE_SPAWN markers.
- **PlayerStats**: Groups bombsMax, bombRange, and speedModifier. Attached to each Player. Initialized with default values. Future power-ups or a character editor modify these values.
- **Protocol Version**: A single byte identifying the network message format version. Included in MSG_GAME_START. Compared on receipt to determine compatibility.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Mac SE frame rate during a 4-player game with active bombs and explosions is at least 10 fps, measured via TickCount() delta over 60 frames.
- **SC-002**: On frames where fewer than 20% of tiles changed, the renderer copies fewer than 25% of total screen pixels (measured by counting dirty tiles vs total tiles).
- **SC-003**: All three build targets (68k MacTCP, PPC MacTCP, PPC OT) compile clean with zero warnings under C89 mode with -Wall -Wextra.
- **SC-004**: A version-mismatched client cannot start a game — 100% of game start attempts between mismatched versions are rejected.
- **SC-005**: The game loads and plays identically with or without a custom 'TMAP' resource — no gameplay regression when falling back to the default level.
- **SC-006**: Application heap usage on Mac SE remains under 1 MB after all optimizations and new data structures are initialized.

## Assumptions

- The dirty rectangle optimization uses the existing tile grid as its tracking granularity (one dirty flag per tile, not per-pixel). This is the natural fit for a tile-based game and matches the technique described in Mac Game Programming (2002) Ch.6.
- 32-bit alignment for CopyBits means aligning to 4-byte boundaries on 8-bit displays and 4-pixel (longword) boundaries on 1-bit displays, per Tricks of the Mac Game Programming Gurus (1995) p.183.
- The 'TMAP' resource format uses a simple binary layout: 2-byte column count, 2-byte row count, followed by (cols x rows) bytes of tile data. Big-endian, no compression. This is consistent with Classic Mac resource conventions.
- Protocol version starts at 2 (v1.0-alpha is implicitly version 1 with no version field; the addition of the version byte is the v2 protocol).
- The PlayerStats struct adds no new network messages — stats are local-only for now. Future power-up messages would be a separate feature.
- Maximum map dimensions (31x25) are chosen to keep total tile data under 775 bytes and total play area under 1024x800 pixels at 32px tiles, fitting within GWorld buffer allocations.

## Research

- **R1: Dirty Rectangle Technique** — Mac Game Programming (2002) Ch.6 pp.188-192 describes grid-based dirty tracking for tile games. Allocate a boolean grid matching tile dimensions, mark dirty on sprite movement, copy only dirty tiles. Tricks of the Mac Game Programming Gurus (1995) Ch.3 confirms 32-bit alignment for CopyBits performance.
- **R2: CopyBits Optimization** — Sex, Lies, and Video Games (1996) Ch.4 pp.138-144 documents CopyBits overhead: matching color depths eliminates color conversion, matching rectangle sizes avoids scaling, foreground=black/background=white skips colorization. These are already followed in the codebase.
- **R3: LockPixels Hoisting** — Macintosh Game Programming Techniques (1996) Ch.7 recommends locking GWorld pixmaps before a drawing batch and unlocking after, not per-draw-call. This is already done in RebuildBackground but not in per-frame sprite drawing.
- **R4: PeerTalk Compatibility** — Verified against peertalk latest (commit 7e89304, 2026-04-06). BomberTalk already uses the current API correctly including PT_LocalAddress, PT_SendUDPBroadcast, and the updated PT_ErrorCallback signature. No changes needed.
- **R5: clog Compatibility** — Verified against clog latest (commit e8d5da9, 2026-04-06). BomberTalk uses clog_init, clog_set_file, clog_set_network_sink, clog_shutdown, and all four log macros correctly. No changes needed.
