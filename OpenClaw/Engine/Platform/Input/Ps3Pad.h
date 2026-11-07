#pragma once

// PS3: the pads read directly through ioPad (SDL's PSL1GHT controller events
// do not reach the game's menus). ioPadGetData only reports changes, so every
// reader goes through this one cache of the last state.
#ifdef __PSL1GHT__
#include <io/pad.h>

namespace Ps3Pad
{
    // The current state of the pad on that port; false when none is there.
    bool State(int port, padData& out);
    // The system menu (PS button) opened / closed: presses made meanwhile
    // (and Select pressed right before it opens) do not reach the game.
    void SetSystemMenu(bool open);
    // Open, or closed less than 0.4 s ago.
    bool SystemMenuQuiet();
    // How many times the system menu has opened (a press during which it
    // opened was meant for the system).
    unsigned SystemMenuCount();
}
#endif
