/*
 * input_sdl.c -- SDL2 keyboard backend (011-macosx-sdl2).
 *
 * The classic backend (input.c) polls GetKeys() into a 128-bit KeyMap indexed
 * by Mac hardware scan code, and the game asks about keys by those codes
 * (KEY_UP_ARROW = 0x7E, etc.). We keep that exact contract so screen_*.c and
 * player.c are unchanged: this file samples SDL's keyboard state, maps each SDL
 * scancode back to its Mac scan code, and stores held bits + accumulated edges
 * in the same byte/bit layout Input_IsKeyDown/WasKeyPressed expect.
 *
 * Edge accumulation mirrors input.c: Input_Poll runs every loop iteration and
 * ORs new key-down transitions into an accumulator so brief taps between frames
 * are never dropped; Input_ConsumeFrame clears it once per frame.
 */
#include "input.h"
#include "game.h"
#include <SDL2/SDL.h>

/* Mac scan code -> SDL scancode. WASD are added as movement alternates. */
typedef struct { unsigned char mac; int sdl; } KeyMapEntry;
static const KeyMapEntry kKeyMap[] = {
    { KEY_UP_ARROW,    SDL_SCANCODE_UP },
    { KEY_UP_ARROW,    SDL_SCANCODE_W },
    { KEY_DOWN_ARROW,  SDL_SCANCODE_DOWN },
    { KEY_DOWN_ARROW,  SDL_SCANCODE_S },
    { KEY_LEFT_ARROW,  SDL_SCANCODE_LEFT },
    { KEY_LEFT_ARROW,  SDL_SCANCODE_A },
    { KEY_RIGHT_ARROW, SDL_SCANCODE_RIGHT },
    { KEY_RIGHT_ARROW, SDL_SCANCODE_D },
    { KEY_SPACE,       SDL_SCANCODE_SPACE },
    { KEY_RETURN,      SDL_SCANCODE_RETURN },
    { KEY_RETURN,      SDL_SCANCODE_KP_ENTER },
    { KEY_ESCAPE,      SDL_SCANCODE_ESCAPE },
    { KEY_Q,           SDL_SCANCODE_Q },
    { KEY_F,           SDL_SCANCODE_F }
};
#define KEYMAP_COUNT ((int)(sizeof(kKeyMap) / sizeof(kKeyMap[0])))

/* 128 bits, byte K/8 bit K%8 -- same layout as the classic KeyMap. */
static unsigned char gCurrentKeys[16];
static unsigned char gAccumEdges[16];

void Input_Init(void)
{
    short i;
    for (i = 0; i < 16; i++) {
        gCurrentKeys[i] = 0;
        gAccumEdges[i] = 0;
    }
}

void Input_Poll(void)
{
    unsigned char newKeys[16];
    const Uint8 *state;
    short i;

    for (i = 0; i < 16; i++) newKeys[i] = 0;

    SDL_PumpEvents();
    state = SDL_GetKeyboardState(NULL);

    for (i = 0; i < KEYMAP_COUNT; i++) {
        if (state[kKeyMap[i].sdl]) {
            unsigned char k = kKeyMap[i].mac;
            newKeys[k >> 3] |= (unsigned char)(1 << (k & 7));
        }
    }

    for (i = 0; i < 16; i++) {
        gAccumEdges[i] |= (unsigned char)(newKeys[i] & ~gCurrentKeys[i]);
        gCurrentKeys[i] = newKeys[i];
    }
}

void Input_ConsumeFrame(void)
{
    short i;
    for (i = 0; i < 16; i++) gAccumEdges[i] = 0;
}

int Input_IsKeyDown(unsigned char keyCode)
{
    return (gCurrentKeys[keyCode >> 3] >> (keyCode & 7)) & 1;
}

int Input_WasKeyPressed(unsigned char keyCode)
{
    return (gAccumEdges[keyCode >> 3] & (1 << (keyCode & 7))) != 0;
}
