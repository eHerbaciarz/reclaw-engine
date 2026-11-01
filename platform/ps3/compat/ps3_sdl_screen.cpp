// See ps3_sdl_screen.h.
// The force-included header renamed these two; here they are SDL's own.
#undef SDL_CreateRenderer
#undef SDL_RenderPresent
#include <SDL2/SDL.h>

namespace
{
    // The game's picture: 854x480, the size the PS3 version draws at.
    const int kW = 854, kH = 480;
    SDL_Renderer* g_Screen = nullptr;     // PSL1GHT renderer (puts pictures on the TV)
    SDL_Texture* g_Picture = nullptr;     // the finished frame, in video memory
    SDL_Surface* g_Surface = nullptr;     // the frame the game draws into
    SDL_Renderer* g_Soft = nullptr;       // software renderer drawing into g_Surface
}

extern "C" SDL_Renderer* ReclawPS3_CreateRenderer(SDL_Window* window, int index, unsigned int flags)
{
    (void)index; (void)flags;
    if (g_Soft) return g_Soft;   // one screen
    g_Screen = SDL_CreateRenderer(window, -1, 0);
    if (!g_Screen) return nullptr;
    g_Surface = SDL_CreateRGBSurfaceWithFormat(0, kW, kH, 32, SDL_PIXELFORMAT_ARGB8888);
    g_Picture = SDL_CreateTexture(g_Screen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kW, kH);
    g_Soft = g_Surface ? SDL_CreateSoftwareRenderer(g_Surface) : nullptr;
    if (!g_Soft || !g_Picture)
    {
        SDL_Log("PS3 screen: %s", SDL_GetError());
        return g_Screen;   // at least something on the screen
    }
    SDL_Log("PS3 screen: the game draws %dx%d in software, shown by the PSL1GHT renderer", kW, kH);
    return g_Soft;
}

extern "C" void ReclawPS3_RenderPresent(SDL_Renderer* renderer)
{
    if (renderer != g_Soft || !g_Soft)
    {
        SDL_RenderPresent(renderer);
        return;
    }
    SDL_UpdateTexture(g_Picture, NULL, g_Surface->pixels, g_Surface->pitch);
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(g_Screen, &outW, &outH);
    // fitted to the screen (16:9 TV: the whole screen)
    double fit = SDL_min((double)outW / kW, (double)outH / kH);
    SDL_Rect dst = { 0, 0, (int)(kW * fit), (int)(kH * fit) };
    dst.x = (outW - dst.w) / 2;
    dst.y = (outH - dst.h) / 2;
    if (dst.x > 0 || dst.y > 0) SDL_RenderClear(g_Screen);
    SDL_RenderCopy(g_Screen, g_Picture, NULL, &dst);
    SDL_RenderPresent(g_Screen);
}
