/*
 * net.h -- PeerTalk wrapper (thin layer)
 *
 * Keeps PeerTalk includes out of game headers.
 * Source: contracts/network-protocol.md
 */

#ifndef NET_H
#define NET_H

void Net_Init(const char *playerName);
void Net_Shutdown(void);
void Net_Poll(void);

void Net_StartDiscovery(void);
void Net_StopDiscovery(void);
void Net_ConnectToAllPeers(void);

/* Send game messages */
void Net_SendPosition(short pixelX, short pixelY, short facing);
void Net_SendBombPlaced(short col, short row, short range);
void Net_SendBombExplode(short col, short row, short range);
void Net_SendBlockDestroyed(short col, short row);
void Net_SendPlayerKilled(unsigned char playerID, unsigned char killerID);
void Net_SendGameStart(unsigned char numPlayers);
void Net_SendGameOver(unsigned char winnerID);

/* Query state */
int Net_HasVersionMismatch(void);
void Net_ResetVersionMismatch(void);
int Net_GetDiscoveredPeerCount(void);
const char *Net_GetDiscoveredPeerName(int index);
const char *Net_GetDiscoveredPeerAddress(int index);
int Net_GetConnectedPeerCount(void);
short Net_GetExpectedPlayers(void);
short Net_ComputeLocalPlayerID(void);

#endif /* NET_H */
