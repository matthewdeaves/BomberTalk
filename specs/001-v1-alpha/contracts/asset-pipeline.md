# Asset Pipeline Contract: BomberTalk v1.0-alpha

**Feature**: `001-v1-alpha`
**Date**: 2026-04-05

## Overview

BomberTalk uses PICT resources stored in the resource fork for all game graphics.
This follows the primary recommendation from Black Art of Macintosh Game Programming
(1996), Mac Game Programming (2002), and Macintosh Game Programming Techniques (1996).

PICTs are loaded into GWorlds at init time. Individual tiles and sprites are extracted
via CopyBits from the sheet GWorld to the work buffer GWorld.

## Why PICT Resources

Source: Macintosh Game Programming Techniques (1996):
"Because games use extensive graphics, you will almost certainly need to use
picture resources."

Source: Black Art (1996):
"Apple engineers incorporated compression into the PICT resource... very efficient
in terms of speed of encoding and decoding."

- OS handles RLE compression/decompression automatically
- Standard resource fork format — works on all Classic Mac OS versions
- Loaded with GetPicture(resourceID), drawn with DrawPicture()
- Resource IDs start at 128 (Apple reserves 0-127)

## Asset Inventory

### Color Macs (640x480, 8-bit color, 32x32 tiles)

| Resource | PICT ID | Dimensions | Description |
|----------|---------|------------|-------------|
| Tile sheet | 128 | 128x32 px | 4 tiles in a row: Floor, Wall, Block, Spawn (32x32 each) |
| Player P0 (white) | 129 | 32x32 px | Base player sprite — palette-swapped for other colors |
| Player P1 (red) | 133 | 32x32 px | Palette swap of P0 |
| Player P2 (blue) | 134 | 32x32 px | Palette swap of P0 |
| Player P3 (yellow) | 135 | 32x32 px | Palette swap of P0 |
| Bomb sprite | 130 | 32x32 px | Single bomb graphic |
| Explosion sprite | 131 | 32x32 px | Explosion center/cross graphic |
| Title graphic | 132 | 320x128 px | "BomberTalk" logo for loading screen |

### Mac SE (512x342, 1-bit monochrome, 16x16 tiles)

| Resource | PICT ID | Dimensions | Description |
|----------|---------|------------|-------------|
| Tile sheet (SE) | 200 | 64x16 px | Same 4 tiles at 16x16 each, 1-bit |
| Player (SE) | 201 | 16x16 px | Single player, 1-bit (all players same on mono) |
| Bomb sprite (SE) | 202 | 16x16 px | Bomb, 1-bit |
| Explosion sprite (SE) | 203 | 16x16 px | Explosion, 1-bit |
| Title graphic (SE) | 204 | 240x80 px | "BomberTalk" logo, 1-bit |

### Resource ID Convention

- 128-199: Color assets (32x32 tiles, 8-bit)
- 200-255: Mac SE assets (16x16 tiles, 1-bit)

The renderer selects the correct ID range at init based on screen dimensions.

### Runtime Rendering Tricks (no extra assets needed)

- **Left/right facing**: CopyBits with horizontally flipped srcRect
- **4 player colors**: One base sprite (white), palette-swapped at build time
  via pixelcraft to produce red/blue/yellow variants. Separate PICT per color.
- **Up/down facing**: Same sprite — top-down round Bomberman looks the same
- **On Mac SE (mono)**: All players look identical (1-bit). Players are
  distinguished by position on the grid, not color.

### Generation Count

**8 images generated from Rockport** (not 11):
4 tiles + 1 player + 1 bomb + 1 explosion + 1 title

Palette swapping the player in pixelcraft produces the 3 additional color
variants. No redundant AI generation needed.

## Tile Sheet Layout

```
Tile sheet (PICT 128, 128x32 pixels):
+--------+--------+--------+--------+
| Floor  | Wall   | Block  | Spawn  |
| 32x32  | 32x32  | 32x32  | 32x32  |
+--------+--------+--------+--------+
  tile 0    tile 1   tile 2   tile 3

Tile index to source rect:
  srcRect.left   = tileIndex * TILE_SIZE
  srcRect.top    = 0
  srcRect.right  = srcRect.left + TILE_SIZE
  srcRect.bottom = TILE_SIZE
```

## Player Sprite Sheet Layout

```
Player sprites (PICT 129, 128x32 pixels):
+--------+--------+--------+--------+
| P0     | P1     | P2     | P3     |
| white  | red    | blue   | yellow |
| 32x32  | 32x32  | 32x32  | 32x32  |
+--------+--------+--------+--------+

Player index to source rect:
  srcRect.left   = playerID * TILE_SIZE
  srcRect.top    = 0
  srcRect.right  = srcRect.left + TILE_SIZE
  srcRect.bottom = TILE_SIZE
```

## Loading Pattern

Source: Black Art (1996) texture loading, Mac Game Programming (2002) tile system.

```c
/* Load a PICT into a GWorld at init time */
static GWorldPtr LoadPICTToGWorld(short pictID, Rect *bounds)
{
    GWorldPtr gw;
    PicHandle pic;
    CGrafPtr oldPort;
    GDHandle oldDevice;
    QDErr err;

    pic = GetPicture(pictID);
    if (pic == NULL) return NULL;

    err = NewGWorld(&gw, 0, bounds, NULL, NULL, 0);
    if (err != noErr) { ReleaseResource((Handle)pic); return NULL; }

    GetGWorld(&oldPort, &oldDevice);
    SetGWorld(gw, NULL);
    LockPixels(GetGWorldPixMap(gw));
    DrawPicture(pic, bounds);
    UnlockPixels(GetGWorldPixMap(gw));
    SetGWorld(oldPort, oldDevice);

    ReleaseResource((Handle)pic);
    return gw;
}
```

## Drawing Pattern

Source: Mac Game Programming (2002) Chapter 10, Tricks of the Gurus (1995).

```c
/* Draw a single tile from the tile sheet to the work buffer */
static void DrawTile(short tileIndex, short destCol, short destRow)
{
    Rect srcRect, dstRect;

    /* Source: region within tile sheet GWorld */
    SetRect(&srcRect,
            tileIndex * gTileSize, 0,
            (tileIndex + 1) * gTileSize, gTileSize);

    /* Destination: position in work buffer */
    SetRect(&dstRect,
            destCol * gTileSize, destRow * gTileSize,
            (destCol + 1) * gTileSize, (destRow + 1) * gTileSize);

    CopyBits(
        (BitMap *)*GetGWorldPixMap(gTileSheetGW),
        (BitMap *)*GetGWorldPixMap(gWorkBuffer),
        &srcRect, &dstRect, srcCopy, NULL);
}
```

## Fallback Strategy

If PICT resources fail to load (missing resource fork, corrupt data):
- Fall back to the colored rectangle rendering (current alpha approach)
- Log warning via clog if available
- Game remains fully playable with placeholder graphics

This means graphics are an enhancement, never a blocker.

## Generation Pipeline

Uses tooling from `~/pixelforge/tools/`:

### Step 1: Generate base artwork

Generate each asset via Rockport (Nova Canvas) at 512x512 PNG.
See `resources/rockport-prompts.md` for per-asset prompts.

### Step 2: Convert to pixel art with Pixelcraft

Use `~/Desktop/pixelcraft` — a Next.js pixel art pipeline with
palette quantization, dithering, and grid export.

**For color Macs** (32x32 tiles, 8-bit):
- Grid: 32x32 (or 128x32 for tile/player strips)
- Palette preset: `mac-system7` (256-color System 7 CLUT)
- Algorithm: `edge_aware` (best for small sprites)
- Dither: `atkinson` (classic Mac dithering algorithm)
- Output: PixelGrid JSON — the exact format `grid2pict` consumes

**For Mac SE** (16x16 tiles, 1-bit monochrome):
- Grid: 16x16 (or 64x16 for strips)
- Palette: custom 2-color (black + white)
- Dither: `atkinson` at 1-bit
- Output: PixelGrid JSON with 2-entry palette

Pixelcraft's export stage (`assembleGrid()`) outputs:
```json
{
  "width": 32, "height": 32,
  "palette": [{"r":0,"g":0,"b":0}, {"r":34,"g":96,"b":34}, ...],
  "pixels": [[0,0,1,1,...], [0,1,2,3,...], ...]
}
```

This is the exact JSON format that `grid2pict` reads.

### Step 3: Convert JSON to PICT with grid2pict

```bash
cd ~/pixelforge/tools && make grid2pict

# Color assets (32x32 tiles, 8-bit)
grid2pict tiles_color.json tiles_color.pict --no-header
grid2pict players_color.json players_color.pict --no-header
grid2pict bomb_color.json bomb_color.pict --no-header
grid2pict explosion_color.json explosion_color.pict --no-header
grid2pict title_color.json title_color.pict --no-header

# Mac SE assets (16x16 tiles, 1-bit or 4-bit)
grid2pict tiles_se.json tiles_se.pict --no-header
grid2pict players_se.json players_se.pict --no-header
grid2pict bomb_se.json bomb_se.pict --no-header
grid2pict explosion_se.json explosion_se.pict --no-header
grid2pict title_se.json title_se.pict --no-header
```

The `--no-header` flag omits the 512-byte file header, producing raw
PICT data suitable for resource embedding.

### Step 4: Embed PICTs in resource fork via Rez

Create a Rez file (`resources/bombertalk_gfx.r`) that reads the raw PICT data:

```rez
/* Color Mac assets (32x32 tiles, 8-bit) */
read 'PICT' (128, "Tiles") "tiles_color.pict";
read 'PICT' (129, "Players") "players_color.pict";
read 'PICT' (130, "Bomb") "bomb_color.pict";
read 'PICT' (131, "Explosion") "explosion_color.pict";
read 'PICT' (132, "Title") "title_color.pict";

/* Mac SE assets (16x16 tiles, 1-bit) */
read 'PICT' (200, "TilesSE") "tiles_se.pict";
read 'PICT' (201, "PlayersSE") "players_se.pict";
read 'PICT' (202, "BombSE") "bomb_se.pict";
read 'PICT' (203, "ExplosionSE") "explosion_se.pict";
read 'PICT' (204, "TitleSE") "title_se.pict";
```

Add to CMakeLists.txt as a resource file:
```cmake
add_application(BomberTalk ${GAME_SOURCES}
    resources/bombertalk.r
    resources/bombertalk_size.r
    resources/bombertalk_gfx.r)
```

Retro68's Rez compiles this into the resource fork of the final .bin.

### Alternative: pict2macbin

If Rez `read` doesn't work with Retro68's Rez, use `pict2macbin` to create
a MacBinary file and include it as a `.rsrc.bin`:

```bash
cd ~/pixelforge/tools && make pict2macbin

pict2macbin resources/bombertalk_gfx.bin \
  tiles_color.pict:128:Tiles \
  players_color.pict:129:Players \
  bomb_color.pict:130:Bomb \
  explosion_color.pict:131:Explosion \
  title_color.pict:132:Title \
  tiles_se.pict:200:TilesSE \
  players_se.pict:201:PlayersSE \
  bomb_se.pict:202:BombSE \
  explosion_se.pict:203:ExplosionSE \
  title_se.pict:204:TitleSE
```

### Step 5: Test on all three Macs

Load with `GetPicture(128)` etc. Verify rendering on each target.
