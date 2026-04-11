# Movement API Contract

**Feature**: 004-smooth-movement
**Date**: 2026-04-10

## Player Module (player.h / player.c)

### Modified Functions

#### Player_Init(playerID, spawnCol, spawnRow)
- **Change**: Sets pixelX/pixelY as authoritative from spawn grid position. Derives gridCol/gridRow. Initializes accumX/accumY to 0, passThroughBombIdx to -1, targetPixelX/targetPixelY to match pixelX/pixelY.
- **Spawn alignment**: `pixelX = spawnCol * tileSize`, `pixelY = spawnRow * tileSize` (top-left of tile, player is tile-aligned at spawn).

#### Player_Update(playerID)
- **Change**: Complete rewrite of movement logic.
- **Old behavior**: Grid-locked movement with cooldown timer. Snap to adjacent tile.
- **New behavior**:
  1. If remote player: interpolate pixelX/pixelY toward targetPixelX/targetPixelY, derive gridCol/gridRow, return.
  2. Read input (IsKeyDown + WasKeyPressed, same priority order as current).
  3. Compute pixel delta: `delta = tileSize * deltaTicks`. Accumulate: `accumX += delta` (or accumY). Extract whole pixels: `movePixels = accumX / ticksPerTile; accumX %= ticksPerTile`.
  4. Move X axis: `newPixelX = pixelX + movePixels * dirX`. Check AABB collision with tilemap. Clamp to tile boundary if blocked.
  5. Move Y axis: same as X.
  6. Apply corner sliding if direction changed and blocked.
  7. Update bomb pass-through state.
  8. Derive gridCol/gridRow from center point.
  9. If position changed: call Net_SendPosition with pixel coords.

#### Player_SetPosition(playerID, pixelX, pixelY, facing)
- **Change**: Parameters change from (col, row, facing) to (pixelX, pixelY, facing).
- **Behavior**: Sets targetPixelX/targetPixelY for remote players. Derives gridCol/gridRow.
- **Called by**: on_position network handler.

### New Functions

#### Player_GetHitbox(playerID, outRect)
- **Purpose**: Returns the player's collision hitbox rect (inset from full tile rect).
- **Output**: `outRect->left = pixelX + inset`, `outRect->top = pixelY + inset`, `outRect->right = pixelX + tileSize - inset`, `outRect->bottom = pixelY + tileSize - inset`.
- **Used by**: Collision checks in Player_Update, explosion kill check in Bomb.

#### Player_MarkDirtyTiles(playerID)
- **Purpose**: Marks all tiles overlapped by the player's bounding box as dirty in the renderer.
- **Behavior**: Computes min/max tile columns and rows from pixelX/pixelY, calls Renderer_MarkDirty for each (1-4 tiles).
- **Used by**: screen_game.c dirty rect marking, on_disconnected cleanup.

## Bomb Module (bomb.h / bomb.c)

### Modified Functions

#### Bomb_ExistsAt — usage change
- **Current**: Called with grid coords to check if bomb blocks movement. Returns 0 or 1 from gBombGrid.
- **New usage**: Still grid-based, but called for each tile the player hitbox overlaps. Caller (Player_Update) iterates tiles covered by proposed AABB and checks each.

#### ExplodeBomb (static, bomb.c) — kill check change
- **Current**: `pl->gridCol == explosion.col && pl->gridRow == explosion.row`
- **New**: AABB overlap between Player_GetHitbox result and explosion tile rect `(col*ts, row*ts, (col+1)*ts, (row+1)*ts)`.

### New Functions

#### Bomb_SetPassThrough(playerID, bombIdx)
- **Purpose**: Record that playerID can walk through bomb at index bombIdx.
- **Called by**: Player_Update after successful Bomb_PlaceAt.

#### Bomb_ClearPassThrough(playerID)
- **Purpose**: Clear pass-through state when player fully leaves the bomb tile.
- **Called by**: Player_Update each frame when passThroughBombIdx != -1.

#### Bomb_IsPassThrough(playerID, col, row)
- **Purpose**: Returns TRUE if the bomb at (col, row) is the player's current pass-through bomb.
- **Used by**: Player_Update collision check to skip the bomb the player is walking off of.

## Renderer Module (renderer.h / renderer.c)

### Modified Functions

#### Renderer_DrawPlayer(playerID, pixelX, pixelY, facing)
- **Change**: Parameters change from (playerID, col, row, facing) to (playerID, pixelX, pixelY, facing).
- **Behavior**: `SetRect(&dstRect, pixelX, pixelY, pixelX + tileSize, pixelY + tileSize)` instead of grid-to-pixel conversion.

## Network Module (net.c)

### Modified Functions

#### Net_SendPosition(pixelX, pixelY, facing)
- **Change**: Parameters change from (col, row, facing) to (pixelX, pixelY, facing).
- **Behavior**: Packs short pixelX/pixelY into MsgPosition v3. Broadcasts via PT_FAST.

#### on_position (static callback)
- **Change**: Unpacks short pixelX/pixelY from MsgPosition v3. Calls Player_SetPosition with pixel coords. Does NOT directly set player pixelX/pixelY — sets targets for interpolation.

#### on_disconnected (static callback)
- **Change**: Before setting active=FALSE, calls Player_MarkDirtyTiles to mark the disconnected player's tiles dirty for cleanup.
