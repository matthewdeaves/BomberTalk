# PeerTalk: Downgrade Benign Drain Errors to DEBUG Level

## Summary

During normal TCP connection teardown in multiplayer games, three error conditions are logged at WARNING/ERROR level but are actually expected and benign. They should be downgraded to CLOG_DBG.

## Error Codes

### 1. OTRcv returning -3155 (kOTNoDataErr) during TCP drain

**File**: `pt_ot.c` (Open Transport transport layer)
**Current level**: CLOG_WARN
**Recommended level**: CLOG_DBG
**Why benign**: After initiating disconnect, the drain loop calls OTRcv to flush remaining data. When no data remains, OTRcv returns kOTNoDataErr (-3155). This is the expected termination condition for the drain loop, not an error.

### 2. TCPRcv returning -23008 (connectionDoesntExist) during MacTCP cleanup

**File**: `pt_mactcp.c` (MacTCP transport layer)
**Current level**: CLOG_ERR
**Recommended level**: CLOG_DBG
**Why benign**: During cleanup after TCPAbort or when the remote side has already closed, TCPRcv returns connectionDoesntExist (-23008). The connection was intentionally torn down; this confirms the cleanup is complete.

### 3. OT endpoint yielding T_DISCONNECT during drain

**File**: `pt_ot.c` (Open Transport transport layer)
**Current level**: CLOG_WARN
**Recommended level**: CLOG_DBG
**Why benign**: When draining an OT endpoint at game start (clearing stale state from a previous game), encountering T_DISCONNECT is expected — it represents the previous session's orderly disconnect that hasn't been consumed yet. OTRcvDisconnect properly consumes it.

## Reproduction Steps

1. Build BomberTalk with 3 machines (Mac SE, Performa 6200, Performa 6400)
2. Play a 3-player game to completion (game over)
3. Check logs on all 3 machines
4. OT errors appear on the 6400 (OT platform), MacTCP errors on the SE and 6200

## Observed Frequency

- OTRcv -3155: 2 occurrences per game end (6400 only)
- TCPRcv -23008: 1 occurrence per game end (6200 only)
- T_DISCONNECT drain: 1 occurrence at game start after previous game (6400 only)

## Proposed Change

In the affected log calls, change `CLOG_WARN`/`CLOG_ERR` to `CLOG_DBG` for these specific error codes during drain/cleanup paths. The error codes should still be logged (useful for debugging) but at DEBUG level to avoid noise in normal operation.
