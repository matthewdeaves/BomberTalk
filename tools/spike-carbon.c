/*
 * spike-carbon.c -- Viability spike for the BomberTalk OS X Carbon build.
 *
 * Proves the load-bearing assumptions of the 011 OS X plan on real hardware:
 *   1. A Carbon Mach-O built with gcc-4.0 against the 10.4u SDK launches on
 *      OS X 10.3-10.7 (PPC + Intel).
 *   2. Color QuickDraw works on OS X: NewGWorld offscreen buffer + CopyBits
 *      to a window (the exact renderer.c pipeline) draws without crashing.
 *   3. GetKeys() input polling works under Carbon.
 *
 * It opens a window, composites a colored scene in an offscreen GWorld,
 * CopyBits it to the window, polls GetKeys briefly, prints SPIKE_OK, quits.
 * Run in the desktop session (osascript) to see it, or over ssh to confirm
 * it launches + draws without a crash (stdout returns the marker).
 *
 * Build: see tools/build-macosx-fat.sh (or the one-off cc line in the PR).
 */

#include <Carbon/Carbon.h>
#include <stdio.h>

int main(void)
{
    WindowRef  win;
    Rect       bounds;
    CGrafPtr   savePort;
    GDHandle   saveDevice;
    GWorldPtr  gw;
    QDErr      err;
    KeyMap     keys;
    long       i;
    RGBColor   red   = { 0xFFFF, 0x0000, 0x0000 };
    RGBColor   green = { 0x0000, 0xFFFF, 0x0000 };
    RGBColor   blue  = { 0x0000, 0x0000, 0xFFFF };

    /* Carbon apps do NOT call InitGraf/InitWindows -- the Toolbox is already
     * initialized. Make this a foreground UI process so a window can appear. */
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    SetFrontProcess(&psn);

    SetRect(&bounds, 60, 60, 60 + 320, 60 + 240);
    win = NewCWindow(NULL, &bounds, "\pBomberTalk Carbon Spike", true,
                     documentProc, (WindowRef)-1L, false, 0);
    if (win == NULL) {
        printf("SPIKE_FAIL: NewCWindow returned NULL\n");
        return 1;
    }

    /* Offscreen GWorld -- the renderer.c work-buffer pattern. */
    err = NewGWorld(&gw, 0, &bounds, NULL, NULL, 0);
    if (err != noErr || gw == NULL) {
        printf("SPIKE_FAIL: NewGWorld err=%d\n", (int)err);
        return 1;
    }

    /* Draw a scene into the offscreen buffer. */
    GetGWorld(&savePort, &saveDevice);
    SetGWorld(gw, NULL);
    LockPixels(GetGWorldPixMap(gw));
    RGBForeColor(&blue);
    PaintRect(&bounds);
    RGBForeColor(&red);
    { Rect r; SetRect(&r, 20, 20, 140, 140); PaintRect(&r); }
    RGBForeColor(&green);
    { Rect r; SetRect(&r, 160, 80, 300, 220); PaintOval(&r); }
    UnlockPixels(GetGWorldPixMap(gw));
    SetGWorld(savePort, saveDevice);

    /* CopyBits offscreen -> window: the exact Renderer_EndFrame blit. */
    SetPortWindowPort(win);
    LockPixels(GetGWorldPixMap(gw));
    CopyBits((BitMap *)*GetGWorldPixMap(gw),
             GetPortBitMapForCopyBits(GetWindowPort(win)),
             &bounds, &bounds, srcCopy, NULL);
    UnlockPixels(GetGWorldPixMap(gw));
    QDFlushPortBuffer(GetWindowPort(win), NULL);

    /* Poll GetKeys a few times to prove input works under Carbon. */
    for (i = 0; i < 3; i++) {
        GetKeys(keys);
    }

    printf("SPIKE_OK: Carbon window + NewGWorld + CopyBits + GetKeys all worked\n");
    fflush(stdout);

    /* Hold the window briefly so an on-screen run is visible, then quit. */
    { EventRecord ev; UInt32 deadline = TickCount() + 120;
      while (TickCount() < deadline) { WaitNextEvent(everyEvent, &ev, 6, NULL); } }

    DisposeGWorld(gw);
    DisposeWindow(win);
    return 0;
}
