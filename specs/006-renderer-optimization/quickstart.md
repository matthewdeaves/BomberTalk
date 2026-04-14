# Quickstart: 006-renderer-optimization

## What This Feature Does

Five internal renderer optimizations that reduce per-frame overhead on Classic Mac hardware. No user-facing changes -- gameplay looks identical, but runs faster.

## Key Files

| File | Changes |
|------|---------|
| `src/renderer.c` | Mask region creation, BeginSpriteDraw/EndSpriteDraw, batched tile drawing, color state init |
| `src/screen_game.c` | Game_Draw() calls BeginSpriteDraw/EndSpriteDraw bracket |
| `include/renderer.h` | New: Renderer_BeginSpriteDraw/EndSpriteDraw declarations |
| `include/tilemap.h` | New: TILEMAP_TILE macro |
| `src/bomb.c` | Use TILEMAP_TILE in explosion raycast |
| `src/player.c` | Use TILEMAP_TILE in collision via CheckTileSolid (optional) |

## Build & Test

```bash
# Build all three targets (must all compile clean)
cd build-68k && make
cd build-ppc-ot && make
cd build-ppc-mactcp && make

# Deploy and test on hardware via classic-mac-hardware MCP
# Visual comparison: gameplay must look identical to pre-optimization
# FPS observation: Mac SE should show improvement in busy scenes
```

## Design Decisions

1. **BeginSpriteDraw/EndSpriteDraw bracket** instead of per-function flag parameter -- preserves standalone callability
2. **BitMapToRegion for mask creation** -- 3.3x faster than CopyMask per Sex Lies benchmarks
3. **TILEMAP_TILE macro** instead of exposing raw pointer -- uses existing public TileMap struct
4. **Multi-pass tile batching** instead of sort-then-draw -- simpler, no temp storage needed
5. **Color state set at init + post-rebuild** instead of per-frame -- verified no code path changes work port state between frames
