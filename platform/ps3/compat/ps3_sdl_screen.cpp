// See ps3_sdl_screen.h.
// The force-included header renamed these two; here they are SDL's own.
#undef SDL_CreateRenderer
#undef SDL_RenderPresent
#include <SDL2/SDL.h>

namespace
{
    SDL_Renderer* g_Screen = nullptr;     // PSL1GHT renderer (puts frames on the TV)
    SDL_Texture* g_Tv = nullptr;          // a whole TV frame in video memory
    SDL_Surface* g_Surface = nullptr;     // the frame the game draws into
    SDL_Renderer* g_Soft = nullptr;       // software renderer drawing into g_Surface
    int g_W = 854, g_H = 480, g_TvW = 1280, g_TvH = 720;

    // Exactly 2x: every pixel to 2x2 (two 32-bit pixels per 64-bit store).
    void Double(const SDL_Surface* src, Uint8* dst, int dstPitch)
    {
        for (int y = 0; y < src->h; ++y)
        {
            const Uint32* s = (const Uint32*)((const Uint8*)src->pixels + y * src->pitch);
            Uint64* d0 = (Uint64*)(dst + (2 * y) * dstPitch);
            Uint64* d1 = (Uint64*)(dst + (2 * y + 1) * dstPitch);
            for (int x = 0; x < src->w; ++x)
            {
                Uint64 p = ((Uint64)s[x] << 32) | s[x];   // big endian: left pixel high
                d0[x] = p;
                d1[x] = p;
            }
        }
    }
}

extern "C" SDL_Renderer* ReclawPS3_CreateRenderer(SDL_Window* window, int index, unsigned int flags)
{
    (void)index; (void)flags;
    if (g_Soft) return g_Soft;   // one screen
    // The window (and so the renderer's view) is the TV's own resolution.
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
    {
        g_TvW = mode.w; g_TvH = mode.h;
        SDL_SetWindowSize(window, g_TvW, g_TvH);
    }
    g_Screen = SDL_CreateRenderer(window, -1, 0);
    if (!g_Screen) return nullptr;
    // 1080p: half of it, doubled exactly; 720p and less: 854x480, stretched
    if (g_TvH >= 1000) { g_W = g_TvW / 2; g_H = g_TvH / 2; }
    else { g_W = 854; g_H = 480; }
    g_Tv = SDL_CreateTexture(g_Screen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, g_TvW, g_TvH);
    g_Surface = SDL_CreateRGBSurfaceWithFormat(0, g_W, g_H, 32, SDL_PIXELFORMAT_ARGB8888);
    g_Soft = g_Surface ? SDL_CreateSoftwareRenderer(g_Surface) : nullptr;
    if (!g_Soft || !g_Tv)
    {
        SDL_Log("PS3 screen: %s", SDL_GetError());
        return g_Screen;
    }
    SDL_Log("PS3 screen: TV %dx%d, the game draws %dx%d in software", g_TvW, g_TvH, g_W, g_H);
    return g_Soft;
}

extern "C" void ReclawPS3_RenderPresent(SDL_Renderer* renderer)
{
    if (renderer != g_Soft || !g_Soft)
    {
        SDL_RenderPresent(renderer);
        return;
    }
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(g_Tv, NULL, &pixels, &pitch) == 0)
    {
        if (g_TvW == 2 * g_W && g_TvH == 2 * g_H)
        {
            Double(g_Surface, (Uint8*)pixels, pitch);
        }
        else if (SDL_Surface* tv = SDL_CreateRGBSurfaceWithFormatFrom(pixels, g_TvW, g_TvH, 32, pitch, SDL_PIXELFORMAT_ARGB8888))
        {
            // fitted to the TV, black around it
            double fit = SDL_min((double)g_TvW / g_W, (double)g_TvH / g_H);
            SDL_Rect dst = { 0, 0, (int)(g_W * fit), (int)(g_H * fit) };
            dst.x = (g_TvW - dst.w) / 2;
            dst.y = (g_TvH - dst.h) / 2;
            if (dst.x > 0 || dst.y > 0) SDL_FillRect(tv, NULL, 0);
            SDL_BlitScaled(g_Surface, NULL, tv, &dst);
            SDL_FreeSurface(tv);
        }
        SDL_UnlockTexture(g_Tv);
    }
    // 1:1 copy of the whole frame to the screen buffer, then show it
    SDL_Rect all = { 0, 0, g_TvW, g_TvH };
    SDL_RenderCopy(g_Screen, g_Tv, &all, &all);
    SDL_RenderPresent(g_Screen);
}
