# Feature Specification: Hot-Path Performance & Memory Optimizations (008)

**Feature Branch**: `008-perf-hotpath`
**Created**: 2026-04-18
**Status**: Draft
**Input**: Book-grounded review of renderer / player / bomb / tilemap / main hot paths against `books/` (Tricks of the Gurus, Sex Lies, Inside Macintosh IV) plus cppcheck 2.21 static analysis. Six confirmed optimizations + two optional; one rejected (CopyMask swap — books explicitly warn against it); one withdrawn on closer analysis (TileMap_Init memset is not actually dead — loop only covers active sub-region of the [25][31] array).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Mac SE Player Gets Smoother Gameplay (Priority: P1)

A player running BomberTalk on a Mac SE (8 MHz 68000, System 6, 4 MB RAM) starts a multiplayer round. Today gameplay measures ~10–19 fps with stutters when bombs explode or multiple players move simultaneously. After this change the same round runs a few frames per second faster and frame-time variance is lower.

**Why this priority**: Constitutional principle V — Mac SE Is the Floor. Every frame we reclaim on SE translates directly into a better experience on the lowest target and unlocks headroom for future features.

**Independent Test**: On a Mac SE (or QEMU Quadra-800 SE-equivalent profile), run a 2-player round with one bomb placed per second for 60 seconds. Compare average / min fps with the F-key overlay against the pre-change baseline. No network-side tests required beyond existing 005/007 regression coverage.

**Acceptance Scenarios**:

1. **Given** a Mac SE running a 2-player round, **When** both players move for 10 seconds, **Then** average fps reported by the overlay is at least equal to the pre-change baseline (target: +2–5 fps net).
2. **Given** a Mac SE with 4 active bombs and 2 active explosions on screen, **When** one bomb detonates, **Then** no frame takes noticeably longer than today's baseline for the same scenario.
3. **Given** all three build targets (68k MacTCP, PPC MacTCP, PPC OT), **When** compiled with `BOMBERTALK_DEBUG=ON` and again with `BOMBERTALK_DEBUG=OFF`, **Then** every target builds with zero new warnings versus the 007 baseline.

---

### User Story 2 - Color Mac Developer Gets Quieter Debug Channel (Priority: P2)

A developer tailing BomberTalk's UDP debug channel (`socat UDP-RECV:7356`) during a multiplayer round on a color Mac today sees a flood of per-frame movement lines that drown out connects, bombs, and kills. After this change, movement debug no longer fires every frame; the debug channel reflects only meaningful state transitions during steady-state gameplay.

**Why this priority**: Debug-channel noise slows down troubleshooting and costs CPU/network: UDP broadcast per moving-player per frame is not free on a 6400. Removing the per-frame chatter is a quality-of-life win and a small but real perf win on color Macs.

**Independent Test**: Capture 10 seconds of `socat UDP-RECV:7356` output during steady-state gameplay with one player holding an arrow key. Count movement-related log lines; expect a drop from ~120+ lines (today) to effectively zero, while place/explode/kill lines still appear.

**Acceptance Scenarios**:

1. **Given** a color Mac build compiled with `BOMBERTALK_DEBUG=ON`, **When** the local player holds an arrow key for 5 seconds with no other events, **Then** no per-frame movement log appears on the UDP debug channel.
2. **Given** the same build, **When** a bomb is placed and explodes during that window, **Then** the placement, explosion, and any kill log lines still appear.

---

### User Story 3 - Developer Trusts the Free-Memory Readout (Priority: P3)

A developer reading the startup heap log line or reacting to the in-gameplay low-heap warning today can be misled by `FreeMem`, which ignores purgeable blocks and under-counts obtainable memory. After this change, the reported figure represents what a purge could actually obtain, matching the semantics documented in Inside Macintosh IV.

**Why this priority**: Not a player-visible change, but it keeps the project's low-heap diagnostic honest — the existing 256 KB threshold logic only makes sense against a "memory a purge could yield" figure.

**Independent Test**: Boot the app on each target and confirm the startup line `Free heap after init: X bytes` reflects `PurgeSpace()` semantics (verified by a one-time diagnostic comparison log, then removed).

**Acceptance Scenarios**:

1. **Given** a build on any target, **When** startup heap-check code runs, **Then** the reported value represents obtainable memory including purgeable blocks (i.e. the "total" result of `PurgeSpace`).
2. **Given** normal steady-state gameplay, **When** the periodic in-gameplay heap check fires, **Then** its threshold comparison uses the same `PurgeSpace`-based value.

---

### User Story 4 - Codebase Loses Its Dead Weight (Priority: P3)

A contributor opening the tilemap / renderer modules today will find a public function never called anywhere, plus a Mac SE branch in the PICT loader that cannot execute. After this change, both are gone. Reviewers stop wondering whether they need to update these code paths when modifying tilemap or renderer behaviour.

**Why this priority**: Pure quality win. Zero runtime impact, smaller cognitive surface.

**Independent Test**: `cppcheck --std=c89 --enable=unusedFunction -I include src/` reports no `TileMap_IsSolid` line; manual inspection of `LoadPICTResources` shows no `if (gGame.isMacSE)` branch; all three targets still build clean.

**Acceptance Scenarios**:

1. **Given** the post-change tree, **When** cppcheck is run as above, **Then** `TileMap_IsSolid` no longer appears as an unused function (because it has been removed from both `.c` and `.h`).
2. **Given** `renderer.c`'s `LoadPICTResources`, **When** inspected, **Then** it no longer contains any code gated on `gGame.isMacSE` (because that branch is statically unreachable).
3. **Given** all three build targets, **When** built, **Then** no build breaks from the removal.

---

### User Story 5 - PPC 6200 Player Gets More Round Headroom (Priority: P3, optional)

On a PPC 6200 with a tight 4 MB app partition, the title-screen sprite (~40 KB GWorld on color Macs) stays in memory for the entire session even after the player leaves the title screen. After this change, disposing the title sprite on menu → lobby transition returns those 40 KB to the heap before the round starts.

**Why this priority**: Only ships if the user explicitly approves, because it softens the project's "no allocation during gameplay" convention. The disposal happens pre-gameplay (menu transition), not during an active round, so it honours the spirit of the rule.

**Independent Test**: Log `PurgeSpace()` on entry to `Lobby_Init`; expect a ~40 KB delta on color Macs between the baseline build and the change; Mac SE unchanged (SE never loads title PICT).

**Acceptance Scenarios**:

1. **Given** user approval for FR-009, **When** the menu → lobby transition fires on a color Mac, **Then** reported obtainable memory increases by approximately the title sprite's GWorld size (≈ 40 KB, within ±5 KB).
2. **Given** the same build, **When** the player plays a full round and returns to lobby, **Then** the game does not attempt to redraw the title sprite (the title screen is single-entry per session).

---

### Edge Cases

- **Log level downgrade regression**: If per-frame debug logs are silenced, a developer debugging a movement bug loses visibility. Mitigation: provide either a runtime verbose toggle OR retain one log-per-direction-change (edge-triggered) rather than one log-per-frame. Implementation may choose either approach under FR-004.
- **Inlined rect assignments vs `SetRect`**: Hand-rolled four-field assignments must match QuickDraw's half-open rectangle convention (`right`/`bottom` exclusive). Mitigation: keep `SetRect` at system boundaries (window creation, one-time layout) for clarity; inline only inside per-frame AABB inner loops.
- **Dead Mac SE branch deletion loses future-proofing**: If someone later adds 1-bit PICT resources for Mac SE, the deleted branch must be re-added. Mitigation: leave a one-line comment at the deletion site pointing to the SE resource IDs still defined in `game.h` (`rPictTilesSE`, `rPictPlayerSE`, `rPictBombSE`, `rPictExplosionSE`, `rPictTitleSE`) so the re-add path is discoverable.
- **`TileMap_IsSolid` re-introduction**: If a future change needs a simple solid-tile query, callers must use `TileMap_GetTile(col,row) == TILE_WALL || == TILE_BLOCK`, or re-introduce the function. Mitigation: note in the commit message that `CheckTileSolid` (static, in player.c) is the in-project pattern for collision queries.
- **`PurgeSpace` availability**: Available since System 4.1; all targets ship System 6+. No Gestalt check required. Returns two values (`total`, `contig`); this feature only uses `total`.
- **Title-sprite disposal and update events**: If a window update event fires after menu transition but before normal draw, no code path may reference the freed GWorld. Mitigation: NULL the pointer after `DisposeGWorld`; existing fallback path already NULL-checks.
- **Coordinate packing bit-width (FR-008)**: `(col << 8) | row` assumes each fits in 8 bits; `MAX_GRID_COLS=31`, `MAX_GRID_ROWS=25` — safe. If grid dimensions ever grow past 255 this needs revisiting.
- **Caller-side bounds in FR-001**: Replacing `Bomb_ExistsAt(col,row)` calls with direct `gBombGrid[row][col]` reads inside `CheckTileSolid` drops the bounds check. The caller (`CollideAxis`) already clamps `minCol/maxCol/minRow/maxRow` to the valid map range before the inner loop, so unguarded direct access is safe there — but any other callsite that inlines must show a bounds-clamp precondition.

## Requirements *(mandatory)*

### Functional Requirements

**Core optimizations (must ship individually or together per the "independently mergeable" rule):**

- **FR-001 (P1)**: The tilemap-collision inner loop in the player module MUST NOT invoke a function per tile to obtain map column/row bounds or to test bomb occupancy. Map bounds (`cols`, `rows`) MUST be read once per collision call into local variables and reused inside the inner loop. Bomb occupancy MUST be a direct `gBombGrid[row][col]` read in the innermost check, guarded by the caller's pre-loop bounds clamp. Pass-through bomb logic MUST remain intact. *(Review item P1; Tricks of the Gurus p.802–805.)*

- **FR-002 (P1)**: The bomb-explosion kill-check inner loops (`bomb.c` around lines 161–180 and 244–263) and the collision-rect construction in the player module hot path MUST NOT invoke the Toolbox `SetRect` trap per iteration per frame. Rectangle construction in these per-frame loops MUST use direct struct-field assignment. The public `SetRect`-based API at system boundaries (window creation, one-time layout, screen-draw helpers) MUST remain unchanged. *(Review item P3; Tricks of the Gurus p.41867 Mac SE benchmark.)*

- **FR-003 (P1)**: The renderer's fallback `Renderer_DrawPlayer` path (currently renderer.c ~lines 924–965) MUST batch colour-state changes across players rather than setting `ForeColor`/`BackColor` inside each player's draw. The batching MUST mirror the existing per-tile-type pass pattern in `Renderer_RebuildBackground`. The PICT draw path MUST be unchanged. *(Review item P4.)*

- **FR-004 (P2)**: Per-frame debug logging on the movement and position-update hot paths MUST NOT fire every frame during steady-state gameplay. Specifically: `Player_Update`'s post-move log, `Player_SetPosition`'s per-packet log, and any equivalent per-frame `CLOG_DEBUG` in the bomb or net layers MUST be either (a) removed, (b) edge-triggered on a meaningful state change (direction change, target change of ≥1 tile), or (c) gated behind a runtime verbose flag off by default. Meaningful transitions (spawn, death, bomb place/explode, connect, disconnect, game over, position target received for a currently-inactive peer) MUST still be logged at `CLOG_INFO`. *(Review item P5.)*

- **FR-005 (P3)**: The unused public function `TileMap_IsSolid` (confirmed unused by cppcheck 2.21 multi-file scan) MUST be removed from both `src/tilemap.c` and `include/tilemap.h`. No caller exists in the current codebase. *(Cppcheck-confirmed dead code; new scope item not in the original review list.)*

- **FR-006 (P3)**: The statically-unreachable Mac SE branch inside `LoadPICTResources` (renderer.c ~lines 401–409) MUST be removed. `LoadPICTResources` is only invoked from `Renderer_Init` under `if (!gGame.isMacSE)`, so the internal `if (gGame.isMacSE)` branch cannot execute. The unreachable ternary in the mask-region construction at the same site (renderer.c ~lines 437–439) MUST likewise be simplified. A one-line code comment MUST be left pointing to the still-defined SE PICT resource IDs in `game.h` so a future re-add path is discoverable. *(Review item P7; manual reachability analysis — cppcheck does not flag control-flow dead code across functions.)*

- **FR-007 (P3)**: Low-heap reporting (`main.c` ~lines 271–273 and 363–367) MUST use `PurgeSpace()` for its "total obtainable" value rather than `FreeMem()`. The comparison threshold (256 KB, `LOW_HEAP_WARNING_BYTES`) MUST remain unchanged. *(Review item P8; Inside Macintosh IV p.7680.)*

**Optional optimizations (defer unless time allows; independently gateable):**

- **FR-008 (P3, optional)**: The renderer's dirty-tile list (`gDirtyListCol`, `gDirtyListRow` in renderer.c ~lines 97–98) MAY be stored as a single packed `short` array encoding `(col << 8) | row`. If done, memory footprint MUST drop by approximately 1.5 KB; observable behaviour MUST be unchanged. *(Review item P9.)*

- **FR-009 (P3, optional, user-approval gated)**: The title-screen sprite GWorld (`gTitleSprite`) MAY be disposed on the first transition out of the menu screen (menu → lobby) to reclaim ~40 KB on color Macs. If implemented: the sprite pointer MUST be NULL'd after `DisposeGWorld`; all draw paths that reference it MUST NULL-check (they already do); the game MUST NOT require redrawing the title for the remainder of the session. This FR is gated because it softens the project's "no allocation during gameplay" convention — although the disposal itself happens pre-gameplay at a screen boundary, it introduces a lifecycle beyond the current pure init/shutdown pair. *(Review item M1.)*

**Non-functional requirements (apply to all FRs):**

- **FR-N1**: All changes MUST compile clean on the three build targets (68k MacTCP `build-68k/`, PPC MacTCP `build-ppc-mactcp/`, PPC OT `build-ppc-ot/`) with C89 (`-std=c89`, no `//` comments, no mixed declarations, no VLAs, no `stdint.h`). No new compiler warnings versus the 007 baseline.
- **FR-N2**: No change MAY alter the network protocol wire format or bump `BT_PROTOCOL_VERSION` (currently 5). Change is local-timing / local-draw only.
- **FR-N3**: No change MAY add a public header API beyond at most one inline helper if strictly required. `TileMap_IsSolid` is actively being *removed* from the public API under FR-005.
- **FR-N4**: FR-001 … FR-007 MUST each be independently mergeable: if hardware testing reveals a regression in one, the others still ship.
- **FR-N5**: No change MAY require re-running the existing 005 / 006 / 007 hardware test plans end-to-end; the change set MUST be verifiable by spot-check (FPS reading via F-key overlay, log capture via `socat`, heap log line at startup) on top of existing regression coverage.

### Key Entities *(n/a — no new data structures)*

This feature alters local-only timing, draw-call batching, logging, and dead-code removal. No network message, persistent data, public API contract, or game-state structure changes. `TileMap_IsSolid` is the only public API item affected, and it is being *removed*, not replaced.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On Mac SE hardware (or the QEMU Quadra SE-equivalent profile), average gameplay fps during a 2-player round with active bombs is at least equal to the pre-change baseline, with a target improvement of +2–5 fps net from FR-001 + FR-002 + FR-003 combined. Measured via the in-game F-key FPS overlay over a 60-second sample.
- **SC-002**: On color Mac targets with `BOMBERTALK_DEBUG=ON`, the UDP debug channel emits zero movement-related log lines during a 5-second steady-state hold of the arrow key; bomb placement, explosion, and kill lines still appear. Verified via `socat UDP-RECV:7356`.
- **SC-003**: On all three targets, a full build completes with zero new compiler warnings compared to the 007 baseline, with existing CMake flags unchanged.
- **SC-004**: The startup heap log line reports obtainable memory including purgeable blocks (`PurgeSpace` semantics); the value does not falsely trigger the 256 KB low-heap warning on a fresh boot on PPC 6200.
- **SC-005**: After a full round (place bombs, take kills, return to lobby), no new leak is observed — post-round obtainable memory matches pre-round within ±1 KB on each target.
- **SC-006**: `cppcheck --std=c89 --enable=unusedFunction -I include src/` reports no unused-function findings after the change (`TileMap_IsSolid` is the only current finding; removing it clears the report).
- **SC-007 (optional, only if FR-009 enabled)**: On color Macs, obtainable memory after the first menu → lobby transition is approximately `baseline + titleSpriteSize` (≈ 40 KB delta within ±5 KB), and the game never needs to redraw the title sprite for the remainder of the session.

### Assumptions

- Pre-change Mac SE baseline fps is the 2026-04-10 measurement recorded in CLAUDE.md (~10–19 fps). If subsequent fixes (e.g. 006, 007) have moved the baseline, the current run's baseline is captured once before implementation begins.
- Hardware re-testing uses the existing classic-mac-hardware-mcp flow documented in CLAUDE.md; no new test infrastructure required.
- No new map formats or grid sizes are added in this change; `MAX_GRID_COLS=31`, `MAX_GRID_ROWS=25` holds for packed-coord encoding under FR-008.
- The constitutional rule "All memory allocated at init time — no malloc during gameplay" is interpreted as "no allocation / disposal during an active round (`SCREEN_GAME`)". Screen transitions between loading / menu / lobby are not "during gameplay" for the purposes of FR-009.
- `PurgeSpace` is available on all three targets (introduced System 4.1; all targets ship System 6+). No Gestalt check needed.
- The `gMap.tiles` field (declared `[MAX_GRID_ROWS][MAX_GRID_COLS]`) continues to cover more space than the active `rows × cols` sub-region; the `memset` calls in `TileMap_Init` and `TileMap_LoadFromResource` are *not* dead (they initialise the inactive sub-region to `TILE_FLOOR`). The original review item P6 is therefore withdrawn from scope.

### Scope Boundaries

**In scope:**
- Cache hoisting in `CollideAxis` and `ExplodeBomb` inner loops (FR-001).
- Inline `SetRect` only inside per-frame AABB inner loops in `bomb.c` and `player.c` (FR-002).
- Batch colour-state changes in the renderer fallback player draw (FR-003).
- Reduce per-frame debug log volume on movement / position paths (FR-004).
- Remove the unused public `TileMap_IsSolid` function (FR-005).
- Delete the statically-unreachable Mac SE branch in `LoadPICTResources` (FR-006).
- Switch `FreeMem` → `PurgeSpace` in heap warnings (FR-007).
- Optional: packed dirty list (FR-008), title disposal with explicit approval (FR-009).

**Out of scope / explicitly rejected:**
- Swapping `CopyBits(srcCopy, maskRgn)` for `CopyMask` — *Tricks of the Gurus* p.6239 explicitly advises against `CopyMask` in game-critical paths; book-verified non-starter. Current code path is already book-optimal.
- Removing the `memset` in `TileMap_Init` or `TileMap_LoadFromResource` — closer analysis shows the loops only cover the active `rows × cols` sub-region of the larger `[25][31]` array, so the `memset` is *not* dead work; the original review item P6 is withdrawn.
- Any change to network message formats, protocol version, or peer-matching logic.
- Any new public API beyond at most one inline helper.
- Any change to the screen state machine or menu flow (apart from the title-sprite dispose hook in optional FR-009).
- Any change detected but left untouched by cppcheck: variable-scope reductions (rejected — conflict with C89 top-of-block-declarations rule); the `ownerIdx >= 0` always-true check in `ExplodeBomb` (left in place with its self-documenting comment as defensive guard).
- Any new hardware-test plan; reuse existing 005 / 006 / 007 coverage.

### References (fact-checked 2026-04-18)

**Books (under `books/`):**

- *Tricks of the Mac Game Programming Gurus* (1995), Appendix A "Avoiding Function Call and Toolbox Trap Overhead" (p.802–805). Mac SE benchmark (p.41867 in the text dump): function-call 475 ticks / 300 000 calls; macro 77 ticks / 300 000 calls (≈ 6× speedup); trap dispatch 926 ticks / 300 000 calls (≈ 12× slower than macro). Supports FR-001, FR-002.
- *Tricks of the Mac Game Programming Gurus* (1995), p.6239: "Stay away from CopyBits()'s fanciful cousins, CopyMask() and CopyDeepMask(). Most of the operations you might want to perform with CopyMask() will be better served by various skip-draw methods." Rules out the originally-proposed P2.
- *Inside Macintosh Volume IV* (1986), `PurgeSpace` entry (≈ p.7680 in our text dump): "`PurgeSpace` returns in `total` the total amount of space in bytes that could be obtained by a general purge (without actually doing the purge); this amount includes space that is already free." Supports FR-007.
- *Sex, Lies and Video Games* (1996), p.5311 and surrounding: `CopyMask` history and performance notes, corroborating the rejection of the CopyMask swap.
- Existing project record in `CLAUDE.md` under 006-renderer-optimization: confirms the current `srcCopy + maskRgn` path is the book-endorsed choice; FR-003 builds on that pattern by batching colour state, not replacing the draw path.

**Static analysis:**

- `cppcheck 2.21 --std=c89 --enable=all --inconclusive -I include src/` run on 2026-04-18 confirmed `TileMap_IsSolid` is unused across the codebase (FR-005). Other findings were evaluated: `bomb.c:188` always-true is self-documented defensive code; variable-scope suggestions are rejected under C89; all other hot-path claims (FR-001 through FR-004, FR-006, FR-007) are not cppcheck-detectable (they are performance, reachability, or semantic issues) and remain supported by the book references above.
