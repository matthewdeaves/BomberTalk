# Deploy and Test BomberTalk (004-smooth-movement)

## Context

BomberTalk is a networked Bomberman clone for Classic Macs. The 004-smooth-movement branch adds pixel-by-pixel movement. We've been debugging a MacTCP shutdown crash that freezes the 6200 and Mac SE when quitting after gameplay. The fix (in PeerTalk) skips UDPRelease when a UDPRead is still pending — MacTCP PPC has a bug where UDPRelease crashes with pending async reads.

All three binaries are freshly built via `./build-all.sh all` with the full dependency chain (clog -> PeerTalk -> BomberTalk).

## Deploy

Deploy to all 3 Macs using the classic-mac-hardware MCP server's `execute_binary` tool:

1. **Mac SE (68k MacTCP):**
   - machine: `macse`, platform: `mactcp`
   - binary: `/home/matt/BomberTalk/build-68k/BomberTalk.bin`

2. **Performa 6200 (PPC MacTCP):**
   - machine: `performa6200`, platform: `mactcp`
   - binary: `/home/matt/BomberTalk/build-ppc-mactcp/BomberTalk.bin`

3. **Performa 6400 (PPC Open Transport):**
   - machine: `performa6400`, platform: `opentransport`
   - binary: `/home/matt/BomberTalk/build-ppc-ot/BomberTalk.bin`

## Test Plan

Have the user run `socat UDP-RECV:7356 -` on the Linux machine to capture UDP debug logs.

1. **Play a game** on 2 or 3 machines to completion (one player left alive)
2. **Game over** should transition all machines back to lobby
3. **Quit from lobby** (Cmd-Q or File > Quit) on each machine
4. **Expected:** All machines quit cleanly — no freeze, no crash
5. **Check socat output** for these diagnostic messages from the 6200:
   - `MacTCP shutdown: TCP phase 1 (abort)` — TCP cleanup
   - `MacTCP shutdown: TCP phase 3 (release)` — TCP release
   - `MacTCP shutdown: UDP discovery cleanup` — UDP cleanup
   - `Discovery UDP: safe=0` — means pending read was skipped (expected)
   - `MacTCP shutdown: UDP message cleanup` — message UDP
   - `MacTCP shutdown: disposing UPPs` — final cleanup
   - `MacTCP shutdown complete` — full success
   - `PeerTalk shutdown complete` — PeerTalk done

6. The 6400 (OT) should quit cleanly as before — it doesn't have the MacTCP bug.
7. The Mac SE may or may not show UDP logs (UDP sink is disabled on SE due to MacTCP send overhead).

## If It Still Crashes

Download logs from the machine that crashed:
```
download_file(machine="performa6200", remote_path="BomberTalk Log")
```

The socat output will show exactly which shutdown phase was reached before the crash. Report the last few lines.

## Rebuilding

If code changes are needed, use the build script:
```bash
./build-all.sh all        # all 3 targets
./build-all.sh ppc-mactcp # just 6200
./build-all.sh 68k        # just Mac SE
./build-all.sh ppc-ot     # just 6400
```

Dependencies (clog, PeerTalk) are built from source automatically via CMake FetchContent. To use local checkouts, set `CLOG_DIR` and/or `PEERTALK_DIR` environment variables.

## Git State

- **BomberTalk:** branch `004-smooth-movement`, all changes committed
- **PeerTalk:** branch `main`, all changes committed (UDPRelease workaround + diagnostic logging)
- **clog:** branch `main`, clean
