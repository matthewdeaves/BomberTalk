/*
 * input.c -- Keyboard polling via GetKeys()
 *
 * GetKeys() returns 128 bits representing all keyboard keys.
 * We store current + previous state to detect key-down transitions.
 * Accumulated edges capture brief taps between frames (critical for
 * slow machines like Mac SE at ~6fps).
 * Source: Black Art (1996) Ch. 4, Mac Game Programming (2002) Ch. 7.
 */

#include "input.h"
#include "game.h"
#include <clog.h>

/* KeyMap is 16 bytes on every target. On Classic Mac it is 4 longs, but
 * under Carbon it is an array of big-endian UInt32 structs, so element-wise
 * arithmetic on KeyMap[i] does not compile. We store the state as four raw
 * 32-bit words instead and view GetKeys() output through a word pointer:
 * bitwise accumulate/compare is byte-order-agnostic, and the bit extractors
 * (Input_IsKeyDown / Input_WasKeyPressed) use byte access, so the result is
 * identical on 68k, PPC, and Intel. (011-macosx-sdl2)
 *
 * NOTE: this assumes 32-bit `long` (ILP32): 4 words == KeyMap's 16 bytes.
 * True on every current target (68k, PPC, i386). A future LP64 build
 * (arm64/x86-64) would need `unsigned int[4]` or a 16-byte memcpy instead
 * -- but that path (SDL2) won't call GetKeys() at all. */
static unsigned long gCurrentKeys[4];
static unsigned long gPreviousKeys[4];
static unsigned long gAccumEdges[4];  /* accumulated key-down edges between frames */

void Input_Init(void)
{
    short i;
    for (i = 0; i < 4; i++) {
        gCurrentKeys[i] = 0;
        gPreviousKeys[i] = 0;
        gAccumEdges[i] = 0;
    }
}

/*
 * Input_Poll -- Sample keyboard and accumulate new key-down edges.
 *
 * Call this every main loop iteration (outside the frame gate).
 * Brief taps that happen between frames are OR'd into gAccumEdges
 * so they won't be missed at low frame rates.
 */
void Input_Poll(void)
{
    KeyMap newKeys;
    unsigned long *nw = (unsigned long *)newKeys;
    short i;
    static long sLogTick = 0;
    long now;

    GetKeys(newKeys);

    /* OR new key-down edges into accumulator (word view; see Input_Init) */
    for (i = 0; i < 4; i++) {
        gAccumEdges[i] |= (nw[i] & ~gCurrentKeys[i]);
        gCurrentKeys[i] = nw[i];
    }

    /* Log any non-zero keymap words every 2 seconds */
    now = TickCount();
    if (now - sLogTick >= 120) {
        if (gCurrentKeys[0] != 0 || gCurrentKeys[1] != 0 ||
            gCurrentKeys[2] != 0 || gCurrentKeys[3] != 0) {
            CLOG_INFO("Keys: [%08lX %08lX %08lX %08lX]",
                      (unsigned long)gCurrentKeys[0],
                      (unsigned long)gCurrentKeys[1],
                      (unsigned long)gCurrentKeys[2],
                      (unsigned long)gCurrentKeys[3]);
            sLogTick = now;
        }
    }
}

/*
 * Input_ConsumeFrame -- Reset accumulated edges for next frame.
 *
 * Call this once per frame, after all Input_WasKeyPressed checks.
 * Copies current keys to previous for IsKeyDown, clears edge accumulator.
 */
void Input_ConsumeFrame(void)
{
    short i;
    for (i = 0; i < 4; i++) {
        gPreviousKeys[i] = gCurrentKeys[i];
        gAccumEdges[i] = 0;
    }
}

/*
 * Input_IsKeyDown -- Is this key currently held?
 *
 * KeyMap is 128 bits (16 bytes). Key code K lives in byte K/8, bit K%8.
 * Byte-based access is endian-independent (both 68k and PPC are big-endian,
 * but long-based bit extraction maps bits to wrong bytes on big-endian).
 * Source: Black Art (1996) p.87, Inside Macintosh: Toolbox Essentials.
 */
int Input_IsKeyDown(unsigned char keyCode)
{
    const unsigned char *keys = (const unsigned char *)gCurrentKeys;
    return (keys[keyCode >> 3] >> (keyCode & 7)) & 1;
}

/*
 * Input_WasKeyPressed -- Was this key pressed since last frame?
 *
 * Checks accumulated edges, not just current vs previous snapshot.
 * This catches brief taps that happened between frames.
 * Source: Mac Game Programming (2002) WasKeyPressed pattern.
 */
int Input_WasKeyPressed(unsigned char keyCode)
{
    const unsigned char *accum = (const unsigned char *)gAccumEdges;
    unsigned char byteIdx = keyCode >> 3;
    unsigned char bitMask = 1 << (keyCode & 7);

    return (accum[byteIdx] & bitMask) != 0;
}
