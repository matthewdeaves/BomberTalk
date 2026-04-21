# Feature Specification: Renderer Review Cleanup

**Feature Branch**: `010-renderer-review-cleanup`  
**Created**: 2026-04-21  
**Status**: Draft  
**Input**: Book-grounded code review fixes and cppcheck cleanup for 009-bomb-animation branch

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Mac SE Memory Recovery (Priority: P1)

As a Mac SE player (4 MB RAM), the game should release temporary resources as soon as they are no longer needed so that maximum heap is available for gameplay networking and rendering.

**Why this priority**: The Mac SE has the tightest memory budget of all three target Macs. A 178 KB splash resource held past the loading screen directly reduces heap available for PeerTalk TCP buffers and offscreen bitmaps during gameplay. Additionally, unnecessary heap allocations during sprite loading fragment memory.

**Independent Test**: Build and run the Mac SE (68k MacTCP) target. After the loading screen transitions to the menu, verify that the splash resource has been freed and heap usage is lower than before this change.

**Acceptance Scenarios**:

1. **Given** the game is on the loading screen displaying the splash PICT, **When** the loading screen transitions to the menu, **Then** the splash resource is released from memory.
2. **Given** the game loads Mac SE bomb sprites at startup, **When** sprite masks are built from the 16x16 bitmaps, **Then** no heap allocations are made for temporary flood-fill working memory (stack memory is used instead).
3. **Given** the game is running on any target Mac, **When** the splash PICT is needed again (it is not), **Then** the system does not attempt to re-load it (the load-attempted flag prevents redundant resource lookups).

---

### User Story 2 - Mac SE Rendering Performance (Priority: P1)

As a Mac SE player, bomb sprite animation should not introduce unnecessary per-frame overhead from redundant system calls that the existing sprite draw bracket already handles.

**Why this priority**: The Mac SE runs gameplay at 10-19 fps. Every unnecessary system call in the per-frame draw loop directly reduces frame rate. Redundant calls were identified that duplicate work already performed by the sprite draw bracket.

**Independent Test**: Build and run the Mac SE target. Place bombs in a single-player game. Verify that frame rate is not degraded compared to the previous build, and ideally measure a small improvement with 3 active bombs on screen.

**Acceptance Scenarios**:

1. **Given** the sprite draw bracket has set foreground/background colour state, **When** a Mac SE bomb sprite is drawn, **Then** no additional colour state calls are made per bomb.
2. **Given** 3 bombs are active on screen (maximum typical scenario), **When** the frame renders, **Then** 6 fewer system calls occur per frame compared to before this change.

---

### User Story 3 - Colour Mac Mask Building Correctness (Priority: P2)

As a colour Mac player, sprite mask generation at load time should be robust against malformed or unexpected sprite data, preventing out-of-bounds memory reads.

**Why this priority**: A missing bounds check during mask building could read past the colour table if a pixel value exceeds the table size. While unlikely with well-formed PICTs from the current pipeline, defensive code prevents crashes if sprite resources are regenerated with different tools.

**Independent Test**: Build and run the colour Mac (PPC OT or 68k OT) target. Verify bomb and player sprites render correctly with proper transparency masks. Confirm no crashes during PICT loading.

**Acceptance Scenarios**:

1. **Given** a sprite PICT is loaded into a colour GWorld, **When** the background pixel index exceeds the colour table size, **Then** the index is clamped to a valid range and mask building completes without reading invalid memory.
2. **Given** a sprite PICT with a valid colour table, **When** the mask is built by scanning pixels, **Then** the colour table validity check is evaluated once (not per-pixel), and the mask result is identical to the previous implementation.

---

### User Story 4 - Static Analysis Clean Build (Priority: P3)

As a developer maintaining the codebase, all static analysis warnings introduced or surfaced by the bomb animation branch should be resolved, keeping the codebase clean for future contributors.

**Why this priority**: Clean static analysis results prevent warning fatigue and catch real bugs early. These are low-risk, high-confidence changes.

**Independent Test**: Run cppcheck across all source files. Verify that warnings present before this change are resolved. Verify all three targets compile clean.

**Acceptance Scenarios**:

1. **Given** the renderer source file, **When** cppcheck is run, **Then** the variable scope warnings for `dstRow` and `ok` are resolved.
2. **Given** the renderer source file, **When** cppcheck is run, **Then** `Renderer_BlitToWindow` no longer triggers a "should have static linkage" warning.
3. **Given** the bomb source file, **When** cppcheck is run, **Then** the always-true condition warning is resolved.
4. **Given** the full source tree, **When** all three targets are built, **Then** compilation completes with zero errors and zero new warnings.

---

### Edge Cases

- What happens if the splash PICT resource is missing from the resource fork? The system logs a warning and continues (current behaviour preserved).
- What happens if a Mac SE bomb sprite PICT fails to load? The renderer falls back to the animated oval path (current behaviour preserved).
- What happens if the colour table has zero entries? The mask builder falls back to index-based comparison (current behaviour preserved).
- What happens if BuildBombMaskByFloodFill receives a sprite larger than 16x16? Stack arrays are sized to a safe maximum covering the largest possible tile size.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The splash PICT resource MUST be released from memory after the loading screen finishes drawing it, recovering approximately 178 KB of heap.
- **FR-002**: The Mac SE bomb sprite draw path MUST NOT issue foreground/background colour state calls that duplicate work performed by the sprite draw bracket.
- **FR-003**: The colour table validity check in the mask builder MUST be evaluated once per sprite (outside the pixel loop), not once per pixel.
- **FR-004**: The background pixel index MUST be bounds-checked against the colour table size before use as an array index.
- **FR-005**: The Mac SE bomb sprite source rectangle MUST use the runtime tile size variable, not a hardcoded constant.
- **FR-006**: The flood-fill mask builder MUST use stack-allocated working memory instead of heap allocations for temporary buffers under 2 KB.
- **FR-007**: Duplicate bomb resource ID array initialisations MUST be consolidated into shared file-scope constants.
- **FR-008**: Variable declarations MUST be scoped to the narrowest block where they are used (resolves cppcheck variableScope).
- **FR-009**: Functions used only within their translation unit MUST have static linkage (resolves cppcheck staticFunction).
- **FR-010**: Conditions that are always true due to type promotion MUST be simplified (resolves cppcheck knownConditionTrueFalse).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Heap usage after the loading screen is at least 170 KB lower than before this change on all three target platforms.
- **SC-002**: Per-frame system call count during bomb rendering on Mac SE is reduced by at least 4 calls when 2+ bombs are active.
- **SC-003**: All three build targets (68k MacTCP, PPC OT, PPC MacTCP) compile with zero errors and zero new warnings.
- **SC-004**: cppcheck run on the full source tree produces zero new warnings and resolves all warnings identified in the review.
- **SC-005**: Game behaviour (bomb animation, splash screen, sprite transparency) is visually identical to the pre-change build on all three target platforms.
- **SC-006**: Mac SE gameplay frame rate with 3 active bombs is equal to or better than the pre-change build.

## Assumptions

- The splash PICT is only displayed during the loading screen and is never needed again after transition to the menu screen.
- Mac SE bomb sprites are always 16x16 pixels, but the code should use the runtime tile size for forward compatibility.
- The flood-fill working buffers for 16x16 sprites total approximately 1280 bytes, well within safe stack limits for 68k Macs.
- The cppcheck warnings in pre-existing files (bomb.c) are included in scope for completeness alongside the bomb animation branch fixes.
- No protocol version bump is needed as all changes are local rendering and memory management only.

## Dependencies

- Requires the 009-bomb-animation branch to be merged or used as base (this branch is based on 009-bomb-animation).
- Book references: Programming QuickDraw (1992) p.3789, Tricks of the Gurus (1995) p.41865, Black Art (1996) Ch.3, Sex Lies and Video Games (1996) p.6620.
