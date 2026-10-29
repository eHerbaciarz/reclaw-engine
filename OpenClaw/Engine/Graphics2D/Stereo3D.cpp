#include "Stereo3D.h"

#include <SDL2/SDL.h>

#ifdef __3DS__
#include <3ds.h>
#endif

namespace
{
    int g_Eye = 0;
    float g_Slider = 0.0f;
    bool g_3DOn = false;
    // Largest move at full slider, in pixels (the top screen is 400 wide).
    const float kMaxShift = 6.0f;
}

namespace Stereo3D
{
    bool BeginFrame(bool inLevel)
    {
#ifdef __3DS__
        g_Slider = inLevel ? osGet3DSliderState() : 0.0f;
        bool want = g_Slider > 0.01f;
        if (want != g_3DOn)
        {
            gfxSet3D(want);
            g_3DOn = want;
        }
        return want;
#else
        (void)inLevel;
        return false;
#endif
    }

    int Eye() { return g_Eye; }
    void SetEye(int eye) { g_Eye = eye; }

    int Offset(float depth)
    {
        if (g_Eye == 0) return 0;
        float d = depth < -1.0f ? -1.0f : (depth > 1.0f ? 1.0f : depth);
        return (int)(g_Eye * d * g_Slider * kMaxShift + (g_Eye * d >= 0 ? 0.5f : -0.5f));
    }

    void StoreRightEye(SDL_Window* pWindow)
    {
#ifdef __3DS__
        SDL_Surface* s = pWindow ? SDL_GetWindowSurface(pWindow) : nullptr;
        if (!s || s->format->BytesPerPixel != 4) return;
        u16 width = 0, height = 0;   // the framebuffer is turned: width 240, height 400
        u32* dest = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, &width, &height);
        if (!dest) return;
        int rows = SDL_min((int)width, s->h);
        int cols = SDL_min((int)height, s->w);
        const u32* src = (const u32*)s->pixels;
        int pitch = s->pitch / 4;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x)
                dest[width - y - 1 + width * x] = src[x + y * pitch];
        GSPGPU_FlushDataCache(dest, width * height * 4);
#else
        (void)pWindow;
#endif
    }
}
