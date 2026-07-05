/*
 * platform_posix.c -- POSIX/SDL2 time base (011-macosx-sdl2).
 *
 * The shared game core times everything in "ticks" (Mac TickCount units,
 * 1/60.15 sec on real hardware). On SDL we derive the same 60 Hz tick from
 * the SDL millisecond clock so all the tick-based gameplay and network timers
 * behave identically to the classic builds. SDL_GetTicks64 is monotonic and
 * 64-bit, so the *60 scale never overflows within a session.
 */
#include "mac_shim.h"
#include <SDL2/SDL.h>

long TickCount(void)
{
    return (long)((SDL_GetTicks64() * 60ULL) / 1000ULL);
}
