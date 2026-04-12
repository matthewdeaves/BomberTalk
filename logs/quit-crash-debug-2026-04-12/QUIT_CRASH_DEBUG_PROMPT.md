# PeerTalk Test App Debug Prompt

Use this prompt with Claude working on the peertalk repo (`~/peertalk`).

---

## Prompt

I'm debugging a quit crash that happens on the Performa 6200 (PPC MacTCP) after a multiplayer game in BomberTalk. The PeerTalk test apps (test_fast, test_reliable, test_lifecycle, etc.) all quit cleanly on the 6200. csend also quits cleanly. Only BomberTalk crashes.

### Your task

Starting from the PeerTalk test apps that work, incrementally add BomberTalk-like features to find what triggers the crash. Build and deploy to the 6200 + 6400 after each change using the classic-mac-hardware MCP server.

### Test progression

**Phase 1: Baseline — confirm test apps work**
1. Build and run `test_reliable` (TCP messaging like BomberTalk) on 6200 + 6400. Confirm clean quit.
2. Build and run `test_lifecycle` (connect → disconnect → reconnect cycle) on 6200 + 6400. Confirm clean quit.

**Phase 2: Add BomberTalk features one at a time**

Starting from `test_reliable` (since it uses TCP like BomberTalk), create a modified version that adds one feature per iteration. After each addition, build for PPC MacTCP + PPC OT, deploy to 6200 + 6400, test, and verify clean quit.

Add features in this order (most likely crash triggers first):

1. **clog UDP broadcast sink** — Add `clog_set_network_sink()` with a UDP broadcast function (like BomberTalk's `udp_log_sink`). This sends `PT_SendUDPBroadcast` on every log message. BomberTalk logs at DBG level during gameplay = hundreds of UDP sends/sec through MacTCP's 68k emulator.

2. **High-frequency UDP sends** — Change the test to send PT_FAST (UDP) position messages every poll iteration (like BomberTalk's `Net_SendPosition` every frame). This is in addition to the TCP reliable messages.

3. **GWorld offscreen buffers** — Allocate large offscreen GWorld buffers (like BomberTalk's renderer: ~400KB for two 480x416 buffers). This reduces available heap for MacTCP.

4. **ExitToShell instead of return 0** — Change the exit to use `ExitToShell()` instead of `return 0`. BomberTalk used ExitToShell in older builds.

5. **PT_StopDiscovery before PT_Shutdown** — Add `PT_StopDiscovery()` call before `PT_Shutdown()` in cleanup. BomberTalk does this.

6. **Game→lobby disconnect cycle** — After the test data exchange, call `PT_DisconnectAll()` (or `PT_Disconnect` per peer), then restart discovery, reconnect, exchange more data, then quit. This mirrors BomberTalk's game→lobby→quit flow.

### Build commands

```bash
export RETRO68_TOOLCHAIN=~/Retro68-build/toolchain

# PPC MacTCP (for 6200)
cd ~/peertalk/build-ppc-mactcp
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake -DPT_PLATFORM=MACTCP
make -j$(nproc)

# PPC OT (for 6400)  
cd ~/peertalk/build-ppc-ot
cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake -DPT_PLATFORM=OT
make -j$(nproc)
```

### Deploy and run

Use the classic-mac-hardware MCP server:
```
execute_binary(machine="performa6200", platform="ppc", binary_path="path/to/test.bin")
execute_binary(machine="performa6400", platform="ppc", binary_path="path/to/test.bin")
```

### What to report

For each test iteration, report:
- What feature was added
- Whether the 6200 quit cleanly or crashed
- If crashed, what the last socat UDP output showed (run `socat UDP-RECV:7356 -` on the host)

### Key files

- Test apps: `~/peertalk/tests/test_reliable.c`, `test_lifecycle.c`, `test_fast.c`
- Test common: `~/peertalk/tests/test_common.h` (shared init/shutdown)
- BomberTalk net.c: `~/BomberTalk/src/net.c` (UDP log sink at line ~266, Net_Init at ~288)
- BomberTalk main.c: `~/BomberTalk/src/main.c` (shutdown sequence at ~335)
- MacTCP platform: `~/peertalk/src/platform/mactcp/pt_mactcp.c`

### Context

The clog UDP broadcast sink is the prime suspect. BomberTalk is the only PeerTalk app that uses `clog_set_network_sink()` to broadcast log messages via `PT_SendUDPBroadcast`. At DBG level, this generates hundreds of `UDPWrite` calls per second through MacTCP's 68k driver under the PPC emulator. The PeerTalk test apps and csend never use a network sink and quit cleanly.

MacTCP 2.1 (Glenn Anderson patch) is a 68k DRVR. On the 6200 (PPC), it runs under the 68k emulator with Mixed Mode Manager handling PPC↔68k boundary crossings. Per "Tricks of the Mac Game Programming Gurus" (1995): "The traditional interfaces are in 68K machine code and always will be. On Power Macs, you'll always be suffering the performance penalty of running network code under the 68K emulator."
