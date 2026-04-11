# Feature Specification: Smooth Sub-Tile Player Movement

**Feature Branch**: `004-smooth-movement`  
**Created**: 2026-04-10  
**Status**: Draft  
**Input**: User description: "Implement true sub-tile smooth pixel movement, replacing tile-snap movement with classic Bomberman-style continuous movement where players can be caught by explosions while between tiles."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Smooth Player Movement (Priority: P1)

As a player, I move smoothly across the game board rather than snapping from tile to tile, so the game feels like classic Bomberman with fluid, responsive controls.

When I hold an arrow key, my character slides pixel-by-pixel in that direction at a consistent speed. Movement looks smooth on all three target machines (Mac SE, Performa 6200, Performa 6400) despite different frame rates, because speed is tick-based rather than frame-based.

**Why this priority**: This is the core of the feature. Without smooth movement, nothing else (explosion overlap, corner sliding) matters. It also directly exercises PeerTalk by changing the position synchronization model.

**Independent Test**: Can be fully tested in single-player by holding arrow keys and observing the player sprite sliding smoothly between tiles instead of snapping. Verify on all three machines that movement speed is consistent regardless of frame rate.

**Acceptance Scenarios**:

1. **Given** a player standing on a tile, **When** the player holds an arrow key, **Then** the player sprite visually slides in that direction over multiple frames rather than jumping instantly to the next tile.
2. **Given** the Mac SE running at ~10fps and the 6400 running at ~26fps, **When** both players hold the same direction for 1 second, **Then** both players travel the same pixel distance (within 1 tile tolerance due to tick quantization).
3. **Given** a player moving toward a wall tile, **When** the player's leading edge reaches the wall boundary, **Then** the player stops flush against the wall without overlapping it.
4. **Given** a player holding a direction then releasing the key, **When** the key is released, **Then** the player stops at their current sub-tile position (no momentum or drift).

---

### User Story 2 - Explosion Danger While Between Tiles (Priority: P1)

As a player navigating the board, I can be killed by an explosion if any part of my character overlaps an exploding tile, creating the tense near-miss gameplay that defines Bomberman.

Currently, explosions only kill players standing on the exact same grid tile. With smooth movement, a player partially overlapping an explosion tile is caught in the blast.

**Why this priority**: This is the gameplay payoff of smooth movement. Without sub-tile explosion collision, smooth movement is purely cosmetic. The near-miss tension is what makes Bomberman compelling.

**Independent Test**: Place a bomb, move the player so they are partially overlapping the explosion path, and verify the player dies. Then position the player just outside the explosion tiles and verify survival.

**Acceptance Scenarios**:

1. **Given** a player whose bounding box partially overlaps an explosion tile (even by 1 pixel), **When** the explosion activates, **Then** the player is killed.
2. **Given** a player whose bounding box does NOT overlap any explosion tile, **When** a nearby explosion activates, **Then** the player survives.
3. **Given** a player with a slightly smaller hitbox than tile size, **When** the player is positioned at the very edge of an explosion tile, **Then** there is a small safe margin making near-misses possible and satisfying.

---

### User Story 3 - Bomb Walk-Off (Priority: P2)

As a player who just placed a bomb, I can walk off the bomb tile but cannot walk back onto it, matching standard Bomberman bomb interaction.

Currently, bombs block all movement through their tile via the spatial grid check. With smooth movement, the player needs to be able to leave the tile they placed a bomb on, since they're standing on it when they place it.

**Why this priority**: Without bomb walk-off, placing a bomb traps the player on that tile until it explodes, making the game unplayable. This is essential for basic bomb gameplay.

**Independent Test**: Place a bomb while standing on a tile, walk away in any direction, then attempt to walk back onto the bomb tile. The player should be able to leave but not return.

**Acceptance Scenarios**:

1. **Given** a player who just placed a bomb on their current tile, **When** the player moves away from the bomb tile, **Then** the player can move freely off the bomb.
2. **Given** a player who has fully left a bomb tile, **When** the player attempts to move back onto that bomb tile, **Then** the player is blocked by the bomb as a solid obstacle.
3. **Given** a player standing on a bomb they placed, **When** the bomb explodes, **Then** the player is killed by the explosion (bombs the player stands on still kill them).

---

### User Story 4 - Network Position Sync (Priority: P2)

As a multiplayer participant, I see other players moving smoothly on my screen, with their positions synchronized over the network using pixel coordinates instead of grid coordinates.

**Why this priority**: The game is a PeerTalk demo. Smooth movement must work over the network, not just locally. This directly proves PeerTalk handles higher-frequency position data.

**Independent Test**: Connect two machines, move a player on one, and observe smooth movement of the remote player on the other machine.

**Acceptance Scenarios**:

1. **Given** two connected players, **When** player A moves smoothly on machine A, **Then** player A's movement appears smooth on machine B (interpolated between network updates).
2. **Given** a network position update arrives, **When** the remote player's reported position differs from their displayed position, **Then** the remote player visually interpolates toward the correct position rather than snapping.
3. **Given** the protocol version is bumped to 3, **When** a v2 client connects to a v3 client, **Then** both display a protocol mismatch warning in the lobby (existing behavior preserved).

---

### User Story 5 - Corner Sliding (Priority: P3)

As a player navigating tight corridors, when I am nearly aligned with a corridor opening, the game nudges me into alignment so I can smoothly turn corners without pixel-perfect positioning.

This is a quality-of-life feature present in all classic Bomberman games. Without it, players frequently get stuck on tile corners when trying to turn, which is frustrating.

**Why this priority**: This is polish that significantly improves the feel of movement but is not required for basic functionality. The game is playable without it, just less pleasant.

**Independent Test**: Move the player toward a corridor opening while slightly misaligned (a few pixels off-center), and verify the player is automatically nudged into alignment to enter the corridor.

**Acceptance Scenarios**:

1. **Given** a player moving right who is within the nudge threshold of a corridor opening above, **When** the player presses up, **Then** the player is horizontally nudged to align with the corridor and begins moving up.
2. **Given** a player who is too far from alignment with a corridor (beyond the nudge threshold), **When** the player attempts to turn into the corridor, **Then** the player is blocked and no nudge occurs.

---

### User Story 6 - Disconnected Player Cleanup (Priority: P1)

As a player in a multiplayer game, when another player quits BomberTalk, their character is immediately removed from my screen and the game continues without visual artifacts.

Currently, when a player disconnects, they are marked inactive but their sprite remains rendered on screen at their last position because the dirty rectangle system skips inactive players, leaving a ghost sprite.

**Why this priority**: This is a bug that affects every multiplayer session. A ghost player sprite is confusing and breaks the game experience. It also undermines the PeerTalk demo — disconnect handling is a core networking feature.

**Independent Test**: Start a 2-3 player game across machines, have one player quit via Cmd-Q, and verify the quitting player's sprite disappears from all remaining machines within one frame.

**Acceptance Scenarios**:

1. **Given** a 3-player game in progress, **When** one player quits BomberTalk, **Then** the quitting player's sprite disappears from all remaining players' screens within one frame.
2. **Given** a player disconnects while standing on a tile, **When** the disconnect is processed, **Then** the tile where the player was standing is redrawn showing the background (floor/spawn tile) with no ghost sprite.
3. **Given** a player disconnects during gameplay, **When** the remaining players continue playing, **Then** the disconnected player's bombs (if any active) continue their fuse timer and explode normally.
4. **Given** a player disconnects with smooth movement and their sprite was straddling multiple tiles, **When** the disconnect is processed, **Then** all tiles the player's bounding box overlapped are marked dirty and redrawn.

---

### User Story 7 - Compile-Time Debug Toggle (Priority: P2)

As a developer, I can build BomberTalk with or without debug logging (clog UDP broadcasts) via a compile-time flag, so that release builds have zero overhead from the 100+ logging call sites across the codebase.

Currently, all builds include clog UDP broadcast logging which formats strings and sends UDP packets on every CLOG_INFO/CLOG_DEBUG call. On the Mac SE at 8MHz, the string formatting overhead from ~100 call sites is measurable. A compile-time flag eliminates these calls entirely in release builds.

**Why this priority**: Performance improvement that directly benefits the Mac SE (constitution principle V). Debug logging is essential during development but unnecessary in production. The overhead from formatting and sending ~100 log messages per second is non-trivial on an 8MHz CPU.

**Independent Test**: Build with debug OFF, verify no clog UDP packets are received on port 7355. Build with debug ON, verify packets are received. Compare FPS on Mac SE between debug and release builds.

**Acceptance Scenarios**:

1. **Given** a build with debug logging enabled (default), **When** the game runs, **Then** clog UDP broadcasts are sent and receivable via `socat UDP-RECV:7355 -` (same as current behavior).
2. **Given** a build with debug logging disabled, **When** the game runs, **Then** no clog UDP broadcasts are sent and no string formatting overhead is incurred.
3. **Given** both debug and release builds, **When** comparing FPS on the Mac SE, **Then** the release build shows measurable improvement (even a few percent matters at 10fps).
4. **Given** the CMake build system, **When** a developer configures a build, **Then** debug logging is ON by default and can be disabled via a single CMake option.

---

### Edge Cases

- What happens when a player is moving and a bomb explodes on a tile they are partially overlapping? The player dies immediately upon explosion activation if any overlap exists.
- What happens when two players collide at sub-tile positions? Players pass through each other (standard Bomberman behavior).
- What happens on the Mac SE where frames can be ~6 ticks apart? Large pixel jumps per frame (e.g., 12 pixels at speed 2 px/tick) must still collide correctly with walls. Collision checks must test against all tiles between the old and new position.
- What happens when a network position update places a remote player inside a wall? Accept the position as-is (network state is authoritative per constitution principle VIII). The remote machine is responsible for its own collision.
- What happens when the player holds two arrow keys simultaneously (e.g., up and right)? Only one direction is processed per frame (same as current behavior). Priority order matches current implementation.
- What happens when a player disconnects while their sprite is between tiles (sub-tile position)? All tiles overlapped by their bounding box are marked dirty before deactivation, ensuring complete visual cleanup.
- What happens to a disconnected player's active bombs? Bombs continue their fuse timer and explode normally. The ownerID on the bomb is still valid for kill attribution.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Player positions MUST be stored as pixel coordinates (pixelX, pixelY) as the authoritative position, with grid coordinates (gridCol, gridRow) derived using the player's center point: gridCol = (pixelX + tileSize/2) / tileSize. This ensures bomb placement and game logic reference the tile the player is mostly on, matching standard Bomberman behavior.
- **FR-002**: Player movement MUST be continuous, advancing pixelX/pixelY by a speed value multiplied by elapsed ticks (deltaTicks) each frame in the held direction.
- **FR-003**: Movement speed MUST be resolution-independent, expressed as ticks required to cross one tile. A player on the Mac SE (16px tiles) and a player on the 6400 (32px tiles) with the same speed setting MUST cross one tile in the same real-world time.
- **FR-004**: Player collision with walls and blocks MUST use axis-separated bounding box checks against the tilemap. Movement along each axis (X then Y) MUST be resolved independently.
- **FR-005**: The player collision hitbox MUST be slightly smaller than tile size (inset by a configurable number of pixels) to allow forgiving near-misses with explosions and easier corridor navigation.
- **FR-006**: Explosion kill detection MUST use bounding box overlap between the player hitbox and explosion tile rects. Any overlap (even 1 pixel after hitbox inset) results in death.
- **FR-007**: When a player places a bomb on their current tile, the player MUST be able to walk off that bomb tile. Once the player's hitbox fully leaves the bomb tile, the bomb MUST become solid and block re-entry.
- **FR-008**: Wall collision MUST handle large per-frame pixel jumps (on slow machines with high deltaTicks) by checking all tiles between the old and new position, not just the destination tile.
- **FR-009**: The network position message MUST transmit pixel coordinates as short integers (2 bytes each) instead of grid coordinates as single bytes. The message MUST include playerID, pixelX, pixelY, and facing direction.
- **FR-010**: The network protocol version MUST be bumped to version 3. Clients with mismatched protocol versions MUST display a warning in the lobby (existing v2 behavior preserved).
- **FR-011**: Remote player positions MUST be interpolated toward received network positions over multiple frames rather than snapping instantly, to smooth out network update frequency differences.
- **FR-012**: Corner sliding MUST automatically nudge a player into corridor alignment when the player is within a configurable pixel threshold of the corridor opening, allowing smooth turns.
- **FR-013**: The dirty rectangle system MUST mark all tiles overlapped by the player's bounding box as dirty (up to 4 tiles when straddling a corner), both before and after each movement update.
- **FR-014**: All movement, collision, and timing logic MUST use tick-based calculations (deltaTicks) to ensure consistent behavior across machines running at different frame rates (Mac SE ~10fps to 6400 ~26fps).
- **FR-015**: All code MUST be C89/C90 compliant with no mixed declarations, VLAs, or C99 features.
- **FR-016**: Players MUST pass through each other (no player-to-player collision), consistent with standard Bomberman behavior.
- **FR-017**: When a player disconnects, all tiles overlapped by their bounding box MUST be marked dirty before the player is deactivated, ensuring their sprite is erased from the screen within one frame.
- **FR-018**: When a player disconnects during gameplay, the disconnected player MUST be removed from the game visually and logically. Active bombs placed by the disconnected player MUST continue their fuse timers and explode normally.
- **FR-019**: A compile-time CMake option (BOMBERTALK_DEBUG, default ON) MUST control whether clog logging calls are compiled into the binary. When OFF, all CLOG_* macro invocations MUST compile to nothing (zero runtime cost).
- **FR-020**: The build system MUST support building both debug and release variants per platform target. Debug builds include clog logging; release builds exclude it entirely.

### Key Entities

- **Player Position**: Authoritative pixel coordinates (pixelX, pixelY), derived grid coordinates (gridCol, gridRow), facing direction, movement speed (ticks per tile).
- **Player Hitbox**: A rectangle inset from the full tile-size rect at the player's pixel position. Used for wall collision and explosion overlap detection.
- **Bomb Pass-Through State**: Per-player tracking of which bomb (if any) the player is currently allowed to walk through because they placed it while standing on that tile.
- **Network Position Message**: PlayerID, pixel X, pixel Y, facing direction. Sent via unreliable UDP every frame the player moves.
- **Interpolation State**: Per-remote-player target position received from network, used to smoothly lerp the displayed position.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Players visually slide between tiles over multiple frames on all three target machines, with no frame showing an instantaneous tile-to-tile snap during normal movement.
- **SC-002**: A player moving continuously in one direction for 3 seconds travels the same number of tiles (within 1 tile tolerance) on the Mac SE and the Performa 6400, confirming tick-based speed consistency.
- **SC-003**: A player partially overlapping an explosion tile (by at least 1 pixel beyond the hitbox inset) is killed, while a player fully outside all explosion tiles survives.
- **SC-004**: After placing a bomb, a player can walk away from the bomb tile in any unobstructed direction without being blocked by their own bomb.
- **SC-005**: Remote players appear to move smoothly on the observing machine rather than jumping between positions, with interpolation visually smoothing network update gaps.
- **SC-006**: All three builds (68k MacTCP, PPC MacTCP, PPC Open Transport) compile cleanly and run the smooth movement system without crashes or visual artifacts.
- **SC-007**: The Mac SE maintains its current gameplay frame rate (~10fps) with the new movement system, with no measurable regression from the additional collision calculations.
- **SC-008**: When nearly aligned with a corridor opening (within the nudge threshold), a player can smoothly turn into the corridor without getting stuck on tile corners.
- **SC-009**: When a player disconnects during gameplay, their sprite disappears from all remaining players' screens within one frame, with no ghost sprites remaining at the disconnected player's last position.
- **SC-010**: Release builds (debug logging disabled) show measurable FPS improvement on the Mac SE compared to debug builds, confirming the logging overhead is eliminated.

## Clarifications

### Session 2026-04-10

- Q: How are grid coordinates derived from pixel position (top-left corner vs center point)? -> A: Center point: gridCol = (pixelX + tileSize/2) / tileSize. Standard Bomberman behavior — bomb placement targets the tile the player is mostly on.

## Assumptions

- Player-to-player collision is not implemented (players pass through each other), consistent with standard Bomberman and current BomberTalk behavior.
- The hitbox inset value will be tuned through playtesting. An initial value of 2 pixels on 16px tiles (Mac SE) and 4 pixels on 32px tiles (color Macs) is a reasonable starting point.
- Corner sliding nudge threshold will similarly be tuned. Initial value of 6 pixels on 32px tiles and 3 pixels on 16px tiles.
- Network position updates continue to be sent every frame the player moves (same frequency as current implementation), just with larger payload.
- Remote player interpolation speed will be tuned to balance smoothness vs responsiveness. A reasonable default is to lerp toward the target position at a rate that closes 50% of the gap per frame.
- Movement speed will initially match the current effective speed: crossing one tile in ~12 ticks (~200ms), matching the current MOVE_COOLDOWN_TICKS value.
- Only one direction of movement is processed per frame (no diagonal movement), matching current behavior and standard Bomberman.
