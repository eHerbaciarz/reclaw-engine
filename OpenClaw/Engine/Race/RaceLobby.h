#pragma once

// Race lobby (main menu > Multiplayer): split screen or network race, level,
// host / join. Runs its own menu-styled screens.

#include <SDL2/SDL.h>

#include "Race.h"

namespace RaceLobby
{
    // True when a race was set up (out filled); false when the player backed out.
    bool Run(SDL_Renderer* pRenderer, Race::Setup& out);
}
