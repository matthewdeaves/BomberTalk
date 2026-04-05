# Build Verification Checklist: BomberTalk v1.0-alpha

**Purpose**: Verify BomberTalk builds and runs correctly on all three target Macs
**Created**: 2026-04-05
**Feature**: [spec.md](../spec.md)

## Pre-Build Checks

- [ ] CHK001 PeerTalk SDK is built for 68k MacTCP (`~/peertalk/build-68k/libpeertalk.a` exists)
- [ ] CHK002 PeerTalk SDK is built for PPC OT (`~/peertalk/build-ppc-ot/libpeertalk.a` exists)
- [ ] CHK003 PeerTalk SDK is built for PPC MacTCP (`~/peertalk/build-ppc-mactcp/libpeertalk.a` exists)
- [ ] CHK004 clog is built for 68k (`~/clog/build-m68k/libclog.a` exists)
- [ ] CHK005 clog is built for PPC (`~/clog/build-ppc/libclog.a` exists)
- [ ] CHK006 Retro68 toolchain is installed at `~/Retro68-build/toolchain/`
- [ ] CHK007 CMake 3.15+ is available

## Compilation

- [ ] CHK008 68k MacTCP build compiles with no errors
- [ ] CHK009 68k MacTCP build has no warnings (beyond known Retro68 warnings)
- [ ] CHK010 PPC OT build compiles with no errors
- [ ] CHK011 PPC OT build has no warnings
- [ ] CHK012 PPC MacTCP build compiles with no errors
- [ ] CHK013 PPC MacTCP build has no warnings
- [ ] CHK014 All builds produce .bin output files

## Mac SE (68k, System 6, MacTCP)

- [ ] CHK015 BomberTalk.bin transfers to Mac SE successfully
- [ ] CHK016 Application launches without bus error or crash
- [ ] CHK017 Loading screen displays "BomberTalk" text
- [ ] CHK018 Menu screen displays with keyboard navigation working
- [ ] CHK019 Quit exits cleanly to Finder
- [ ] CHK020 PeerTalk initializes (check clog output if available)
- [ ] CHK021 Lobby discovers peers on LAN
- [ ] CHK022 Game screen renders tile map
- [ ] CHK023 Player movement responds to arrow keys
- [ ] CHK024 Frame rate is 10+ fps during gameplay
- [ ] CHK025 FreeMem() after init shows adequate memory remaining

## Performa 6400 (PPC, System 7.6.1, Open Transport)

- [ ] CHK026 BomberTalk.bin transfers to Performa 6400 successfully
- [ ] CHK027 Application launches without crash
- [ ] CHK028 Loading screen and menu work correctly
- [ ] CHK029 Lobby discovers peers (including Mac SE via MacTCP)
- [ ] CHK030 Game screen renders correctly at color depth
- [ ] CHK031 Player movement is smooth and responsive
- [ ] CHK032 Network messages send/receive without errors

## Performa 6200 (PPC, System 7.5.3, MacTCP)

- [ ] CHK033 BomberTalk.bin transfers to Performa 6200 successfully
- [ ] CHK034 Application launches without crash
- [ ] CHK035 Loading screen and menu work correctly
- [ ] CHK036 Lobby discovers peers (cross-stack: MacTCP <-> OT)
- [ ] CHK037 Game screen renders correctly
- [ ] CHK038 Network messages send/receive without errors

## Cross-Platform Multiplayer

- [ ] CHK039 Mac SE discovers Performa 6400 (MacTCP <-> OT)
- [ ] CHK040 Mac SE discovers Performa 6200 (MacTCP <-> MacTCP)
- [ ] CHK041 Performa 6200 discovers Performa 6400 (MacTCP <-> OT)
- [ ] CHK042 Three-way game: all three Macs connected and playing
- [ ] CHK043 Position sync works between all machine pairs
- [ ] CHK044 Bomb events sync reliably between all machines
- [ ] CHK045 Player disconnect is handled gracefully on all machines
- [ ] CHK046 Game completes a full round without crashes

## Notes

- Check items off as completed: `[x]`
- Document any issues or workarounds found during testing
- The Mac SE is the most constrained target — test it first
- Network testing requires all three Macs on the same LAN segment
