# Deepening & Testability Roadmap

Deeper modules → a testable core. This is the todo list for turning BomberTalk's
platform-coupled tree into a small **portable core** that can be unit-tested on the
host (Linux/POSIX) — which is the *same* carve the SDL2 / Apple-Silicon track needs.
Generated 2026-07-05 from an `improve-codebase-architecture` review on branch
`011-macosx-sdl2`.

## Why now

Every bug fixed this session was in nearly-pure logic and was found **by hand on real
hardware**, slowly:

| Bug | Where | Kind of logic | Should have been… |
|-----|-------|---------------|-------------------|
| KI-008 | `renderer.c` `ReadPixelRGB` | pixel byte → RGB, per depth | a host unit test (feed bytes, assert RGB) |
| KI-006 | `screen_game.c` reactivation / alive-count | array logic over players | a host unit test (quit a peer, assert game ends) |
| coord false-kills (004) | `net.c` normalization | `(pixelX<<8)/tileSize` round-trip | a host round-trip test (16px ↔ 32px) |
| collision (004) | `player.c` axis-separated AABB | geometry | a host table-driven test |

None of these needed a Mac. They needed a **seam** between the logic and the Toolbox.

## Current test state (reviewed)

- **No unit tests, no CTest.** `CMakeLists.txt` has no `enable_testing`/`add_test`.
- **CI** (`.github/workflows/ci.yml`) builds the three classic targets + runs `cppcheck`
  (`warning,performance`). Good coverage of "does it compile / obvious static bugs",
  zero coverage of "does the logic compute the right answer".
- **The only functional test is a human on three Macs.** That is why KI-006/008 survived
  to hardware.

## The core is already almost pure

Platform/Toolbox references per source (higher = more coupled):

```
tilemap.c   4     player.c   2     bomb.c     5     <- gameplay CORE, nearly pure C89
input.c     6     screens.c  1
screen_*.c  9-26  main.c    33     net.c     98     renderer.c 216   <- platform edge
```

The exact things blocking a host build of the core, and the fix for each:

| Blocker | Files | Fix |
|---------|-------|-----|
| `Rect` / `SetRect` / `Point` | player.c, bomb.c | tiny host geometry shim (4-field struct + `SetRect`) |
| `Renderer_MarkDirty(col,row)` | player.c (1), bomb.c (2) | it's a *presentation notification* — stub to no-op on host, or route through an injected callback |
| `Handle` + Resource Manager | tilemap.c (TMAP load) | already has a `kLevel1` static fallback — guard the RM path, host uses the fallback |
| `CLOG_*` | all | already portable (`clog_posix.c` ships in the OS X build) |

Nothing here is deep. The core wants to be portable; it's held back by a handful of
edges.

## Deepening todo list (each item states its test payoff)

- [x] **D1 — Host type shim.** `tests/host_mac_types.h`: `RGBColor`/`ColorTable` +
  `Rect`/`Point`/`SetRect`, included only under `BT_HOST_TEST`. *Unblocked compiling pure
  units on the host.*
- [x] **D2 — Portable-core carve.** *Done:* pure logic carved into standalone, host-built
  units — `netcoord.c` (coord normalisation), `movement.c` (accumulator), `tilemap_parse.c`
  (TMAP validate/clamp/sanitise), `collision.c` (`Collide_ResolveAxis` — axis-separated AABB),
  `raycast.c` (`Ray_Reach` — bomb blast reach), and `wincond.c` (`Win_Decide` +
  `Win_ShouldReactivate`). Portable gameplay constants live in `coredefs.h` (Toolbox-free;
  `game.h` includes it). The three gameplay-critical carves took plain-value arguments (flat
  tile/bomb arrays, tallied counts) rather than `gGame`/`Player`/`TileMap` pointers — no
  fixture needed, no dirty/clock port needed, and the hot inner loops stay direct-indexed and
  allocation-free (Constitution V/VII). The Mac callers (`CollideAxis`, `ExplodeBomb`, the
  game-over tally) became thin wrappers that gather globals and apply the result.
- [ ] **D3 — `net_wire.h` serialization seam.** Move message pack/unpack behind one unit
  that owns byte order (today "both big-endian" is a comment, not code). *Payoff:
  endianness round-trip tests; also the prerequisite for a little-endian M5 to play the
  classic Macs.*
- [x] **D4 — Renderer pixel-format extraction.** `renderer.h` was **already** a clean
  backend-agnostic seam (18 calls, no backend types — verified). Depth-aware pixel read
  lifted to `pixfmt.c` (`PixFmt_ReadRGB`), host-testable without a live GWorld.
- [x] **D5 — Host test target + CI job.** `tests/Makefile` (native gcc, zero deps) +
  `tests/test_util.h` C89 assert runner; new `host-tests` job in `.github/workflows/ci.yml`
  beside the three build jobs. The loop is closed.

Remaining order: finish **D2** (needs a `Player`/`TileMap` host fixture + dirty/clock
ports) → **T2/T4/T5** ride on it → **D3**. The renderer **backend** split (a full
`renderer_sdl.c`) stays on the M5 track; the header seam already exists, so it blocks
nothing here.

## First tests to write (regression for what we just fixed)

- [x] **T1 `PixFmt_ReadRGB`** — 8-bit(ctab), 16-bit(555), 32-bit(xRGB) + the white≠black
  distinctness that was the actual failure. *(KI-008)* — `tests/test_pixfmt.c`, 21 checks.
- [x] **T2 win-condition** — sole survivor wins, mutual-destruction draw, undecided while
  dying, one-player match never ends, and the KI-006 reactivation guard (target==position →
  no phantom reactivation). *(KI-006)* — `tests/test_wincond.c`, 14 checks.
- [x] **T3 coord round-trip** — round-trip exact per size + grid-cell preserved across
  16px/32px + no short overflow at max field. *(004)* — `tests/test_netcoord.c`, 925 checks.
- [x] **T4 collision** — axis-separated AABB against a fixture tilemap: free movement, flush
  wall/block stop on all four axes, bomb-tile solidity + walk-off pass-through, play-bounds
  clamp. *(004)* — `tests/test_collision.c`, 17 checks.
- [x] **T5 bomb raycast** — range, wall-stops-short, block-destroy-and-stop, wall shadows a
  block behind it, map-edge stop. *(005)* — `tests/test_raycast.c`, 21 checks.
- [x] **T6 movement accumulator** — one tile crossed in `ticksPerTile` ticks on both 16px
  and 32px (resolution independence) + carry invariants. — `tests/test_movement.c`, 216 checks.

**Shipped this session (011):** D1, D2, D4, D5 complete; T1–T6 green — 1234 checks across
7 host suites, wired into CI. Portable core is now: `pixfmt`, `netcoord`, `movement`,
`tilemap_parse`, `collision`, `raycast`, `wincond` (+ `coredefs.h`). All four Mac targets
(68k MacTCP, PPC OT, PPC MacTCP, OS X Carbon fat ppc+i386) stay green and warning-clean;
cppcheck clean.

**Remaining:** D3 (`net_wire.h` endianness seam) and the SDL2 renderer backend (M5 track;
`renderer.h` seam already exists).

## Non-goals / guardrails

- **Do not** move the core behind per-call accessors in hot paths — the 006/008 work
  cached PixMap pointers precisely to avoid Toolbox-call overhead on the Mac SE. The
  carve must keep hot paths allocation- and indirection-free (Constitution V, VII).
- **Do not** try to host-test `renderer.c` compositing or `net.c` transport — those are
  the platform edge and are validated on hardware. Test the *logic*, not the Toolbox.
- Keep it C89 in the core so the same files compile under Retro68 and native gcc.
