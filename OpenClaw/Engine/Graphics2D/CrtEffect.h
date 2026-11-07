#pragma once
// Options > Graphics > CRT effect: scanlines along the rows of the game's
// pixels and darker corners, drawn over the level like an old TV.
#include <SDL2/SDL.h>

namespace CrtEffect
{
    bool IsOn();
    void SetOn(bool on);
    // Over the whole frame (window size w x h at render scale 1); levelScale:
    // the level's pixels' size on the screen.
    void Render(SDL_Renderer* renderer, int w, int h, float levelScale);
}
