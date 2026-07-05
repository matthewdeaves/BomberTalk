/*
 * sdl_backend.h -- SDL-typed glue internal to the POSIX/SDL2 backend.
 *
 * mac_shim.c implements the QuickDraw text/rect subset by drawing into an
 * SDL_Surface. renderer_sdl.c owns that surface (the "work buffer") and tells
 * the shim which surface to target for the current screen draw. This header
 * carries the SDL types between the two backend files; it is NOT included by
 * any shared-core code (that only ever sees the Toolbox-free mac_shim.h).
 *
 * POSIX/SDL2 build only (011-macosx-sdl2).
 */
#ifndef SDL_BACKEND_H
#define SDL_BACKEND_H

#include <SDL2/SDL.h>

/* Point the QuickDraw text/rect shim at the surface subsequent MoveTo/
 * DrawString/PaintRect/... calls should render into. Passing NULL disables
 * drawing (measurement calls like StringWidth still work). */
void MacShim_SetTarget(SDL_Surface *surface);

/* Save the current composited work surface to a BMP (headless validation).
 * Returns 0 on success. */
int Renderer_SaveScreenshot(const char *path);

#endif /* SDL_BACKEND_H */
