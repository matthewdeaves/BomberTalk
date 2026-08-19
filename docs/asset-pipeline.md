# Asset pipeline

One command turns generated PNG frames into every asset the game ships:

```sh
tools/build-asset.sh <frames-dir> <asset-name>
```

It trues the frames first, derives each tier from the trued result, writes the
PICTs and the SDL atlas into `resources/gfx/`, and regenerates the Rez data
blocks. Nothing untrued reaches a build.

This document covers the tooling. For the resource layout the renderers expect
— which PICT holds what, how sheets are cut up — see
[`specs/001-v1-alpha/contracts/asset-pipeline.md`](../specs/001-v1-alpha/contracts/asset-pipeline.md).

## Requirements

| Tool | Why | Install |
|---|---|---|
| [pixeltrue](https://github.com/matthewdeaves/pixeltrue) | truing, palette enforcement, resize, dither, atlas | `cd ../pixeltrue && swift build -c release` |
| ImageMagick | PNG decoding only (`magick`) | `brew install imagemagick` |
| python3 | PICT writer and manifest reader | ships with macOS / `brew install python` |

`build-asset.sh` finds pixeltrue at `../pixeltrue/.build/release/pixeltrue`, then
on `PATH`; override with `PIXELTRUE=/path/to/pixeltrue`.

## What it does

**1 — True.** Every frame in the directory is snapped to **one grid voted across
the whole directory** (`pixeltrue batch --true`), so an animation cannot wobble
between frames. Truing keeps the generator's own colours: the correction score
then measures grid error, and each tier enforces its own palette afterwards.

Backgrounds are knocked out first (`--knockout auto` by default, per asset in the
manifest) — border-connected background removal, so enclosed same-coloured
regions inside the sprite survive.

**2 — Gate.** Two things are checked, *before anything is written*.

*Grid confidence* first: 0 means no periodic structure was found, 1 means a
razor-sharp grid. Below `--min-confidence` (default 0.15) the run fails, because
a correction score cannot tell "trued cleanly" from "there was no grid to find" —
a photo or a smooth render still yields *a* pitch, and its cells still cohere at
it. Real generated sprites land around 0.5; content with no block structure lands
near 0. Note that `--knockout auto` raises the floor a little by manufacturing a
hard silhouette edge, so a non-sprite can score ~0.2 rather than ~0.

*Then each frame's correction score and verdict*. A `red` verdict, or any score
above `--score-max` (default 35), fails the run — regenerate that frame instead
of shipping it. `amber` frames warn: usable, worth a hand-check in Aseprite.

**3 — Derive.** Each tier is resized and palette-mapped from the trued frames:

| Tier | Output | Format |
|---|---|---|
| `color` | 32×32, System 7 palette | indexed PICT (4-bit if ≤16 colours, else 8-bit) |
| `se` | 16×16, 1-bit, Atkinson dither | **1-bit BitMap PICT** |
| `sdl` | 64×64, System 7 palette | BMP sprite atlas |

**4 — Embed.** `scripts/embed-gfx.py` rewrites `resources/bombertalk_gfx.r` and
the low-memory variant from everything in the manifest. Skip with `--no-embed`.

## The manifest

`resources/gfx/assets.json` is the single source of truth for both scripts: what
tiers an asset has, its output sizes and palettes, the resource ids, and whether
each resource survives into the low-memory (Mac SE / 68k MacTCP) build. **Add an
asset there before building it.** Resource ids must match the `rPict*` defines in
[`include/game.h`](../include/game.h).

File naming follows what is already on disk: `<name>_<tier>.pict` for a
single-frame asset, `<name>_<tier>_f<N>.pict` when `frames > 1`.

## Why a bespoke PICT writer

`tools/png2pict.py` exists because neither obvious option can produce what the
Mac SE needs:

- **ImageMagick** writes PICTs, but always as an **8-bit PixMap** — `-depth 1`
  and `-monochrome` are silently ignored by its PICT coder.
- **pixelcraft's `grid2pict`** writes 4-bit and 8-bit indexed **PixMaps**.

Both are Color QuickDraw constructs. The Mac SE is a 68000 with **original
QuickDraw**, where `DrawPicture` understands opcode `$0098` only when it carries
a **BitMap** (rowBytes with the high bit clear). A PixMap picture is not
something original QuickDraw can draw. That is why the SE tier goes through
`--mode 1bit`, which emits a genuine 1-bit BitMap picture with no colour table.

The colour tier uses `--mode indexed`, which also packs 4-bit when the image fits
in 16 colours — half the resource-fork bytes of ImageMagick's always-8-bit
output, and structurally the same picture `grid2pict` produced (verified
byte-for-byte on the existing bomb frames).

ImageMagick is used only as a PNG **decoder**; no PICT semantics come from it.

### Transparency conventions

These are not cosmetic — the renderers key off them:

- **Colour tier**: the background key is **magenta `#FF00FF`**, forced into
  palette slot 0, and the **top-left pixel must carry it**. `CreateMaskFromGWorld`
  in [`src/renderer.c`](../src/renderer.c) reads the top-left pixel's RGB and
  treats every matching pixel as transparent. `png2pict.py` warns if the
  top-left pixel is not the key.
- **SE tier**: **white (bit 0) is transparent**, black (bit 1) is ink.
  `LoadPICTToBitMap` auto-detects and flips inverted polarity, but getting it
  right here avoids relying on that.

## Verifying on real QuickDraw

Modern decoders are not an oracle for these files. ImageMagick renders a 1-bit
BitMap PICT as solid black regardless of its contents; macOS `sips` renders it
solid white and rejects `grid2pict`'s PixMap files outright. Both read the
PixMap form correctly, which is why the indexed path is cross-checked against
ImageMagick in the tests and the BitMap path is not.

`tests/test_png2pict.py` (run by `make -C tests check`) therefore parses the
emitted picture back by walking the opcode stream per *Inside Macintosh: Imaging
With QuickDraw*, and asserts the pixels survive the round trip. That catches
structural, offset and PackBits errors. It cannot catch a misreading of the
spec — only hardware can.

**So the SE tier still needs one hardware confirmation**, and it is worth doing
once, early, before a whole sprite set is built on the assumption:

1. Build with the low-memory resource fork and run on the SE (or a System 6/7
   guest under `../QemuMac`, which has genuine QuickDraw — far cheaper than
   round-tripping to hardware).
2. Look for the bomb sprites. Rectangles instead of sprites means the PICT did
   not draw.
3. If the 1-bit BitMap form fails, the fallback is `--mode indexed` for the SE
   tier too, or pixelcraft's `tools/grid2pict.c` — but note that fallback assumes
   Color QuickDraw, so on a true SE it is a step backwards, not sideways.

Colour Macs are already proven: PICT loading was confirmed on the G5 (see
[`notes/known-issues.md`](../notes/known-issues.md), KI-008).

## Options

```
--tiers a,b,c     tiers to build (default: every tier in the manifest)
--score-max N     fail if any frame's correction score exceeds N (default 35)
--min-confidence F  fail if the detected grid's confidence is below F (default 0.15)
--allow-amber     accept amber verdicts without the hand-check note
--knockout SPEC   override the manifest: auto, none, or #RRGGBB
--keep-work       keep the intermediate directory for inspection
--no-embed        emit PICTs but skip regenerating the .r files
```

`--keep-work` is the one to reach for when output looks wrong: it leaves the
trued frames and every tier's PNGs on disk, so you can see whether truing or a
later stage caused it.

## Example

```sh
$ tools/build-asset.sh ~/gen/bomb-frames bomb
build-asset: bomb -- 3 frame(s) from /Users/matt/gen/bomb-frames
  pixeltrue: ../pixeltrue/.build/release/pixeltrue
  tiers:     color se sdl

true: detecting one grid across all frames
  grid:  pitch 8.13333x8.06667 offset 5.98333,7.25 confidence 0.56,0.53
  frame: bomb_f0.png                  score   23.5  amber
  frame: bomb_f1.png                  score   27.6  amber
  frame: bomb_f2.png                  score   26.3  amber
  warning: amber frame(s) worth a hand-check in Aseprite: ...

color: 32x32, palette system7, dither none
  bomb_color_f0.pict (3224 bytes, indexed)
  ...
se: 16x16, palette 1bit-mac, dither atkinson
  bomb_se_f0.pict (628 bytes, 1bit)
  ...
sdl: 64x64, palette system7, dither none
  bomb_atlas.bmp (sprite atlas)

wrote resources/bombertalk_gfx.r (8 PICTs, 203.4 KB of picture data)
wrote resources/bombertalk_gfx_lowmem.r (4 PICTs, 21.8 KB of picture data)
```
