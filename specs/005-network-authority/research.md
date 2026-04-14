# Research: Network Authority & Robustness Improvements

**Feature**: 005-network-authority
**Date**: 2026-04-14

## Book Consultation Summary

Per Constitution IX ("The Books Are Gospel"), the following books were consulted:

- **Tricks of the Mac Game Programming Gurus (1995), Ch. 10**: Outland server/referee model and Novel Netwar peer-to-peer model document the two network architecture patterns. Marathon interview confirms "each machine tracks all state" approach. Our owner-authoritative model is the natural evolution from broadcast-all (Novel Netwar) toward selective authority (Outland) without requiring a dedicated host.
- **Tricks of the Mac Game Programming Gurus, Ch. 0**: TickCount-based timing with 5-60 tick intervals is standard for game events. Our 30-tick grace period and stagger fit this pattern.
- **Tricks of the Mac Game Programming Gurus, Ch. 3**: MaxApplZone/MoreMasters heap initialization is already implemented. Off-screen rendering must always update atomically — confirmed that our grace period design (Game_Draw still runs, showing frozen final frame) is safe.
- **Not found in books**: No established Classic Mac patterns for network message staggering, runtime FreeMem monitoring during gameplay, or TCP disconnect grace periods. These are novel additions documented in Decisions 5-7 below.

## Decision 1: Authority Model for Bomb Explosions

**Decision**: Owner-authoritative with chain-cascade override. The bomb owner broadcasts MSG_BOMB_EXPLODE when their fuse expires. For chain explosions, the chain-triggering machine broadcasts all chained explosions regardless of individual bomb ownership.

**Rationale**: Marathon (per "Tricks of the Mac Game Programming Gurus" networking chapter) uses a model where each machine tracks all state independently and only sends its own inputs. BomberTalk already does this for player death (each machine is authoritative for its own player). Extending to bomb explosions is the natural next step. The chain-cascade rule avoids orphaned explosions when a chain crosses ownership boundaries.

**Alternatives considered**:
- *Broadcast-all (current)*: Works, but 3x TCP traffic per explosion. Rejected because the redundancy was flagged as the highest-impact issue.
- *Per-bomb owner even in chains*: Would require deferred send when chain arrives via network. Complex and fragile — if the owner disconnected mid-chain, nobody sends.
- *Host-authoritative*: Would require designating a host. Rejected — BomberTalk's architecture is pure peer-to-peer with no host concept (Constitution I: prove PeerTalk works as peer-to-peer SDK).

## Decision 2: Authority Model for Game Over

**Decision**: Lowest-rank-connected player (via `PT_GetPeerRank()`) is authoritative for broadcasting MSG_GAME_OVER. Failsafe: non-authority machines send after 60-tick timeout if no network game-over received.

**Rationale**: Rank 0 is deterministic (lowest IP), already used for player ID assignment. Using the same mechanism for game-over authority avoids new state. The failsafe timeout handles the edge case where rank 0 disconnects between detection and broadcast.

**Alternatives considered**:
- *First-to-detect sends*: Non-deterministic, doesn't reduce messages (all machines detect simultaneously).
- *Killer is authoritative*: Only works for kills, not for draws (aliveCount == 0) or timeout scenarios.
- *Host-based*: Same rejection as Decision 1.

## Decision 3: Block-Destroyed Authority

**Decision**: MSG_BLOCK_DESTROYED follows the same authority as its parent explosion. Whoever sends MSG_BOMB_EXPLODE also sends the associated MSG_BLOCK_DESTROYED messages.

**Rationale**: Block destruction is computed inside ExplodeBomb's raycast. Since the explosion broadcaster already executes the raycast, it naturally has the block destruction results. Non-broadcasting machines also execute the same raycast locally, so all machines agree on which blocks were destroyed. The existing deduplication (`TileMap_GetTile() == TILE_FLOOR` check) remains as safety net.

**Alternatives considered**:
- *Leave as broadcast-all*: Works due to deduplication, but inconsistent with the explosion authority model and still generates 3x traffic for block destruction.

## Decision 4: Implementation of Broadcast Flag in ExplodeBomb

**Decision**: The `broadcast` parameter in `ExplodeBomb()` controls both MSG_BOMB_EXPLODE and MSG_BLOCK_DESTROYED. In `Bomb_Update()`, the broadcast flag is set based on fuse expiry: `localPlayerID == bomb.ownerID`. `Bomb_ForceExplodeAt()` already passes broadcast=FALSE, which remains correct.

**Rationale**: ExplodeBomb already has a `broadcast` parameter. The change is minimal: one condition in `Bomb_Update()`. Note: chain explosions (bomb A's raycast triggering bomb B) are not currently implemented — the raycast checks for walls and blocks only. The chain authority rule from Clarification Session 2026-04-14 is preserved as a future design constraint: if chains are added, the broadcast flag must propagate from the parent.

**Alternatives considered**:
- *Separate broadcast flags per message type*: Over-engineering. The authority decision is per-explosion-event, not per-message-type.

## Decision 5: Grace Period Implementation

**Decision**: Add a `disconnectGraceTimer` field to GameState. After sending/receiving MSG_GAME_OVER and completing death animation wait, start 30-tick grace period. During grace period, continue calling `Net_Poll()` but don't process new game events. After timer expires, disconnect and transition to lobby.

**Rationale**: The current code calls `Net_DisconnectAllPeers()` immediately after `Net_SendGameOver()`. The 30-tick grace allows TCP delivery of the game-over message before teardown. 30 ticks (~0.5s) is sufficient for Mac SE at 3-10fps to receive and process the message (5-10 frames).

**Alternatives considered**:
- *TCP linger option*: Not available in MacTCP. OT has SO_LINGER but inconsistent behavior across versions.
- *Acknowledgment protocol*: Each machine ACKs game-over before disconnect. Complex, adds a new message type, and still needs a timeout for unresponsive machines.

## Decision 6: Mesh Stagger Implementation

**Decision**: After receiving MSG_GAME_START (receiver path) or after sending it (initiator path), delay the first `Net_ConnectToAllPeers()` call by `rank * 30` ticks. The initiator (who pressed Return) is already connected before sending game start, so the stagger mainly affects receivers.

**Rationale**: The 6200's connect failure occurs because the Mac SE is still processing MSG_GAME_START when the 6200 tries to connect. A rank-based stagger naturally spaces out connection attempts. The Mac SE (lowest IP = rank 0) doesn't wait, giving it maximum time to prepare its listener.

**Alternatives considered**:
- *Fixed delay for all*: Would slow mesh formation unnecessarily. Rank-based stagger lets the fastest path (rank 0 → ready immediately) proceed without delay.
- *PeerTalk-level listener readiness signal*: Would require PeerTalk API changes. Out of scope.

## Decision 7: Heap Monitoring Approach

**Decision**: Compile-time `LOW_HEAP_WARNING_BYTES` constant (default 262144 = 256KB). Check at init (after all allocations) and periodically in main loop (every 1800 ticks ~= 30 seconds). CLOG_WARN on low condition.

**Rationale**: Lightweight monitoring that adds zero overhead when heap is healthy (one `FreeMem()` call per 30 seconds, returns immediately). The 256KB threshold is below the 6200's current 462KB free, so no false positives today.

**Alternatives considered**:
- *Runtime-configurable threshold*: Over-engineering for a diagnostic feature. Compile-time constant is sufficient and can be tuned per build.
- *Aggressive monitoring (every frame)*: `FreeMem()` is a trap call. Per-frame on Mac SE would add measurable overhead.

## Decision 8: Protocol Version Bump

**Decision**: Bump `BT_PROTOCOL_VERSION` from 4 to 5. No wire format changes — the behavioral change (fewer broadcasts) is the version signal.

**Rationale**: v4 and v5 clients interoperate correctly: v4 sends redundant messages that v5 deduplicates, v5 sends fewer messages that v4 processes normally. The version bump documents the behavioral change and allows future diagnostics. Lobby version mismatch indicator already handles display.

**Alternatives considered**:
- *No version bump*: Would work functionally but makes debugging mixed-version scenarios harder. Version bumps are cheap.

## Decision 9: PeerTalk Drain Error Handling

**Decision**: Document specific error codes and recommended log levels in a PeerTalk GitHub issue. Do not patch BomberTalk to suppress these logs — they originate in PeerTalk's transport layer.

**Rationale**: The errors (OTRcv -3155, TCPRcv -23008, T_DISCONNECT drain) are logged inside PeerTalk's `pt_ot.c` and `pt_mactcp.c`. BomberTalk has no visibility into these log calls. The fix belongs in PeerTalk.

**Error codes for the issue**:
- OTRcv returning `-3155` (`kOTNoDataErr`) during TCP drain after disconnect — expected, means drain is complete
- TCPRcv returning `-23008` (`connectionDoesntExist`) during MacTCP cleanup — expected after abort
- OT endpoint yielding `T_DISCONNECT` event during drain — expected, stale OT state being consumed

**Alternatives considered**:
- *BomberTalk log filter*: Would suppress all PeerTalk WRN/ERR logs, not just the benign ones. Too broad.
