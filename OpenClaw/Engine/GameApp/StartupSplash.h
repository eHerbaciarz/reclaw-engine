#pragma once

// Optional dedication screen shown while the game loads.
//
// Enabled at build time with OPENCLAW_SPLASH_TEXT (UTF-8), e.g. on Switch:
//     make SPLASH="Text-Title"
// Show() fades the text in right after the window exists, the screen then stays
// up during resource loading, and Finish() keeps it visible for a minimum time
// before fading out to the menu. Without the define both calls do nothing.

struct SDL_Renderer;

namespace StartupSplash
{
    void Show(SDL_Renderer* pRenderer);
    void Finish(SDL_Renderer* pRenderer);
}
