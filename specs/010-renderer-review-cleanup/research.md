# Research: Renderer Review Cleanup

**Date**: 2026-04-21  
**Branch**: `010-renderer-review-cleanup`

## Decision 1: Splash PICT Resource Lifecycle

**Decision**: Release the splash PicHandle with `ReleaseResource()` immediately after `DrawPicture()`, then set the static pointer to NULL.

**Rationale**: Programming QuickDraw (1992) p.3789 warns: "There have been some reports of pictures getting purged during DrawPicture(), especially if the picture is particularly large." The colour splash is 178 KB -- large enough to trigger this. Since it is only drawn once during the 2-second loading screen and never needed again, holding it wastes heap. On Mac SE (4 MB RAM, ~1 MB app heap), 178 KB is ~18% of available heap.

**Alternatives considered**:
- `HNoPurge()` + keep cached: Wastes 178 KB for the entire session for a one-shot use.
- `KillPicture()`: Destroys the handle but doesn't return it to the resource chain. `ReleaseResource()` is correct for resource-loaded PicHandles.

---

## Decision 2: ForeColor/BackColor Elimination in SE Bomb Path

**Decision**: Remove the per-bomb `ForeColor(blackColor); BackColor(whiteColor)` calls from the Mac SE PICT path in `Renderer_DrawBomb`, relying on `Renderer_BeginSpriteDraw()` which already sets them.

**Rationale**: Tricks of the Mac Game Programming Gurus (1995) p.41865 benchmarks Mac SE trap overhead at 926 ticks per 300,000 calls (~3.1 microseconds per call). With 3 bombs x 2 traps x ~15 fps = 90 wasted traps/sec. The identical fix was already applied in 008 FR-003 for `Renderer_DrawPlayer` -- this extends the same pattern to bombs.

**Alternatives considered**:
- Keep the calls as defensive code: Rejected because the bracket contract is well-established and relied upon by all other sprite draw functions.

---

## Decision 3: Stack vs Heap for Flood-Fill Buffers

**Decision**: Replace `NewPtr`/`DisposePtr` heap allocations in `BuildBombMaskByFloodFill` with fixed-size stack arrays using a compile-time maximum of 32x32 (1024 pixels).

**Rationale**: Black Art of Macintosh Game Programming (1996) Ch.3 documents heap fragmentation as a key concern on memory-constrained Macs. The current code allocates `visited` (256 bytes), `stackX` (512 bytes), `stackY` (512 bytes) via NewPtr for each of 3 frames -- 6 NewPtr + 6 DisposePtr = 12 Memory Manager traps. Stack arrays totalling ~2560 bytes (for 32x32 max) are well within 68k stack limits and eliminate both the trap overhead and fragmentation risk.

**Alternatives considered**:
- Use `TILE_SIZE_SMALL` (16) as the bound: Too restrictive if tile sizes change. Using `TILE_SIZE_LARGE` (32) as the upper bound costs 2560 bytes on stack but handles both SE and colour Macs.
- Keep heap allocation with a reusable buffer: Over-engineering for a 3-call-at-init function.

---

## Decision 4: ctab Loop Hoist Strategy

**Decision**: Split `CreateMaskFromGWorld` pixel scan into two code paths: one for valid ctab (RGB distance comparison) and one for fallback (index comparison). The ctab check moves outside the row/col loops.

**Rationale**: The current code checks `if (ctab != NULL && *ctab != NULL)` on every pixel (1024 times per 32x32 sprite, x6 sprites = 6144 evaluations at load time). Since ctab validity cannot change during a scan, hoisting eliminates the branch from the inner loop.

**Alternatives considered**:
- Function pointer dispatch: Over-engineering for a load-time-only function.
- Single `int hasCtab` flag checked per-pixel: Still a branch per pixel, just cheaper. Full hoist is cleaner.

---

## Decision 5: bgIndex Bounds Check

**Decision**: Clamp `bgIndex` to `(*ctab)->ctSize` before using it as a ctab array index. Also clamp inner-loop pixel index lookups.

**Rationale**: If a malformed PICT produces pixel values exceeding ctSize, the current code reads past the ctab array. While unlikely with the current pixelcraft pipeline, this is a system boundary (external resource data) where validation is appropriate per defensive coding standards.

**Alternatives considered**:
- Assert and crash: Not appropriate for a resource-loading path where graceful fallback exists.

---

## Decision 6: bomb.c ownerIdx Condition

**Decision**: Remove the `ownerIdx >= 0` half of the condition since `ownerID` is `unsigned char` and the assignment to `short` guarantees non-negative.

**Rationale**: cppcheck correctly identifies this as always-true. The remaining `ownerIdx < MAX_PLAYERS` check is the meaningful guard.

**Alternatives considered**:
- Cast to unsigned short: Masks the intent. Simply removing the dead check is clearest.
