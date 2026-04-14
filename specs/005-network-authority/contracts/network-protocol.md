# Network Protocol Contract: v5 Authority Changes

**Feature**: 005-network-authority
**Date**: 2026-04-14
**Protocol Version**: 5 (was 4)

## Wire Format

No wire format changes. All message structs remain identical to v4. The version bump reflects behavioral changes only.

## Behavioral Changes

### MSG_BOMB_EXPLODE (0x03) — Authority: Bomb Owner + Chain Cascade

**v4 behavior**: All machines broadcast when any local fuse expires.

**v5 behavior**:
- **Fuse expiry**: Only the machine where `localPlayerID == bomb.ownerID` broadcasts.
- **Chain explosion**: Not currently implemented (raycast checks walls/blocks only, not bombs). If added in the future, the chain-triggering machine must broadcast all chained explosions regardless of bomb ownership.
- **Force-explode (receive path)**: Unchanged. Receiving machine calls `Bomb_ForceExplodeAt()` with `broadcast=FALSE`.
- **Owner disconnected**: Remaining machines explode locally with `broadcast=FALSE`. No network message sent. All machines compute identical explosions from their local bomb state.

**Compatibility**: v4 clients still broadcast all explosions. v5 clients deduplicate via existing `Bomb_ForceExplodeAt()` (bomb already exploded locally → no-op). Safe to mix.

### MSG_BLOCK_DESTROYED (0x04) — Authority: Follows Parent Explosion

**v4 behavior**: All machines broadcast for every block destroyed.

**v5 behavior**: Only the machine that broadcasts MSG_BOMB_EXPLODE sends the associated MSG_BLOCK_DESTROYED messages. The `broadcast` flag in `ExplodeBomb()` already gates both.

**Compatibility**: v4 clients still broadcast all. v5 clients deduplicate via existing `TileMap_GetTile() == TILE_FLOOR` check. Safe to mix.

### MSG_GAME_OVER (0x07) — Authority: Lowest Rank Connected

**v4 behavior**: All machines broadcast when game-over detected.

**v5 behavior**:
- Only the lowest-rank connected machine (via `PT_GetPeerRank()`) sends MSG_GAME_OVER.
- Non-authority machines detect game over locally but do not send immediately.
- Failsafe: if non-authority machine detects game over and receives no MSG_GAME_OVER within 60 ticks (~1s), it sends as backup.
- Authority fallback: if the lowest-rank machine has disconnected, the next-lowest-rank takes over.

**Compatibility**: v4 clients still broadcast all. v5 clients already use `pendingGameOver` / screen check to ignore duplicates. Safe to mix.

## Shutdown Coordination

**v5 addition**: After game-over processing completes (death animations done or timeout), all machines observe a 30-tick (~0.5s) grace period before calling `Net_DisconnectAllPeers()`. During the grace period, `Net_Poll()` continues to run, allowing final TCP delivery.

**Purpose**: Ensures the Mac SE (slowest machine) receives MSG_GAME_OVER before TCP teardown. Changes disconnect reason from 2 (connection lost) to 0 (clean) on slow machines.

## Mesh Formation

**v5 addition**: After receiving MSG_GAME_START, machines delay their first TCP connect attempt by `rank * 30 ticks` (~0.5s per rank). Rank 0 (lowest IP, typically the Mac SE) connects immediately. Higher-rank machines wait, giving slow machines time to prepare their TCP listener.

**Existing safety net**: The 2-second mesh retry and 15-second mesh timeout remain unchanged.

## Message Summary (v5)

| Message              | Transport    | Authority (v5)                  | Dedup Mechanism                   |
|----------------------|-------------|--------------------------------|-----------------------------------|
| MSG_POSITION (0x01)  | PT_FAST     | Local player only (unchanged)  | playerID check                    |
| MSG_BOMB_PLACED (0x02)| PT_RELIABLE | Placer only (unchanged)        | Bomb_ExistsAt() grid check        |
| MSG_BOMB_EXPLODE (0x03)| PT_RELIABLE | Bomb owner + chain cascade   | Bomb_ForceExplodeAt() no-op       |
| MSG_BLOCK_DESTROYED (0x04)| PT_RELIABLE | Follows parent explosion   | TileMap_GetTile() == TILE_FLOOR   |
| MSG_PLAYER_KILLED (0x05)| PT_RELIABLE | Victim's machine (unchanged) | deathTimer already set            |
| MSG_GAME_START (0x06)| PT_RELIABLE | Initiator only (unchanged)     | gameStartReceived flag            |
| MSG_GAME_OVER (0x07) | PT_RELIABLE | Lowest rank connected          | pendingGameOver / screen check    |
