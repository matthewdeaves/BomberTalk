# Quickstart: Mac OS X + Modern macOS Builds

How to build, run, and validate each new target once implemented. Classic Mac builds are unchanged (see root `CLAUDE.md`).

## Prerequisites

- `~/peertalk` at **v1.12.1**, `~/clog` present.
- SSH config for the OS X fleet (`mini-intel`, `imac-g5`, `quicksilver`, `yosemite`) — already configured.
- SDL2 dev libs on the Linux box (`apt install libsdl2-dev` or equivalent).
- classic-mac-hardware MCP for any Classic Mac file/run ops.

## A. SDL2 build on the Linux dev box (primary dev loop, M5 proxy)

```bash
mkdir -p build-sdl2 && cd build-sdl2
cmake .. -DBT_TARGET=sdl2 -DPEERTALK_DIR=~/peertalk -DCLOG_DIR=~/clog
make
./bombertalk --name Alice          # window opens
# second peer (same box or another): ./bombertalk --name Bob
```

**Validate (US2/SC-002)**: play a full round — move (held keys), place bombs, watch explosions, block destruction, death, game-over — all in the SDL2 window, two instances talking over PeerTalk.

## B. Byte-order unit tests (US3/SC-003)

```bash
cd build-sdl2 && ctest           # or: make test
```

Expect: `MsgPosition` encode→decode round-trip passes, and the fixed big-endian byte vector decodes to the expected `pixelX`/`pixelY`.

## C. Carbon viability spike (US4 GATE — run FIRST for OS X)

```bash
# push spike + build fat on the 10.7 host
rsync -az --protocol=29 --exclude '.git' --exclude '/build*' ./ mini-intel:bt-fat/bombertalk/
rsync -az --protocol=29 ~/peertalk/ mini-intel:bt-fat/peertalk/
rsync -az --protocol=29 ~/clog/     mini-intel:bt-fat/clog/
ssh mini-intel 'cd bt-fat/bombertalk && bash tools/spike-carbon-window.sh'
# run ON SCREEN (not headless) on the mini and a PPC:
bash tools/osx-screen-run.sh mini-intel spike-carbon-window
bash tools/osx-screen-run.sh quicksilver spike-carbon-window
```

**Gate**: window opens + colored rect draws on both → proceed to D. Otherwise re-scope (research R2).

## D. OS X fat + ppc-only builds (after spike passes)

```bash
# fat ppc+i386, min 10.4 (G4/G5/Intel mini)
ssh mini-intel 'cd bt-fat/bombertalk && CLOG_DIR=$HOME/bt-fat/clog PEERTALK_DIR=$HOME/bt-fat/peertalk \
  bash tools/build-macosx-fat.sh'
ssh mini-intel 'lipo -info bt-fat/bombertalk/build-macosx-fat/BomberTalk.app/Contents/MacOS/BomberTalk'
#   -> ... ppc i386

# ppc-only, min 10.3.9 (G3)
ssh mini-intel 'cd bt-fat/bombertalk && SDK=/Developer/SDKs/MacOSX10.3.9.sdk MIN=10.3.9 ARCHS=ppc \
  OUT=build-macosx-ppc103 CLOG_DIR=$HOME/bt-fat/clog PEERTALK_DIR=$HOME/bt-fat/peertalk \
  bash tools/build-macosx-fat.sh'
```

**Validate (US4/SC-006)**: launch on-screen on the G4/G5/mini (fat) and G3 (ppc-only); play a round.

## E. Native macOS 26 on the M5 (after A is pushed)

```bash
brew install sdl2
git pull && mkdir -p build-macos && cd build-macos
cmake .. -DBT_TARGET=sdl2 && make
./bombertalk --name Mac
```

**Validate (US5/SC-007)**: builds native arm64 with no shared-core edits; plays a round; then package `.app` (M5-side task).

## F. Cross-era play — the payoff (US3/SC-008)

1. Start a Classic Mac (e.g. Mac SE over MacTCP) via the classic-mac-hardware MCP.
2. Start a little-endian client (Linux SDL2, or the M5, or the Intel-mini OS X app).
3. They discover each other, connect, and play the same game — big-endian ↔ little-endian, proven.

## Definition of done for this feature

- SC-001..SC-009 in `spec.md` all met.
- Three classic builds green + byte-identical on the wire.
- Spike decision recorded; OS X app runs on the fleet (or re-scoped per the spike).
- SDL2 game playable on Linux and buildable native on the M5.
- Cross-era Classic↔modern game demonstrated.
