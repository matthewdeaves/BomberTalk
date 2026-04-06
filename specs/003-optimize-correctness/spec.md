# Feature Specification: Performance & Correctness Optimizations

**Feature Branch**: `003-optimize-correctness`  
**Created**: 2026-04-06  
**Status**: Draft  
**Input**: Code review findings cross-referenced with Classic Mac game programming books, validated against 3-machine hardware test results.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Faster Rendering on All Macs (Priority: P1)

A player launches BomberTalk on any of the three target Macs (Mac SE, Performa 6200, Performa 6400). During gameplay with bombs exploding and players moving, the game renders frames as fast as the hardware allows without wasting cycles on QuickDraw colorization overhead. The player experiences smoother gameplay, particularly on color Macs where CopyBits calls were previously penalized by incorrect foreground/background color state.

**Why this priority**: This is the highest-impact optimization. The book "Sex, Lies and Video Games" (1996) benchmarks show CopyBits with incorrect foreground/background colors runs up to 2.5x slower (241 ticks vs 95 ticks) in the worst case. The actual speedup depends on whether QuickDraw was previously performing colorization on this hardware — the 2.5x figure is the theoretical maximum penalty, not a guaranteed improvement. Currently this normalization only happens on Mac SE code paths, meaning color Macs may have been running CopyBits at reduced speed for every frame blit.

**Independent Test**: Build and deploy to a color Mac (Performa 6200 or 6400). Use the FPS counter (F key) to measure frame rate before and after the fix. Any measurable FPS improvement on color Macs confirms the fix is working. Mac SE behavior should remain unchanged (already had normalization).

**Acceptance Scenarios**:

1. **Given** the game is running on a color Mac, **When** a frame is rendered with dirty rectangle blitting, **Then** ForeColor is black and BackColor is white before every srcCopy CopyBits call.
2. **Given** the game is running on the Mac SE, **When** a frame is rendered, **Then** behavior is identical to the current build (no regression).
3. **Given** the game is running on any Mac, **When** the FPS counter is enabled, **Then** frame rate is equal to or better than the previous build.

---

### User Story 2 - Smooth Explosions on Remote Machines (Priority: P2)

A player is in a networked game. A remote player's bomb explodes and destroys multiple breakable blocks. Instead of the remote machine stuttering as it rebuilds the background tilemap once per destroyed block, it accumulates the block changes and rebuilds the background exactly once that frame. The explosion appears smooth with no visible hitch.

**Why this priority**: During gameplay with range-upgraded bombs, a single explosion can destroy up to 4 blocks. Each block destruction currently triggers a full background tilemap rebuild on remote machines. This means 4 full-screen redraws in a single frame, causing a visible stutter. Batching these into one rebuild per frame eliminates the stutter.

**Independent Test**: In a 2+ player networked game, have one player place a bomb near multiple breakable blocks. On the remote machine, observe whether the explosion causes a visible frame hitch. With the fix, the explosion should render smoothly.

**Acceptance Scenarios**:

1. **Given** a remote player's bomb destroys 4 blocks, **When** the block-destroyed messages arrive, **Then** the background is rebuilt exactly once (not 4 times).
2. **Given** two bombs chain-explode destroying blocks in the same frame, **When** the explosions are processed, **Then** the background is still rebuilt only once that frame.
3. **Given** a local bomb destroys blocks, **When** the explosion is processed locally, **Then** the background is rebuilt once (same as current behavior, via deferred flag).

---

### User Story 3 - Efficient Tilemap Reloading Between Rounds (Priority: P3)

A multiplayer game ends and players return to the lobby. When a new round starts, the tilemap resets to its original state (all breakable blocks restored) without re-loading the TMAP resource from disk. The round transition is faster because it restores from a cached copy of the initial map data rather than hitting the Resource Manager again.

**Why this priority**: Currently the tilemap is fully re-initialized twice: once at startup for window sizing, and again at every round start. The second call re-loads and releases the TMAP resource unnecessarily. While not a gameplay performance issue, it adds unnecessary Resource Manager overhead during round transitions.

**Independent Test**: Play multiple rounds in sequence. Verify that breakable blocks are correctly restored each round and that the round transition does not cause unnecessary delay.

**Acceptance Scenarios**:

1. **Given** a round has ended with some blocks destroyed, **When** a new round starts, **Then** all breakable blocks from the original map are restored.
2. **Given** a TMAP resource was loaded at startup, **When** a new round starts, **Then** the resource is NOT re-loaded from the Resource Manager.
3. **Given** no TMAP resource exists (fallback to static level data), **When** a new round starts, **Then** the static level data is restored correctly.

---

### User Story 4 - Instant Bomb Collision Check (Priority: P4)

A player moves around the game grid. When the game checks whether a tile contains a bomb (for movement blocking), the check completes in constant time regardless of how many bombs are active on the map. This reduces per-frame CPU work on the Mac SE where every cycle counts.

**Why this priority**: The current bomb-exists check does a linear scan of all 16 bomb slots. While acceptable at current game scale, replacing it with a spatial grid lookup is a clean O(1) optimization that costs only 775 bytes of static memory.

**Independent Test**: Gameplay should be identical. Place multiple bombs and verify movement is still correctly blocked by bombs. No visible behavior change expected.

**Acceptance Scenarios**:

1. **Given** a bomb exists at a grid position, **When** a player tries to move to that position, **Then** the move is blocked (same as current behavior).
2. **Given** a bomb at a position explodes, **When** a player tries to move to that position after the explosion, **Then** the move is allowed.
3. **Given** 16 bombs are active simultaneously, **When** collision is checked, **Then** the check completes in constant time (no linear scan).

---

### User Story 5 - Clean Peer Pointer on Disconnect (Priority: P5)

A networked player disconnects from the game. Their peer pointer in the player struct is cleared to NULL, preventing any possibility of stale pointer dereferencing if the player slot is later reused.

**Why this priority**: Defensive correctness fix. The peer pointer is currently only used for identity matching and is never dereferenced, so this is low risk. But clearing it on disconnect is the correct pattern.

**Independent Test**: In a networked game, have a player disconnect and reconnect. Verify the game handles reconnection correctly without crashes.

**Acceptance Scenarios**:

1. **Given** a connected player, **When** they disconnect, **Then** their peer pointer is set to NULL.
2. **Given** a player has disconnected, **When** a new player connects to the same slot, **Then** the new peer pointer is correctly assigned.

---

### Edge Cases

- What happens if ForeColor/BackColor are called on a port that is not the current port? The correct port must be set before color normalization.
- What happens if multiple deferred rebuild requests accumulate and then the game transitions to a non-game screen before the rebuild executes? No explicit clear is needed: Renderer_BeginFrame() is only called from Game_Draw(), so the flag is harmless on non-game screens and will be consumed on the next round start when Game_Draw() resumes.
- What happens if the tilemap reset function is called before the initial load? It should be a safe no-op or fallback to static level data.
- What happens if a bomb is placed and removed in the same frame? The spatial grid must handle both set and clear correctly within one frame.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Renderer MUST set foreground color to black and background color to white before every srcCopy-mode CopyBits call on all platforms, not just Mac SE. Transparent-mode CopyBits calls (used for sprite blitting) are excluded as the book benchmarks specifically measure srcCopy overhead.
- **FR-002**: Renderer MUST provide a deferred background rebuild mechanism that coalesces multiple rebuild requests into a single rebuild per frame.
- **FR-003**: Bomb explosion and network block-destroyed handlers MUST use the deferred rebuild mechanism instead of triggering immediate background rebuilds.
- **FR-004**: Direct (immediate) background rebuild calls MUST be preserved for one-time initialization paths (renderer init, game round init).
- **FR-005**: Tilemap module MUST cache the initial map state after first load and provide a reset function that restores from the cache without re-loading resources.
- **FR-006**: Game round initialization MUST call tilemap reset instead of full tilemap initialization for round restarts.
- **FR-007**: Bomb module MUST maintain a spatial grid for O(1) bomb-position lookups, updated on bomb placement and removal. This grid replaces both the Bomb_ExistsAt() linear scan and the inline duplicate-check scan in Bomb_PlaceAt().
- **FR-008**: Network disconnect handler MUST clear the disconnected player's peer pointer to NULL.
- **FR-012**: Renderer MUST log each background rebuild call via clog to enable verification of SC-002 (single rebuild per frame during explosions).
- **FR-009**: All changes MUST be C89/C90 compliant with no mixed declarations, no VLAs, and no C99 features.
- **FR-010**: All changes MUST build cleanly on all three targets (68k MacTCP, PPC MacTCP, PPC OT).
- **FR-011**: No runtime memory allocation MUST be introduced during gameplay. All new data structures must be statically allocated.

### Key Entities

- **Spatial Bomb Grid**: Static boolean grid indicating bomb presence per tile. Set on bomb placement, cleared on bomb deactivation. Sized to maximum grid dimensions.
- **Tilemap Cache**: Static copy of the initial tilemap state, populated after first load. Used by reset function to restore map without Resource Manager calls.
- **Rebuild Flag**: Static boolean in the renderer, set by deferred rebuild requests, checked and cleared once per frame at the start of rendering.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Frame rate on color Macs (Performa 6200/6400) is equal to or improved versus the previous build, as measured by the in-game FPS counter.
- **SC-002**: During a remote explosion destroying multiple blocks, the background is rebuilt exactly once per frame (verifiable via log output counting rebuild calls).
- **SC-003**: Round transitions restore all breakable blocks correctly without additional Resource Manager calls (verifiable by playing multiple rounds).
- **SC-004**: Game builds cleanly on all three targets with zero warnings related to these changes.
- **SC-005**: Mac SE frame rate shows no regression (remains at or above 15fps as measured by in-game FPS counter).
- **SC-006**: Gameplay behavior (movement blocking, bomb placement, explosions, network sync) is identical to the previous build across all three machines.
