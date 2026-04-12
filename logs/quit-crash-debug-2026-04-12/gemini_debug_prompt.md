# BomberTalk Quit Crash Debug Prompt

Use this prompt with Gemini (ultrathink mode) with access to the BomberTalk repo, peertalk repo, and the books/ directory.

---

## Prompt

I have a Classic Mac networked Bomberman game (BomberTalk) that crashes on quit on both MacTCP machines (Performa 6200/PPC and Mac SE/68k) but quits cleanly on the Open Transport machine (Performa 6400/PPC). The crash happens when quitting from the lobby after playing a full game. Gameplay itself works fine on all 3 machines.

**Known-good commit**: `858a48c` in BomberTalk — games quit fine after play on all machines at this commit.

**Current HEAD**: `e469d3e` — crashes on quit on both MacTCP machines.

### Key changes between known-good and current

**BomberTalk changes (858a48c → e469d3e):**

1. **Game-to-lobby transition now calls `Net_StopDiscovery()` + `Net_DisconnectAllPeers()`** before `Screens_TransitionTo(SCREEN_LOBBY)`. Previously it just transitioned directly. See `src/screen_game.c` lines around the `Screens_TransitionTo(SCREEN_LOBBY)` calls. `Net_DisconnectAllPeers()` calls `PT_DisconnectAll()` which sends TCP goodbye frames synchronously before disconnecting.

2. **Shutdown order changed from Renderer→Window→Net to Net→Renderer→Window** in `src/main.c`. The old order at 858a48c was:
   ```
   Renderer_Shutdown()
   DisposeWindow()
   Net_Shutdown()    // MacTCP teardown happened LAST
   ```
   Current order:
   ```
   Net_Shutdown()    // MacTCP teardown happens FIRST
   Renderer_Shutdown()
   DisposeWindow()
   ```

3. **`on_disconnected` callback now always clears peer pointers** (not just in-game). Previously it returned early if not `SCREEN_GAME`.

4. **`Lobby_Init()` now clears stale peer pointers** and resets game state on re-entry.

5. **`Renderer_Shutdown()` now redirects QuickDraw** to the window before disposing GWorlds (SetPort/SetGWorld).

**PeerTalk changes (7e89304 → 0f94dbf):**

11+ commits specifically about MacTCP shutdown, including:
- `PT_DisconnectAll` added (sends goodbye frames, disconnects all peers)
- Three-phase TCP shutdown (abort → wait → release)
- MacTCP PPC: UDPRead uses finite timeout (2 sec) because UDPRelease bus-errors with pending reads under Mixed Mode emulation
- 68k: UDPRelease always called (skipping corrupts driver state across app launches)
- Synchronous goodbye send removed from `PT_Shutdown` (was freezing)

### The crash behavior

- All 3 machines play a full game, transition back to lobby successfully
- Cmd-Q on 6200 (PPC MacTCP): crash
- Cmd-Q on Mac SE (68k MacTCP): crash
- Cmd-Q on 6400 (PPC OT): clean quit
- UDP broadcast logging from clog has been completely disabled (clog is file-only now) — crash persists
- File logging with CLOG_FLUSH_ALL doesn't survive the crash (log file only contains init + first lobby entry, game data lost)
- Adding an early `break` from the main loop after Cmd-Q (to skip the final Net_Poll/Screens_Draw iteration) did NOT fix the crash

### What I need you to investigate

1. **Read the reference books in `books/`** — especially "Sex, Lies and Video Games" (1996), "Tricks of the Mac Game Programming Gurus" (1995), and "Black Art of Macintosh Game Programming" (1996). Look for guidance on:
   - Proper MacTCP shutdown sequences
   - Disposing network resources while the application is still handling events
   - Interaction between File Manager (FSWrite/PBFlushFileSync) and MacTCP driver during shutdown
   - Whether calling MacTCP operations (TCPAbort, UDPRelease) with active async completions can corrupt the System heap

2. **Compare the shutdown paths** at `858a48c` (working) vs HEAD (broken) in both BomberTalk and PeerTalk. The working version shut down networking LAST (after renderer and window disposal). The broken version shuts down networking FIRST. Could this ordering matter?

3. **Investigate `PT_DisconnectAll` during game-to-lobby transition.** This sends synchronous TCP goodbye frames via `TCPSend` to peers that may be simultaneously disconnecting (all 3 machines transition at the same time). Could this leave MacTCP driver state corrupted in a way that makes the later `PT_Shutdown` crash?

4. **Check PeerTalk's three-phase MacTCP shutdown** in `src/platform/mactcp/pt_mactcp.c` function `mactcp_shutdown()`. The 0.5-second spin-wait for TCP async operations and the 2.5-second wait for UDP read timeout — are these sufficient? Could there be a race condition?

5. **The File Manager interaction**: clog calls `SetFPos(fsFromLEOF)` + `FSWrite` + `PBFlushFileSync` on every log message with CLOG_FLUSH_ALL. During MacTCP shutdown, PeerTalk logs via CLOG (which triggers File Manager calls). Could File Manager operations during MacTCP teardown cause issues? The MacTCP driver and File Manager both operate through the Device Manager — could there be a reentrancy issue?

### Files to read

- `BomberTalk/src/main.c` — shutdown sequence
- `BomberTalk/src/net.c` — Net_Shutdown, Net_DisconnectAllPeers
- `BomberTalk/src/screen_game.c` — game-to-lobby transition
- `peertalk/src/core/pt_core.c` — PT_Shutdown, PT_DisconnectAll, send_goodbye
- `peertalk/src/platform/mactcp/pt_mactcp.c` — mactcp_shutdown, mactcp_release_udp_stream, mactcp_poll, issue_udp_read
- `peertalk/src/platform/opentransport/pt_ot.c` — for comparison (OT works fine)
- `clog/src/clog_mac.c` — file logging implementation
- `books/` — reference material

### My hypothesis

The `PT_DisconnectAll()` call during game-to-lobby (which sends synchronous TCP goodbyes while peers are simultaneously disconnecting) is corrupting MacTCP driver state in the System heap. This corruption is dormant during lobby operation but manifests as a crash when `PT_Shutdown` tries to release the already-corrupted TCP/UDP streams. The OT machine doesn't crash because OT has a completely different, more robust teardown path.

The simplest test would be: remove the `Net_StopDiscovery()` + `Net_DisconnectAllPeers()` calls from the game-to-lobby transition in `screen_game.c` (reverting to the 858a48c behavior) and see if quit works again.
