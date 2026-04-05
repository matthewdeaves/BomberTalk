/*
 * bombertalk.r -- Menu resources for BomberTalk
 *
 * Apple menu (desk accessories) and File menu (Quit).
 * Source: Macintosh Game Programming Techniques (1996), Chapter 4.
 */

#include "Retro68APPL.r"
#include "Menus.r"

resource 'MENU' (128, "Apple") {
    128,
    textMenuProc,
    allEnabled,
    enabled,
    apple,
    {
        "About BomberTalk\311", noIcon, noKey, noMark, plain
    }
};

resource 'MENU' (129, "File") {
    129,
    textMenuProc,
    allEnabled,
    enabled,
    "File",
    {
        "Quit", noIcon, "Q", noMark, plain
    }
};
