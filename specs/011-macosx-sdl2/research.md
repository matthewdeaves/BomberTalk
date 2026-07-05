# Phase 0 Research: Mac OS X + Modern macOS Builds

All NEEDS CLARIFICATION items from the plan are resolved below. Each entry: Decision / Rationale / Alternatives considered.

## R1 — GUI backend strategy across the era span

**Decision**: Two new GUI backends behind one renderer seam. **Carbon + QuickDraw** for OS X 10.3–10.7 (reuses `renderer.c`), **SDL2** for macOS 26+ arm64 (new `renderer_sdl.c`). Networking stays PeerTalk POSIX/BSD on both.

**Rationale**: QuickDraw exists on OS X (32-bit Carbon) but was removed in macOS 10.15, so it cannot serve Apple Silicon. Modern GUI frameworks do not exist on 10.3. No single technology spans the range, so ≥2 backends are unavoidable. Carbon maximizes reuse of the 1,581-line QuickDraw renderer for the legacy fleet; SDL2 is C-callable, gives true held-key input, and runs on Linux for development.

**Alternatives considered**:
- *SDL 1.2 for legacy + SDL2 for modern*: avoids Carbon, but SDL 1.2 must be built fat on the gcc-4.0/no-CMake 10.7 host (a yak-shave), is not "native," and reuses none of `renderer.c`. Rejected — user explicitly wants a native OS X app.
- *Cocoa/Metal for modern*: most native, but requires an Objective-C/Swift renderer rewrite and drags a non-C toolchain into the tree. Deferred as a possible future polish; SDL2 delivers the goal with far less work and keeps the C89 core clean.
- *Single backend for everything*: impossible (see rationale).

## R2 — QuickDraw + Carbon windowing viability on OS X (the risk)

**Decision**: Gate the entire OS X app (US4) on a **hardware viability spike** before any large build investment. The spike is a minimal Carbon Mach-O that opens a window, `CopyBits` a colored rect, reads a key, and quits — built fat on `mini-intel` and run on the Intel mini (10.7 i386) and one PPC Mac, launched in the desktop session (not headless SSH).

**Rationale**: PeerTalk validated only BSD sockets + *console* apps on this fleet; it explicitly documents "no native app-window path" because it never needed graphics. QuickDraw was deprecated in 10.4 and kept for 32-bit legacy apps; "still runnable on 10.7 i386" is a hardware fact, not a documented guarantee, so we determine it empirically. PPC 10.3–10.5 QuickDraw is high-confidence; the 10.7 Intel cell is the real unknown. Cost of the spike (~1 hour) vs. cost of discovering it late (a week) makes gating mandatory.

**Alternatives considered**:
- *Assume it works and build the full app*: rejected — asserting undocumented runtime availability from memory violates the "verify, don't assume" discipline and risks large wasted work.
- *Skip Carbon, use SDL for the Intel mini too*: kept as the **fallback** if the spike shows QuickDraw is unusable on Lion i386 (then: native Carbon PPC-only for 10.3–10.5 + an alternative for the mini). Not the primary plan.

**Secondary finding**: A Carbon *windowed* app launched via bare `ssh host ./app` likely cannot reach the logged-in Aqua session's WindowServer. On-screen runs use PeerTalk's `osx-screen-run.sh` osascript mechanism (launch in the desktop session), not headless SSH.

## R3 — Cross-era endianness for BomberTalk message payloads

**Decision**: Add a thin byte-order layer (`net_wire.h`) that serializes all multi-byte message fields in **big-endian** (Classic-Mac/network order). On big-endian builds (68k/PPC) the conversions compile to no-ops; on little-endian builds (Intel OS X, arm64, Linux) they byte-swap on encode/decode. `net.c` message pack/unpack routes through it.

**Rationale**: `CLAUDE.md` already flags this: *"Network message structs need no byte swapping on Classic Mac. Will need conversion if POSIX build is added later."* The wire spans big-endian (68k/PPC classic + PPC OS X 10.3–10.5) and little-endian (Intel 10.7 + arm64 M5). Without conversion, `MsgPosition` shorts and protocol-version fields corrupt across eras, causing false explosion kills and out-of-bounds grid crashes. Choosing big-endian as canonical keeps classic builds byte-identical (zero cost, zero risk to the validated classic path) and matches "network byte order."

**Alternatives considered**:
- *Little-endian canonical*: would force byte-swaps onto every classic build (perf cost on the Mac SE, and touches the validated path). Rejected.
- *Rely on PeerTalk to swap*: PeerTalk frames its own protocol but does not know BomberTalk's application payload layout. It cannot swap fields it doesn't understand. Rejected.
- *Send text/portable encoding*: over-engineered for fixed small structs; adds parsing cost on the Mac SE. Rejected.

**Scope**: Every BomberTalk message with multi-byte fields: `MsgPosition` (pixelX/Y shorts), MSG_GAME_START (protocol version), MSG_BOMB_EXPLODE, MSG_BLOCK_DESTROYED, MSG_PLAYER_KILLED, MSG_GAME_OVER (winnerID + any coords). Single-byte fields (playerID, facing) need no conversion. No protocol version bump if the byte *layout* is unchanged (only host-side interpretation changes) — confirm during data-model.

## R4 — SDL2 renderer mapping of the existing renderer surface

**Decision**: Implement the public `renderer.h` calls the screens already use (screen begin/end, gameplay begin/end frame, draw player/bomb/explosion, tile/background, text, mark-dirty) in SDL2 terms. Background as an SDL texture rebuilt on demand; sprites as textures blitted with `SDL_RenderCopy`; dirty-rect calls become no-ops or simple full-frame renders (modern GPUs make partial-redraw optimization unnecessary). Text via a simple bitmap font (avoid SDL_ttf dependency) or SDL2's built-in surface text helpers.

**Rationale**: The screens call an abstract surface ("draw a player at grid X"), so SDL2 can satisfy it without the QuickDraw-specific internals (GWorld locking, mask regions, `CopyBits` alignment). Dirty rectangles were a Mac SE optimization; they are pointless on a GPU compositor, so those calls collapse to no-ops. Keeping the same public surface means `screens.c`/`screen_*.c` are unchanged across backends.

**Alternatives considered**:
- *Port the full QuickDraw pipeline (dirty rects, mask regions) to SDL*: needless complexity; the optimizations targeted 8 MHz hardware.
- *SDL_Renderer vs SDL_Surface software blits*: use `SDL_Renderer` (hardware-accelerated, high-DPI friendly) as primary. Software surface path is a fallback if needed.

## R5 — Asset conversion (PICT → SDL)

**Decision**: Derive SDL2 assets (PNG or embedded raw RGBA) from the existing sprite sources at build/prep time, reusing the `scripts/embed-gfx.py` pipeline as the reference. Ship converted assets in `resources/gfx/`. SDL2 renderer falls back to colored rectangles if assets are missing (Constitution VI).

**Rationale**: Classic uses PICT resources unusable on modern platforms. The project already has a Python asset pipeline and source PICTs, so conversion is a scripted derivation, not new art. Avoiding SDL_image (loading raw/embedded RGBA) keeps dependencies minimal, but SDL_image PNG loading is acceptable if simpler on macOS.

**Alternatives considered**:
- *Reuse PICT at runtime on SDL*: no portable PICT decoder worth carrying. Rejected.
- *Hand-author new art*: unnecessary; the existing sprites define the look.

## R6 — Carbon fat build pipeline (adapting PeerTalk's script)

**Decision**: Copy `~/peertalk/tools/build-macosx-fat.sh` into BomberTalk's `tools/`, adapt sources/targets, and **extend it to emit a windowed `.app` bundle** (Info.plist, bundle layout, Carbon framework linkage) rather than a bare console binary. Produce the fat ppc+i386 (min 10.4) app and the ppc-only (min 10.3.9) app. Drive it from Linux via rsync+SSH to `mini-intel` (no CMake/git there). Link PeerTalk's POSIX backend + clog.

**Rationale**: PeerTalk's script is the hardware-validated pattern for building fat binaries on the 10.7 host against the 10.4u / 10.3.9 SDKs. Reusing it (Principle I spirit) minimizes risk. The only delta from PeerTalk's console apps is the windowed-app packaging + Carbon/QuickDraw framework linkage — exactly the part the US4 spike de-risks.

**Alternatives considered**:
- *CMake target for OS X*: the 10.7 host has no CMake/git; PeerTalk deliberately uses a standalone shell script. Match that.
- *Build on the M5 with an old SDK*: modern Xcode cannot target 10.3–10.7 or emit PPC. Must use the vintage host.

## R7 — Warnings & PeerTalk version

**Decision**: Consume PeerTalk **v1.12.1** (already the `~/peertalk` checkout). Accept only the two documented third-party warnings (clog `implicit fsync`, Apple `crt1.o -mlong-branch`), each justified in-tree; everything else warning-clean. Optionally pin the CMake FetchContent fallback to a `v1.12.1` tag for reproducibility.

**Rationale**: Matches PeerTalk's documented known-warnings list and the project's "never ignore warnings" rule. The OS X build consumes `~/peertalk` directly via rsync, so it is already on v1.12.1; the CMake pin only affects classic FetchContent builds.

**Alternatives considered**: none — this is a compliance requirement.

## Open item deferred to hardware (not a blocker for Phase 1)

- Final LE↔BE interop proof (US3 acceptance #1–2 on real hardware) requires a Classic/PPC Mac powered on. Byte-order logic is fully unit-testable on Linux first, so design proceeds now.
