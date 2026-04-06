# Network Protocol Contract: BomberTalk v1.0-alpha

**Feature**: `001-v1-alpha`
**Date**: 2026-04-05

## Overview

BomberTalk uses PeerTalk SDK for all networking. This contract defines the message
types, their formats, and when they are sent/received.

## PeerTalk Integration

### Initialization

```c
/* In net.c — Net_Init() */
PT_Init(&gPTCtx, playerName);

/* Register message types (7 total — no MSG_PLAYER_INFO, IDs computed locally) */
PT_RegisterMessage(gPTCtx, MSG_POSITION,        PT_FAST);
PT_RegisterMessage(gPTCtx, MSG_BOMB_PLACED,      PT_RELIABLE);
PT_RegisterMessage(gPTCtx, MSG_BOMB_EXPLODE,     PT_RELIABLE);
PT_RegisterMessage(gPTCtx, MSG_BLOCK_DESTROYED,   PT_RELIABLE);
PT_RegisterMessage(gPTCtx, MSG_PLAYER_KILLED,     PT_RELIABLE);
PT_RegisterMessage(gPTCtx, MSG_GAME_START,        PT_RELIABLE);
PT_RegisterMessage(gPTCtx, MSG_GAME_OVER,         PT_RELIABLE);

/* Register callbacks */
PT_OnPeerDiscovered(gPTCtx, on_peer_discovered, NULL);
PT_OnPeerLost(gPTCtx, on_peer_lost, NULL);
PT_OnConnected(gPTCtx, on_connected, NULL);
PT_OnDisconnected(gPTCtx, on_disconnected, NULL);
PT_OnError(gPTCtx, on_error, NULL);

PT_OnMessage(gPTCtx, MSG_POSITION, on_position, NULL);
PT_OnMessage(gPTCtx, MSG_BOMB_PLACED, on_bomb_placed, NULL);
PT_OnMessage(gPTCtx, MSG_BOMB_EXPLODE, on_bomb_explode, NULL);
PT_OnMessage(gPTCtx, MSG_BLOCK_DESTROYED, on_block_destroyed, NULL);
PT_OnMessage(gPTCtx, MSG_PLAYER_KILLED, on_player_killed, NULL);
PT_OnMessage(gPTCtx, MSG_GAME_START, on_game_start, NULL);
PT_OnMessage(gPTCtx, MSG_GAME_OVER, on_game_over, NULL);

/* Start discovery */
PT_StartDiscovery(gPTCtx);
```

### Polling

```c
/* In main loop — every iteration */
PT_Poll(gPTCtx);
```

### Shutdown

```c
PT_StopDiscovery(gPTCtx);
PT_Shutdown(gPTCtx);
```

## Message Type IDs

| ID | Name | Transport | Direction | Screen |
|----|------|-----------|-----------|--------|
| 0x01 | MSG_POSITION | PT_FAST (UDP) | Broadcast | Game |
| 0x02 | MSG_BOMB_PLACED | PT_RELIABLE (TCP) | Broadcast | Game |
| 0x03 | MSG_BOMB_EXPLODE | PT_RELIABLE (TCP) | Broadcast | Game |
| 0x04 | MSG_BLOCK_DESTROYED | PT_RELIABLE (TCP) | Broadcast | Game |
| 0x05 | MSG_PLAYER_KILLED | PT_RELIABLE (TCP) | Broadcast | Game |
| 0x06 | MSG_GAME_START | PT_RELIABLE (TCP) | Any peer -> All | Lobby |
| 0x07 | MSG_GAME_OVER | PT_RELIABLE (TCP) | Any peer -> All | Game |

## Game Flow Sequence

### 1. Discovery Phase (Lobby Screen)

```
Player A starts BomberTalk, enters lobby
  -> PT_StartDiscovery(ctx)
  -> PeerTalk broadcasts UDP every 2s on port 7353

Player B starts BomberTalk, enters lobby
  -> PT_StartDiscovery(ctx)
  -> Discovers Player A via UDP broadcast
  -> on_peer_discovered fires on both sides
  -> Both lobbies update to show each other
```

### 2. Connection Phase (Lobby -> Game Transition)

```
Any player presses Start (requires 2+ connected peers)
  -> PT_Connect(ctx, peer) for each discovered peer (if not already connected)
  -> PeerTalk establishes TCP on port 7354 (tiebreaker: lower IP initiates)
  -> on_connected fires on both sides

Player who pressed Start broadcasts game start
  -> PT_Broadcast(ctx, MSG_GAME_START, &start, sizeof(start))
  -> All players (including the sender) transition to game screen

Player IDs assigned locally (no network message needed)
  -> Each client sorts connected peer IPs (including own IP)
  -> Lowest IP = player 0, next = player 1, etc.
  -> Spawn corners: 0=(1,1), 1=(13,1), 2=(1,11), 3=(13,11)
  -> All clients compute identical assignments deterministically

Duplicate MSG_GAME_START handling
  -> If two players press Start simultaneously, each client honors
     the first MSG_GAME_START received and ignores subsequent ones
```

### 3. Gameplay Phase

```
Every move:
  Local player moves
  -> PT_Broadcast(ctx, MSG_POSITION, &pos, sizeof(pos))
  -> Remote players receive in on_position callback
  -> Update remote player positions

Bomb placed:
  -> PT_Broadcast(ctx, MSG_BOMB_PLACED, &bomb, sizeof(bomb))
  -> All clients create bomb with synchronized fuse timer

Bomb explodes (each client independently):
  -> Check explosion results locally
  -> PT_Broadcast(ctx, MSG_BOMB_EXPLODE, &explode, sizeof(explode))
  -> If blocks destroyed: PT_Broadcast MSG_BLOCK_DESTROYED for each
  -> If player killed: PT_Broadcast MSG_PLAYER_KILLED
```

### 4. Disconnect Handling

```
Player disconnects (quit, crash, cable pull):
  -> PeerTalk detects via TCP timeout (60s) or clean disconnect
  -> on_disconnected fires on remaining players
  -> Game removes disconnected player's sprite
  -> Game continues with remaining players
```

## Module API Contracts

### net.h — Public Interface

```c
/* Initialize PeerTalk with player name */
void Net_Init(const char *playerName);

/* Shut down PeerTalk */
void Net_Shutdown(void);

/* Poll PeerTalk (call every frame) */
void Net_Poll(void);

/* Start/stop peer discovery */
void Net_StartDiscovery(void);
void Net_StopDiscovery(void);

/* Connect to all discovered peers */
void Net_ConnectToAllPeers(void);

/* Send game messages */
void Net_SendPosition(short col, short row, short facing);
void Net_SendBombPlaced(short col, short row, short range);
void Net_SendBombExplode(short col, short row, short range);
void Net_SendBlockDestroyed(short col, short row);
void Net_SendPlayerKilled(unsigned char playerID, unsigned char killerID);
void Net_SendGameStart(unsigned char numPlayers);
void Net_SendGameOver(unsigned char winnerID);

/* Query state */
int Net_GetDiscoveredPeerCount(void);
const char* Net_GetDiscoveredPeerName(int index);
int Net_IsConnected(void);

/* Player ID assignment (deterministic, no network message needed) */
/* Sort connected peer IPs + local IP. Lowest = player 0. */
short Net_ComputeLocalPlayerID(void);
```

### screens.h — Public Interface

```c
/* Screen state management */
void Screens_Init(void);
void Screens_TransitionTo(ScreenState newScreen);
void Screens_Update(void);
void Screens_Draw(WindowPtr window);
ScreenState Screens_GetCurrent(void);
```
