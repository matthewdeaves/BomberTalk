# Data Model: Renderer Review Cleanup

**Date**: 2026-04-21  
**Branch**: `010-renderer-review-cleanup`

## Affected Data Structures

No new data structures are introduced. This feature modifies the lifecycle and allocation strategy of existing structures.

### Renderer_DrawSplashBackground statics

- `splashPic` (static PicHandle): **Change**: Set to NULL after `ReleaseResource()` instead of held indefinitely.
- `loadAttempted` (static int): Unchanged. Prevents redundant `GetPicture()` calls.

### BuildBombMaskByFloodFill locals

- `visited`: **Change**: From `Ptr` (heap via NewPtr) to `unsigned char[1024]` (stack). Max 32x32.
- `stackX`: **Change**: From `short *` (heap via NewPtr) to `short[1024]` (stack). Max 32x32.
- `stackY`: **Change**: From `short *` (heap via NewPtr) to `short[1024]` (stack). Max 32x32.
- `storage` (Ptr): Unchanged. The output mask bitmap storage remains heap-allocated (caller-owned, freed in Renderer_Shutdown).

### CreateMaskFromGWorld locals

- `dstRow`: **Change**: Declaration moved into the row loop body for narrower scope.
- `bgIndex`: **Change**: Clamped to `[0, ctSize]` after read from pixel data.

### File-scope constants (new)

- `kBombColorIds[BOMB_ANIM_FRAMES]`: `{rPictBombFrame0, rPictBombFrame1, rPictBombFrame2}` — replaces duplicate local arrays in LoadPICTResources and LoadSEBombSprites.
- `kBombSEIds[BOMB_ANIM_FRAMES]`: `{rPictBombSEFrame0, rPictBombSEFrame1, rPictBombSEFrame2}` — replaces local array in LoadSEBombSprites.

### Renderer_BlitToWindow

- **Change**: Linkage from extern to static. Declaration removed from renderer.h if present.

### bomb.c ExplodeBomb

- `ownerIdx`: **Change**: Condition simplified from `ownerIdx >= 0 && ownerIdx < MAX_PLAYERS` to `ownerIdx < MAX_PLAYERS`.
