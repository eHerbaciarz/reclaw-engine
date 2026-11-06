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
}
#endif
