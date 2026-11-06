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
#include <malloc.h>
#include "../../../OpenClaw/Engine/Util/PortSettings.h"

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

    // RSX present (default; port_settings.txt "ps3_rsx_present=0" for the CPU
    // copy): the game draws into one of two frames in main memory the RSX can
    // read; the RSX scales it into the display buffer itself. The CPU does not
    // copy the 8 MB of a 1080p picture and does not wait for the TV.
    bool g_Rsx = false;
    Uint32* g_Main[2] = { nullptr, nullptr };
    u32 g_MainOffset[2] = { 0, 0 };
    u32 g_MainSeq[2] = { 0, 0 };
    int g_MainIdx = 0;
    u32 g_DisplayOffset[2] = { 0, 0 };
    const u8 kLabel = 200;   // labels 200, 201: transfer of frame 0, 1 done

    void Flip(int buffer);
    void WaitFlip();
    volatile u32* Label(int i) { return (volatile u32*)gcmGetLabelAddress((u8)(kLabel + i)); }

    bool InitRsxFrames()
    {
        const size_t bytes = (size_t)g_W * g_H * 4;
        const size_t size = (bytes + 0xFFFFF) & ~(size_t)0xFFFFF;   // mapped in 1 MB pieces
        for (int i = 0; i < 2; ++i)
        {
            g_Main[i] = (Uint32*)memalign(1 << 20, size);
            if (!g_Main[i]) return false;
            SDL_memset(g_Main[i], 0, size);
            if (gcmMapMainMemory(g_Main[i], (u32)size, &g_MainOffset[i]) != 0) return false;
            *Label(i) = 0;
            g_MainSeq[i] = 0;
        }
        return true;
    }

    // The RSX command buffer back to its start, every frame: it is only 64 KB
    // (SDL's setup) and running past its end froze the console after a few
    // hundred frames (as SDL's own PSL1GHT renderer did). Called when the RSX
    // has run everything written so far.
    void ResetCommandBuffer()
    {
        gcmContextData* c = gGcmContext;
        gcmControlRegister* ctrl = gcmGetControlRegister();
        u32 begin = 0;
        if (!c || !ctrl || rsxAddressToOffset(c->begin, &begin) != 0) return;
        rsxFlushBuffer(c);
        Uint32 t0 = SDL_GetTicks();
        while (ctrl->get != ctrl->put && SDL_GetTicks() - t0 < 100) usleep(20);
        if (ctrl->get != ctrl->put)
        {
            static int s_Logged = 0;
            if (s_Logged++ < 5) SDL_Log("PS3 screen: the RSX is still busy (get %x, put %x)", (unsigned)ctrl->get, (unsigned)ctrl->put);
            return;
        }
        rsxSetJumpCommand(c, begin);   // at the end of what was written: back to the start
        __asm__ volatile("sync" ::: "memory");
        ctrl->put = begin;             // the RSX runs the jump and stops at the start
        t0 = SDL_GetTicks();
        while (ctrl->get != begin && SDL_GetTicks() - t0 < 100) usleep(20);
        c->current = c->begin;
    }

    void RsxPresent()
    {
        // As in PSL1GHT's samples: the last flip done before the next is
        // queued (the display buffer written now is then off the screen).
        if (!g_FirstFlip) WaitFlip();
        g_FirstFlip = false;
        ResetCommandBuffer();
        const int i = g_MainIdx;
        gcmTransferScale scale;
        SDL_memset(&scale, 0, sizeof(scale));
        const bool exact = (g_TvW % g_W) == 0 && (g_TvH % g_H) == 0 && g_TvW / g_W == g_TvH / g_H;
        // Fit with the picture's own proportions (black bars if needed).
        double fit = SDL_min((double)g_TvW / g_W, (double)g_TvH / g_H);
        int outW = (int)(g_W * fit), outH = (int)(g_H * fit);
        int outX = (g_TvW - outW) / 2, outY = (g_TvH - outH) / 2;
        scale.conversion = GCM_TRANSFER_CONVERSION_TRUNCATE;
        scale.format = GCM_TRANSFER_SCALE_FORMAT_A8R8G8B8;
        scale.operation = GCM_TRANSFER_OPERATION_SRCCOPY;
        scale.clipX = (s16)outX; scale.clipY = (s16)outY;
        scale.clipW = (u16)outW; scale.clipH = (u16)outH;
        scale.outX = (s16)outX; scale.outY = (s16)outY;
        scale.outW = (u16)outW; scale.outH = (u16)outH;
        scale.ratioX = (s32)(((long long)g_W << 20) / outW);
        scale.ratioY = (s32)(((long long)g_H << 20) / outH);
        scale.inW = (u16)g_W; scale.inH = (u16)g_H;
        scale.pitch = (u16)(g_W * 4);
        scale.origin = GCM_TRANSFER_ORIGIN_CORNER;
        scale.interp = exact ? GCM_TRANSFER_INTERPOLATOR_NEAREST : GCM_TRANSFER_INTERPOLATOR_LINEAR;
        scale.offset = g_MainOffset[i];
        scale.inX = 0; scale.inY = 0;
        gcmTransferSurface surface;
        SDL_memset(&surface, 0, sizeof(surface));
        surface.format = GCM_TRANSFER_SURFACE_FORMAT_A8R8G8B8;
        surface.pitch = (u16)g_Pitch;
        surface.offset = g_DisplayOffset[g_Current];

        rsxSetTransferScaleMode(gGcmContext, GCM_TRANSFER_MAIN_TO_LOCAL, GCM_TRANSFER_SURFACE);
        rsxSetTransferScaleSurface(gGcmContext, &scale, &surface);
        rsxSetWriteBackendLabel(gGcmContext, (u8)(kLabel + i), ++g_MainSeq[i]);
        Flip(g_Current);   // flip, then the RSX waits for it before the next transfer
        g_Current ^= 1;

        // Draw the next frame into the other one, once the RSX has read it.
        g_MainIdx ^= 1;
        const int n = g_MainIdx;
        const Uint32 t0 = SDL_GetTicks();
        while (*Label(n) != g_MainSeq[n] && SDL_GetTicks() - t0 < 200) usleep(100);
        static int s_Frames = 0, s_Late = 0;
        if (*Label(n) != g_MainSeq[n] && s_Late++ < 5)
            SDL_Log("PS3 screen: frame %d: the RSX has not read frame %d (label %u, want %u)", s_Frames, n,
                    (unsigned)*Label(n), (unsigned)g_MainSeq[n]);
        if (++s_Frames % 600 == 0) SDL_Log("PS3 screen: %d frames shown", s_Frames);
        g_Surface->pixels = g_Main[n];
    }

    void WaitFlip()
    {
        const Uint32 t0 = SDL_GetTicks();
        while (gcmGetFlipStatus() != 0 && SDL_GetTicks() - t0 < 1000) usleep(200);
        if (gcmGetFlipStatus() != 0)
        {
            static int s_Logged = 0;
            gcmControlRegister* ctrl = gcmGetControlRegister();
            if (s_Logged++ < 5) SDL_Log("PS3 screen: no flip for 1 s (RSX get %x, put %x)", ctrl ? (unsigned)ctrl->get : 0u, ctrl ? (unsigned)ctrl->put : 0u);
        }
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
            g_DisplayOffset[i] = offset;
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
    g_Rsx = PortSettings::GetInt("ps3_rsx_present", 1) != 0 && InitRsxFrames();
    if (g_Rsx)
        g_Surface = SDL_CreateRGBSurfaceWithFormatFrom(g_Main[0], g_W, g_H, 32, g_W * 4, SDL_PIXELFORMAT_ARGB8888);
    else
        g_Surface = SDL_CreateRGBSurfaceWithFormat(0, g_W, g_H, 32, SDL_PIXELFORMAT_ARGB8888);
    g_Soft = g_Surface ? SDL_CreateSoftwareRenderer(g_Surface) : nullptr;
    if (!g_Soft)
    {
        SDL_Log("PS3 screen: %s", SDL_GetError());
        return nullptr;
    }
    SDL_Log("PS3 screen: TV %dx%d, the game draws %dx%d in software (own gcm display buffers, %s)", g_TvW, g_TvH, g_W, g_H,
            g_Rsx ? "scaled by the RSX" : "copied by the CPU");
    return g_Soft;
}

extern "C" void ReclawPS3_RenderPresent(SDL_Renderer* renderer)
{
    if (renderer != g_Soft || !g_Soft)
    {
        SDL_RenderPresent(renderer);
        return;
    }
    if (g_Rsx)
    {
        RsxPresent();
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
