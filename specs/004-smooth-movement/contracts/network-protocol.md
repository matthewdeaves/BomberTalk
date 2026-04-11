# Network Protocol Contract: v3 (Smooth Movement)

**Feature**: 004-smooth-movement
**Date**: 2026-04-10

## Protocol Version

- **BT_PROTOCOL_VERSION**: 3 (was 2)
- Sent in MSG_GAME_START payload
- Receivers reject mismatches and show warning in lobby (existing v2 behavior preserved)

## MSG_POSITION (PT_FAST / UDP)

### v3 Layout (8 bytes)

| Offset | Size | Type           | Field    | Description                          |
|--------|------|----------------|----------|--------------------------------------|
| 0      | 1    | unsigned char  | playerID | Sender's player index (0-3)         |
| 1      | 1    | unsigned char  | facing   | Direction enum (DIR_UP/DOWN/LEFT/RIGHT) |
| 2      | 2    | short          | pixelX   | Player X position in pixels (big-endian) |
| 4      | 2    | short          | pixelY   | Player Y position in pixels (big-endian) |
| 6      | 2    | unsigned char[2] | pad    | Reserved, set to 0                   |

### Behavioral Contract

- **Send frequency**: Every frame the local player's position changes (same as v2)
- **Transport**: PT_FAST (unreliable UDP broadcast)
- **Byte order**: Big-endian native (both 68k and PPC) — no conversion needed between Classic Mac peers
- **Range**: pixelX: 0 to `TileMap_GetCols() * tileSize`, pixelY: 0 to `TileMap_GetRows() * tileSize`
- **Receiver behavior**: Set `targetPixelX`/`targetPixelY` on the remote player. Do NOT set `pixelX`/`pixelY` directly — the interpolation system advances the display position each frame.

### v2 Compatibility

- v2 clients send 5-byte MsgPosition with `unsigned char gridCol/gridRow`
- v3 clients receiving a v2 message: `len < sizeof(MsgPosition_v3)` — silently drop (protocol mismatch already warned in lobby)
- v2 clients receiving a v3 message: `len > sizeof(MsgPosition_v2)` — they read only the first 5 bytes, interpreting pixelX high byte as gridCol. This produces garbage coordinates. The lobby protocol version warning should prevent mixed games.

## MSG_BOMB_PLACE (PT_RELIABLE / TCP)

No structural changes. Bomb placement still sends grid coordinates:

| Offset | Size | Type          | Field    | Description                |
|--------|------|---------------|----------|----------------------------|
| 0      | 1    | unsigned char | playerID | Who placed the bomb        |
| 1      | 1    | unsigned char | gridCol  | Tile column (derived from center of player) |
| 2      | 1    | unsigned char | gridRow  | Tile row (derived from center of player)    |
| 3      | 1    | unsigned char | range    | Explosion range            |

**Change**: The gridCol/gridRow values are now derived from pixel position using center-point formula: `gridCol = (pixelX + tileSize/2) / tileSize`.

## All Other Messages

No changes to MSG_GAME_START, MSG_BOMB_EXPLODE, MSG_BLOCK_DESTROYED, MSG_PLAYER_KILLED, MSG_GAME_OVER. These all use grid coordinates or player IDs which remain unchanged.
