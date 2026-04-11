/*
 * screens.h -- Screen state machine
 *
 * Each screen implements Init/Update/Draw. The dispatcher
 * routes to the current screen.
 */

#ifndef SCREENS_H
#define SCREENS_H

#include "game.h"

void Screens_Init(void);
void Screens_TransitionTo(ScreenState newScreen);
void Screens_Update(void);
void Screens_Draw(WindowPtr window);
/* Per-screen interfaces (called by screens.c dispatcher) */
void Loading_Init(void);
void Loading_Update(void);
void Loading_Draw(WindowPtr window);

void Menu_Init(void);
void Menu_Update(void);
void Menu_Draw(WindowPtr window);

void Lobby_Init(void);
void Lobby_Update(void);
void Lobby_Draw(WindowPtr window);

void Game_Init(void);
void Game_Update(void);
void Game_Draw(WindowPtr window);

#endif /* SCREENS_H */
