# Rockport Asset Generation Prompts

**Model**: Nova Canvas via Rockport
**Size**: 512x512 for all (divisible by 16, good detail for downscaling)
**Total images**: 8 (not 11 — one player recolored via palette swap, facing via CopyBits flip)

**Post-process pipeline**:
1. Feed PNG into ~/Desktop/pixelcraft (mac-system7 palette, edge_aware, atkinson dither)
   - Color: 32x32 grid → PixelGrid JSON
   - SE: 16x16 grid, 2-color palette → PixelGrid JSON
2. For player: palette-swap to create P0/P1/P2/P3 color variants
3. `~/pixelforge/tools/grid2pict input.json output.pict --no-header`
4. Embed via Rez `read 'PICT'` or `pict2macbin`

**Runtime tricks (no extra assets needed)**:
- Left/right facing: horizontal flip via CopyBits (swap srcRect left/right)
- 4 player colors: palette swap on the single player sprite
- Up/down facing: same sprite (top-down round Bomberman character looks the same)

Generate each of these as a separate image. All top-down/overhead perspective.

---

## 1. Floor Tile

```
Top-down pixel art of a grass floor tile for a Bomberman game. Dark green with subtle texture variation. Simple repeating pattern that tiles seamlessly. Flat overhead view. Clean pixel art style, limited palette, no anti-aliasing. Single tile filling entire image, no border. Black background outside the tile area.
```

**Size**: 512x512
**Scale to**: 32x32 (color), 16x16 (SE mono)
**PICT ID**: 128 (color), 200 (SE)

---

## 2. Wall Tile

```
Top-down pixel art of an indestructible stone wall pillar for a Bomberman game. Gray stone blocks with subtle brick texture. Should look solid, heavy, permanent. Flat overhead view. Clean pixel art style, limited palette, bold edges, high contrast. Single tile filling entire image. Black background outside tile area.
```

**Size**: 512x512
**Scale to**: 32x32 (color), 16x16 (SE mono)
**PICT ID**: 128 (color), 200 (SE)
**Note**: Tile index 1 in the tile sheet

---

## 3. Destructible Block Tile

```
Top-down pixel art of a destructible wooden crate block for a Bomberman game. Brown/tan wood with visible planks or cross-hatch pattern. Should look breakable and distinct from gray stone walls. Flat overhead view. Clean pixel art style, limited palette, bold edges. Single tile filling entire image. Black background outside tile area.
```

**Size**: 512x512
**Scale to**: 32x32 (color), 16x16 (SE mono)
**PICT ID**: 128 (color), 200 (SE)
**Note**: Tile index 2 in the tile sheet

---

## 4. Spawn Point Tile

```
Top-down pixel art of a player spawn point tile for a Bomberman game. Light green grass base similar to floor tile but with a small white diamond or circle marker in the center indicating a start position. Flat overhead view. Clean pixel art style, limited palette. Single tile filling entire image. Black background outside tile area.
```

**Size**: 512x512
**Scale to**: 32x32 (color), 16x16 (SE mono)
**PICT ID**: 128 (color), 200 (SE)
**Note**: Tile index 3 in the tile sheet

---

## 5. Player Character (ONE — recolor for all 4 players)

```
Pixel art of a cute Bomberman character in the classic Dynablaster style. 3/4 front-facing view — the character is a small standing figure seen from slightly above, facing the viewer. Round white head, simple dot eyes, small round body, stubby arms at sides, small feet at the bottom. Classic Bomberman/Dynablaster proportions — big head, small body. White as the primary color with black outline. The character should be symmetrical left-to-right so horizontal flipping looks natural for movement. Centered in image on solid bright green background for easy keying. Must read clearly when scaled down to 16x16 pixels.
```

**Size**: 512x512
**Scale to**: 32x32 (color), 16x16 (SE mono)
**PICT ID**: 129 (color base), 201 (SE)
**Post-process**: Palette-swap white body to red (P1), blue (P2), yellow (P3) using pixelcraft.
Produces 4 variants from 1 generation. Left/right facing handled by CopyBits horizontal flip at runtime. Up/down facing uses same sprite (classic Bomberman always shows front-facing view regardless of movement direction).

---

## 6. Bomb

```
Top-down pixel art of a classic round black bomb for a Bomberman game. Spherical black bomb with a lit orange/yellow fuse sparking at the top. Bold shape with slight highlight shine. Must be immediately recognizable as a bomb even at 16x16 pixels. Centered in image on solid bright green background.
```

**Size**: 512x512
**Scale to**: 32x32 (color), 16x16 (SE mono)
**PICT ID**: 130 (color), 202 (SE)

---

## 7. Explosion

```
Top-down pixel art of a bomb explosion burst for a Bomberman game. Bright orange, red, and yellow starburst explosion. Symmetrical in all directions so it tiles in a cross pattern. High contrast, very visible against green background. Must look dramatic even at 16x16 pixels. Centered in image on solid bright green background.
```

**Size**: 512x512
**Scale to**: 32x32 (color), 16x16 (SE mono)
**PICT ID**: 131 (color), 203 (SE)

---

## 8. Title Logo

```
Retro pixel art game logo reading "BomberTalk" in bold stylized lettering. 1990s arcade game title style. Bright colors — white or yellow text with red/orange outlines and slight 3D drop shadow effect. Centered on dark background. Clean readable text that works at various scales.
```

**Size**: 1280x512 (Nova Canvas, divisible by 16)
**Scale to**: 320x128 (color), 240x80 (SE mono)
**PICT ID**: 132 (color), 204 (SE)

---

## Post-Generation Checklist

After generating all 8 images:

- [ ] Run each through pixelcraft with mac-system7 palette, edge_aware, atkinson dither
- [ ] Color versions at 32x32 grid (tiles, player, bomb, explosion)
- [ ] SE versions at 16x16 grid with 2-color (B&W) palette
- [ ] Palette-swap player sprite to create 4 color variants (white, red, blue, yellow)
- [ ] Combine 4 tile images into single 128x32 tile sheet JSON, run grid2pict
- [ ] Player variants can be individual 32x32 PICTs or combined into 128x32 strip
- [ ] Run grid2pict --no-header on all JSONs
- [ ] Embed in resource fork (Rez read or pict2macbin)
- [ ] Title: scale to 320x128 (color) and 240x80 (SE)
- [ ] Test GetPicture() loading on all three Macs
