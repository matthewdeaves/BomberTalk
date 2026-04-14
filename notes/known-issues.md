# Known Issues & Future Improvements

Observations from hardware testing that don't block current functionality but could be addressed in future work. Each entry includes context, severity, and suggested fix.

---

## KI-001: Grace Period Race Between Fast and Slow Machines

**Observed**: v1.7.0+, game 1 of 006 testing session (2026-04-14)
**Severity**: Low (cosmetic — game completes correctly)
**Machines**: Mac SE (P0, authority) vs Performa 6400 (P1)

**Symptoms**: The 6400 receives `MSG_GAME_OVER`, runs its 90-tick grace period (~1.5s at 60fps), and tears down TCP. The Mac SE, running at ~10fps, processes ticks slower — its 90-tick grace period takes ~9 seconds wall-clock. The 6400 disconnects before the SE finishes its grace period, causing a `reason: 2` (remote disconnect) on the SE.

**Log evidence**:
```
[SE 121233] Game over: starting grace period (90 ticks)
[SE 122766] Peer BomberTalk disconnected (reason: 2)  <- 6400 tore down first
[SE 123066] Grace period complete, disconnecting       <- SE finished later
```

**Impact**: The SE still transitions to lobby correctly. The disconnect is handled gracefully. No crash or data loss. The `reason: 2` is logged but has no gameplay effect.

**Suggested fix**: Scale `DISCONNECT_GRACE_TICKS` by machine speed, or use wall-clock time instead of tick count for the grace period. Alternatively, increase the constant from 90 to 180 ticks (~3s on PPC, ~18s on SE) to give more margin. The tradeoff is slower lobby re-entry on fast machines.

**Frequency**: 1 out of 3 games in testing. Games 2 and 3 had clean `reason: 0` disconnects on all machines.

---

## KI-002: TCP Connect Failure on Performa 6200 During Mesh Formation

**Observed**: v1.6.0+, games 2 and 3 of 006 testing session (2026-04-14)
**Severity**: Low (mesh stagger recovers automatically)
**Machines**: Performa 6200 (MacTCP) connecting to Mac SE (MacTCP)

**Symptoms**: The 6200 fails its initial TCP connect to the Mac SE with `Error 3: TCP connect failed`. The mesh stagger mechanism (introduced in 005) delays the retry by `rank * 30 ticks`, after which the connection succeeds.

**Log evidence**:
```
[6200 48966] Error 3: TCP connect failed
[6200 49000] Game start received, mesh stagger: rank 1, delay 30 ticks
[6200 49300] Re-discovered disconnected peer: BomberTalk
[SE  128383] Incoming TCP connection accepted
```

**Impact**: None — mesh completes successfully every time. The 30-tick stagger was specifically designed for this scenario. The game starts normally.

**Suggested fix**: Could investigate why the 6200's first TCP connect fails (possibly MacTCP listener not ready on the SE when the 6200 tries to connect). Increasing the initial stagger or adding a pre-connect delay might eliminate the warning, but the current retry mechanism is robust.

**Frequency**: 2 out of 3 games. This appears to happen when the 6200 is not the game initiator (i.e., when it receives `MSG_GAME_START` from another machine and immediately tries to connect to all peers).

---

## KI-003: OTRcv Error -3155 During Connection Tiebreaker on 6400

**Observed**: v1.6.0+, game 1 of 006 testing session (2026-04-14)
**Severity**: Informational (debug-level, handled correctly)
**Machines**: Performa 6400 (Open Transport)

**Symptoms**: During mesh formation, when both the 6400 and another peer simultaneously try to connect to each other, the tiebreaker cancels the outgoing connection. While draining the cancelled endpoint, `OTRcv` returns `-3155` (`kOTLookErr`), meaning an asynchronous event arrived during the drain.

**Log evidence**:
```
[6400 55450] OTRcv error -3155 in disconnect drain
[6400 55450] OTRcv error -3155 in disconnect drain
[6400 55800] Tiebreaker: cancelling outgoing, accepting incoming
[6400 55816] Incoming TCP connection accepted
```

**Impact**: None — the tiebreaker resolves correctly and the mesh completes. This is expected OT behavior during simultaneous connect scenarios.

**Suggested fix**: Could suppress the `-3155` log message during tiebreaker drain since it's expected, or downgrade from `DBG` to a lower level. Not worth changing the drain logic — it's working correctly.

**Frequency**: 1 out of 3 games (only when simultaneous connect happens, which is timing-dependent).

---

## KI-004: Fuse Expiry Before Network Bomb Explode Message

**Observed**: v1.7.0+ (005-network-authority), all 3 games of 006 testing session
**Severity**: Informational (by design — safety net working as intended)
**Machines**: Performa 6400 (8 events), Performa 6200 (7 events), Mac SE (1 event)

**Symptoms**: A non-owner machine's local bomb fuse timer expires before it receives `MSG_BOMB_EXPLODE` from the bomb owner. The bomb explodes locally without broadcasting (since only the owner broadcasts). When the network message eventually arrives, it's a no-op (bomb already gone).

**Log evidence**:
```
[6400 63866] Bomb at (1,10) fuse expired locally (owner=P2, not local P1)
[6400 63866] Bomb exploding at (1,10) range=1 owner=P2 remote
```

**Impact**: None — this is the force-explode safety net from 005 working exactly as designed. The bomb still explodes at the correct time. The Mac SE has fewer occurrences (1 vs 7-8) because it's slower — by the time its fuse expires, the network message has usually arrived.

**Suggested fix**: Not a bug. Could reduce log level from `INF` to `DBG` since this is expected behavior and clutters the log. The asymmetry (fast machines expire first) is inherent to the owner-authoritative design and doesn't cause desync because explosions are idempotent.

**Frequency**: 16 total across 3 games (consistently happens for non-owner bombs on faster machines).
