# Research: Smooth Sub-Tile Player Movement

**Feature**: 004-smooth-movement
**Date**: 2026-04-10

## R1: Resolution-Independent Speed Model

**Decision**: Express speed as `ticksPerTile` (ticks to cross one tile). Derive `pixelsPerTick = tileSize / ticksPerTile` at init time. Advance position by `pixelsPerTick * deltaTicks` each frame.

**Rationale**: Using ticks-per-tile as the base unit means the same speed constant produces identical real-world crossing times regardless of tile size (16px on SE vs 32px on color). Integer division at init avoids per-frame division. The current MOVE_COOLDOWN_TICKS of 12 maps directly: `ticksPerTile = 12` means a player crosses one tile in ~200ms.

**Alternatives considered**:
- Pixels-per-tick as base: Requires different constants for 16px vs 32px tiles. Rejected — violates resolution independence.
- Fixed-point fractional speed: Adds complexity for C89 (no native fixed-point). Not needed unless sub-pixel precision matters. Rejected for now — integer math is sufficient. Can revisit if speed tuning demands finer granularity.

**Residual concern**: At `ticksPerTile=12` and `tileSize=16`, `pixelsPerTick = 16/12 = 1` (integer truncation). Actual crossing time becomes 16 ticks (~267ms) instead of 12 ticks (~200ms). For `tileSize=32`: `pixelsPerTick = 32/12 = 2`, crossing takes 16 ticks (~267ms). Both platforms match (16 ticks) but are slower than ideal. Mitigation: use `ticksPerTile=16` as the base (exact division for both tile sizes) giving ~267ms crossing time, or use a fractional accumulator: `accumPixels += tileSize * deltaTicks; pixels = accumPixels / ticksPerTile; accumPixels %= ticksPerTile`. The fractional accumulator approach is recommended — it's 2 extra integer operations per frame but gives exact speed regardless of tile size.

## R2: AABB Tilemap Collision Approach

**Decision**: Axis-separated collision with clamping. Move X first, check tilemap, clamp. Then move Y, check tilemap, clamp. Player hitbox is `(pixelX + inset, pixelY + inset)` to `(pixelX + tileSize - inset, pixelY + tileSize - inset)`.

**Rationale**: Axis separation is the standard approach from Mac Game Programming (2002) Ch. 9 for tile-based games. It prevents diagonal corner-cutting glitches where moving diagonally could skip through a wall corner. Resolving X and Y independently makes clamping straightforward — just set pixelX or pixelY to the tile boundary.

**Alternatives considered**:
- Combined XY check: Simpler but allows corner-cutting through diagonal walls. Rejected.
- Swept AABB (continuous collision): Handles tunneling through thin walls at high speed. Overkill — walls are full tiles, minimum 16px wide. Even at max deltaTicks=10 and speed 2px/tick, movement is 20px — cannot skip a 16px wall. Rejected.

**Hitbox inset values**: 2px for 16px tiles (12.5%), 4px for 32px tiles (12.5%). This means the player's collision box is 12x12 on SE and 24x24 on color Macs. Provides ~2-4px of forgiveness when squeezing past explosion edges.

## R3: Explosion AABB Overlap Detection

**Decision**: Replace `gridCol == explosion.col` check with standard AABB rect overlap test between player hitbox and explosion tile rect.

**Rationale**: The player hitbox (with inset) overlaps explosion tile rects at pixel granularity. This is O(players * active_explosions) per frame — with MAX_PLAYERS=4 and typical explosion count ~5-15, that's 20-60 integer comparisons. Negligible.

**AABB overlap test** (standard):
```
overlap = !(playerRight <= explosionLeft ||
            playerLeft >= explosionRight ||
            playerBottom <= explosionTop ||
            playerTop >= explosionBottom)
```

**Alternatives considered**:
- Grid-only check using derived gridCol/gridRow: Simpler but defeats the purpose of smooth movement. Player in the middle of a tile gets the same behavior as current system. Rejected.
- Per-pixel bitmask collision: Way too expensive for 68000. Rejected.

## R4: Bomb Walk-Off (Pass-Through) Model

**Decision**: Per-player `passThroughBombIdx` field (short, -1 = none). Set to the bomb array index when player places a bomb. Cleared when player hitbox no longer overlaps the bomb tile. After clearing, bomb blocks normally via AABB.

**Rationale**: Single index per player is sufficient because a player can only stand on one bomb at a time (they can only place one bomb at their current position). The bomb index directly references `gGame.bombs[]` for O(1) lookup. Using index rather than grid position handles the case where a bomb at the same position is placed by a different player.

**Alternatives considered**:
- Flag on the Bomb struct: Would need per-player flags on each bomb (4 bits). More complex and wastes space on bombs that don't need it. Rejected.
- Grid-position based tracking: Track the (col, row) of the pass-through bomb. Simpler but doesn't distinguish between bombs if two players place on the same tile simultaneously (unlikely but possible in Bomberman). Rejected in favor of index.

## R5: Corner Sliding Implementation

**Decision**: When the player presses a perpendicular direction and is blocked, check if the player's position along the movement axis is within `nudgeThreshold` pixels of alignment with the nearest corridor opening. If so, nudge position toward alignment by `pixelsPerTick * deltaTicks` (same speed as normal movement).

**Rationale**: This is the standard Bomberman corner-assist. The nudge uses movement speed so it doesn't feel instantaneous or jarring. The threshold should be ~1/3 of a tile (5px for 16px tiles, 10px for 32px tiles) — wide enough to catch most near-misses, narrow enough that players don't slide unexpectedly.

**Algorithm**:
1. Player presses UP while moving RIGHT
2. Calculate alignment: `offset = pixelX % tileSize`
3. If `offset != 0` (not aligned) AND `offset < nudgeThreshold`: nudge pixelX toward `pixelX - offset` (round down to tile boundary)
4. If `offset != 0` AND `(tileSize - offset) < nudgeThreshold`: nudge pixelX toward `pixelX + (tileSize - offset)` (round up)
5. Check if the tile in the desired direction (UP) is passable at the nudged position
6. If passable: apply nudge and begin moving in new direction
7. If not passable: block (no corridor there even after alignment)

**Alternatives considered**:
- Instant snap to alignment: Feels jerky, especially on Mac SE at 10fps. Rejected.
- No corner sliding: Playable but frustrating. Deferred to P3 priority but recommended.

## R6: Network Protocol v3 Design

**Decision**: Expand MsgPosition from 5 bytes to 8 bytes. Replace `unsigned char gridCol/gridRow` with `short pixelX/pixelY` (2 bytes each, big-endian native). Keep playerID and facing as unsigned char. Drop animFrame (unused). Add 1 padding byte for alignment.

**Rationale**: Classic Mac is big-endian on both 68k and PPC, so no byte-swapping needed between machines. 8 bytes per position update at ~10-26fps = 80-208 bytes/sec per player — trivial for even LocalTalk bandwidth. The protocol version bump to 3 triggers the existing mismatch warning in lobby for v2 clients.

**Message layout**:
```
Offset  Size  Field
0       1     playerID (unsigned char)
1       1     facing (unsigned char)
2       2     pixelX (short, big-endian)
4       2     pixelY (short, big-endian)
6       2     padding/reserved
Total: 8 bytes
```

**Alternatives considered**:
- Keep grid coords + add sub-tile offset byte: Saves 1 byte but adds complexity (offset 0-31 within tile). More error-prone. Rejected.
- Variable-length encoding: Unnecessary complexity for 3-byte savings. Rejected.

## R7: Remote Player Interpolation

**Decision**: Store `targetPixelX/targetPixelY` from network updates. Each frame, lerp displayed position toward target: `displayX += (targetX - displayX) * deltaTicks / INTERP_TICKS`. Use `INTERP_TICKS = 4` (~67ms) as interpolation half-life.

**Rationale**: Linear interpolation with tick-based rate produces smooth movement on all platforms. At 26fps (6400), each frame moves ~60% of remaining distance. At 10fps (SE), each frame moves ~100% — effectively snapping, which is fine because at 10fps there's less visual difference between snap and lerp anyway.

**Alternatives considered**:
- No interpolation (snap to received position): Acceptable for grid-based but looks jerky with pixel positions, especially on 6400 at 26fps where position updates arrive every 2-4 frames. Rejected.
- Extrapolation (predict future position based on velocity): Requires velocity tracking, introduces rubber-banding on direction changes. Too complex for the benefit. Rejected.
- Fixed pixel-per-frame lerp: Frame-rate dependent. Rejected per constitution principle VII (tick-based everything).

## R8: Disconnect Ghost Sprite Fix

**Decision**: In `on_disconnected`, before setting `active = FALSE`, mark all tiles overlapped by the player's bounding box as dirty via `Renderer_MarkDirty`. With smooth movement, this may be up to 4 tiles (player straddling a corner).

**Rationale**: The current bug occurs because `active = FALSE` causes the dirty rect loop in `screen_game.c` to skip the player, so their last rendered position is never redrawn from the background buffer. The fix is to mark dirty BEFORE deactivation. This is a 2-4 line change in `net.c:on_disconnected`.

**Alternatives considered**:
- Mark all dirty on disconnect (full screen redraw): Works but wasteful. A full rebuild on every disconnect is unnecessary when we know exactly which tiles need refreshing. Rejected.
- Keep tracking inactive player positions for one extra frame: More complex, requires state machine. Rejected.

## R9: CopyBits Alignment for Non-Tile-Aligned Sprites

**Decision**: Accept the misalignment penalty for sprite CopyBits at sub-tile positions. Do NOT try to align player sprite destination rects — aligning would expand the drawn area and cause visual artifacts (background bleed) or require masking complexity.

**Rationale**: Sex, Lies and Video Games (1996) pp.143-148 documents that long-word aligned CopyBits is nearly 2x faster (58 vs 36 ticks for their test). The existing renderer uses `AlignRect32()` for background restore (tile-to-tile CopyBits), which is always tile-aligned. However, `Renderer_DrawPlayer` draws sprites at exact pixel positions. With smooth movement, player pixelX/pixelY will rarely be tile-aligned, meaning sprite CopyBits calls will be misaligned most of the time.

**Impact analysis**: The penalty applies to the 4 player sprite draws per frame (MAX_PLAYERS=4). On the Mac SE (1-bit monochrome), PaintRect is used instead of CopyBits for players — no alignment penalty. On color Macs, the transparent-mode CopyBits for PICT sprites is already non-srcCopy (uses transparent transfer mode), which the books note is slower regardless of alignment ("avoid transfer modes — they are evil"). So the misalignment penalty on top of the transfer mode penalty is relatively smaller.

**Mitigation**: If performance testing reveals measurable regression on PPC Macs, consider pre-shifting sprite GWorlds to match common sub-tile offsets (0, 8, 16, 24 for 32px tiles = 4 copies). This is a classic Bomberman engine optimization but adds memory cost (4x sprite memory). Defer unless hardware testing shows a problem.

**Book references**:
- Sex, Lies and Video Games (1996): "Long word aligned test dropped from 58 to 36 ticks. Almost doubling the speed of CopyBits" (p.148)
- Sex, Lies and Video Games (1996): "Make sure your pixel data is long word aligned. You'll have to look hard to find an optimization that almost doubles CopyBits' speed" (p.148)
- Sex, Lies and Video Games (1996): "Avoid transfer modes — they are evil" (p.150)
- Macintosh Game Animation (1985): Fast-moving sprites can jump over collision targets; check intermediate positions

## R10: Compile-Time Debug Toggle

**Decision**: CMake option `BOMBERTALK_DEBUG` (default ON). When OFF, define `CLOG_STRIP` preprocessor macro that causes all `CLOG_*` macros to expand to `((void)0)`. Also skip `clog_init()`, `clog_set_file()`, and `clog_set_network_sink()` calls. Do not link clog library in release builds.

**Rationale**: There are 103 clog call sites across 12 source files. Each call formats a string (sprintf-style) and potentially sends a UDP packet via `udp_log_sink`. On the Mac SE at 8MHz, string formatting is expensive — even if the UDP send is fast, the formatting of ~100 messages per game loop iteration (across all subsystems) adds measurable overhead. A compile-time toggle eliminates all of this at zero runtime cost, unlike a runtime flag which still pays the string formatting cost before checking the flag.

**Implementation approach**: clog supports a `CLOG_STRIP` define (in clog.h) that makes all macros expand to `((void)0)`. The CMake option just needs to:
1. Add `-DCLOG_STRIP` to compiler flags when BOMBERTALK_DEBUG=OFF
2. Guard `clog_init`/`clog_set_file`/`clog_set_network_sink` calls with `#ifndef CLOG_STRIP`
3. Optionally skip linking libclog.a (saves a few KB in the binary)

**Alternatives considered**:
- Runtime toggle via menu option: Still pays string formatting cost. On Mac SE, checking a global flag 100x per frame is cheap but formatting strings is not. Rejected for performance-critical path.
- Log level filtering (CLOG_LVL_NONE): clog supports log levels, but the macro still evaluates the format string before checking the level. Only truly zero-cost with compile-time elimination. Rejected as primary mechanism.
- Separate debug/release build directories by default: Could build both automatically (`build-68k-debug`, `build-68k-release`, etc.) but doubles build time. Better to let the developer choose. Rejected as default, but documented as an option.
