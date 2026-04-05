/*
 * input.h -- Keyboard polling via GetKeys()
 *
 * Source: Black Art of Macintosh Game Programming (1996) Ch. 4,
 *         Mac Game Programming (2002) Ch. 7.
 */

#ifndef INPUT_H
#define INPUT_H

void Input_Init(void);
void Input_Poll(void);
int  Input_IsKeyDown(unsigned char keyCode);
int  Input_WasKeyPressed(unsigned char keyCode);

#endif /* INPUT_H */
