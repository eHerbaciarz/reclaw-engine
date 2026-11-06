// See ps3_sdl_screen.h.
//
// The game draws with SDL's software renderer into a frame in memory. That
// frame goes to the TV through two display buffers of our own in video
// memory, flipped with gcm - exactly what PSL1GHT's own samples do. SDL's
// PSL1GHT renderer is not used: it froze the console after ~250 frames
// (its picture copies through the RSX), and it cannot blend anyway.
// The force-included header renamed these two; here they are SDL's own.
#undef SDL_CreateRenderer
#undef SDL_RenderPresent
#include <SDL2/SDL.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <unistd.h>

namespace
{
    SDL_Surface* g_Surface = nullptr;     // the frame the game draws into
    SDL_Renderer* g_Soft = nullptr;       // software renderer drawing into g_Surface
    int g_W = 854, g_H = 480, g_TvW = 1280, g_TvH = 720;

    // Display buffers in video memory (shown in turn)
    Uint32* g_Buffer[2] = { nullptr, nullptr };
    Uint32 g_Pitch = 0;
    int g_Current = 0;
    bool g_FirstFlip = true;

    void WaitFlip()
    {
        while (gcmGetFlipStatus() != 0) usleep(200);
        gcmResetFlipStatus();
    }

    void Flip(int buffer)
    {
        if (gcmSetFlip(gGcmContext, (u8)buffer) == 0)
        {
            rsxFlushBuffer(gGcmContext);
            gcmSetWaitFlip(gGcmContext);
        }
    }

    bool InitBuffers()
    {
        g_Pitch = (Uint32)g_TvW * 4;
        for (int i = 0; i < 2; ++i)
        {
            g_Buffer[i] = (Uint32*)rsxMemalign(64, (size_t)g_Pitch * g_TvH);
            if (!g_Buffer[i]) return false;
            SDL_memset(g_Buffer[i], 0, (size_t)g_Pitch * g_TvH);
            u32 offset = 0;
            if (rsxAddressToOffset(g_Buffer[i], &offset) != 0) return false;
            if (gcmSetDisplayBuffer((u8)i, offset, g_Pitch, (u32)g_TvW, (u32)g_TvH) != 0) return false;
        }
        gcmResetFlipStatus();
        Flip(1);   // show a black buffer; the game draws into buffer 0 first
        return true;
    }

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
    (void)window; (void)index; (void)flags;
    if (g_Soft) return g_Soft;   // one screen
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
    {
        g_TvW = mode.w; g_TvH = mode.h;
    }
    // 1080p: half of it, doubled exactly; 720p and less: 854x480, stretched
    if (g_TvH >= 1000) { g_W = g_TvW / 2; g_H = g_TvH / 2; }
    else { g_W = 854; g_H = 480; }
    if (!gGcmContext || !InitBuffers())
    {
        SDL_Log("PS3 screen: no display buffers");
        return nullptr;
    }
    g_Surface = SDL_CreateRGBSurfaceWithFormat(0, g_W, g_H, 32, SDL_PIXELFORMAT_ARGB8888);
    g_Soft = g_Surface ? SDL_CreateSoftwareRenderer(g_Surface) : nullptr;
    if (!g_Soft)
    {
        SDL_Log("PS3 screen: %s", SDL_GetError());
        return nullptr;
    }
    SDL_Log("PS3 screen: TV %dx%d, the game draws %dx%d in software (own gcm display buffers)", g_TvW, g_TvH, g_W, g_H);
    return g_Soft;
}

extern "C" void ReclawPS3_RenderPresent(SDL_Renderer* renderer)
{
    if (renderer != g_Soft || !g_Soft)
    {
        SDL_RenderPresent(renderer);
        return;
    }
    // the buffer we write into must not be on the screen any more
    if (!g_FirstFlip) WaitFlip();
    g_FirstFlip = false;
    Uint8* dst = (Uint8*)g_Buffer[g_Current];
    if (g_TvW == 2 * g_W && g_TvH == 2 * g_H)
    {
        Double(g_Surface, dst, (int)g_Pitch);
    }
    else if (SDL_Surface* tv = SDL_CreateRGBSurfaceWithFormatFrom(dst, g_TvW, g_TvH, 32, (int)g_Pitch, SDL_PIXELFORMAT_ARGB8888))
    {
        double fit = SDL_min((double)g_TvW / g_W, (double)g_TvH / g_H);
        SDL_Rect r = { 0, 0, (int)(g_W * fit), (int)(g_H * fit) };
        r.x = (g_TvW - r.w) / 2;
        r.y = (g_TvH - r.h) / 2;
        if (r.x > 0 || r.y > 0) SDL_FillRect(tv, NULL, 0);
        SDL_BlitScaled(g_Surface, NULL, tv, &r);
        SDL_FreeSurface(tv);
    }
    Flip(g_Current);
    g_Current ^= 1;
}

extern "C" void ReclawPS3_GameSize(int* w, int* h)
{
    if (w) *w = g_W;
    if (h) *h = g_H;
}
