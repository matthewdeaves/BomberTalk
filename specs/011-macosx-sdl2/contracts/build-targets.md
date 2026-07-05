# Contract: Build Targets

## 1. Classic Mac (existing — MUST stay green)

Unchanged CMake toolchain builds (68k MacTCP, PPC OT, PPC MacTCP). This feature MUST NOT alter their output behavior; on-wire bytes byte-identical (SC-004). Guard: any seam refactor is verified by rebuilding all three clean.

## 2. SDL2 (native CMake target)

- **Discovery**: `find_package(SDL2)`. If absent, the SDL2 target is **skipped with a clear message**; classic and Carbon targets are unaffected (FR of graceful skip).
- **Dev build (Linux)**: `cmake -DBT_TARGET=sdl2 .. && make` → runnable `bombertalk` linking SDL2 + PeerTalk POSIX backend + clog. Used for development and the US2 acceptance (playable round on Linux).
- **Native build (M5, macOS 26 arm64)**: same CMake target; `brew install sdl2` (or vendored). No shared-core source changes (SC-007). Packaging (`.app`, sign/notarize, Retina) are M5-side tasks.
- **Compiles the POSIX seam**: `renderer_sdl.c`, `input_sdl.c`, `main_posix.c`, `platform_posix.c`, `net_wire.h` (little-endian path), plus the shared core.

## 3. Carbon / OS X fat (shell script via `mini-intel`)

Adapted from `~/peertalk/tools/build-macosx-fat.sh`, extended to emit a windowed `.app` bundle.

- **Host**: `mini-intel` (OS X 10.7, gcc-4.0, `/Developer/SDKs/MacOSX10.4u.sdk` + `MacOSX10.3.9.sdk`, no CMake/git). Driven from Linux via rsync+SSH.
- **Fat build** (covers G4 10.4, G5 10.5, Intel mini 10.7):
  `SDK=/Developer/SDKs/MacOSX10.4u.sdk MIN=10.4 ARCHS='ppc i386' bash tools/build-macosx-fat.sh`
  → `lipo -info` shows `ppc i386`.
- **PPC-only build** (covers G3 10.3.9):
  `SDK=/Developer/SDKs/MacOSX10.3.9.sdk MIN=10.3.9 ARCHS=ppc OUT=build-macosx-ppc103 bash tools/build-macosx-fat.sh`
- **Links**: PeerTalk POSIX backend + clog + Carbon.framework (QuickDraw). Compiles the QuickDraw renderer (`renderer.c`, color path), `input.c` (GetKeys under Carbon), `main.c` (WaitNextEvent under Carbon), `net_wire.h` (big-endian no-op on PPC, swap on i386).
- **GATE**: only pursued after the viability spike (contract below) passes.
- **On-screen run**: launch in the desktop session via the `osx-screen-run.sh` osascript pattern, NOT bare `ssh host ./app` (WindowServer session requirement).

## 4. Carbon viability spike (GATE for target 3)

Minimal Carbon Mach-O: open a window, `InitGraf`/`CopyBits` a colored rect, read one key via GetKeys, quit.

- Built fat on `mini-intel`; run on the **Intel mini (10.7 i386)** and **one PPC Mac** in the desktop session.
- **Pass**: a window opens and the rect draws on both.
- **Fail**: re-scope target 3 (e.g. PPC-only native Carbon for 10.3–10.5 + SDL/alt for the Intel mini). Decision recorded in research/tasks.

## Warnings policy (all targets)

Warning-clean except the two documented third-party warnings — clog `implicit declaration of 'fsync'` and Apple `crt1.o … -mlong-branch … no longer needed` — each justified in-tree. Any other warning is fixed or explicitly suppressed with rationale.
