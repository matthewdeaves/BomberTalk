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

static KeyMap gCurrentKeys;
static KeyMap gPreviousKeys;
static KeyMap gAccumEdges;  /* accumulated key-down edges between frames */

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
    short i;
    static long sLogTick = 0;
    long now;

    GetKeys(newKeys);

    /* OR new key-down edges into accumulator */
    for (i = 0; i < 4; i++) {
        gAccumEdges[i] |= (newKeys[i] & ~gCurrentKeys[i]);
        gCurrentKeys[i] = newKeys[i];
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
    unsigned char *keys = (unsigned char *)gCurrentKeys;
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
    unsigned char *accum = (unsigned char *)gAccumEdges;
    unsigned char byteIdx = keyCode >> 3;
    unsigned char bitMask = 1 << (keyCode & 7);

    return (accum[byteIdx] & bitMask) != 0;
}
