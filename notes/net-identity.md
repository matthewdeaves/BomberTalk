# Net identity and authority (KI-009, KI-011)

BomberTalk is one application on the generic PeerTalk transport. PeerTalk
tells every message callback which `PT_Peer` the message came from; it does
not know what a "player" is. The rules below are BomberTalk's, in
`src/net_handlers.c` (wiring) and `src/net_auth.c` (pure rules, host tested in
`tests/test_net_auth.c`). Nothing in PeerTalk changed.

## Sender slot

A peer's slot is the stored binding `gGame.players[i].peer`, set when the
roster is seated (`Net_ComputeLocalPlayerID`, from `PT_GetPeerRank`) or when a
late joiner is admitted. It is not recomputed from the live rank, so ranks
shifting on a join or leave cannot reassign identities. `Player_Init` must not
clear it. `Lobby_Init` and `on_disconnected` clear it.

## Who may say what

| Message | Rule |
|---|---|
| POSITION, BOMB_PLACED | in-game; claimed slot must equal sender slot (NETAUTH_SELF) |
| PLAYER_KILLED | claimed victim must equal sender slot: each machine reports only its own death |
| BOMB_EXPLODE | sender must own the bomb at that cell (NETAUTH_OWNER); no such bomb: dropped |
| BLOCK_DESTROYED, MAP_STATE, GAME_OVER | any seated peer (NETAUTH_SEATED) |
| GAME_START | only while in the lobby; any player may press Start, so no single authorised sender |

Unseated peers are dropped for everything except a late-join admission via
POSITION or BOMB_PLACED. Dropped messages are logged at CLOG_DEBUG.
GAME_OVER is not lowest-rank-only because the 005 failsafe makes a
non-authority machine send it when the authority is gone or silent.

## Late join

See KI-011. One rule, applied identically by every machine, so rosters agree:
admit into the free slot your live rank gives you and which you claim,
otherwise be ignored.

## Limits

- UDP position messages are attributed by source IP. Same-IP or spoofed-source
  packets are out of scope (trusted LAN); two machines behind one NAT address
  look like one peer.
- A seated peer can still send bogus block-destroyed, map-snapshot or
  game-over messages; values are range-checked, not authenticated.
- KI-010 (bomb range clamp) is separate and still open.
