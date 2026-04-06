# Contract: Renderer Internal API Changes

**Feature**: 003-optimize-correctness
**Date**: 2026-04-06

## New Function: Renderer_RequestRebuildBackground

**Purpose**: Request a deferred background rebuild that will execute at most once per frame.

**Declaration** (renderer.h):
```c
void Renderer_RequestRebuildBackground(void);
```

**Behavior**:
- Sets internal flag `gNeedRebuildBg = TRUE`
- Does NOT immediately rebuild the background
- Multiple calls within the same frame are idempotent (flag is already TRUE)
- The actual rebuild occurs at the start of the next `Renderer_BeginFrame()` call

**Callers**:
- `bomb.c` `ExplodeBomb()` — replaces direct `Renderer_RebuildBackground()` call
- `net.c` `on_block_destroyed()` — replaces direct `Renderer_RebuildBackground()` call

**Non-callers** (keep direct `Renderer_RebuildBackground()`):
- `renderer.c` `Renderer_Init()` — one-time initialization, needs immediate result
- `screen_game.c` `Game_Init()` — one-time per-round setup, needs immediate result

## Modified Behavior: Renderer_BeginFrame

**Change**: At the start of `Renderer_BeginFrame()`, before dirty rect processing, check `gNeedRebuildBg`. If TRUE, call `Renderer_RebuildBackground()` and clear the flag.

**Ordering**: Rebuild MUST happen before the bg-to-work dirty rect copy, so the work buffer gets the updated background.

## Modified Behavior: ForeColor/BackColor Normalization

**Change**: Remove `if (gGame.isMacSE)` guards around `ForeColor(blackColor)` / `BackColor(whiteColor)` calls in:
- `Renderer_RebuildBackground()` (before tile sheet CopyBits)
- `Renderer_BeginFrame()` (before bg-to-work CopyBits)

**New**: Add `ForeColor(blackColor)` / `BackColor(whiteColor)` in:
- `Renderer_BlitToWindow()` (before work-to-window CopyBits)

**Rationale**: Ensures srcCopy CopyBits runs at full speed on all platforms per "Sex, Lies and Video Games" (1996) benchmarks.
