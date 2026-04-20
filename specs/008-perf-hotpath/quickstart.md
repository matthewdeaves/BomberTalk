# Quickstart: Hot-Path Performance & Memory Optimizations (008)

**Phase**: 1 (Design & Contracts)
**Audience**: Developer implementing on branch `008-perf-hotpath`.
**Prerequisite**: Retro68/RetroPPC toolchain installed per CLAUDE.md. Environment variable `RETRO68_TOOLCHAIN` set.

## Build all three targets before any edits (baseline)

```bash
# From repo root
mkdir -p build-68k && (cd build-68k && \
  cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake && \
  make)

mkdir -p build-ppc-ot && (cd build-ppc-ot && \
  cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
    -DPT_PLATFORM=OT && make)

mkdir -p build-ppc-mactcp && (cd build-ppc-mactcp && \
  cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
    -DPT_PLATFORM=MACTCP && make)
```

All three must build clean with **zero new warnings** versus `git merge-base HEAD main` as the baseline reference.

## Verify cppcheck baseline

```bash
cppcheck --std=c89 --enable=all --inconclusive \
  --suppress=missingIncludeSystem -I include src/ 2>&1 | tee /tmp/cppcheck-before.txt

cppcheck --std=c89 --enable=unusedFunction -I include src/ 2>&1 | tee /tmp/cppcheck-unused-before.txt
```

Expected baseline (2026-04-18 measurement):

- One `unusedFunction: TileMap_IsSolid` finding.
- One `knownConditionTrueFalse: ownerIdx>=0` finding (bomb.c:188 — self-documented defensive guard, LEAVE).
- Several `variableScope` style suggestions — IGNORE, they conflict with C89 top-of-block declarations.

## Per-FR verification recipe

### FR-001 (Cache `TileMap_Get*`, inline bomb-grid read)

1. Build all three targets.
2. On Mac SE (real or QEMU): start 2-player round, hold arrow key for 10 s, read F-key FPS overlay. Baseline: CLAUDE.md 2026-04-10 measurement (~10-19 fps gameplay).
3. Acceptance: **average fps ≥ baseline, target +2 fps net**.
4. Sanity: no movement regressions (collision against walls/blocks/bombs still stops the sprite flush).

### FR-002 (Inline `SetRect` in per-frame AABB loops)

1. Build all three targets.
2. On Mac SE: place a bomb, walk into the explosion — confirm death flash + game-over flow.
3. Walk partly into an explosion tile from any corner — confirm death fires (AABB overlap correctness preserved).
4. Acceptance: **existing 005 hardware-test scenarios pass unchanged**; no visible correctness regression.

### FR-003 (Batch `ForeColor` in fallback `Renderer_DrawPlayer`)

1. Build with PICT resources ABSENT (rename `data/BomberTalk.r` or equivalent) so the fallback rectangle path is exercised on color Macs.
2. Run 2-player round; confirm players render as coloured rectangles with black frames (color Macs) or marked cross/square (Mac SE).
3. Acceptance: **no visual regression** in fallback draw; SE frame rate should improve slightly (harder to measure directly; pair with FR-001 measurement).

### FR-004 (Edge-trigger debug logging)

1. Color Mac build with `BOMBERTALK_DEBUG=ON`.
2. On a companion host: `socat -u UDP-RECV:7356,reuseaddr -` listening to debug channel.
3. Start round, hold arrow key for 5 s with no other events.
4. Acceptance: **zero movement-related log lines during the 5 s hold**. On direction change or bomb event, a single log line fires.
5. Sanity: placement / explode / kill / connect / disconnect logs still appear during those events.

### FR-005 (Remove `TileMap_IsSolid`)

1. Edit `include/tilemap.h` to delete the declaration.
2. Edit `src/tilemap.c` to delete the body.
3. `grep -rn TileMap_IsSolid src/ include/` → zero hits.
4. Build all three targets. Acceptance: **all three build clean**.
5. `cppcheck --std=c89 --enable=unusedFunction -I include src/` → zero unused-function lines. Acceptance: **matches `/tmp/cppcheck-unused-before.txt` minus the `TileMap_IsSolid` line**.

### FR-006 (Delete dead Mac SE branch in `LoadPICTResources`)

1. Edit `src/renderer.c` to remove `if (gGame.isMacSE) { ... } else { ... }` wrapper in `LoadPICTResources` (keep only the else-branch body, which handles color Macs).
2. Simplify mask-region construction ternary at the same site.
3. Leave a one-line comment: `/* Mac SE uses fallback rectangles; SE PICT IDs (rPict*SE in game.h) are reserved for future use. */`
4. Build all three targets. Acceptance: **all three build clean, Mac SE renders unchanged** (its code path never called `LoadPICTResources` anyway).

### FR-007 (`FreeMem` → `PurgeSpace`)

1. Edit `src/main.c` lines ~271-273 and ~363-367 to replace each `FreeMem()` call with:
   ```c
   long total, contig;
   PurgeSpace(&total, &contig);
   /* use `total` in place of former FreeMem() */
   ```
2. Build all three targets.
3. Boot app. Acceptance: **startup log line reports a value ≥ former FreeMem-based value** on a freshly-launched app (purgeable blocks now counted). On PPC 6200, the value should not falsely trigger the 256 KB warning immediately post-init.

### FR-008 (OPTIONAL: pack dirty list)

1. Edit `src/renderer.c`: collapse `gDirtyListCol[]` + `gDirtyListRow[]` into a single `short gDirtyList[MAX_GRID_ROWS * MAX_GRID_COLS]`.
2. Update `Renderer_MarkDirty` + the two `Renderer_BeginFrame` / `Renderer_BlitToWindow` iteration loops + `Renderer_ClearDirty` to pack/unpack via local helpers.
3. Build all three targets. Acceptance: **visual output unchanged**; `wc -c build-68k/*.BIN` does not regress.

### FR-009 (OPTIONAL, GATED: dispose title sprite on menu → lobby)

**Only implement if user has signed off on spec FR-009 gating question.**

1. Edit `src/screen_lobby.c` `Lobby_Init` — add one-shot dispose of `gTitleSprite` and its mask region.
2. NULL both pointers after dispose.
3. Verify existing draw-path NULL checks (`gTitleSprite != NULL` / `gTitleMaskRgn != NULL`) in `screen_menu.c` and `screen_loading.c`.
4. Build all three targets.
5. On a color Mac: add temporary diagnostic `CLOG_INFO("heap delta: %ld", PurgeSpaceTotal() - before)` around the dispose; confirm ~40 KB reclaim. Remove the diagnostic before merge.

## Full-branch acceptance

Before opening a PR:

```bash
# All three targets build clean
make -C build-68k clean all
make -C build-ppc-ot clean all
make -C build-ppc-mactcp clean all

# Cppcheck shows fewer or equal findings vs baseline
cppcheck --std=c89 --enable=all --inconclusive \
  --suppress=missingIncludeSystem -I include src/ 2>&1 | diff /tmp/cppcheck-before.txt -

# Feature branch rebased on main, no conflicts
git fetch origin main && git rebase origin/main
```

Then run the existing hardware-test plans from 005 / 006 / 007 against the rebased branch — spot-check only; no end-to-end re-run required per spec FR-N5.

## Git hygiene

- One commit per FR: message format `perf: <FR id> <one-line summary>` (e.g. `perf: FR-001 cache TileMap bounds in collision loop`).
- Commit trailer per the project default (`Co-Authored-By: Claude Opus 4.7 (1M context)`).
- Merge via PR — do not fast-forward to main without CI/hardware validation.
