# Quit Crash Fix Plan — Implementation Ready

## Before Starting

Restore all repos to their committed HEAD before implementing:
```bash
# BomberTalk — discard debug workaround (3 modified files)
cd ~/BomberTalk && git checkout -- src/main.c src/net.c src/screen_lobby.c

# PeerTalk — clean (just untracked QUIT_CRASH_DEBUG_PROMPT.md, ignore it)
# clog — clean
```

BomberTalk HEAD is `e469d3e`, PeerTalk HEAD is `0f94dbf`, clog HEAD is `98e5c82`.

## Root Cause
`PT_StartDiscovery` called a second time (after game→lobby) calls `udp_listen` and `tcp_listen` which issue new UDPRead/PassiveOpen operations on existing streams. Meanwhile, old aborted TCP streams (STREAM_FREE but not TCPReleased) still occupy the stream table with stale MacTCP driver references. On PPC MacTCP (68k DRVR under emulator), `mactcp_shutdown` crashes trying to release this mixed old+new state.

## Part 1: The PeerTalk Fix

### 1A. Add `cleanup_streams` platform op to PT_PlatformOps

**File: `peertalk/src/core/pt_internal.h`** (~line 127)

Add new function pointer after `poll`:
```c
void      (*cleanup_streams)(struct PT_Context_Internal *ctx);
```

This releases all TCP and UDP streams so fresh ones can be created by `udp_listen`/`tcp_listen`.

### 1B. Implement `mactcp_cleanup_streams` 

**File: `peertalk/src/platform/mactcp/pt_mactcp.c`**

New function — releases all TCP streams (STREAM_FREE ones via TCPRelease), releases both UDP streams (UDPRelease cancels pending reads per MacTCP Programmer's Guide line 1422), frees buffers, clears handles. Then recreates fresh TCP streams (TCPCreate) and UDP streams (UDPCreate) with new buffers.

**TCP cleanup** (for each stream slot where `ts->stream != NULL`):
```c
/* TCPRelease handles abort implicitly per MacTCP docs line 4012:
 * "If there is an open connection on the stream, the connection is
 *  first broken as though a TCPAbort command had been issued."
 * "The receive buffer area passed to MacTCP in the TCPCreate call
 *  is returned to the user." (line 4014) — reuse same buffer. */
pb.csCode = TCPRelease;
PBControlSync(&pb);
/* Buffer returned to us — reuse it for fresh TCPCreate (Principle V: zero alloc after init) */
ts->stream = create_tcp_stream(driverRef, ts->buffer, TCP_BUF_SIZE, tcp_upp, (Ptr)ts);
ts->state = STREAM_FREE;
ts->owner = NULL;
ts->flags = 0;
ts->send_pending = 0;
```

**UDP cleanup** (for discovery_udp and message_udp):
```c
/* UDPRelease cancels pending UDPRead per MacTCP docs line 1422:
 * "Any outstanding commands on that stream are terminated with error."
 * "The ownership of the receive buffer area passes back to you." (line 1424)
 * Reuse same buffer for fresh UDPCreate (Principle V: zero alloc after init). */
upb.csCode = UDPRelease;
PBControlSync(&upb);
us->read_pending = 0;
/* Buffer returned to us — reuse for fresh UDPCreate */
us->stream = create_udp_stream(driverRef, us->buffer, UDP_BUF_SIZE, udp_upp, (Ptr)us, port);
```

**Zero allocation:** Per MacTCP docs, TCPRelease and UDPRelease return buffer ownership to the caller. We reuse the same buffers for the fresh streams. This satisfies PeerTalk Constitution Principle V ("All buffers MUST be allocated at init. Zero malloc in the send/receive path") — no new memory allocated during rediscovery.

Add to the ops table at ~line 1095:
```c
static PT_PlatformOps mactcp_ops = {
    mactcp_init,
    mactcp_shutdown,
    ...
    mactcp_poll,
    mactcp_cleanup_streams   /* NEW */
};
```

### 1C. Call cleanup_streams from PT_StartDiscovery

**File: `peertalk/src/core/pt_core.c`** (~line 408)

Before creating new listeners, call the platform cleanup if streams already exist:
```c
PT_Status PT_StartDiscovery(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    PT_Status status;
    if (!ctx) return PT_ERR_INVALID_ARG;

    /* If rediscovering after a previous session, release old streams
     * and create fresh ones.  MacTCP requires explicit TCPRelease/
     * UDPRelease before reusing stream slots — aborted-but-not-released
     * streams corrupt the driver's internal state on PPC.
     * Ref: MacTCP Programmer's Guide (1989) line 1853:
     * "UDP Release must be called to release memory held by the driver.
     *  Failure to do so may produce unpredictable results." */
    if (ctx->discovery_listening && ctx->platform_ops->cleanup_streams) {
        ctx->platform_ops->cleanup_streams(ctx);
    }

    ctx->discovery_active = 1;
    ctx->discovery_listening = 1;
    ctx->discovery_timer = 0;

    status = ctx->platform_ops->udp_listen(ctx, PT_DISCOVERY_PORT);
    ...
```

The guard `ctx->discovery_listening` ensures cleanup only happens on second+ calls (first call has `discovery_listening == 0` from init).

### 1D. Remove PPC UDPRead timeout workaround

**File: `peertalk/src/platform/mactcp/pt_mactcp.c`** (~line 302)

Replace the `#ifdef __m68k__` / `#else` block in `issue_udp_read` with a single line:
```c
us->read_pb.csParam.receive.timeOut = 0;  /* infinite — UDPRelease cancels */
```

Remove all PPC-specific comments about Mixed Mode and bus errors. The dirty stream state was the cause, not Mixed Mode.

### 1E. Simplify mactcp_shutdown back toward v1.0.0

**File: `peertalk/src/platform/mactcp/pt_mactcp.c`** (~line 549)

With clean stream state guaranteed by `cleanup_streams`, the complex three-phase shutdown with PPC-specific waits is unnecessary. Simplify to the v1.0.0 pattern:

```c
static void mactcp_shutdown(PT_Context_Internal *ctx)
{
    int i;
    TCPiopb pb;
    UDPiopb upb;

    /* TCP: TCPRelease each stream (does implicit abort if connected) */
    for (i = 0; i < MAX_TCP_STREAMS; i++) {
        TCPStreamSlot *ts = &g_mactcp.tcp_streams[i];
        if (ts->stream) {
            memset(&pb, 0, sizeof(pb));
            pb.ioCRefNum = g_mactcp.driver_ref;
            pb.tcpStream = ts->stream;
            pb.csCode = TCPRelease;
            PBControlSync((ParmBlkPtr)&pb);
        }
        if (ts->buffer) {
            DisposePtr(ts->buffer);
        }
    }

    /* UDP: UDPRelease cancels pending reads per MacTCP docs */
    mactcp_release_udp_stream(&g_mactcp.discovery_udp, &upb, "Discovery");
    mactcp_release_udp_stream(&g_mactcp.message_udp, &upb, "Message");

    /* Dispose UPPs after all async operations complete */
    if (g_mactcp.tcp_upp) DisposeTCPNotifyUPP(g_mactcp.tcp_upp);
    if (g_mactcp.udp_upp) DisposeUDPNotifyUPP(g_mactcp.udp_upp);

    CLOG_INFO("PeerTalk shutdown complete");
    ctx->platform_state = NULL;
}
```

Also simplify `mactcp_release_udp_stream` — remove the PPC-specific wait-before-release path. Just UDPRelease directly (it cancels pending reads), then DisposePtr.

### 1F. Implement for OT and POSIX platforms

**File: `peertalk/src/platform/opentransport/pt_ot.c`**
**File: `peertalk/src/platform/posix/pt_posix.c`**

Add `cleanup_streams` to both ops tables. OT and POSIX can use a simple implementation or NULL (OT handles this natively, POSIX uses standard sockets). If NULL, the `PT_StartDiscovery` guard `ctx->platform_ops->cleanup_streams` skips the call.

---

## Part 2: Restore BomberTalk Features

### 2A. Restore `screen_lobby.c`

**File: `BomberTalk/src/screen_lobby.c`** (~line 56)

Remove the `roundStartTick` workaround. Restore unconditional `Net_StartDiscovery()`:
```c
Net_ResetVersionMismatch();
Net_StartDiscovery();
CLOG_INFO("Lobby entered, discovery started");
```

This now works because PeerTalk's `PT_StartDiscovery` calls `cleanup_streams` internally.

### 2B. Restore `main.c` shutdown

**File: `BomberTalk/src/main.c`** (~line 334)

Restore the committed HEAD shutdown sequence:
```c
CLOG_INFO("Shutting down");

/* Shut down networking first: tears down MacTCP/OT streams before
 * we free GWorlds and the window.  Prevents async MacTCP completions
 * from referencing freed buffers after ExitToShell reclaims the app heap. */
Net_Shutdown();

/* Renderer_Shutdown redirects QuickDraw to the window before
 * disposing GWorlds — required per Sex, Lies & Video Games (1996)
 * p.104 to avoid leaving stale Font Manager references in the
 * System heap (Bus Error in Finder's StdText after exit). */
Renderer_Shutdown();

if (gGame.window) {
    DisposeWindow(gGame.window);
    gGame.window = NULL;
}

#ifndef CLOG_STRIP
clog_shutdown();
#endif

ExitToShell();
return 0; /* not reached */
```

### 2C. Restore `net.c` Net_Shutdown

**File: `BomberTalk/src/net.c`** (~line 345)

Restore `PT_StopDiscovery` and `clog_set_network_sink` clearing:
```c
void Net_Shutdown(void)
{
    if (gPTCtx) {
#ifndef CLOG_STRIP
        clog_set_network_sink(NULL, NULL);
#endif
        PT_StopDiscovery(gPTCtx);
        PT_Shutdown(gPTCtx);
        gPTCtx = NULL;
    }
}
```

### 2D. Restore clog UDP broadcast sink

**File: `BomberTalk/src/net.c`** (~line 298)

Restore the UDP broadcast logging in `Net_Init`. This is what makes `socat UDP-RECV:7356 -` work for remote debugging:
```c
#ifndef CLOG_STRIP
    if (!gGame.isMacSE) {
        const char *lip = PT_LocalAddress(gPTCtx);
        const char *s = lip;
        int i = 0;
        gLogPrefix[i++] = '[';
        while (*s && i < 17) {
            gLogPrefix[i++] = *s++;
        }
        gLogPrefix[i++] = ']';
        gLogPrefix[i++] = ' ';
        gLogPrefixLen = i;
        clog_set_network_sink(udp_log_sink, NULL);
    }
#endif
```

This is safe now because:
1. The clog sink uses `PT_SendUDPBroadcast` (synchronous UDPWrite) which doesn't interfere with stream state
2. `Net_Shutdown` clears the sink before `PT_Shutdown` so no writes happen during teardown
3. The root cause was dirty stream state from rediscovery, not the sink itself

---

## Part 3: Test Plan

Build all 3 targets (68k, PPC MacTCP, PPC OT). Deploy to all 3 Macs.

1. **Quit from lobby (no game)** — Cmd-Q on all 3 → all quit clean
2. **3-player game → lobby → Cmd-Q** — all quit clean
3. **3-player game → lobby → SECOND game → lobby → Cmd-Q** — all quit clean (proves stream cleanup works for multiple rounds)
4. **Repeat #3 three times** — verify no resource leaks (64-stream limit)
5. **Verify socat** — run `socat UDP-RECV:7356 -` on host, confirm UDP log broadcasts appear from 6200 and 6400 during gameplay
6. **Verify file logging** — download "BomberTalk Log" from 6200, confirm full game + clean shutdown logged

---

## Codebase Changes Summary

| # | Repo | File | Change | Type |
|---|------|------|--------|------|
| 1 | PeerTalk | pt_internal.h | Add `cleanup_streams` to PT_PlatformOps | Fix |
| 2 | PeerTalk | pt_mactcp.c | Implement `mactcp_cleanup_streams` — release+recreate all streams | Fix |
| 3 | PeerTalk | pt_core.c | Call `cleanup_streams` in PT_StartDiscovery on second+ call | Fix |
| 4 | PeerTalk | pt_mactcp.c | Remove PPC 2-sec UDPRead timeout, use timeout=0 everywhere | Fix |
| 5 | PeerTalk | pt_mactcp.c | Simplify mactcp_shutdown to v1.0.0-style (TCPRelease, UDPRelease, done) | Fix |
| 6 | PeerTalk | pt_mactcp.c | Simplify mactcp_release_udp_stream — remove PPC wait path | Fix |
| 7 | PeerTalk | pt_ot.c | Add cleanup_streams (NULL or simple impl) to OT ops table | Fix |
| 8 | PeerTalk | pt_posix.c | Add cleanup_streams (NULL or simple impl) to POSIX ops table | Fix |
| 9 | BomberTalk | screen_lobby.c | Remove roundStartTick workaround, restore Net_StartDiscovery | Restore |
| 10 | BomberTalk | main.c | Restore ExitToShell, shutdown comments, Net-first order | Restore |
| 11 | BomberTalk | net.c | Restore PT_StopDiscovery + clog sink clearing in Net_Shutdown | Restore |
| 12 | BomberTalk | net.c | Restore clog UDP broadcast sink in Net_Init | Restore |

## MacTCP Book References

All from `peertalk/books/MacTCP_Programmers_Guide_1989.txt`:
- **Line 1422**: "UDPRelease closes a UDP stream. Any outstanding commands on that stream are terminated with an error."
- **Line 1853**: "UDP Release must be called to release memory held by the driver. Failure to do so may produce unpredictable results."
- **Line 2142**: "A TCP connection on a stream can be closed and another connection opened without releasing the TCP stream. MacTCP can support 64 open TCP streams simultaneously."
- **Line 2440**: Buffer "cannot be modified or relocated until TCPRelease is called."
- **Line 3594**: "TCPAbort returns the TCP stream to its initial state."
- **Line 4012**: "TCPRelease closes a TCP stream. If there is an open connection on the stream, the connection is first broken as though a TCPAbort command had been issued."
- **Line 1186**: UDPRead "minimum allowed value for the command time-out is 2 seconds. A zero command time-out means infinite."
