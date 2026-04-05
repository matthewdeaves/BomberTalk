# Quickstart: BomberTalk v1.0-alpha

**Feature**: `001-v1-alpha`
**Date**: 2026-04-05

## Prerequisites

- Retro68 toolchain installed at `~/Retro68-build/toolchain/`
- PeerTalk SDK built for all targets (see ~/peertalk/CLAUDE.md)
- clog library built for all targets (see ~/clog)
- CMake 3.15+

## Environment Variables

```bash
export RETRO68_TOOLCHAIN=~/Retro68-build/toolchain
export PEERTALK_DIR=~/peertalk
export CLOG_DIR=~/clog
```

## Build Commands

### 68k MacTCP (Mac SE — System 6)

```bash
cd ~/BomberTalk
mkdir -p build-68k && cd build-68k
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPEERTALK_DIR=$PEERTALK_DIR \
  -DCLOG_DIR=$CLOG_DIR
make
```

Output: `build-68k/BomberTalk.bin` (use Basilisk II or real hardware)

### PPC Open Transport (Performa 6400 — System 7.6.1)

```bash
cd ~/BomberTalk
mkdir -p build-ppc-ot && cd build-ppc-ot
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT \
  -DPEERTALK_DIR=$PEERTALK_DIR \
  -DCLOG_DIR=$CLOG_DIR
make
```

Output: `build-ppc-ot/BomberTalk.bin` (transfer to Performa 6400)

### PPC MacTCP (Performa 6200 — System 7.5.3)

```bash
cd ~/BomberTalk
mkdir -p build-ppc-mactcp && cd build-ppc-mactcp
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=MACTCP \
  -DPEERTALK_DIR=$PEERTALK_DIR \
  -DCLOG_DIR=$CLOG_DIR
make
```

Output: `build-ppc-mactcp/BomberTalk.bin` (transfer to Performa 6200)

## Transfer to Real Hardware

Use one of:
- **netatalk** (AFP file sharing from Linux)
- **mini vMac** or **Basilisk II** for 68k testing
- **SheepShaver** for PPC testing
- Physical floppy / Ethernet transfer

## Running

1. Launch BomberTalk on each Mac
2. Loading screen appears, then main menu
3. All players select "Play"
4. Players appear in the lobby as PeerTalk discovers them
5. Any player presses Start when 2+ players are connected
6. Play Bomberman! (No host — all peers are equal)

## Controls

| Key | Action |
|-----|--------|
| Arrow keys | Move player |
| Spacebar | Place bomb |
| Cmd-Q | Quit game |

## Troubleshooting

- **"No players found"**: Ensure all Macs are on the same LAN/subnet. Check MacTCP
  or Open Transport network configuration.
- **Bus error on launch**: Verify the correct binary for each platform (68k vs PPC).
- **Slow frame rate on Mac SE**: Expected — 10+ fps target. Reduce to 16x16 tiles
  if needed (future enhancement).
- **Connection timeout**: PeerTalk uses ports 7353 (UDP discovery), 7354 (TCP),
  7355 (UDP fast). Ensure no firewall/router blocks these.
