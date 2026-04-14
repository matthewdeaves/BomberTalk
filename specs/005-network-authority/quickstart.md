# Quickstart: 005-network-authority

**Branch**: `005-network-authority`

## What This Feature Does

Reduces redundant TCP traffic by making bomb explosions, block destruction, and game-over messages owner-authoritative instead of broadcast-all. Adds graceful shutdown coordination, mesh formation timing, and heap monitoring.

## Files to Modify

| File | Changes |
|------|---------|
| `include/game.h` | Bump BT_PROTOCOL_VERSION to 5. Add new constants (DISCONNECT_GRACE_TICKS, MESH_STAGGER_PER_RANK, GAME_OVER_FAILSAFE_TICKS, LOW_HEAP_WARNING_BYTES, HEAP_CHECK_INTERVAL_TICKS). Add new GameState fields (disconnectGraceTimer, meshStaggerTimer, gameOverAuthority, localGameOverDetected, gameOverFailsafeTimer, heapCheckTimer). |
| `src/bomb.c` | In `Bomb_Update()`, change `ExplodeBomb(bomb, TRUE)` to `ExplodeBomb(bomb, gGame.localPlayerID == bomb.ownerID)`. Chain explosions propagate the parent's broadcast flag. No changes to `Bomb_ForceExplodeAt()`. |
| `src/screen_game.c` | Game-over detection: check `Net_IsLowestRankConnected()` before sending. Non-authority path: set failsafe timer. Grace period: replace immediate disconnect with timer-based delayed disconnect. |
| `src/screen_lobby.c` | Add mesh stagger: delay first `Net_ConnectToAllPeers()` by `rank * 30` ticks after entering `gWaitingForMesh`. |
| `src/net.c` | Add `Net_IsLowestRankConnected()` function. |
| `include/net.h` | Declare `Net_IsLowestRankConnected()`. |
| `src/main.c` | Add `LOW_HEAP_WARNING_BYTES` check after init. Add periodic heap check in `MainLoop()` (every 1800 ticks). Initialize new GameState fields in `InitGameState()`. |

## Build & Test

```bash
# Build all three targets
cd build-68k && make && cd ..
cd build-ppc-ot && make && cd ..
cd build-ppc-mactcp && make && cd ..

# Deploy and test on hardware via MCP server
# 3-player game, monitor logs:
socat -u UDP-RECV:7356,reuseaddr -

# Verify per SC-001 through SC-008:
# - 1 MSG_BOMB_EXPLODE per explosion (not 3)
# - 1 MSG_GAME_OVER per game end (not 3)
# - Mac SE disconnect reason 0 (clean)
# - Zero TCP connect failures during mesh
# - Heap monitoring logs at init
```

## Key Implementation Notes

1. **ExplodeBomb broadcast flag**: The chain propagation is automatic because `ExplodeBomb` is called recursively. When bomb A (broadcast=TRUE) hits bomb B, it calls `ExplodeBomb(bombB, TRUE)` through the existing raycast-triggers-bomb path. When a non-owner's fuse expires (broadcast=FALSE), any chain from that also stays FALSE. This is correct because the owner's machine will also trigger the same chain with broadcast=TRUE.

2. **Grace period polling**: During the 30-tick disconnect grace, the main loop must still call `Net_Poll()` so TCP buffers flush. The game-over state prevents new game events from being processed (screen check guards).

3. **Mesh stagger**: Only affects the receiver path (machines that get MSG_GAME_START from someone else). The initiator already has connections established before sending game start.

4. **Failsafe timer**: Critical for robustness. If the authority (rank 0) crashes or disconnects between detecting game over and sending the message, another machine must step in. 60 ticks (~1s) is long enough to avoid false failsafe triggers from normal network latency but short enough to avoid stuck games.
