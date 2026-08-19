# BomberTalk Finishing Plan

Goal: take BomberTalk from "networking showcase with placeholder art" to a game that
looks and feels finished on every target — nice animated sprites on the modern SDL
client, proper colour PICTs on the classic colour Macs, and hand-tuned 1-bit versions
on the Mac SE. Written 2026-08-19 after a full review of the tree at v1.11.x.

Each stage ends with something Matt can run and see. Stages are sized for one
spec-kit feature each (`/speckit.*`) where they touch code. Constitution applies
throughout: C89, all targets stay green, Mac SE is the floor, no malloc in gameplay.

## Where the project actually stands

Done and solid: shared C89 core on five targets, PeerTalk auto-mesh, cross-endian
play, smooth movement, owner-authoritative bombs, mid-game join, dirty-rect
renderers, 8 host test suites (1275 checks) in CI, all known issues KI-001..008
fixed. This is a *late-stage* project.

Missing for "finished": most sprites (only bombs + splash exist as PICTs — players,
tiles, explosions render as coloured rectangles everywhere), any sprites at all on
SDL (rect fallback ships today), sound (zero), power-ups (none — PlayerStats fields
exist but nothing changes them), one map only, no rounds/score, menu/game-over
screens are bare text.

Asset pipeline (REVISED 2026-08-19 — pixelcraft retired in favour of dedicated
pixel-art services/tools):

- **Generation + animation — PixelLab** (pixellab.ai, has an API): characters,
  tilesets, walk/skeleton animation, sprite-sheet export. Primary tool — at
  BomberTalk's sizes (32×32/16×16, tiny roster) one tool should cover it.
- **(Optional fallback) Retro Diffusion** (retrodiffusion.ai, API
  $0.015–0.18/image, no subscription): purpose-trained pixel-art model producing
  the cleanest static base sprites. Add only if PixelLab's base sprites
  disappoint; costs a couple of dollars to find out.
- **Pixel-truing + tier derivation — pixeltrue** (new project,
  `~/Documents/pixeltrue`, github.com/matthewdeaves/pixeltrue): AI "pixel art" is
  often only pixel-LOOKING — N×N blocks with anti-aliased edges, off-grid drift,
  bloated palette. Every generated sprite is trued: detect the logical grid, snap
  each block to one true pixel, hard-quantize to the palette (System 7 / 1-bit),
  Atkinson dither for the SE tier, and report grid/colour-count/correction score
  (high score = sloppy generation, regenerate). Replaces both the old pixelcraft
  quantizer and didder. DEPENDENCY: bombertalk stage 1 needs pixeltrue stages 1–3
  (CPU core, truing, CLI) — see pixeltrue/DESIGN.md.
- **Touch-up — Aseprite** (~$20, scriptable CLI): human pass, essential at
  16×16 1-bit where every pixel is a decision; also sheet/atlas export.
- **PICT encoding — ImageMagick** (writes PICT directly; verify a sample loads in
  QuickDraw on real hardware early; pixelforge's grid2pict remains as fallback
  last-mile writer only if IM's PICTs misbehave).
- **Full-screen art — Rockport (Nova Canvas on Bedrock)**, as today: title,
  game-over, splash refreshes, marketing/blog images. General image models are
  right for big canvases; they are NOT used for sprites (not grid-aligned —
  that's the pixelcraft trap this revision retires).

Generate at each tier's native size where quality demands (RD does sized output);
derive only where it holds up (64→32 box filter, then hand-check). Runtime tricks
unchanged (resources/rockport-prompts.md): CopyBits h-flip for facing, palette
swap for P0–P3, one sprite for up/down.

## Art tiers — one sprite identity, three outputs

Every sprite is designed once (one prompt/identity in RD + PixelLab), emitted per
tier at native size where possible, derived + hand-checked where not:

| Tier | Size | Format | Frames | Notes |
|------|------|--------|--------|-------|
| SDL modern | 64×64 | BMP atlas (SDL core loads BMP; no new deps) | full (4-frame walk, bomb pulse, explosion bloom) | nearest-neighbour integer scale; window 2×/3× |
| Colour classic | 32×32 | indexed PICT, System-7 palette | 2–3 max | existing rPict slots 128–135, 140–142 |
| Mac SE | 16×16 | 1-bit PICT, white=transparent | 1–2 | lowmem .r variant; budget ≤ ~40 KB added |

Sprite roster to produce: player (1 master; palette-swap ×4, flip for facing) with
walk + death frames; tile sheet (floor, wall, destructible block + crumble);
explosion set (centre, arm, end — the classic cross pieces); title art; power-up
icons (stage 6); bomb refresh optional (exists).

## Stages

1. **Asset pipeline one-command tool.** ✅ **Built 2026-08-19** —
   `tools/build-asset.sh <frames-dir> <name>` takes generated PNG frames →
   **pixel-truing first** (pixeltrue `batch --true`, one grid voted across all
   frames so animations cannot wobble; correction score gates the run, a red
   verdict fails before anything is written) → emits colour PICTs, SE 1-bit
   PICTs, the SDL BMP atlas, and regenerates the .r data blocks. Sizes, ids,
   palettes and low-memory policy live in `resources/gfx/assets.json`, which
   `scripts/embed-gfx.py` now also reads (no more hardcoded frame table).
   Documented in `docs/asset-pipeline.md`.

   The PICT question resolved differently than assumed: **ImageMagick cannot
   write the SE tier at all** — its PICT coder ignores `-depth 1`/`-monochrome`
   and always emits an 8-bit PixMap, and grid2pict emits 4-bit/8-bit PixMaps.
   Both are Color QuickDraw constructs, and the SE's original QuickDraw draws
   opcode `$0098` only as a BitMap. So `tools/png2pict.py` writes both forms
   directly: a genuine 1-bit BitMap picture for the SE, and an indexed PixMap
   for colour Macs that packs 4-bit when it fits (half ImageMagick's bytes, and
   byte-for-byte the same structure grid2pict produced on the existing bomb
   frames). ImageMagick is now only a PNG decoder. `tests/test_png2pict.py`
   parses the emitted opcode stream back and is wired into `make -C tests check`.

   **Still open — the hardware half:** no modern decoder can judge a 1-bit
   BitMap PICT (ImageMagick renders it solid black, `sips` solid white), so the
   SE tier needs one confirmation on a real SE or a System 6/7 guest under
   `../QemuMac` before a whole sprite set is built on it. Fallback if it fails
   is `--mode indexed` for the SE tier too. Colour Macs are already proven
   (KI-008, confirmed on the G5).
   *Test: one command, three artifacts, all builds green, bombs visible on the SE.*
2. **Full static sprite set, classic Macs.** Players, tiles, explosion, title —
   colour PICTs + SE 1-bit PICTs into the existing resource slots (the renderers
   already load these IDs and fall back to rects; this stage just fills the slots,
   plus SE-side load code where the colour-only guard skips it today).
   *Test: a game on the 6400 and the SE with zero coloured rectangles visible.*
3. **SDL sprite backend.** BMP atlas loader, animated sprite draw (timing from
   existing tick constants — fuse ticks drive bomb pulse, accumulator drives walk
   phase), integer scaling + window size option, vsync/frame cap (kills the CPU
   spin cousin of KI-007 on modern hosts).
   *Test: modern client looks like a real 16-bit-era Bomberman in motion.*
4. **Classic animation pass.** 2-frame walk + bomb pulse on colour Macs (budget:
   measure fps on the 6200 before/after); SE stays static or 2-frame only if the
   frame budget allows. Death flash already exists — keep.
   *Test: side-by-side 6400 vs SDL reads as the same game, richer vs simpler.*
5. **Sound.** 5 effects: place, explosion, death, pickup, menu-select. SDL:
   SDL_QueueAudio WAVs. Classic: async SndPlay of 'snd ' resources (Tricks p.89,
   Black Art ch.7). Mac SE: menu sounds only, or fully off (perf floor).
   *Test: bombs go boom on G5 and laptop; SE frame rate unchanged.*
6. **Power-ups** (the one real gameplay gap): flame+, bomb+, speed. Determinism by
   construction: the bomb OWNER already broadcasts MSG_BLOCK_DESTROYED — add a drop
   byte to that message (owner rolls, everyone applies). Pickup = walk-over, apply
   locally + reliable broadcast. Protocol bump; lobby already rejects mismatches.
   Drop/pickup logic goes in the portable core with host tests.
   *Test: two clients each see the same drops in the same places, every game.*
7. **Maps + lobby map select.** 2–3 new TMAPs (varying density/layout); initiator
   picks the map, map id rides in MSG_GAME_START (late-join map sync already
   handles the rest).
   *Test: pick map 3 in the lobby; everyone loads map 3, late joiner too.*
8. **Rounds, score & screen polish.** Best-of-N with score screen between rounds,
   rematch without tearing down the mesh; title art on menu; game-over screen with
   winner + scores instead of bare text.
   *Test: play a best-of-3 and be able to tell who's winning without the logs.*
9. **(Stretch) Practice bots.** 1–3 local AI players issuing normal inputs
   (network-transparent by construction). Random-walk-with-bomb-avoidance is
   enough for solo testing/demo (Tricks "Dungeon" AI as reference).
10. **Release v2.0.** Version bump, fresh screenshots/GIFs for README + blog,
    binaries for each target, itch.io page if desired.

## Guardrails

- gfx.r is 587 KB (mostly splash); SE ships the 59 KB lowmem variant — keep the SE
  budget tight and measure FreeMem() after stage 2.
- Any new multi-byte wire field goes through net_wire.c (D3 seam), and any protocol
  change bumps BT_PROTOCOL_VERSION.
- Sprite-frame selection derives from existing sim state (facing, accumulators,
  fuse ticks) — renderers only read; no new network traffic for animation.
- Stages 2–4 are pure renderer/resources: no protocol bump, safe to ship
  incrementally. Stage 6 is the only protocol change in the plan.
