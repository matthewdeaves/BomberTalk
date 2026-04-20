---

description: "Task list for 008-perf-hotpath feature implementation"
---

# Tasks: Hot-Path Performance & Memory Optimizations (008)

**Input**: Design documents from `/specs/008-perf-hotpath/`
**Prerequisites**: plan.md ✅, spec.md ✅, research.md ✅, data-model.md ✅, contracts/tilemap-api.md ✅, quickstart.md ✅

**Tests**: No automated-test harness in scope — the project is a cross-compiled Classic Mac game verified via on-target manual test per Principle II and CLAUDE.md. Every FR's acceptance check is an on-target manual action defined in `quickstart.md`.

**Organization**: One commit-sized task per functional requirement, grouped by the user story it serves. FR-N4 (independently mergeable) is preserved — every FR lands in its own commit and can be reverted without affecting the others. Builds on all three targets after each task (or after the last task touching a given file in a batch).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Maps task to user story from spec.md (US1–US5)
- Every task cites the exact file and the commit-message prefix to use

## Path Conventions

- Cross-compiled native game at repo root. Sources in `src/`, headers in `include/`, books in `books/`, specs in `specs/`. Build outputs in `build-68k/`, `build-ppc-mactcp/`, `build-ppc-ot/` (CMake-generated, untracked).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Baseline capture before any source changes so regressions are measurable.

- [X] T001 Capture pre-change cppcheck baseline: `cppcheck --std=c89 --enable=all --inconclusive --suppress=missingIncludeSystem -I include src/ 2>&1 > /tmp/cppcheck-before.txt` and `cppcheck --std=c89 --enable=unusedFunction -I include src/ 2>&1 > /tmp/cppcheck-unused-before.txt` (expect one unusedFunction hit: `TileMap_IsSolid`, one knownConditionTrueFalse at `src/bomb.c:188`)
- [ ] T002 [P] Build all three targets from clean on branch tip to confirm a green baseline: `build-68k/`, `build-ppc-ot/`, `build-ppc-mactcp/` per the recipe in `specs/008-perf-hotpath/quickstart.md`. Record any pre-existing warnings; SC-003 will require zero *new* warnings versus this set.
- [ ] T003 [P] On a Mac SE or QEMU SE-equivalent, run a 60 s 2-player round with F-key FPS overlay active; record average / min fps as the pre-change baseline for SC-001 verification. Store the number in the feature branch's scratch note (not committed).

**Checkpoint**: Baselines captured. Every subsequent task compares its output against these numbers.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Plumbing that FR-001 requires before it can be implemented safely.

**⚠️ CRITICAL**: Only T004 is blocking. The other FRs do NOT depend on it, and if T004 is deferred, FR-001 alone is delayed — the rest of the feature can still land.

- [X] T004 [US1] Promote the bomb occupancy grid to the `bomb` module header so a macro can read it from `player.c`. Move the declaration of `gBombGrid[MAX_GRID_ROWS][MAX_GRID_COLS]` from `static` in `src/bomb.c` to an `extern` declaration in `include/bomb.h`, keeping the *definition* in `src/bomb.c`. Add the helper macro `#define BOMB_GRID_CELL(col, row) (gBombGrid[(row)][(col)])` in `include/bomb.h`, paralleling the existing `TILEMAP_TILE` macro in `include/tilemap.h`. This consumes the FR-N3 "at most one inline helper" budget. Build all three targets and confirm zero new warnings. Commit prefix: `perf: US1/T004 expose BOMB_GRID_CELL macro for hot-path inline access`

**Checkpoint**: `gBombGrid` visible via macro from `src/player.c`. FR-001 can now proceed.

---

## Phase 3: User Story 1 — Mac SE Player Gets Smoother Gameplay (Priority: P1) 🎯 MVP

**Goal**: Lift Mac SE gameplay fps by ≥ 2 fps against the baseline captured in T003 by removing per-tile function-call and trap overhead from the collision and explosion-kill hot paths, and by removing redundant per-sprite colour-state traps from the renderer fallback path.

**Independent Test**: Complete T003 baseline exists → after T005 + T006 + T007 the Mac SE F-key fps reading during an active round is ≥ baseline with a target gain of +2 fps. Collision / kill / render correctness unchanged per the acceptance scenarios in spec §User Story 1.

### Implementation for User Story 1

- [X] T005 [US1] FR-001: in `src/player.c` `CollideAxis`, cache `TileMap_GetCols()` and `TileMap_GetRows()` into `mapCols` / `mapRows` locals **once** at function entry (after the `TileMap_Get()` call), and reuse them in the `minCol`/`maxCol`/`minRow`/`maxRow` clamp lines that currently call them directly. In `CheckTileSolid` (same file), replace the `Bomb_ExistsAt(col, row)` call with the new `BOMB_GRID_CELL(col, row)` macro read — the caller has already bounds-clamped, so the direct read is safe. Preserve the existing pass-through-bomb logic verbatim. Build all three targets. Run a single movement test (wall bump, bomb walk-off, corner slide) to confirm no correctness regression. Commit prefix: `perf: US1/T005 FR-001 cache TileMap bounds and inline bomb-grid read in CollideAxis` (depends on T004)
- [X] T006 [US1] FR-002: in `src/bomb.c` `ExplodeBomb` (around lines 161–180) replace the per-iteration `SetRect(&expRect, ...)` call with direct four-field assignment `expRect.left = ts * gExplosions[e].col; expRect.top = ts * gExplosions[e].row; expRect.right = expRect.left + ts; expRect.bottom = expRect.top + ts;`. Do the same in `Bomb_Update`'s per-frame AABB kill-check loop (around lines 244–263). Boundary `SetRect` calls (e.g. in non-hot paths) remain untouched. Build all three targets. Verify with the bomb-walk and corner-kill scenarios from quickstart §FR-002. Commit prefix: `perf: US1/T006 FR-002 inline SetRect in bomb AABB kill-check hot loops`
- [X] T007 [US1] FR-003: in `src/renderer.c` `Renderer_DrawPlayer` fallback branch (around lines 924–965), drop the now-redundant `ForeColor(blackColor); BackColor(whiteColor)` re-asserts at function entry and the trailing `ForeColor(blackColor)` after marker-draw — all are already established once per frame by `Renderer_BeginSpriteDraw` and restored by `Renderer_EndSpriteDraw`. Keep the single `RGBForeColor(kPlayerColors[playerID & 3])` / `ForeColor(whiteColor)` calls that encode the player-specific colour. On Mac SE, skip the entry `ForeColor(blackColor); BackColor(whiteColor)` pair — the bracket guarantees them. On color Macs, keep the `ForeColor(blackColor); FrameRect(&dstRect)` pair (the frame colour differs from the body colour). Build all three targets. Run the PICT-absent fallback scenario from quickstart §FR-003 to confirm no visual regression. Commit prefix: `perf: US1/T007 FR-003 drop redundant colour asserts in fallback player draw`

**Checkpoint**: US1 MVP complete. On a Mac SE, rerun the 60 s 2-player round from T003; compare fps; expect ≥ baseline, target +2 fps. Collision and render correctness unchanged.

---

## Phase 4: User Story 2 — Color Mac Developer Gets Quieter Debug Channel (Priority: P2)

**Goal**: Eliminate per-frame movement log flood on the UDP debug channel so meaningful events (bomb place, explode, kill, connect, disconnect) are legible in real-time.

**Independent Test**: With the color Mac build (`BOMBERTALK_DEBUG=ON`) and `socat -u UDP-RECV:7356,reuseaddr -` listening, hold an arrow key for 5 s with no other events — zero movement log lines appear. Bomb place / explode / kill events during that window still appear.

### Implementation for User Story 2

- [X] T008 [US2] FR-004: in `src/player.c` `Player_Update`, replace the unconditional per-frame `CLOG_DEBUG("P%d move ...")` (around lines 469–472) with an edge-triggered version: fire only when `p->gridCol != sLastLoggedCol[playerID] || p->gridRow != sLastLoggedRow[playerID] || p->facing != sLastLoggedFacing[playerID]`, and update the statics on each fire. Declare the three statics as `static short sLastLoggedCol[MAX_PLAYERS] = {-1, -1, -1, -1};` etc. at file scope, above the function, in C89 style. In the same file in `Player_SetPosition` (around lines 126–127), apply the same pattern keyed by `playerID` — fire only when the target moved ≥ 1 tile from the previously-logged target or the facing changed. Build all three targets. Run the quickstart §FR-004 `socat` capture recipe. Commit prefix: `perf: US2/T008 FR-004 edge-trigger movement debug logs`

**Checkpoint**: US2 complete. `socat` capture shows zero movement lines during steady-state hold. Direction changes / tile crossings still logged once each.

---

## Phase 5: User Story 3 — Developer Trusts the Free-Memory Readout (Priority: P3)

**Goal**: Heap-diagnostic readout reports obtainable memory (purge-eligible + free) rather than strictly-free, matching Inside Macintosh IV semantics.

**Independent Test**: Startup log line `Free heap after init: X bytes` reflects `PurgeSpace()` semantics (informally: X is ≥ the former `FreeMem()` value; formally: a temporary debug print confirms X == PurgeSpace total).

### Implementation for User Story 3

- [X] T009 [US3] FR-007: in `src/main.c`, replace the two `FreeMem()` callsites (around lines 271–273 in the periodic heap-check block and around lines 363–367 in the post-init heap report) with `PurgeSpace(&total, &contig)` using freshly declared locals at top-of-block per C89 rules, and use the `total` value in place of the former `FreeMem()` call. Keep the 256 KB threshold (`LOW_HEAP_WARNING_BYTES`) unchanged. Build all three targets. Boot on each target; compare the startup log to the pre-change output; confirm the number does not falsely trigger the low-heap warning on PPC 6200. Commit prefix: `perf: US3/T009 FR-007 use PurgeSpace for low-heap reporting`

**Checkpoint**: US3 complete. Heap readout matches the Inside Macintosh IV definition.

---

## Phase 6: User Story 4 — Codebase Loses Its Dead Weight (Priority: P3)

**Goal**: Remove cppcheck-confirmed unused function and a statically-unreachable code branch so reviewers stop wondering whether these paths are load-bearing.

**Independent Test**: `cppcheck --std=c89 --enable=unusedFunction -I include src/` reports zero unused functions after T010. `grep -rn 'if (gGame.isMacSE)' src/renderer.c` returns zero hits inside `LoadPICTResources` after T011. All three targets build clean.

### Implementation for User Story 4

- [X] T010 [P] [US4] FR-005: remove `int TileMap_IsSolid(short col, short row);` declaration from `include/tilemap.h` (currently at line 16) and remove the function body from `src/tilemap.c` (currently at lines 183–191). Run `grep -rn TileMap_IsSolid src/ include/` — expect zero hits. Build all three targets. Rerun `cppcheck --std=c89 --enable=unusedFunction -I include src/` — expect zero unused-function findings. Commit prefix: `cleanup: US4/T010 FR-005 remove unused TileMap_IsSolid (cppcheck-confirmed)`
- [X] T011 [P] [US4] FR-006: in `src/renderer.c` `LoadPICTResources` (around lines 395–449), remove the `if (gGame.isMacSE) { ... } else { ... }` outer wrapper, keeping only the else-branch body (the one that loads `rPictTiles` / `rPictPlayerP0..P3` / `rPictBomb` / `rPictExplosion` / `rPictTitle`). Replace the dead mask-region ternary `gGame.isMacSE ? 240 : 80` (and its sibling for width) with the plain color-Mac constants `320` and `128`. Leave one comment line at the deletion site: `/* Mac SE uses rectangle fallback path; rPict*SE resource IDs (see game.h) are reserved for future Mac SE PICT support. */`. Build all three targets. Mac SE output must be unchanged (its code path never entered `LoadPICTResources`). Commit prefix: `cleanup: US4/T011 FR-006 delete unreachable Mac SE branch in LoadPICTResources`

**Checkpoint**: US4 complete. Dead weight removed. Cppcheck baseline for the unused-function pass is now clean.

---

## Phase 7: User Story 5 — PPC 6200 Player Gets More Round Headroom (Priority: P3, optional, gated)

**Goal**: Reclaim ~40 KB on color Macs by disposing the title sprite at menu → lobby transition.

**⚠️ Gated on explicit user approval of spec FR-009. Skip this phase unless approval is recorded in the commit / PR discussion.**

**Independent Test**: Temporary diagnostic log of `PurgeSpace` total before and after the first `Lobby_Init` call shows a ~40 KB delta on color Macs. Mac SE unchanged (never loads title PICT). Subsequent window update events do not attempt to redraw the title.

### Implementation for User Story 5

- [ ] T012 [US5] FR-009 (GATED): in `src/screen_lobby.c` `Lobby_Init`, add a one-shot dispose of `gTitleSprite` and `gTitleMaskRgn` on first entry per session. Preferred approach: add a public helper `void Renderer_DisposeTitle(void);` in `include/renderer.h` + body in `src/renderer.c` that calls `UnlockPixels` + `DisposeGWorld` + NULL-out on `gTitleSprite`, then `DisposeRgn` + NULL-out on `gTitleMaskRgn`, with internal NULL-guards so double-call is a no-op. Invoke it once from `Lobby_Init` after the existing cleanup block. Temporarily add a `CLOG_INFO("title dispose delta: %ld", ...)` diagnostic around the call measuring `PurgeSpace` total; verify the ~40 KB reclaim on a color Mac; **remove the diagnostic before committing**. Note this changes the FR-N3 budget because `Renderer_DisposeTitle` is a new public helper — the FR-001 macro remains the single "inline helper"; `Renderer_DisposeTitle` is an additional non-inline entry and must be justified in the commit message as the minimum surface needed for FR-009 (alternatively: inline the dispose in `Lobby_Init` directly and leave the renderer's title-pointer visibility issue as-is — this keeps the FR-N3 budget intact; choose this path unless there's a reason to expose the helper). Build all three targets. Commit prefix: `perf: US5/T012 FR-009 dispose title sprite on menu→lobby transition (gated)`

**Checkpoint**: US5 complete (if enabled). Color Mac heap headroom increased by ~40 KB post-menu.

---

## Phase 8: Optional Storage Optimisation

**Goal**: Minor memory win via packed dirty-list coordinates. Optional; defer if time-constrained.

**Independent Test**: `wc -c build-*/BomberTalk.BIN` size non-regressing; renderer visual output unchanged across a full round.

### Implementation

- [ ] T013 [P] [US1-EXT] FR-008 (OPTIONAL): in `src/renderer.c`, collapse `gDirtyListCol[MAX_GRID_ROWS * MAX_GRID_COLS]` + `gDirtyListRow[...]` into a single `short gDirtyList[MAX_GRID_ROWS * MAX_GRID_COLS]` array with packed `(col << 8) | row` encoding. Introduce two private helper macros `DIRTY_COL(x) ((short)(((x) >> 8) & 0xFF))` / `DIRTY_ROW(x) ((short)((x) & 0xFF))` at file scope in `renderer.c`. Update `Renderer_MarkDirty` to pack, and the two dirty-iterating loops in `Renderer_BeginFrame` and `Renderer_BlitToWindow` to unpack. `Renderer_ClearDirty` only needs the count cleared; the list contents are irrelevant after clear. Build all three targets. Play a full round; visual output unchanged. `FreeMem` post-init should be ~1.5 KB higher than before this task. Commit prefix: `perf: T013 FR-008 pack dirty list coords (-1.5KB)`

**Checkpoint**: FR-008 optional task complete.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final verification and merge prep.

- [ ] T014 Re-run cppcheck per quickstart "Full-branch acceptance" and diff against `/tmp/cppcheck-before.txt` — expect delta = `-` one line for the old `TileMap_IsSolid` unusedFunction hit, no new findings. The `bomb.c:188` knownConditionTrueFalse hit persists (self-documented defensive guard).
- [ ] T015 Re-run the Mac SE fps measurement from T003 under the same scenario after T005 + T006 + T007 are merged; compare to T003 baseline; record delta in the PR description. Expect average ≥ baseline; target ≥ +2 fps.
- [ ] T016 Re-run the color-Mac `socat` capture from T008's acceptance check after T008 is merged; confirm zero movement log lines during 5 s hold; attach the log excerpt to the PR description.
- [X] T017 [P] Update `CLAUDE.md` "Recent Changes" entry to describe 008-perf-hotpath: list the FRs landed (001–007 core; 008 / 009 if shipped), cite the books and cppcheck inputs, and note the book-verified out-of-scope items (CopyMask swap rejected per Tricks p.6239; TileMap_Init memset retained per data-model analysis).
- [ ] T018 [P] Full build on all three targets from clean; diff binary sizes vs pre-branch; record in PR description for Principle V evidence.
- [ ] T019 Run existing 005 / 006 / 007 on-hardware spot-check scenarios per CLAUDE.md — no full regression, just the named scenarios (005: authority/disconnect grace; 006: dirty-rect renderer visual; 007: wall-clock timer drift). Record pass/fail in PR description.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)** — T001, T002, T003 run in parallel. Zero dependencies. Must complete before any source edits.
- **Foundational (Phase 2)** — T004 only gates US1's T005. T004 can proceed immediately after Setup; it does not block US2, US3, US4.
- **User Stories**:
  - US1 (Phase 3, P1, MVP): T005 depends on T004; T006 and T007 are independent of T004.
  - US2 (Phase 4, P2): T008 depends on nothing in this branch; can run in parallel with all of US1.
  - US3 (Phase 5, P3): T009 independent; can run in parallel with US1 / US2 / US4.
  - US4 (Phase 6, P3): T010 and T011 independent of each other and of US1 / US2 / US3.
  - US5 (Phase 7, P3, gated): T012 depends on user approval, not on any other task; can run in parallel once gated.
  - Optional (Phase 8): T013 depends on nothing; can run in parallel with US1..US5.
- **Polish (Phase 9)** — T014, T015, T016 depend on the respective US they verify; T017, T018, T019 depend on all landed tasks; T014 and T017/T018 can run in parallel once source is frozen.

### User Story Dependencies (per FR-N4 independently-mergeable rule)

- **US1 (P1, MVP)** — depends only on T004 for its T005 sub-task; T006 and T007 stand alone.
- **US2 (P2)** — zero dependencies.
- **US3 (P3)** — zero dependencies.
- **US4 (P3)** — zero dependencies.
- **US5 (P3, gated)** — zero code dependencies; gated on user approval.
- **Optional (FR-008)** — zero dependencies.

### Within Each User Story

- One task per FR.
- Every task concludes with `build all three targets` verification.
- Every task is one commit.

### Parallel Opportunities

- Phase 1 (T001, T002, T003): all parallel.
- Phase 2 (T004): single task, not parallel.
- Phase 3–8: any combination of tasks that touch *different* files can run in parallel (see per-task file-path headers).
  - T005 touches `src/player.c`.
  - T006 touches `src/bomb.c`.
  - T007 touches `src/renderer.c`.
  - T008 touches `src/player.c` — ⚠️ **conflicts with T005**, must be sequential against T005.
  - T009 touches `src/main.c`.
  - T010 touches `include/tilemap.h` + `src/tilemap.c`.
  - T011 touches `src/renderer.c` — ⚠️ **conflicts with T007 and T013**, sequential.
  - T012 touches `src/screen_lobby.c` (+ optionally `include/renderer.h` + `src/renderer.c`) — ⚠️ **conflicts with T007 / T011 / T013** if renderer helper route taken.
  - T013 touches `src/renderer.c` — ⚠️ **conflicts with T007 / T011 / T012**, sequential.
- Maximum safely-parallel first wave after Phase 2: **T005 (player.c), T006 (bomb.c), T007 (renderer.c), T009 (main.c), T010 (tilemap.c+h)** — five tasks, five distinct files. T008, T011, T013 land sequentially after their respective file's first task.

---

## Parallel Example: MVP First Wave (after T004)

```bash
# Five independent commits, five distinct files, no cross-file conflicts:
# T005 → src/player.c       (FR-001)
# T006 → src/bomb.c         (FR-002)
# T007 → src/renderer.c     (FR-003)
# T009 → src/main.c         (FR-007)
# T010 → src/tilemap.{c,h}  (FR-005)
```

After this wave, sequential second wave on the already-touched files:

```bash
# T008 → src/player.c       (FR-004, after T005)
# T011 → src/renderer.c     (FR-006, after T007)
# T013 → src/renderer.c     (FR-008 optional, after T011)
# T012 → src/screen_lobby.c (FR-009 gated)
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Complete Phase 1 (T001, T002, T003) to capture baselines.
2. Complete Phase 2 (T004) to expose `BOMB_GRID_CELL`.
3. Complete Phase 3 (T005, T006, T007) — the three P1 tasks.
4. **STOP and VALIDATE**: run T015 (Mac SE fps measurement). If regression observed, revert only the offending task (FR-N4 independently-mergeable).
5. Ship the MVP PR covering FR-001 + FR-002 + FR-003. Principle V (Mac SE Is the Floor) is the headline benefit.

### Incremental Delivery

1. MVP PR (US1): T001..T007 + T015.
2. US2 PR: T008 + T016. Small, self-contained, no test-plan re-run needed.
3. US3 PR: T009 alone. Independent correctness-only change.
4. US4 PR: T010 + T011 + T014. Dead-code cleanup bundle; cppcheck delta is the evidence.
5. (Optional) US5 PR: T012, only after explicit user approval.
6. (Optional) FR-008 PR: T013, at any time; memory win only.
7. Final polish PR: T017 + T018 + T019 (may be merged along with the MVP PR if the scope stays small).

### Parallel Team Strategy

With multiple developers (unlikely on this project but documented for completeness):

1. Team completes Phase 1 + Phase 2 together.
2. One developer pair takes US1 (MVP) — they own `src/player.c`, `src/bomb.c`, `src/renderer.c` coordination.
3. Meanwhile the other pair takes US2 + US3 + US4 (all independent).
4. US5 / FR-008 pulled in as capacity allows.
5. Polish phase performed by whoever lands last.

---

## Notes

- **Commit style**: each task is one commit; prefix shown per task. Co-author trailer `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>` per repo default.
- **Rollback policy** (FR-N4): if any task's on-target verification fails, revert only that commit. The remaining commits stand on their own.
- **CLAUDE.md update** (T017) is deliberately deferred to the polish phase so the final "Recent Changes" wording reflects what actually shipped (some FRs may be dropped late).
- **No auto-tests**: verification is on-target manual per Principle VII. Every task's "Build all three targets" step is the only mechanical gate.
- **Book / cppcheck inputs** frozen at 2026-04-18; if a reviewer flags a stale citation later, rerun the relevant grep and update `spec.md` §References before landing.
