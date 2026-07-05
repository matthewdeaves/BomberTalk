# Contract: Renderer Backend Surface

The screens (`screens.c`, `screen_*.c`) call a **backend-agnostic** subset of `renderer.h`. Any backend (QuickDraw, SDL2) MUST implement this surface with equivalent observable behavior. QuickDraw-specific internals (GWorld locking, mask regions, dirty-rect grid, `CopyBits` alignment) are implementation details of the QuickDraw backend and are NOT part of this contract — an SDL2 backend may implement them as no-ops or full-frame renders.

## Lifecycle

| Call | Contract |
|---|---|
| `Renderer_Init()` | Create window/surfaces; establish default draw state. Returns success/failure; on failure the app degrades gracefully. |
| `Renderer_Shutdown()` | Release all backend resources. |

## Frame brackets

| Call | Contract |
|---|---|
| `Renderer_BeginScreenDraw()` / `Renderer_EndScreenDraw()` | For menu/loading/lobby: clear to background, draw text directly, present. |
| `Renderer_BeginFrame()` / `Renderer_EndFrame()` | For gameplay: prepare work buffer from background; on end, present the frame to the window. |
| `Renderer_BeginSpriteDraw()` / `Renderer_EndSpriteDraw()` | Bracket sprite draws. QuickDraw uses this to hoist port/color state; SDL2 may treat as no-ops. Draw functions must behave correctly whether or not bracketed. |

## Drawing primitives (must render equivalently)

| Call | Contract |
|---|---|
| `Renderer_RebuildBackground()` / `Renderer_RequestRebuildBackground()` | (Re)compose the static tilemap. Immediate vs. deferred-coalesced; SDL2 may rebuild a background texture. |
| Draw player (id, pixel position, facing) | Player sprite at sub-tile pixel position; falls back to colored rect if sprite missing. |
| Draw bomb (position, animation frame) | Bomb sprite with the pulse-loop frame; fallback oval/rect. |
| Draw explosion (tiles) | Explosion sprite/rects over affected tiles. |
| Draw text (string, position) | Render UI text; a bitmap font is acceptable on SDL2 (no SDL_ttf dependency required). |
| `Renderer_MarkDirty(col,row)` / `Renderer_MarkAllDirty()` | Advisory. QuickDraw uses for partial redraw; SDL2 may ignore (full-frame render each frame). Screens MUST keep calling them — backends decide whether to act. |

## Invariants

- **Graphics never block** (Constitution VI): every sprite path has a colored-rectangle fallback.
- **Backend swap is invisible to screens**: `screens.c`/`screen_*.c` compile unchanged against any backend.
- **No backend types in shared headers**: `renderer.h` exposes no SDL or QuickDraw types.
- **Visual parity target**: SDL2 output matches the classic color-Mac layout (grid, sprite placement, screen structure); pixel-exact match is not required.
