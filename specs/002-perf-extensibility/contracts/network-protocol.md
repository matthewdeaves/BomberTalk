# Network Protocol Contract: Performance & Extensibility Upgrade

**Feature**: `002-perf-extensibility`
**Date**: 2026-04-06
**Extends**: [001-v1-alpha/contracts/network-protocol.md](../../001-v1-alpha/contracts/network-protocol.md)

## Protocol Version

```c
#define BT_PROTOCOL_VERSION 2
```

Protocol version 1 is implicit (v1.0-alpha, no version field — pad byte was 0).
Protocol version 2 is the first explicit version, introduced in this feature.

## Changed Messages

### MSG_GAME_START (0x06) — PT_RELIABLE

**Before (v1.0-alpha)**:
```c
typedef struct {
    unsigned char numPlayers;   /* 1 byte */
    unsigned char pad;          /* 1 byte (always 0) */
} MsgGameStart;                 /* 2 bytes */
```

**After (v1.1)**:
```c
typedef struct {
    unsigned char numPlayers;   /* 1 byte */
    unsigned char version;      /* 1 byte (BT_PROTOCOL_VERSION = 2) */
} MsgGameStart;                 /* 2 bytes — size unchanged */
```

**Send behavior**: Sender sets `version = BT_PROTOCOL_VERSION` before broadcasting.

**Receive behavior**:
1. Check `msg->version == BT_PROTOCOL_VERSION`
2. If match: honor game start (existing behavior)
3. If mismatch: reject game start, log warning, set `gVersionMismatch = TRUE`
4. Lobby screen checks `gVersionMismatch` and displays indicator text

**Backwards compatibility**: Old clients (v1.0-alpha) send version=0 (the old pad byte). New clients detect 0 != 2 and reject. This is intentional — mixed-version games are not supported.

### MSG_GAME_OVER (0x07) — PT_RELIABLE

**Format unchanged**:
```c
typedef struct {
    unsigned char winnerID;     /* 1 byte (0-3 valid, 0xFF = draw) */
    unsigned char pad;          /* 1 byte */
} MsgGameOver;                  /* 2 bytes */
```

**Receive behavior change**:
1. Check `msg->winnerID < MAX_PLAYERS` before indexing `gGame.players[]`
2. If `winnerID >= MAX_PLAYERS` (including 0xFF): treat as draw/no winner
3. Log the winner ID regardless of validity for debugging

## Unchanged Messages

All other messages retain their v1.0-alpha format and behavior:

| ID | Name | Format | Change |
|----|------|--------|--------|
| 0x01 | MSG_POSITION | 5 bytes | None |
| 0x02 | MSG_BOMB_PLACED | 5 bytes | None |
| 0x03 | MSG_BOMB_EXPLODE | 3 bytes | None |
| 0x04 | MSG_BLOCK_DESTROYED | 2 bytes | None |
| 0x05 | MSG_PLAYER_KILLED | 2 bytes | None |

## Updated net.h API

```c
/* Existing functions — unchanged */
void Net_Init(const char *playerName);
void Net_Shutdown(void);
void Net_Poll(void);
void Net_StartDiscovery(void);
void Net_StopDiscovery(void);
void Net_ConnectToAllPeers(void);
void Net_SendPosition(short col, short row, short facing);
void Net_SendBombPlaced(short col, short row, short range);
void Net_SendBombExplode(short col, short row, short range);
void Net_SendBlockDestroyed(short col, short row);
void Net_SendPlayerKilled(unsigned char playerID, unsigned char killerID);
void Net_SendGameOver(unsigned char winnerID);
int Net_GetDiscoveredPeerCount(void);
const char* Net_GetDiscoveredPeerName(int index);
const char* Net_GetDiscoveredPeerAddress(int index);
int Net_IsConnected(void);
short Net_ComputeLocalPlayerID(void);
int Net_GetConnectedPeerCount(void);

/* Modified function — version now included automatically */
void Net_SendGameStart(unsigned char numPlayers);
/* Implementation: sets msg.version = BT_PROTOCOL_VERSION before broadcast */

/* New query function */
int Net_HasVersionMismatch(void);
/* Returns TRUE if a MSG_GAME_START with wrong version was received */
```

## Game Flow Change: Version Check

```
Player A presses Start in lobby
  -> Net_SendGameStart(numPlayers)
     -> msg.version = BT_PROTOCOL_VERSION (2)
     -> PT_Broadcast(ctx, MSG_GAME_START, &msg, sizeof(msg))

Player B receives MSG_GAME_START
  -> on_game_start callback fires
  -> if msg.version != BT_PROTOCOL_VERSION:
       -> CLOG_WARN("Version mismatch: got %d, expected %d")
       -> gVersionMismatch = TRUE
       -> Do NOT set gGame.gameStartReceived
       -> Player B stays in lobby
  -> if msg.version == BT_PROTOCOL_VERSION:
       -> Existing behavior (honor game start)
```
