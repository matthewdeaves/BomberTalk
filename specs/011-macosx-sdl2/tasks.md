---
description: "Task list for 011-macosx-sdl2 implementation"
---

# Tasks: Mac OS X + Modern macOS Builds (Carbon/QuickDraw + SDL2)

**Input**: Design documents from `/specs/011-macosx-sdl2/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Included where the spec explicitly asks for them — byte-order unit tests (US3) and a headless simulation smoke test (US1). No other test tasks are generated.

**Machine legend** (informational, not part of the checklist format):
`[box]` = doable entirely on the Linux dev box · `[osx-hw]` = requires the OS X fleet powered on (driven from the box over SSH) · `[m5]` = requires the M5 MacBook Air · `[classic-hw]` = requires a Classic Mac (via classic-mac-hardware MCP).

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build-system wiring and scaffolding shared by all new targets.

- [ ] T001 `[box]` Add a `BT_TARGET` option (`classic` default | `sdl2`) to `CMakeLists.txt` and gate the POSIX/SDL2 source set behind it, leaving the classic toolchain builds untouched.
- [ ] T002 [P] `[box]` Pin the PeerTalk FetchContent fallback `GIT_TAG` to `v1.12.1` in `CMakeLists.txt` (FR-017); leave `PEERTALK_DIR` local-checkout override intact.
- [ ] T003 [P] `[box]` Create `tests/` with a minimal assert-based C test harness wired to `ctest` (no external framework), plus a `CMakeLists` hook enabled only for `BT_TARGET=sdl2`.
- [ ] T004 [P] `[box]` Create `tools/` and seed `osx-screen-run.sh` from the `~/peertalk/tools/` pattern (on-screen launch via osascript).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The platform seam skeleton every non-classic target compiles against.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T005 `[box]` Create `include/platform.h`: `BT_PLATFORM_POSIX` / `BT_PLATFORM_CARBON` selection macros, a portable `Rect` typedef (`{short left,top,right,bottom;}`) for non-Toolbox builds, a `Tick`/`TickCount` alias, and backend-selection guards — no logic yet (data-model §1).
- [ ] T006 `[box]` Create `src/platform_posix.c` skeleton with the `TickCount()` shim signature (monotonic → 60 Hz, `unsigned long`) stubbed.
- [ ] T007 [P] `[box]` Create `include/net_wire.h` skeleton: `bt_hton16`/`bt_ntoh16` declarations behind a build-time endianness define (no-op on big-endian, swap on little-endian) — signatures only (contract: network-wire-endianness.md).
- [ ] T008 `[box]` In `CMakeLists.txt`, define the POSIX/SDL2 source set (`renderer_sdl.c`, `input_sdl.c`, `main_posix.c`, `platform_posix.c`, shared core) and assert the classic builds exclude every new file.

**Checkpoint**: empty seam compiles on Linux; three classic builds still green.

---

## Phase 3: User Story 1 — Portable Game Core Off the Toolbox (Priority: P1) 🎯 Foundation MVP

**Goal**: Game logic + `net.c` compile and run on POSIX with zero Classic Mac headers, behind the platform seam.

**Independent Test**: Compile the shared core on Linux `-std=c89 -Wall -Wextra` against a headless renderer stub and run a smoke test that steps the sim and connects to a PeerTalk POSIX peer.

- [ ] T009 [US1] `[box]` Route `TickCount()` uses in shared core (`src/screen_game.c`, `src/net.c`) through `platform.h` so POSIX resolves to the shim; classic path unchanged (spec AC US1-2).
- [ ] T010 [US1] `[box]` Point `player.c`/`bomb.c` AABB `Rect` usage at the `platform.h` `Rect` (identical field layout) so no Toolbox `Rect` is needed on POSIX (spec AC US1-3).
- [ ] T011 [US1] `[box]` Audit `src/player.c`, `src/bomb.c`, `src/tilemap.c`, `src/screens.c`, `src/net.c` and guard/remove any Classic Mac header includes so each compiles POSIX (spec AC US1-1).
- [ ] T012 [US1] `[box]` Implement the `src/platform_posix.c` `TickCount()` shim fully (monotonic clock → 60 Hz ticks-since-start, TickCount wrap semantics).
- [ ] T013 [P] [US1] `[box]` Ensure `src/tilemap.c` has a Resource-Manager-free POSIX path (load `kLevel1` static data; no `'TMAP'` resource calls on POSIX).
- [ ] T014 [US1] `[box]` Add `src/renderer_null.c` headless renderer stub implementing the `renderer.h` public surface as no-ops so the core links without SDL.
- [ ] T015 [US1] `[box]` Write `tests/test_sim_smoke.c`: step the simulation N frames and connect to a PeerTalk POSIX peer, asserting no crash (SC-001 partial).
- [ ] T016 [US1] `[box]` Verify shared core compiles on Linux with zero warnings and no Classic Mac header inclusion (SC-001); rebuild all three classic targets to confirm still green + unchanged (FR-019).

**Checkpoint**: portable core proven headless on Linux.

---

## Phase 4: User Story 2 — Playable SDL2 BomberTalk on the Dev Box (Priority: P1)

**Goal**: A graphical, playable BomberTalk in an SDL2 window on Linux — ~95% of the M5's app.

**Independent Test**: Build `BT_TARGET=sdl2` on Linux, launch two instances, play a full round (movement, bombs, explosions, block destruction, death, game-over) rendered in the window.

- [ ] T017 [US2] `[box]` Extend `scripts/embed-gfx.py` to emit SDL assets (PNG or embedded raw RGBA) from the existing PICT sprite sources into `resources/gfx/` (research R5).
- [ ] T018 [US2] `[box]` Implement `src/renderer_sdl.c` lifecycle: `Renderer_Init`/`Renderer_Shutdown` (SDL window + `SDL_Renderer`), per contract renderer-backend.md.
- [ ] T019 [US2] `[box]` Implement `src/renderer_sdl.c` frame brackets: `BeginFrame`/`EndFrame`, `BeginScreenDraw`/`EndScreenDraw`, `BeginSpriteDraw`/`EndSpriteDraw` (present via SDL).
- [ ] T020 [US2] `[box]` Implement `src/renderer_sdl.c` background: `RebuildBackground`/`RequestRebuildBackground` as an SDL background texture; `MarkDirty`/`MarkAllDirty` as accepted no-ops (research R4).
- [ ] T021 [US2] `[box]` Implement `src/renderer_sdl.c` sprites: draw player/bomb (pulse frame)/explosion via `SDL_RenderCopy`, with colored-rectangle fallback when assets missing (Constitution VI).
- [ ] T022 [US2] `[box]` Implement `src/renderer_sdl.c` text: bitmap-font drawing for loading/menu/lobby/game UI text (no SDL_ttf dependency).
- [ ] T023 [US2] `[box]` Implement `src/input_sdl.c`: `SDL_GetKeyboardState` held-key state + edge accumulator matching `input.h` semantics (`Input_IsKeyDown`/`WasKeyPressed`/`ConsumeFrame`) (spec AC US2-2).
- [ ] T024 [US2] `[box]` Implement `src/main_posix.c`: poll loop (`SDL_PollEvent` + `Net_Poll` + `Input_Poll` + fixed-tick `Screens_Update`/`Screens_Draw`), Cmd/Ctrl-Q quit (Constitution VII).
- [ ] T025 [US2] `[box]` Wire the `BT_TARGET=sdl2` CMake target to link SDL2 + PeerTalk POSIX backend + clog; skip cleanly with a message if `find_package(SDL2)` fails (contract build-targets.md).
- [ ] T026 [US2] `[box]` Validate: play a full round across two SDL2 instances on Linux — all screens render and transition, gameplay events sync (SC-002, spec AC US2-1..4).

**Checkpoint**: the modern game runs and is playable on the dev box.

---

## Phase 5: User Story 3 — Cross-Era Play Across Endianness (Priority: P1)

**Goal**: Little-endian builds interoperate with big-endian Classic/PPC Macs over the wire.

**Independent Test**: Byte-order unit tests pass on the Linux (little-endian) build; full LE↔BE deferred to hardware.

- [ ] T027 [P] [US3] `[box]` Implement `bt_hton16`/`bt_ntoh16` in `include/net_wire.h`: no-op on big-endian, byte-swap on little-endian, selected by a build-time endianness define (contract network-wire-endianness.md).
- [ ] T028 [US3] `[box]` Route `MsgPosition.pixelX`/`pixelY` through `net_wire` in `src/net.c` `Net_SendPosition` (encode) and `on_position` (decode) — the only two multi-byte wire fields; no other message changed (data-model §2).
- [ ] T029 [P] [US3] `[box]` Write `tests/test_wire_endian.c`: `MsgPosition` encode→decode round-trip and a fixed big-endian byte vector decoding to expected `pixelX`/`pixelY` (SC-003).
- [ ] T030 [US3] `[box]`+`[classic-hw]` Confirm classic builds emit byte-identical `MsgPosition` bytes before/after by capturing a fixed message's on-wire bytes (SC-004).
- [ ] T031 [US3] `[box]` Confirm no `BT_PROTOCOL_VERSION` bump is required (layout/size unchanged) and record it in `data-model.md` (spec AC US3-4).

**Checkpoint**: byte order proven on Linux; ready for the LE↔BE hardware proof (F, in quickstart).

---

## Phase 6: User Story 4 — Native OS X Application for 10.3–10.7 (Priority: P2) — GATED

**Goal**: A native Carbon + QuickDraw windowed app for the PPC/Intel fleet, reusing `renderer.c`.

**Independent Test**: The viability spike opens a window and draws on the Intel mini (10.7) and a PPC Mac; then the fat/ppc-only apps play a round on hardware.

- [ ] T032 [US4] `[box]` Write `tools/spike-carbon-window.sh` + a minimal Carbon spike source (open window, `CopyBits` a colored rect, `GetKeys`, quit) (contract build-targets.md §4).
- [ ] T033 [US4] `[osx-hw]` Build the spike fat on `mini-intel` via rsync+SSH; run on-screen on the Intel mini (10.7 i386) and one PPC via `osx-screen-run.sh`. **GATE**: record pass/fail (SC-005). If fail, re-scope per research R2 before continuing.
- [ ] T034 [US4] `[box]` Adapt `tools/build-macosx-fat.sh` from `~/peertalk`: BomberTalk sources, `Carbon.framework` link, `.app` bundle + `Info.plist` (contract build-targets.md §3).
- [ ] T035 [US4] `[box]` Make `renderer.c` (color path), `input.c` (`GetKeys`), and `main.c` (`WaitNextEvent`) compile under Carbon Mach-O, resolving any Carbon-vs-classic Toolbox header differences behind `platform.h` — no QuickDraw rewrite (spec AC US4-2).
- [ ] T036 [US4] `[osx-hw]` Produce the fat ppc+i386 (min 10.4) `.app`; verify `lipo -info` shows `ppc i386` (spec AC US4-3).
- [ ] T037 [US4] `[osx-hw]` Produce the ppc-only (min 10.3.9) build for the G3 (spec AC US4-4).
- [ ] T038 [US4] `[osx-hw]`+`[classic-hw]` Validate on-screen: play a round on G4/G5/Intel-mini (fat) and G3 (ppc-only); demonstrate cross-era play vs a Classic Mac (SC-006, SC-008, spec AC US4-5).

**Checkpoint**: native OS X app runs on the fleet (or is re-scoped per the spike).

---

## Phase 7: User Story 5 — Native macOS 26+ App on Apple Silicon (Priority: P2)

**Goal**: The pushed SDL2 sources build native arm64 on the M5 with minimal platform work.

**Independent Test**: On the M5, `brew install sdl2`, build `BT_TARGET=sdl2`, run a round vs a Linux peer and a Classic Mac.

- [ ] T039 [US5] `[m5]` Build the SDL2 target natively on macOS 26 arm64 (`brew install sdl2`, `cmake -DBT_TARGET=sdl2`) and confirm zero shared-core source changes are needed (SC-007, spec AC US5-1).
- [ ] T040 [US5] `[m5]` Package the `.app` bundle (Info.plist, icon) and document code-sign/notarize steps for macOS 26 (spec AC US5-4).
- [ ] T041 [US5] `[m5]` Add Retina/high-DPI handling in `src/renderer_sdl.c` (SDL high-DPI flag + logical render size) (spec AC US5-2).
- [ ] T042 [US5] `[m5]`+`[classic-hw]` Validate on the M5: play a round vs a Linux POSIX peer and vs a Classic Mac (SC-007, SC-008, spec AC US5-3).

**Checkpoint**: native Apple Silicon app plays across the fleet.

---

## Phase 8: Polish & Cross-Cutting Concerns

- [ ] T043 [P] `[box]` Warning sweep across all new code; ensure warning-clean except the two documented third-party warnings (clog `fsync`, Apple `crt1.o -mlong-branch`), each justified in-tree (FR-018, SC-009).
- [ ] T044 [P] `[box]` Update root `CLAUDE.md` architecture section with the platform seam, the three renderer backends, and the endianness layer.
- [ ] T045 [P] `[box]` Add the `011-macosx-sdl2` entry to the `## Recent Changes` block and any release notes.
- [ ] T046 `[box]` Run the `quickstart.md` sections A–B end-to-end on Linux and confirm SC-001..SC-004/SC-009 for the on-box scope.
- [ ] T047 `[box]` Final regression: rebuild all three classic targets clean and reconfirm byte-identical wire output (Constitution II, FR-019).

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)** → no deps.
- **Foundational (P2)** → after Setup; **blocks all user stories**.
- **US1 (P3)** → after Foundational. Foundation for US2/US3/US4/US5.
- **US2 (P4)** → after US1.
- **US3 (P5)** → after US1; parallelizable with US2 (touches `net.c`/`net_wire.h`, not the SDL renderer).
- **US4 (P6)** → after US1; its T033 spike **gates** T034–T038. Independent of US2/US3 for the app itself; needs US3 for the cross-era demo (T038).
- **US5 (P7)** → after US2 is pushed (it builds the same SDL2 sources); needs US3 for the cross-era demo (T042).
- **Polish (P8)** → after the targeted stories land.

### On-this-machine vs handoff

- **Fully on the Linux box now**: Setup, Foundational, US1, US2, US3, plus authoring the US4 script/spike and adapting the fat-build script (T032, T034, T035). This is the "do as much as we can and push" scope.
- **Needs the OS X fleet (driven from the box)**: T033, T036, T037, T038.
- **Handoff to the M5**: US5 (T039–T042).
- **Needs a Classic Mac**: T030, T038, T042 (cross-era proofs).

### Parallel opportunities

- Setup: T002, T003, T004 in parallel.
- US1: T013 parallel with the T009–T012 seam edits (different concern).
- US2 vs US3: once US1 lands, US2 (renderer/input/loop) and US3 (net_wire/net.c) proceed in parallel.
- US3: T027 and T029 in parallel (impl vs test).
- Polish: T043, T044, T045 in parallel.

---

## Parallel Example: after US1 lands

```bash
# Track A (graphics): US2 renderer/input/loop on the SDL backend
Task: "Implement renderer_sdl.c lifecycle + brackets + sprites (T018–T022)"
Task: "Implement input_sdl.c held-key backend (T023)"

# Track B (wire): US3 endianness, independent files
Task: "Implement bt_hton16/ntoh16 in net_wire.h (T027)"
Task: "Write byte-order unit test tests/test_wire_endian.c (T029)"
```

---

## Implementation Strategy

### MVP path (on this machine)

1. Setup + Foundational → seam compiles.
2. **US1** → portable core proven headless (first real milestone).
3. **US2** → **playable SDL2 game on Linux** — the visible MVP and the M5 proxy.
4. **US3** → cross-era byte order, unit-proven on Linux.
5. Push. The M5 now inherits a proven SDL2 game (US5 = mostly packaging).

### Then, as hardware allows

6. **US4 spike (T033) first** — cheap go/no-go for the whole OS X app; run opportunistically whenever the fleet is powered on.
7. US4 fat/ppc-only builds + hardware validation.
8. US5 on the M5.
9. The payoff: cross-era Classic-Mac ↔ modern-Mac game (quickstart §F).

### Notes

- `[P]` = different files, no incomplete-task dependency.
- Commit after each task or logical group; branch is `011-macosx-sdl2`.
- Every checkpoint is an independently testable stopping point.
- Classic builds must be green at every checkpoint (Constitution II) — never regress the validated path.
