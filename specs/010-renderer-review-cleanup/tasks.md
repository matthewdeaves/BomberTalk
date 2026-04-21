# Tasks: Renderer Review Cleanup

**Input**: Design documents from `/specs/010-renderer-review-cleanup/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md

**Tests**: No test tasks generated (no automated test framework; verification is manual via QEMU and cppcheck).

**Organization**: Tasks grouped by user story for independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: File-scope constants and shared prep work

- [ ] T001 Add file-scope `static const short kBombColorIds[BOMB_ANIM_FRAMES]` and `kBombSEIds[BOMB_ANIM_FRAMES]` constants in src/renderer.c (replaces duplicate local arrays in LoadPICTResources and LoadSEBombSprites)

---

## Phase 2: User Story 1 - Mac SE Memory Recovery (Priority: P1)

**Goal**: Release splash PICT after drawing (178 KB) and eliminate heap allocations in flood-fill mask builder.

**Independent Test**: Build 68k MacTCP target. Run in QEMU or Mac SE hardware. Verify splash displays on loading screen, then heap is larger after menu transition. Verify bomb sprites still render correctly with masks.

### Implementation for User Story 1

- [ ] T002 [US1] Release splash PicHandle in Renderer_DrawSplashBackground after DrawPicture: call `ReleaseResource((Handle)splashPic)` then set `splashPic = NULL` in src/renderer.c (line ~1500)
- [ ] T003 [US1] Replace heap allocations in BuildBombMaskByFloodFill with stack arrays: change `visited` to `unsigned char visited[1024]`, `stackX` to `short stackX[1024]`, `stackY` to `short stackY[1024]`; remove NewPtr/DisposePtr calls; add `memset(visited, 0, ...)` at top in src/renderer.c (line ~560)
- [ ] T004 [US1] Update LoadSEBombSprites to use `kBombSEIds` constant instead of local `ids[]` array in src/renderer.c (line ~641)

**Checkpoint**: Mac SE heap pressure reduced. Splash still displays. Bomb masks still work.

---

## Phase 3: User Story 2 - Mac SE Rendering Performance (Priority: P1)

**Goal**: Remove redundant ForeColor/BackColor trap calls from the Mac SE bomb draw path.

**Independent Test**: Build 68k MacTCP target. Place bombs in single-player game on Mac SE. Verify bombs render identically. Optionally compare FPS with 3 active bombs.

### Implementation for User Story 2

- [ ] T005 [US2] Remove `ForeColor(blackColor); BackColor(whiteColor);` from the Mac SE PICT path in Renderer_DrawBomb (rely on BeginSpriteDraw bracket) in src/renderer.c (lines ~1319-1320)

**Checkpoint**: Mac SE bomb rendering identical, 6 fewer traps/frame with 3 bombs.

---

## Phase 4: User Story 3 - Colour Mac Mask Building Correctness (Priority: P2)

**Goal**: Hoist ctab check outside pixel loop and add bgIndex bounds check.

**Independent Test**: Build PPC OT target. Run in QEMU. Verify all bomb and player sprites have correct transparency masks. No crashes during loading.

### Implementation for User Story 3

- [ ] T006 [US3] Bounds-check bgIndex against `(*ctab)->ctSize` after reading from pixel data in CreateMaskFromGWorld in src/renderer.c (line ~372)
- [ ] T007 [US3] Hoist `if (ctab != NULL && *ctab != NULL)` check outside the row/col pixel scan loops in CreateMaskFromGWorld: split into two code paths (ctab-present vs fallback) in src/renderer.c (lines ~396-417)
- [ ] T008 [US3] Move `dstRow` declaration into the row loop body for narrower scope in CreateMaskFromGWorld in src/renderer.c (line ~351)
- [ ] T009 [US3] Use `ts` variable instead of hardcoded `16` in Mac SE bomb srcRect in Renderer_DrawBomb in src/renderer.c (line ~1314)
- [ ] T010 [US3] Update LoadPICTResources to use `kBombColorIds` constant instead of local `bombIds[]` array in src/renderer.c (line ~689)

**Checkpoint**: Masks identical. No out-of-bounds reads. cppcheck variableScope resolved for dstRow.

---

## Phase 5: User Story 4 - Static Analysis Clean Build (Priority: P3)

**Goal**: Resolve all cppcheck warnings from the review.

**Independent Test**: Run `cppcheck --enable=all --std=c89 -I include src/` and verify zero new warnings. Build all three targets clean.

### Implementation for User Story 4

- [ ] T011 [P] [US4] Make Renderer_BlitToWindow static in src/renderer.c (line ~1420) and remove declaration from include/renderer.h if present
- [ ] T012 [P] [US4] Reduce scope of `ok` variable in LoadSEBombSprites: move declaration into the for-loop body in src/renderer.c (line ~642)
- [ ] T013 [P] [US4] Simplify `ownerIdx >= 0 && ownerIdx < MAX_PLAYERS` to `ownerIdx < MAX_PLAYERS` in ExplodeBomb in src/bomb.c (line ~184)

**Checkpoint**: cppcheck clean. All three targets compile with zero errors and zero new warnings.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Verification across all targets

- [ ] T014 Build all three targets (68k MacTCP, PPC OT, PPC MacTCP) and verify zero errors/warnings
- [ ] T015 Run cppcheck on full source tree and verify all identified warnings are resolved
- [ ] T016 Test in QEMU (Quadra 800): verify bomb animation, splash screen, sprite transparency unchanged
- [ ] T017 Commit and push to 010-renderer-review-cleanup branch

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - T001 first
- **US1 (Phase 2)**: Depends on T001 (uses kBombSEIds constant)
- **US2 (Phase 3)**: No dependencies on other stories - can run after Phase 1
- **US3 (Phase 4)**: Depends on T001 (uses kBombColorIds constant)
- **US4 (Phase 5)**: No dependencies on other stories - can run in parallel
- **Polish (Phase 6)**: Depends on all story phases complete

### Parallel Opportunities

- T005 (US2) can run in parallel with T002-T004 (US1) -- different code sections
- T011, T012, T013 (US4) can all run in parallel -- different files/functions
- T006-T010 (US3) are sequential within CreateMaskFromGWorld but independent of US1/US2

---

## Implementation Strategy

### Recommended Order (Single Developer)

1. T001 (shared constants)
2. T002, T003, T004 (US1 - highest heap impact)
3. T005 (US2 - trivial, high value)
4. T006, T007, T008, T009, T010 (US3 - mask correctness)
5. T011, T012, T013 (US4 - cppcheck cleanup)
6. T014, T015, T016, T017 (verification and ship)

---

## Notes

- All changes are in existing files -- no new files created
- No protocol version bump needed (local changes only)
- C89 constraint: stack arrays must use compile-time constants, not VLAs
- The `memset` for visited array in T003 replaces NewPtrClear's zero-initialization
