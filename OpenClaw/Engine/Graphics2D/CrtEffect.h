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

    // Just before every SDL_RenderPresent of the game's screen (menus,
    // loading screens, movies, the level): the CRT effect and, in classic
    // 4:3, black bars at the sides - on everything shown.
    void BeforePresent(SDL_Renderer* renderer);

    // Options > Graphics > FPS counter: frames per second in the lower left
    // corner of the picture (drawn in BeforePresent).
    bool FpsCounterOn();
    void SetFpsCounter(bool on);
}
