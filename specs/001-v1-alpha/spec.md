# Feature Specification: BomberTalk v1.0-alpha

**Feature Branch**: `001-v1-alpha`
**Created**: 2026-04-05
**Status**: Active
**Input**: BOMBERMAN_CLONE_PLAN.md + PeerTalk SDK constitution (three apps are the spec)

## Clarifications

### Session 2026-04-05

- Q: What happens when a player disconnects mid-game? → A: Game continues for remaining players — all peers are equal, no host authority
- Q: What happens after a round ends (last player standing)? → A: Return to lobby — players stay connected, any player can start another round
- Q: How should BomberTalk handle the Mac SE's monochrome 512x342 display? → A: Same 15x13 grid, 16x16 tiles on SE (240x208). Color Macs use 32x32 tiles (480x416). Game logic identical, only render scale differs.
- Q: Do we need a host? → A: No. Pure peer-to-peer. Menu has "Play" and "Quit." Any player can press Start in lobby once 2+ peers are connected. Player IDs assigned deterministically by sorting connected peer IPs (same tiebreaker PeerTalk uses internally). MSG_PLAYER_INFO removed.

## User Scenarios & Testing

### User Story 1 - Game Boots and Shows Menu (Priority: P1)

A player double-clicks BomberTalk on any of the three target Macs. The application
initializes the Toolbox, PeerTalk, and shows a loading screen followed by a main menu
with options: Play and Quit.

**Why this priority**: Without a working app shell and screen system, nothing else can
be built or demonstrated. This proves the build system works on all three platforms.

**Independent Test**: Launch BomberTalk on each Mac. Loading screen appears, transitions
to menu. Quit exits cleanly. No crashes, no bus errors.

**Acceptance Scenarios**:

1. **Given** BomberTalk is launched on a Mac SE (System 6, MacTCP), **When** the app
   starts, **Then** a loading screen shows "BomberTalk" text, transitions to main menu
   within 3 seconds, menu responds to keyboard input, Quit exits to Finder.

2. **Given** BomberTalk is launched on Performa 6400 (System 7.6.1, OT), **When** the
   app starts, **Then** identical behavior to Mac SE — same screens, same menu.

3. **Given** BomberTalk is launched on Performa 6200 (System 7.5.3, MacTCP), **When**
   the app starts, **Then** identical behavior — same screens, same menu.

---

### User Story 2 - Player Discovery and Lobby (Priority: P2)

A player selects "Play" from the menu. The lobby screen appears showing the local
player and any other BomberTalk instances discovered on the LAN via PeerTalk's UDP
broadcast discovery. Players can see who's online. Any player can press Start once
2-4 players are connected. There is no host — all peers are equal.

**Why this priority**: This is the core PeerTalk showcase — automatic peer discovery
and connection across MacTCP and Open Transport machines on the same LAN. This is what
we're proving works. The hostless design mirrors PeerTalk's own peer-to-peer architecture.

**Independent Test**: Launch BomberTalk on 2+ Macs on the same LAN. Each sees the
others appear in the lobby within 5 seconds. Any player presses Start, all transition
to gameplay.

**Acceptance Scenarios**:

1. **Given** BomberTalk is running on Mac SE and Performa 6400, **When** both enter
   the lobby, **Then** each discovers the other via PeerTalk (PT_OnPeerDiscovered),
   player names appear in the lobby list within 5 seconds.

2. **Given** 3 players are in the lobby (Mac SE + 6200 + 6400), **When** any player
   presses Start, **Then** PT_Connect establishes TCP connections to all peers, all
   three transition to gameplay simultaneously. Player IDs are assigned deterministically
   by sorting connected peer IPs (lowest IP = player 0).

3. **Given** a player is in the lobby, **When** another player quits or disconnects,
   **Then** PT_OnPeerLost fires and the departed player disappears from the lobby list.

4. **Given** a player selects "Play", **When** no other players are on the LAN,
   **Then** the lobby shows "Searching for players..." with the local player listed.
   Start button is disabled until 2+ peers are connected.

---

### User Story 3 - Single-Player Movement and Map (Priority: P3)

A single player can move around the Bomberman grid. The 15x13 tile map renders with
colored rectangles (walls=gray, floor=green, blocks=brown). Arrow keys move the player
tile-by-tile. The player cannot walk through walls or destructible blocks.

**Why this priority**: Core gameplay mechanics must work locally before adding network
sync. This validates the renderer, input, tilemap, and player subsystems from the plan.

**Independent Test**: Launch BomberTalk, start a single-player game. Move with arrow
keys. Player stops at walls. Map displays correctly.

**Acceptance Scenarios**:

1. **Given** gameplay has started, **When** the player presses arrow keys, **Then** the
   player sprite moves one tile per input at ~5 moves/second with movement cooldown.

2. **Given** the player is adjacent to a wall tile, **When** the player tries to move
   into it, **Then** the move is rejected and the player stays in place.

3. **Given** gameplay is running on Mac SE (512x342 mono), **When** rendering with
   16x16 tiles, **Then** the tile map draws correctly with the same 15x13 grid as
   color Macs, using GWorld double-buffering at 10+ fps.

---

### User Story 4 - Multiplayer Position Sync (Priority: P4)

Multiple players see each other's positions in real-time. When player A moves on their
Mac, player B's screen updates to show A's new position. Position updates use PeerTalk's
PT_FAST (UDP) transport for low-latency real-time sync.

**Why this priority**: This is the Bomberman pattern from PeerTalk's constitution —
"real-time position updates, small frequent messages." This proves PT_FAST works for
game state sync across all three networking stacks.

**Independent Test**: Two players on different Macs. Player A moves, Player B sees
the movement within 100ms. Verify on all three Mac combinations.

**Acceptance Scenarios**:

1. **Given** Mac SE and Performa 6400 are in a game, **When** Mac SE player moves,
   **Then** Performa 6400 displays the updated position within 2 frames (~66ms).

2. **Given** 3 players are in a game, **When** any player moves, **Then** all other
   players receive the position update via PT_Broadcast with MSG_POSITION type.

3. **Given** a player disconnects mid-game, **When** PT_OnDisconnected fires, **Then**
   the disconnected player's sprite is removed from all other screens.

---

### User Story 5 - Bombs and Explosions (Priority: P5)

A player can place a bomb on their current tile by pressing spacebar. After a 3-second
fuse, the bomb explodes in a cross pattern (up/down/left/right), destroying destructible
blocks and eliminating players caught in the blast. Bomb placement and explosion events
use PT_RELIABLE (TCP) for guaranteed delivery.

**Why this priority**: Bombs are what make it Bomberman. This also exercises PeerTalk's
PT_RELIABLE transport for game events that must not be lost.

**Independent Test**: Place a bomb, wait 3 seconds, watch it explode. Blocks in the
blast radius are destroyed. Verify explosion syncs across all connected players.

**Acceptance Scenarios**:

1. **Given** a player is on a floor tile, **When** they press spacebar, **Then** a bomb
   appears on that tile and a MSG_BOMB_PLACED message is broadcast via PT_RELIABLE.

2. **Given** a bomb has been placed, **When** 3 seconds elapse (180 ticks at 60Hz),
   **Then** the bomb explodes in a cross pattern with range of 1 tile in each direction.

3. **Given** an explosion reaches a destructible block, **When** the explosion fires,
   **Then** the block is destroyed (becomes floor) and a MSG_BLOCK_DESTROYED message
   is broadcast.

4. **Given** a player is in the explosion radius, **When** the explosion fires, **Then**
   the player is eliminated and a MSG_PLAYER_KILLED message is broadcast.

---

### Edge Cases

- What happens when the Mac SE runs out of memory during GWorld allocation?
  Fall back to smaller buffers or show an error dialog.
- What happens when PeerTalk discovery finds more than 4 players?
  Lobby shows all discovered peers but only allows 4 to connect for gameplay.
- What happens when a bomb is placed and the placer disconnects before it explodes?
  The bomb still explodes on all remaining clients (game state is distributed).
- What happens when two players place bombs on the same tile?
  Only one bomb per tile — second placement is rejected.
- How does the 68000 at 8 MHz handle explosion rendering?
  Keep explosion visual simple (flash the tiles, no particle effects).
- What happens when any player disconnects mid-game?
  Game continues for remaining players. There is no host — all peers are equal.
  The disconnected player is simply removed from the game.
- What happens after a round ends (last player standing wins)?
  All players return to the lobby screen. Existing PeerTalk connections remain active
  (no re-discovery needed). Any player can start another round immediately. This
  re-exercises the lobby-to-game transition each round.
- Can a player join a game already in progress (late join)?
  Not supported in v1.0-alpha. Players must be in the lobby before someone presses
  Start. A player who connects after MSG_GAME_START is sent stays in the lobby
  and waits for the next round.
- What if two players press Start simultaneously?
  MSG_GAME_START is sent via PT_RELIABLE (TCP). Each client honors the first
  MSG_GAME_START received and ignores duplicates. All clients compute the same
  player ID assignment (IP sort), so the result is deterministic regardless of
  who pressed Start.
- How does BomberTalk handle the Mac SE's 512x342 monochrome display?
  Same 15x13 grid on all machines. The Mac SE renders with 16x16 pixel tiles
  (240x208 play area), which fits comfortably in 512x342. Color Macs render with
  32x32 pixel tiles (480x416 play area) for 640x480 screens. Game logic, map data,
  and network messages are identical — only the rendering tile size differs. Tile size
  is determined at init based on screen dimensions (screenBits.bounds).

## Requirements

### Functional Requirements

- **FR-001**: Game MUST initialize Mac Toolbox, PeerTalk SDK, and display a loading screen
- **FR-002**: Game MUST present a main menu with Play and Quit options
- **FR-003**: Game MUST discover other BomberTalk instances on the LAN via PeerTalk
- **FR-004**: Game MUST display a lobby showing discovered players with their names
- **FR-005**: Game MUST support 2-4 players in a single game session
- **FR-006**: Game MUST render a 15x13 tile grid using GWorld double-buffering with PICT resource tile graphics (falling back to colored rectangles if resources missing)
- **FR-007**: Game MUST accept arrow key input via GetKeys() polling for movement
- **FR-008**: Game MUST prevent player movement through walls and blocks
- **FR-009**: Game MUST broadcast player positions using PT_FAST (UDP) messages
- **FR-010**: Game MUST support bomb placement with spacebar and 3-second fuse timer
- **FR-011**: Game MUST broadcast bomb/explosion/kill events using PT_RELIABLE (TCP)
- **FR-012**: Game MUST handle player disconnection by removing the player from the game (set inactive, remove sprite) without crashing remaining clients
- **FR-013**: Game MUST build for 68k MacTCP, PPC MacTCP, and PPC Open Transport
- **FR-014**: Game MUST use under 1 MB application heap on Mac SE (excluding PeerTalk's allocation, per SC-006)
- **FR-015**: Game MUST use C89/C90 compatible code throughout
- **FR-016**: Game MUST return all players to the lobby after a round ends (last player standing or all others disconnected), preserving existing connections
- **FR-017**: Game MUST adapt tile rendering size based on screen dimensions: 32x32 on 640x480+ screens, 16x16 on 512x342 (Mac SE). Grid size (15x13) and game logic remain identical across all targets.
- **FR-018**: Game MUST load tile and sprite graphics from PICT resources in the resource fork (IDs 128-199 for color, 200-255 for Mac SE). Fall back to colored rectangles if resources are missing.

### Key Entities

- **Player**: Grid position, facing direction, alive/dead state, bomb count, player ID
- **Bomb**: Grid position, owner player ID, fuse timer, explosion range
- **TileMap**: 15x13 grid of tile types (floor, wall, block, spawn)
- **GameState**: Current screen (loading/menu/lobby/game), list of players, list of bombs. No host flag — all peers equal.
- **NetMessage**: 7 message types: position, bomb placed, bomb explode, block destroyed, player killed, game start, game over. No MSG_PLAYER_INFO — player IDs computed locally by IP sort.

## Success Criteria

### Measurable Outcomes

- **SC-001**: Three different Macs (68k MacTCP, PPC MacTCP, PPC OT) discover each other
  and connect within 10 seconds on the same LAN
- **SC-002**: Position updates are visible on remote machines within 100ms of local movement
- **SC-003**: Mac SE maintains 10+ fps during gameplay with 4 players
- **SC-004**: Game completes a full round (bombs, kills, last player standing) without crashes
- **SC-005**: Player disconnection is handled within 15 seconds (PeerTalk timeout)
- **SC-006**: Total application memory usage stays under 1 MB on Mac SE (excluding PeerTalk)
