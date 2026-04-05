/*
 * input.c -- Keyboard polling via GetKeys()
 *
 * GetKeys() returns 128 bits representing all keyboard keys.
 * We store current + previous state to detect key-down transitions.
 * Source: Black Art (1996) Ch. 4, Mac Game Programming (2002) Ch. 7.
 */

#include "input.h"
#include "game.h"
#include <clog.h>

static KeyMap gCurrentKeys;
static KeyMap gPreviousKeys;

void Input_Init(void)
{
    short i;
    for (i = 0; i < 4; i++) {
        gCurrentKeys[i] = 0;
        gPreviousKeys[i] = 0;
    }
}

void Input_Poll(void)
{
    short i;
    static long sLogTick = 0;
    long now;

    for (i = 0; i < 4; i++) {
        gPreviousKeys[i] = gCurrentKeys[i];
    }
    GetKeys(gCurrentKeys);

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
 * Input_WasKeyPressed -- Was this key just pressed (edge detect)?
 *
 * True if key is down now but was up last frame.
 * Source: Mac Game Programming (2002) WasKeyPressed pattern.
 */
int Input_WasKeyPressed(unsigned char keyCode)
{
    unsigned char *cur = (unsigned char *)gCurrentKeys;
    unsigned char *prev = (unsigned char *)gPreviousKeys;
    unsigned char byteIdx = keyCode >> 3;
    unsigned char bitMask = 1 << (keyCode & 7);

    return (cur[byteIdx] & bitMask) && !(prev[byteIdx] & bitMask);
}
