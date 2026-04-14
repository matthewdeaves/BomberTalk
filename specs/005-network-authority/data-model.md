# Data Model: Network Authority & Robustness Improvements

**Feature**: 005-network-authority
**Date**: 2026-04-14

## Modified Data Structures

### GameState Struct (game.h)

**New fields**:
- `disconnectGraceTimer` (short): Ticks remaining before TCP teardown after game over. 0 = not in grace period. Set to 30 when game-over transition begins. Decremented by deltaTicks each frame.
- `meshStaggerTimer` (short): Ticks remaining before first TCP connect attempt after MSG_GAME_START. 0 = ready to connect. Set to `rank * 30` on game start received.
- `gameOverAuthority` (int): TRUE if this machine should broadcast MSG_GAME_OVER. Computed when game-over condition is detected. Based on `Net_IsLowestRankConnected()`.
- `localGameOverDetected` (int): TRUE if this machine detected game over locally but is not the authority. Used with `gameOverFailsafeTimer` for authority disconnect fallback.
- `gameOverFailsafeTimer` (short): Ticks remaining before non-authority machine sends MSG_GAME_OVER as failsafe. Set to 60 when local game over detected by non-authority. 0 = not active.
- `heapCheckTimer` (long): Ticks since last heap check. Reset to 0 after each check. Checked against 1800 (~30 seconds).

**Memory impact**: 12 bytes added to GameState (single global struct). Negligible.

### Protocol Version Constant (game.h)

- `BT_PROTOCOL_VERSION`: Changed from 4 to 5.

### New Constants (game.h)

```
DISCONNECT_GRACE_TICKS      30    /* ~0.5s grace period before TCP teardown */
MESH_STAGGER_PER_RANK       30    /* ~0.5s per rank before first connect */
GAME_OVER_FAILSAFE_TICKS    60    /* ~1s timeout before non-authority sends */
LOW_HEAP_WARNING_BYTES  262144    /* 256KB threshold for heap warning */
HEAP_CHECK_INTERVAL_TICKS 1800    /* ~30s between periodic heap checks */
```

## Modified Behavior (No Struct Changes)

### Bomb Struct

No field changes. The existing `ownerID` field is used to determine broadcast authority in `ExplodeBomb()`. The existing `broadcast` parameter to `ExplodeBomb()` already controls whether network messages are sent.

### ExplodeBomb Broadcast Logic

**Current**: `Bomb_Update()` calls `ExplodeBomb(bomb, TRUE)` for all fuse expirations.

**New**: `Bomb_Update()` calls `ExplodeBomb(bomb, localPlayerID == bomb.ownerID)` for fuse expirations. `Bomb_ForceExplodeAt()` continues to pass `broadcast=FALSE` (unchanged). Note: chain explosions (bomb A's raycast triggering bomb B) are not currently implemented — the raycast only checks for walls and blocks. If chains are added later, the broadcast flag must propagate from the parent explosion.

**Block destruction**: `Net_SendBlockDestroyed()` inside ExplodeBomb is already gated by the `broadcast` parameter. No change needed — the existing code path is correct.

### MSG_GAME_OVER Broadcast Logic

**Current**: Every machine that detects game over calls `Net_SendGameOver()`.

**New**: Only `Net_IsLowestRankConnected()` machine calls `Net_SendGameOver()`. Other machines set `localGameOverDetected = TRUE` and start `gameOverFailsafeTimer = GAME_OVER_FAILSAFE_TICKS`. If the timer expires without receiving a network MSG_GAME_OVER, the non-authority machine sends as failsafe.

## New Functions

### net.c

- `Net_IsLowestRankConnected()` (int): Returns TRUE if the local machine has the lowest rank among all currently connected peers. Uses `PT_GetPeerRank(ctx, NULL)` for local rank and iterates connected peers to find minimum. Returns TRUE if no peers are connected (single player / all disconnected fallback).

## State Transitions

### Game-Over Shutdown Sequence

**Current**:
```
Game over detected
    → Net_SendGameOver(winner)
    → Net_StopDiscovery()
    → Net_DisconnectAllPeers()
    → Screens_TransitionTo(SCREEN_LOBBY)
```

**New (authority machine)**:
```
Game over detected, Net_IsLowestRankConnected() == TRUE
    → Net_SendGameOver(winner)
    → gGame.gameRunning = FALSE
    → gGame.disconnectGraceTimer = DISCONNECT_GRACE_TICKS
    → (continue main loop, polling network during grace period)
    → disconnectGraceTimer expires
    → Net_StopDiscovery()
    → Net_DisconnectAllPeers()
    → Screens_TransitionTo(SCREEN_LOBBY)
```

**New (non-authority machine)**:
```
Game over detected, Net_IsLowestRankConnected() == FALSE
    → gGame.localGameOverDetected = TRUE
    → gGame.gameOverFailsafeTimer = GAME_OVER_FAILSAFE_TICKS
    → (wait for MSG_GAME_OVER from authority)
    → MSG_GAME_OVER received OR failsafe timer expires
    → same grace period path as authority
```

### Pending Game Over (Receiver Path)

**Current**:
```
MSG_GAME_OVER received
    → pendingGameOver = TRUE
    → wait for death anims or timeout (3s)
    → Net_StopDiscovery()
    → Net_DisconnectAllPeers()
    → Screens_TransitionTo(SCREEN_LOBBY)
```

**New**:
```
MSG_GAME_OVER received
    → pendingGameOver = TRUE
    → localGameOverDetected = FALSE (cancel failsafe if active)
    → wait for death anims or timeout (3s)
    → disconnectGraceTimer = DISCONNECT_GRACE_TICKS
    → (continue polling during grace period)
    → disconnectGraceTimer expires
    → Net_StopDiscovery()
    → Net_DisconnectAllPeers()
    → Screens_TransitionTo(SCREEN_LOBBY)
```

### Mesh Stagger Sequence

**Current (receiver)**:
```
MSG_GAME_START received
    → gWaitingForMesh = TRUE
    → Net_ConnectToAllPeers() on next Update
```

**New (receiver)**:
```
MSG_GAME_START received
    → gGame.meshStaggerTimer = rank * MESH_STAGGER_PER_RANK
    → gWaitingForMesh = TRUE
    → (wait for stagger timer to expire)
    → meshStaggerTimer expires
    → Net_ConnectToAllPeers()
    → (existing mesh retry every 2s continues from here)
```

Note: The initiator (who pressed Return) is already connected before sending MSG_GAME_START, so the stagger only affects the receiver path. The initiator enters waiting-for-mesh with connections already established.
