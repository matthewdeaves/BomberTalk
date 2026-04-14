# Renderer API Changes: 006-renderer-optimization

**Date**: 2026-04-14

## New Public Functions (renderer.h)

### Renderer_BeginSpriteDraw / Renderer_EndSpriteDraw

```c
void Renderer_BeginSpriteDraw(void);
void Renderer_EndSpriteDraw(void);
```

**Purpose**: Bracket all per-frame sprite drawing with a single SavePort/SetPortWork and RestorePort pair. Called by Game_Draw() around the bomb, explosion, and player draw loops.

**Contract**:
- MUST be called as a matched pair within a single frame
- MUST be called after Renderer_BeginFrame() and before Renderer_EndFrame()
- BeginSpriteDraw sets the current port to the work buffer and sets ForeColor(blackColor)/BackColor(whiteColor)
- EndSpriteDraw restores the saved port
- Between Begin/End, all Renderer_Draw* functions skip their own SavePort/RestorePort
- If called outside a frame (no BeginFrame), behavior is undefined (debug assertion in BOMBERTALK_DEBUG builds)

**Call sequence**:
```
Renderer_BeginFrame();
Renderer_BeginSpriteDraw();
  Renderer_DrawBomb(...);       /* N times */
  Renderer_DrawExplosion(...);  /* N times */
  Renderer_DrawPlayer(...);     /* N times */
Renderer_EndSpriteDraw();
Renderer_EndFrame(window);
```

## Modified Public Functions

### Renderer_DrawPlayer / Renderer_DrawBomb / Renderer_DrawExplosion

**Existing signatures unchanged**:
```c
void Renderer_DrawPlayer(short playerID, short pixelX, short pixelY, short facing);
void Renderer_DrawBomb(short col, short row);
void Renderer_DrawExplosion(short col, short row);
```

**Behavioral change**:
- Color Mac path (PICTs loaded): Uses srcCopy + mask region instead of transparent mode. If mask region is NULL, falls back to transparent mode.
- Fallback path (Mac SE or no PICTs): Checks gSpriteDrawActive flag. If true, skips SavePort/RestorePort (port already set by bracket). If false, performs its own SavePort/RestorePort (standalone callability preserved).

## New Public Macro (tilemap.h)

### TILEMAP_TILE

```c
#define TILEMAP_TILE(map, col, row) ((map)->tiles[(row)][(col)])
```

**Purpose**: Direct array access to tilemap data without function call overhead or bounds checking. For use in performance-critical loops where bounds have already been validated.

**Contract**:
- `map` MUST be a valid `TileMap *` obtained from `TileMap_Get()`
- `col` MUST be in range [0, map->cols)
- `row` MUST be in range [0, map->rows)
- Out-of-bounds access is undefined behavior (no checking)
- Returns `unsigned char` tile type value
- The safe `TileMap_GetTile()` function remains the preferred API for all non-hot-path callers

## Unchanged Public API

All other renderer.h and tilemap.h functions retain their existing signatures and behavior:
- Renderer_Init, Renderer_Shutdown, Renderer_RebuildBackground, Renderer_RequestRebuildBackground
- Renderer_BeginFrame, Renderer_EndFrame
- Renderer_BeginScreenDraw, Renderer_EndScreenDraw
- Renderer_MarkDirty, Renderer_MarkAllDirty, Renderer_ClearDirty
- TileMap_Init, TileMap_Reset, TileMap_GetTile, TileMap_SetTile, TileMap_IsSolid
- TileMap_GetCols, TileMap_GetRows, TileMap_Get
