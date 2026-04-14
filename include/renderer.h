/*
 * renderer.h -- GWorld double-buffered rendering
 *
 * Two offscreen buffers: background (static tiles) and work (per-frame).
 * Source: Black Art (1996), Tricks of the Gurus (1995),
 *         Sex Lies and Video Games (1996) buffered animation.
 */

#ifndef RENDERER_H
#define RENDERER_H

#include "game.h"

void Renderer_Init(WindowPtr window);
void Renderer_Shutdown(void);

/* Rebuild the static background (call when blocks destroyed) */
void Renderer_RebuildBackground(void);

/* Request deferred rebuild -- coalesces multiple calls into one per frame */
void Renderer_RequestRebuildBackground(void);

/* Per-frame rendering: copy bg to work, draw sprites, blit to window */
void Renderer_BeginFrame(void);
void Renderer_DrawPlayer(short playerID, short pixelX, short pixelY, short facing);
void Renderer_DrawBomb(short col, short row);
void Renderer_DrawExplosion(short col, short row);

/* Bracket all per-frame sprite drawing to batch port save/restore */
void Renderer_BeginSpriteDraw(void);
void Renderer_EndSpriteDraw(void);

void Renderer_EndFrame(WindowPtr window);
void Renderer_BlitToWindow(WindowPtr window);

/* Dirty rectangle tracking */
void Renderer_MarkDirty(short col, short row);

/*
 * Screen drawing helpers -- for non-gameplay screens (menu, lobby, loading).
 * BeginScreenDraw clears the work buffer and sets the drawing port to it.
 * EndScreenDraw restores the port and blits the work buffer to the window.
 * Between the two calls, use standard QuickDraw (MoveTo, DrawString, etc.)
 * and everything goes into the offscreen buffer -- no flicker.
 */
void Renderer_BeginScreenDraw(void);
void Renderer_EndScreenDraw(WindowPtr window);

/* FPS overlay -- draws directly on window */
void Renderer_DrawFPS(short fps);

#endif /* RENDERER_H */
