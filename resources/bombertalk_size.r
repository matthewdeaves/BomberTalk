/*
 * bombertalk_size.r -- SIZE resource (memory partition)
 *
 * 1.5 MB preferred, 1 MB minimum.
 * Source: BOMBERMAN_CLONE_PLAN.md, research.md R3 memory budget.
 */

#include "Retro68APPL.r"

resource 'SIZE' (-1) {
    dontSaveScreen,
    acceptSuspendResumeEvents,
    enableOptionSwitch,
    canBackground,
    multiFinderAware,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    not32BitCompatible,
    reserved, reserved, reserved, reserved, reserved, reserved, reserved,
    1500 * 1024,    /* preferred size: 1.5 MB */
    1000 * 1024     /* minimum size: 1 MB */
};
