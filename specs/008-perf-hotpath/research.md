# Research: Hot-Path Performance & Memory Optimizations (008)

**Phase**: 0 (Outline & Research)
**Date**: 2026-04-18

All `NEEDS CLARIFICATION` items from the plan were resolved by spec-time fact-checking against the books and a cppcheck pass. This document captures the research that shaped each FR's implementation approach.

## R1 — Is it safe to inline `Bomb_ExistsAt` to a direct `gBombGrid[row][col]` read in `CheckTileSolid`?

**Decision**: Yes. Expose a `static inline` accessor (`Bomb_GridCell`) in `bomb.h` that returns the raw grid byte; call it from `CheckTileSolid` inside player.c.

**Rationale**: `CollideAxis` already performs the bounds clamp needed (`player.c:201-205`: `minCol/maxCol/minRow/maxRow` clamped to valid map range). The `Bomb_ExistsAt` function's own bounds check is therefore redundant in this call site. Removing it saves two `TileMap_GetCols()/GetRows()` function calls + one `Bomb_ExistsAt` function call per tile visited. On Mac SE: each function call ≈ 1.6 µs (per Tricks of the Gurus p.41878 Mac SE benchmark: 475 ticks / 300 000 calls). Three calls × ~4 tiles × 4 players × 30 fps = ~576 calls/sec of function overhead eliminated.

**Alternatives considered**:
- (A) Expose `gBombGrid` as a `const unsigned char (*)[MAX_GRID_COLS]` via a getter — rejected, leaks storage shape.
- (B) Keep `Bomb_ExistsAt` but give it an inline body in `bomb.h` — rejected because cppcheck would still flag it as multi-translation-unit noise and C89 has no `inline` keyword; best we can do is a `static` function in a header, which triggers "unused static" warnings in translation units that don't call it.
- (C) Chosen: add `Bomb_GridCell` as a `static` function in `bomb.h` guarded by `#ifdef` or simply inlined at the single caller via a short helper. Actual final shape: a `#define BOMB_GRID_CELL(c,r) (gBombGrid[r][c])` macro in `bomb.h`, paralleling the existing `TILEMAP_TILE` macro (tilemap.h:29). Same pattern, zero new functions, zero new C89-compat concerns.

**Implementation note**: macro version requires `gBombGrid` to be visible; move its declaration from `bomb.c` (currently `static unsigned char gBombGrid[...]` at bomb.c:17) to `bomb.h` as `extern unsigned char gBombGrid[...]` plus definition in bomb.c. This is the "at most one inline helper" allowed by FR-N3. We are using the full budget.

## R2 — How aggressively should `SetRect` be inlined under FR-002?

**Decision**: Only in per-frame inner loops. Keep `SetRect` at one-time / boundary sites.

**Rationale**: Tricks of the Gurus p.41867-82 Mac SE benchmark shows a 12× gap between trap dispatch (926 ticks / 300 000 calls) and macro inline (77 ticks / 300 000 calls) for `SetPt`, which has identical semantics to `SetRect` (both just assign a fixed count of fields). Inlining all `SetRect` calls tree-wide would bloat code size and obscure intent at boundary sites where the call is invoked once per screen transition or window creation. Surgical inlining inside the AABB kill-check loops (bomb.c:164-167, 247-250) and player collision rect (player.c hitbox ops) captures the hot-path win without the cleanup cost elsewhere.

**Specific sites to inline** (identified by reading the diff preview for FR-002):

1. `bomb.c` — ExplodeBomb's explosion AABB rect around the local-kill check (bomb.c:164-167).
2. `bomb.c` — Bomb_Update's per-frame per-explosion rect (bomb.c:247-250).
3. `player.c` — hitbox rect computation in `Player_GetHitbox` (player.c:33-47) if measured hot; currently called once per explosion check per frame — inline.

**Alternatives considered**: tree-wide inline (rejected — cost too high, readability worse), no inline (rejected — book explicitly favours inline for per-frame math).

## R3 — FR-003 fallback draw batching: does it conflict with the existing `Renderer_BeginSpriteDraw` bracket?

**Decision**: No conflict. Refactor `Renderer_DrawPlayer` fallback so it uses a private two-pass helper invoked once, rather than setting colour state per invocation.

**Rationale**: The bracket already hoists port save/restore and colour state to once per frame (renderer.c:835-848). The existing `Renderer_DrawPlayer` fallback re-asserts `ForeColor(blackColor); BackColor(whiteColor)` etc. *inside* each player's body draw. The redundant asserts are the waste. Chosen implementation: keep the caller loop in `Game_Draw` (screen_game.c:330-344) untouched. Inside `renderer.c`, the fallback branch of `Renderer_DrawPlayer` gets a private flag-based pass indicator — but the simpler fix is to restructure `Game_Draw` to invoke a new private helper `Renderer_DrawPlayersFallback(players[], count)` that does the two-pass traversal. Since `Renderer_DrawPlayer` remains a public entry, and the caller only wants the loop done once, the cleanest fix is:

- Make `Renderer_DrawPlayer` skip all redundant colour asserts when `gSpriteDrawActive == TRUE` (the bracket has already set them). The per-player loop in `Game_Draw` now does one colour change per player — acceptable.

**Alternative chosen after further thought**: Rather than adding a new public helper, modify `Renderer_DrawPlayer` fallback to:

1. On color Macs: single `RGBForeColor(kPlayerColors[playerID & 3])` per call (one colour change per draw), skip the redundant `ForeColor(blackColor); FrameRect` frame by keeping a cached "last frame colour" static so `FrameRect` uses the right colour without re-asserting.
2. On Mac SE: skip the redundant `ForeColor(blackColor); BackColor(whiteColor)` at entry (already guaranteed by bracket). Skip the trailing `ForeColor(blackColor)` — the bracket end restores port state anyway.

Net: ~6-8 fewer trap calls per player per frame on SE, ~4 fewer on color Macs. Matches the FR-003 "~16 ForeColor traps/frame eliminated on Mac SE" claim.

**Alternatives considered**: Full two-pass restructure — rejected as higher risk and more diff for similar win.

## R4 — FR-004: edge-trigger debug log vs runtime verbose flag?

**Decision**: Edge-trigger.

**Rationale**: Zero new runtime state; cost is ~4 `short` comparisons per frame. Verbose flag would require a debug key binding, a `gGame.verboseLog` field, and a documentation update. The edge-trigger pattern already exists in the codebase (`screen_game.c` gLastSentPX/PY/Facing gating net sends — a proven local pattern).

**Specific behaviour**:

- `Player_Update` post-move `CLOG_DEBUG` fires only if `(p->pixelX / tileSize) != lastLoggedCol || (p->pixelY / tileSize) != lastLoggedRow || p->facing != lastLoggedFacing`. Static locals per caller.
- `Player_SetPosition` per-packet `CLOG_DEBUG` fires only on facing change OR target moved ≥ 1 tile from last-logged target. Static locals per playerID (small `[MAX_PLAYERS]` arrays inside the function).
- If removing the call entirely is simpler and log visibility is acceptable, that's the fallback.

**Alternatives considered**: Runtime flag (rejected — scope creep); full removal (acceptable fallback if edge-trigger proves awkward).

## R5 — FR-005: is `TileMap_IsSolid` truly unused?

**Decision**: Yes. Remove.

**Rationale**: `cppcheck 2.21 --enable=unusedFunction --std=c89 -I include src/` reports exactly one unused function: `TileMap_IsSolid`. `grep -rn TileMap_IsSolid src/ include/` returns only the declaration (tilemap.h:16) and the definition (tilemap.c:183). All historical callsites have been migrated to `TILEMAP_TILE(map,c,r)` macro access + inline `== TILE_WALL || == TILE_BLOCK` tests in `CheckTileSolid` (player.c:139-140). The function adds public-API surface with no benefit.

**Alternatives considered**: Deprecate with `__attribute__((deprecated))` — rejected (not C89; not worth it for zero-caller function).

## R6 — FR-006: is the Mac SE branch in `LoadPICTResources` truly unreachable?

**Decision**: Yes. Remove the branch; keep a one-line comment pointer.

**Rationale**: `LoadPICTResources` is called from one site (`renderer.c:531-533`) inside `Renderer_Init`, under `if (!gGame.isMacSE)`. `gGame.isMacSE` is assigned once in `DetectScreenSize` (main.c:92-106) before `Renderer_Init` runs (main.c:330 → 357) and is never reassigned. Therefore `gGame.isMacSE == TRUE` never holds inside `LoadPICTResources` — the inner `if (gGame.isMacSE)` branch at renderer.c:401-409 is statically dead. The `gGame.isMacSE ? 240 : 80` ternary in the mask-region step (renderer.c:437-439) is similarly dead — simplify to the color-Mac constants.

**Alternatives considered**: Leave in place with a `#if 0` guard — rejected (future readers think it is activatable).

## R7 — FR-007: `PurgeSpace` availability on all three targets?

**Decision**: Safe on all targets.

**Rationale**: Inside Macintosh IV p.7680 documents `PurgeSpace` without any System version constraint beyond 4.1. All BomberTalk targets ship System 6+ (Mac SE = System 6; PPC 6200 = System 7.5.3; PPC 6400 = System 7.6.1). Retro68 universal headers expose it. The call signature returns two `long` values; only `total` is consumed. Pattern is:

```c
long total, contig;
PurgeSpace(&total, &contig);
if (total < LOW_HEAP_WARNING_BYTES) CLOG_WARN(...);
```

**Alternatives considered**: Gestalt-guard call — rejected (System 6+ guaranteed).

## R8 — FR-008 (optional): packed dirty-list layout on 68k?

**Decision**: Safe but low-priority win.

**Rationale**: Packing `(col << 8) | row` into a single `short` requires one word-aligned load per iteration instead of two from parallel arrays. Cache behaviour unchanged at this small array size (~1.5 KB). The instruction count per iteration drops from `move.w (a0,d0); move.w (a1,d0)` to `move.w (a0,d0)` plus a pair of shifts/ands. On 68000 this is ~4 cycles saved per tile. At typical dirty counts of 10-30 per frame, savings are ~120 cycles/frame — negligible but directionally positive.

The real win is the 1.5 KB reclaim, which on a 4 MB Mac SE is 0.0375% of main memory — also negligible. Leave as optional, defer if no free time.

**Alternatives considered**: Keep as-is (default). Both are acceptable.

## R9 — FR-009 (optional): interaction with update events after title-sprite disposal?

**Decision**: Safe provided pointer is NULL'd after dispose and existing NULL-check in draw paths is preserved.

**Rationale**: Grep for `gTitleSprite` returns: declaration/definition (renderer.c), `LoadPICTResources` assignment (renderer.c:418), mask-region creation (renderer.c:437), and `Renderer_Shutdown` disposal (renderer.c:642-646). No screen's `Draw` function currently calls a helper that uses the title — the title is drawn only by `screen_loading.c` / `screen_menu.c` which are not revisited after menu-to-lobby. A window update event after that transition repaints the *current* screen (lobby or game), not the menu, so no stale reference is dereferenced.

If we ship FR-009:
1. Add `Renderer_DisposeTitle` public helper or inline the dispose in `Lobby_Init`.
2. Guarantee `gTitleSprite == NULL` + `gTitleMaskRgn == NULL` after the call.
3. Test with deliberate menu → lobby transition + window drag to force update events.

**Alternatives considered**: Lazy dispose inside a general "one-shot screen resource" system — rejected (scope creep).

## R10 — Interaction with 005 (authority / stagger) and 007 (wall-clock timers)?

**Decision**: No interaction.

**Rationale**: 005 and 007 changes live in screen_game.c, screen_lobby.c, and net.c (for the authority check). Our feature only touches:

- `player.c` (movement hot path) — not touched by 005/007.
- `bomb.c` (AABB checks) — not touched by 005/007.
- `renderer.c` (fallback draw + LoadPICTResources) — touched by 006 previously but our changes are additive/subtractive only within the fallback path, not the mask-region / lock-batching paths established by 006.
- `tilemap.c`, `tilemap.h` — not touched by 005/007.
- `main.c` (heap check) — the heap check site was introduced in 005; we are changing the readout function only, not the timing.

No regression surface against prior features.

## Summary of decisions

| FR | Approach | Key risk | Mitigation |
|---|---|---|---|
| 001 | `BOMB_GRID_CELL` macro + `extern` array; cache `TileMap_GetCols/Rows` locals | Macro visible globally | Mirror existing `TILEMAP_TILE` pattern |
| 002 | Inline `SetRect` only in identified per-frame loops | Off-by-one on right/bottom | Leave boundary-site `SetRect` calls intact |
| 003 | Drop redundant colour asserts inside fallback `Renderer_DrawPlayer` | Colour state assumption changes | Keep existing bracket ordering; only remove calls that are provably redundant given the bracket |
| 004 | Edge-triggered `CLOG_DEBUG` in `Player_Update`, `Player_SetPosition` | Loss of diagnostic detail | Keep every *transition* logged; also fine to remove log entirely if edge-trigger is awkward |
| 005 | Remove `TileMap_IsSolid` from `.c` and `.h` | Future caller reintroduced | Commit message notes the replacement pattern |
| 006 | Delete unreachable SE branch + ternary in `LoadPICTResources`; leave one-line comment pointing to SE resource IDs | Future Mac SE PICT support | Comment makes re-add path discoverable |
| 007 | Two call-site swaps `FreeMem()` → `PurgeSpace(&total,&contig); use total` | None | Straightforward |
| 008 (opt) | Pack `gDirtyList` into single `short[]` | Decoder arithmetic bugs | Encapsulate in inline helpers `DIRTY_COL(x)` / `DIRTY_ROW(x)` |
| 009 (opt) | Dispose title sprite in `Lobby_Init` (first entry only) | Update-event after dispose | NULL-guarded draw path already exists |
