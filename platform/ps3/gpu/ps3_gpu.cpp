// ReClaw PS3: the game's drawing on the RSX. See ps3_gpu.h.
//
// - The game's renderer is ours: a pointer to g_RendererTag. With the RSX off
//   (gpu=0) everything goes to SDL and the software screen.
// - Textures: A8R8G8B8 pitch-linear textures in the RSX's memory (the same
//   bytes as SDL's ARGB8888 on the big-endian PPU: rows are copied as they
//   are). Render targets: such a texture, drawn into as a colour surface.
// - Drawing: quads (two triangles) collected per texture and blend mode and
//   drawn with one call; clipping (viewport, clip rect) is done on the CPU.
//   Vertices: two halves of one buffer in the RSX's memory, a frame each.
// - The screen: the TV's display buffers (ps3_sdl_screen.cpp), drawn into
//   directly, flipped at SDL_RenderPresent.
// - The RSX command buffer is SDL's 64 KB: back to its start at every frame,
//   and in the middle of one when it is nearly full.

#define RECLAW_GPU_IMPL
#include "ps3_gpu.h"
#include "../../common/reclaw_bleed.h"
#include "../compat/ps3_sdl_screen.h"

#include <SDL2/SDL.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <vector>

#include "reclaw2d_vpo.h"
#include "reclaw2d_fpo.h"
#include "../../../OpenClaw/Engine/Util/PortSettings.h"

namespace
{
    const uint32_t kMagic = 0x52475055;   // "RGPU"
    const int kMaxVertices = 6 * 20000;
    const u8 kFrameLabel = 210;           // labels 210, 211: the frame using vertex half 0 / 1 is done

    struct GpuTexture
    {
        uint32_t magic = kMagic;
        Uint32 format = SDL_PIXELFORMAT_ARGB8888;
        int access = SDL_TEXTUREACCESS_STATIC;
        int w = 0, h = 0;
        Uint32* data = nullptr;           // RSX memory
        u32 offset = 0, pitch = 0;
        bool target = false;
        SDL_BlendMode blend = SDL_BLENDMODE_NONE;
        Uint8 r = 255, g = 255, b = 255, a = 255;
        bool linear = false;
        bool alias = false;               // the game's frame (ReclawGpu_GameFrame): the screen itself
    };

    struct Vertex
    {
        float x, y, u, v;
        Uint8 r, g, b, a;
    };

    struct View
    {
        SDL_Rect viewport = { 0, 0, 0, 0 };
        bool viewportSet = false;
        SDL_Rect clip = { 0, 0, 0, 0 };
        bool clipOn = false;
        float sx = 1.0f, sy = 1.0f;
    };

    char g_RendererTag;
    SDL_Renderer* const kGpuRenderer = (SDL_Renderer*)&g_RendererTag;

    struct Gpu
    {
        bool active = false;
        SDL_Window* window = nullptr;
        gcmContextData* ctx = nullptr;
        int tvW = 1280, tvH = 720;
        u32 display[2] = { 0, 0 };
        u32 displayPitch = 0;
        int current = 0;                  // display buffer drawn into
        rsxVertexProgram* vp = nullptr;
        rsxFragmentProgram* fp = nullptr;
        void* vpUcode = nullptr;
        u32 fpOffset = 0;
        rsxProgramConst* uScaleBias = nullptr;
        int aPosition = 0, aTexcoord = 0, aColor = 0, texUnit = 0;
        Vertex* vbufBase = nullptr;       // two halves
        u32 vbufOffset = 0;
        int half = 0;
        u32 halfSeq[2] = { 0, 0 };
        Vertex* vbuf = nullptr;           // this frame's half
        int vused = 0, batchStart = 0;
        bool overflowLogged = false;
        int batches = 0, resets = 0;
        // what the pending vertices are drawn with
        GpuTexture* batchTex = nullptr;
        SDL_BlendMode batchBlend = SDL_BLENDMODE_INVALID;
        GpuTexture* boundTex = nullptr;
        SDL_BlendMode boundBlend = SDL_BLENDMODE_INVALID;
        bool boundLinear = false;
        GpuTexture* target = nullptr;
        View mainView, targetView;
        GpuTexture gameFrame;
        bool gameFrameOn = false;
        View gameView;
        Uint8 dr = 0, dg = 0, db = 0, da = 255;
        SDL_BlendMode drawBlend = SDL_BLENDMODE_NONE;
        bool inFrame = false;
        bool smooth = false;              // every picture sampled smoothly (Options)
        bool texDirty = false;            // a texture changed during the frame
        GpuTexture* white = nullptr;
        std::vector<GpuTexture*> doomed[2];   // freed when that half's frame is done
    } g;

    bool IsGpu(SDL_Renderer* r) { return r == kGpuRenderer && g.active; }
    bool IsGpuTex(SDL_Texture* t) { return t && *(const uint32_t*)t == kMagic; }
    GpuTexture* Tex(SDL_Texture* t) { return (GpuTexture*)t; }
    View& CurView() { return g.target ? g.targetView : (g.gameFrameOn ? g.gameView : g.mainView); }

    void TargetSize(int& w, int& h)
    {
        if (g.target) { w = g.target->w; h = g.target->h; }
        else { w = g.tvW; h = g.tvH; }
    }

    volatile u32* Label(int i) { return (volatile u32*)gcmGetLabelAddress((u8)(kFrameLabel + i)); }

    //------------------------------------------------------------- textures
    void Upload(GpuTexture* t, const SDL_Rect* rect, const Uint32* pixels, int pitchPixels)
    {
        SDL_Rect r = rect ? *rect : SDL_Rect{ 0, 0, t->w, t->h };
        if (r.x < 0) { pixels -= r.x; r.w += r.x; r.x = 0; }
        if (r.y < 0) { pixels -= (size_t)r.y * pitchPixels; r.h += r.y; r.y = 0; }
        r.w = std::min(r.w, t->w - r.x);
        r.h = std::min(r.h, t->h - r.y);
        if (r.w <= 0 || r.h <= 0) return;
        if (g.inFrame) g.texDirty = true;
        std::vector<Uint32> bled;
        if (t->access != SDL_TEXTUREACCESS_STREAMING)
        {
            // (for smooth sampling: see-through pixels coloured like their neighbours)
            if (const uint32_t* b = ReclawPreparePicture(pixels, pitchPixels, r.w, r.h, bled)) { pixels = b; pitchPixels = r.w; }
        }
        for (int y = 0; y < r.h; ++y)
            memcpy((Uint8*)t->data + (size_t)(r.y + y) * t->pitch + (size_t)r.x * 4, pixels + (size_t)y * pitchPixels, (size_t)r.w * 4);
    }

    GpuTexture* NewTexture(int w, int h, bool target)
    {
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return nullptr;
        GpuTexture* t = new GpuTexture();
        t->w = w;
        t->h = h;
        t->target = target;
        t->pitch = ((u32)w * 4 + 63) & ~63u;
        t->data = (Uint32*)rsxMemalign(128, (size_t)t->pitch * h);
        if (!t->data || rsxAddressToOffset(t->data, &t->offset) != 0)
        {
            if (t->data) rsxFree(t->data);
            delete t;
            SDL_Log("GPU: no memory for a %dx%d texture", w, h);
            return nullptr;
        }
        memset(t->data, 0, (size_t)t->pitch * h);
        return t;
    }

    void FreeTexture(GpuTexture* t)
    {
        if (t->data) rsxFree(t->data);
        t->magic = 0;
        delete t;
    }

    //------------------------------------------------------------- drawing
    // The command buffer nearly full: back to its start (waits for the RSX;
    // its state stays as it is).
    void Room()
    {
        gcmContextData* c = g.ctx;
        if ((char*)c->end - (char*)c->current < 6 * 1024)
        {
            __asm__ volatile("sync" ::: "memory");   // the vertices and pictures written so far first
            ReclawPS3_ResetCommandBuffer();
            ++g.resets;
        }
    }

    void BindTexture(GpuTexture* t)
    {
        gcmTexture tex;
        memset(&tex, 0, sizeof(tex));
        tex.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
        tex.mipmap = 1;
        tex.dimension = GCM_TEXTURE_DIMS_2D;
        tex.cubemap = GCM_FALSE;
        tex.remap = (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
                    (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
                    (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
                    (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
                    (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
                    (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
                    (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
                    (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT);
        tex.width = (u16)t->w;
        tex.height = (u16)t->h;
        tex.depth = 1;
        tex.location = GCM_LOCATION_RSX;
        tex.pitch = t->pitch;
        tex.offset = t->offset;
        rsxLoadTexture(g.ctx, (u8)g.texUnit, &tex);
        rsxTextureControl(g.ctx, (u8)g.texUnit, GCM_TRUE, 0 << 8, 12 << 8, GCM_TEXTURE_MAX_ANISO_1);
        const bool linear = t->linear || g.smooth;
        const u8 f = linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST;
        rsxTextureFilter(g.ctx, (u8)g.texUnit, 0, f, f, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
        rsxTextureWrapMode(g.ctx, (u8)g.texUnit, GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE,
                           GCM_TEXTURE_UNSIGNED_REMAP_NORMAL, GCM_TEXTURE_ZFUNC_NEVER, 0);
        g.boundTex = t;
        g.boundLinear = linear;
    }

    void BindBlend(SDL_BlendMode blend)
    {
        switch (blend)
        {
            case SDL_BLENDMODE_BLEND:
                rsxSetBlendEnable(g.ctx, GCM_TRUE);
                rsxSetBlendFunc(g.ctx, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_ONE, GCM_ONE_MINUS_SRC_ALPHA);
                break;
            case SDL_BLENDMODE_ADD:
                rsxSetBlendEnable(g.ctx, GCM_TRUE);
                rsxSetBlendFunc(g.ctx, GCM_SRC_ALPHA, GCM_ONE, GCM_ZERO, GCM_ONE);
                break;
            case SDL_BLENDMODE_MOD:
                rsxSetBlendEnable(g.ctx, GCM_TRUE);
                rsxSetBlendFunc(g.ctx, GCM_DST_COLOR, GCM_ZERO, GCM_ZERO, GCM_ONE);
                break;
            default:
                rsxSetBlendEnable(g.ctx, GCM_FALSE);
                break;
        }
        rsxSetBlendEquation(g.ctx, GCM_FUNC_ADD, GCM_FUNC_ADD);
        g.boundBlend = blend;
    }

    void FlushBatch()
    {
        const int count = g.vused - g.batchStart;
        if (count > 0 && g.batchTex)
        {
            Room();
            if (g.texDirty)
            {
                // pictures changed during the frame (a movie, the map's rolls)
                rsxInvalidateTextureCache(g.ctx, GCM_INVALIDATE_TEXTURE);
                g.boundTex = nullptr;
                g.texDirty = false;
            }
            if (g.batchTex != g.boundTex || (g.batchTex->linear || g.smooth) != g.boundLinear) BindTexture(g.batchTex);
            if (g.batchBlend != g.boundBlend) BindBlend(g.batchBlend);
            // (the vertices were written after the last draw: not from the cache)
            rsxInvalidateVertexCache(g.ctx);
            rsxDrawVertexArray(g.ctx, GCM_TYPE_TRIANGLES, (u32)g.batchStart, (u32)count);
            ++g.batches;
        }
        g.batchStart = g.vused;
    }

    // The colour surface and the projection of the current target.
    void BindTarget()
    {
        FlushBatch();
        Room();
        int w, h;
        TargetSize(w, h);
        gcmSurface sf;
        memset(&sf, 0, sizeof(sf));
        sf.colorFormat = GCM_SURFACE_A8R8G8B8;
        sf.colorTarget = GCM_SURFACE_TARGET_0;
        sf.colorLocation[0] = GCM_LOCATION_RSX;
        sf.colorOffset[0] = g.target ? g.target->offset : g.display[g.current];
        sf.colorPitch[0] = g.target ? g.target->pitch : g.displayPitch;
        for (int i = 1; i < 4; ++i) { sf.colorLocation[i] = GCM_LOCATION_RSX; sf.colorOffset[i] = 0; sf.colorPitch[i] = 64; }
        sf.depthFormat = GCM_SURFACE_ZETA_Z16;
        sf.depthLocation = GCM_LOCATION_RSX;
        sf.depthOffset = 0;
        sf.depthPitch = 64;
        sf.type = GCM_SURFACE_TYPE_LINEAR;
        sf.antiAlias = GCM_SURFACE_CENTER_1;
        sf.width = (u16)w;
        sf.height = (u16)h;
        sf.x = 0;
        sf.y = 0;
        rsxSetSurface(g.ctx, &sf);
        f32 scale[4] = { w * 0.5f, h * -0.5f, 0.5f, 0.0f };
        f32 offset[4] = { w * 0.5f, h * 0.5f, 0.5f, 0.0f };
        rsxSetViewport(g.ctx, 0, 0, (u16)w, (u16)h, 0.0f, 1.0f, scale, offset);
        rsxSetScissor(g.ctx, 0, 0, (u16)w, (u16)h);
        // pixels to clip space: x * 2/w - 1, 1 - y * 2/h
        const f32 sb[4] = { 2.0f / w, -2.0f / h, -1.0f, 1.0f };
        rsxSetVertexProgramParameter(g.ctx, g.vp, g.uScaleBias, sb);
    }

    void EnsureFrame()
    {
        if (g.inFrame) return;
        // the display buffer drawn into must be off the screen, the vertex
        // half and the textures freed with it no longer used by the RSX
        ReclawPS3_GpuWaitFlip();
        ReclawPS3_ResetCommandBuffer();
        g.half ^= 1;
        const Uint32 t0 = SDL_GetTicks();
        while (*Label(g.half) != g.halfSeq[g.half] && SDL_GetTicks() - t0 < 200) usleep(50);
        for (GpuTexture* t : g.doomed[g.half]) FreeTexture(t);
        g.doomed[g.half].clear();
        g.vbuf = g.vbufBase + (size_t)g.half * kMaxVertices;
        g.inFrame = true;
        g.vused = g.batchStart = 0;
        g.batchTex = nullptr;
        g.batchBlend = SDL_BLENDMODE_INVALID;
        g.boundTex = nullptr;
        g.boundBlend = SDL_BLENDMODE_INVALID;

        gcmContextData* c = g.ctx;
        rsxSetColorMask(c, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
        rsxSetColorMaskMrt(c, 0);
        rsxSetDepthTestEnable(c, GCM_FALSE);
        rsxSetDepthWriteEnable(c, GCM_FALSE);
        rsxSetCullFaceEnable(c, GCM_FALSE);
        rsxSetShadeModel(c, GCM_SHADE_MODEL_SMOOTH);
        rsxSetZMinMaxControl(c, 0, 1, 1);
        rsxLoadVertexProgram(c, g.vp, g.vpUcode);
        rsxLoadFragmentProgramLocation(c, g.fp, g.fpOffset, GCM_LOCATION_RSX);
        rsxInvalidateTextureCache(c, GCM_INVALIDATE_TEXTURE);
        const u32 base = g.vbufOffset + (u32)((size_t)g.half * kMaxVertices * sizeof(Vertex));
        rsxBindVertexArrayAttrib(c, (u8)g.aPosition, 0, base + offsetof(Vertex, x), sizeof(Vertex), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(c, (u8)g.aTexcoord, 0, base + offsetof(Vertex, u), sizeof(Vertex), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(c, (u8)g.aColor, 0, base + offsetof(Vertex, r), sizeof(Vertex), 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_RSX);
        rsxInvalidateVertexCache(c);
        BindTarget();
    }

    void SetBatch(GpuTexture* tex, SDL_BlendMode blend)
    {
        if (tex != g.batchTex || blend != g.batchBlend)
        {
            FlushBatch();
            g.batchTex = tex;
            g.batchBlend = blend;
        }
    }

    bool Room(int vertices)
    {
        if (g.vused + vertices <= kMaxVertices) return true;
        if (!g.overflowLogged) { SDL_Log("GPU: too many drawings in one frame"); g.overflowLogged = true; }
        return false;
    }
    // The visible area of the target in its pixels (viewport and clip).
    SDL_Rect ClipArea()
    {
        int tw, th;
        TargetSize(tw, th);
        const View& v = CurView();
        float x0 = 0, y0 = 0, x1 = (float)tw, y1 = (float)th;
        if (v.viewportSet)
        {
            x0 = std::max(x0, v.viewport.x * v.sx); y0 = std::max(y0, v.viewport.y * v.sy);
            x1 = std::min(x1, (v.viewport.x + v.viewport.w) * v.sx); y1 = std::min(y1, (v.viewport.y + v.viewport.h) * v.sy);
        }
        if (v.clipOn)
        {
            const float ox = v.viewportSet ? v.viewport.x : 0, oy = v.viewportSet ? v.viewport.y : 0;
            x0 = std::max(x0, (ox + v.clip.x) * v.sx); y0 = std::max(y0, (oy + v.clip.y) * v.sy);
            x1 = std::min(x1, (ox + v.clip.x + v.clip.w) * v.sx); y1 = std::min(y1, (oy + v.clip.y + v.clip.h) * v.sy);
        }
        SDL_Rect r = { (int)std::floor(x0), (int)std::floor(y0), 0, 0 };
        r.w = std::max(0, (int)std::ceil(x1) - r.x);
        r.h = std::max(0, (int)std::ceil(y1) - r.y);
        return r;
    }

    // Logical (game) coordinates to target pixels.
    inline void ToPixels(float& x, float& y)
    {
        const View& v = CurView();
        const float ox = v.viewportSet ? v.viewport.x : 0, oy = v.viewportSet ? v.viewport.y : 0;
        x = (ox + x) * v.sx;
        y = (oy + y) * v.sy;
    }

    inline void Put(float x, float y, float u, float v, Uint8 r, Uint8 gg, Uint8 b, Uint8 a)
    {
        Vertex& vx = g.vbuf[g.vused++];
        vx.x = x; vx.y = y; vx.u = u; vx.v = v;
        vx.r = r; vx.g = gg; vx.b = b; vx.a = a;
    }

    // An axis-aligned quad in target pixels, clipped; u/v at its corners.
    void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              Uint8 r, Uint8 gg, Uint8 b, Uint8 a, const SDL_Rect& clip)
    {
        if (x1 <= x0 || y1 <= y0) return;
        const float cx0 = (float)clip.x, cy0 = (float)clip.y, cx1 = (float)(clip.x + clip.w), cy1 = (float)(clip.y + clip.h);
        if (x1 <= cx0 || y1 <= cy0 || x0 >= cx1 || y0 >= cy1) return;
        const float du = (u1 - u0) / (x1 - x0), dv = (v1 - v0) / (y1 - y0);
        if (x0 < cx0) { u0 += (cx0 - x0) * du; x0 = cx0; }
        if (x1 > cx1) { u1 -= (x1 - cx1) * du; x1 = cx1; }
        if (y0 < cy0) { v0 += (cy0 - y0) * dv; y0 = cy0; }
        if (y1 > cy1) { v1 -= (y1 - cy1) * dv; y1 = cy1; }
        if (!Room(6)) return;
        Put(x0, y0, u0, v0, r, gg, b, a);
        Put(x0, y1, u0, v1, r, gg, b, a);
        Put(x1, y0, u1, v0, r, gg, b, a);
        Put(x1, y0, u1, v0, r, gg, b, a);
        Put(x0, y1, u0, v1, r, gg, b, a);
        Put(x1, y1, u1, v1, r, gg, b, a);
    }

    // The texture coordinate of picture column x / row y (the first row in
    // memory is the top, v = 0).
    inline float U(const GpuTexture* t, float x) { return x / t->w; }
    inline float V(const GpuTexture* t, float y) { return y / t->h; }

    void FillPixels(float x0, float y0, float x1, float y1)
    {
        EnsureFrame();
        SetBatch(g.white, g.drawBlend);
        const SDL_Rect clip = ClipArea();
        Quad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, g.dr, g.dg, g.db, g.da, clip);
    }

    void FillLogical(float x, float y, float w, float h)
    {
        float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
        ToPixels(x0, y0);
        ToPixels(x1, y1);
        FillPixels(x0, y0, x1, y1);
    }

    int Copy(GpuTexture* t, const SDL_Rect* srcrect, const SDL_Rect* dstrect, double angle, const SDL_Point* center, SDL_RendererFlip flip)
    {
        EnsureFrame();
        SDL_Rect src = srcrect ? *srcrect : SDL_Rect{ 0, 0, t->w, t->h };
        // (source outside the picture: cut, the destination with it)
        int tw, th;
        TargetSize(tw, th);
        SDL_Rect dst = dstrect ? *dstrect : SDL_Rect{ 0, 0, 0, 0 };
        if (!dstrect)
        {
            const View& v = CurView();
            dst.w = v.viewportSet ? v.viewport.w : (int)(tw / v.sx);
            dst.h = v.viewportSet ? v.viewport.h : (int)(th / v.sy);
        }
        if (src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) return 0;

        float X0 = (float)dst.x, Y0 = (float)dst.y, X1 = (float)(dst.x + dst.w), Y1 = (float)(dst.y + dst.h);
        ToPixels(X0, Y0);
        ToPixels(X1, Y1);
        const float sx = (X1 - X0) / src.w, sy = (Y1 - Y0) / src.h;
        const bool flipH = (flip & SDL_FLIP_HORIZONTAL) != 0, flipV = (flip & SDL_FLIP_VERTICAL) != 0;
        const bool rotated = std::fabs(angle) > 0.001;
        const SDL_Rect clip = ClipArea();
        float pcx = 0, pcy = 0, ca = 1, sa = 0;
        if (rotated)
        {
            float cx = center ? (float)(dst.x + center->x) : (float)dst.x + dst.w / 2.0f;
            float cy = center ? (float)(dst.y + center->y) : (float)dst.y + dst.h / 2.0f;
            ToPixels(cx, cy);
            pcx = cx; pcy = cy;
            const double rad = angle * M_PI / 180.0;
            ca = (float)std::cos(rad);
            sa = (float)std::sin(rad);
        }
        const SDL_BlendMode blend = t->blend;
        // (source outside the picture: cut, the destination with it)
        const int x0 = std::max(src.x, 0), y0 = std::max(src.y, 0);
        const int x1 = std::min(src.x + src.w, t->w), y1 = std::min(src.y + src.h, t->h);
        if (x1 <= x0 || y1 <= y0) return 0;
        // where it goes (flips mirror the position within the source)
        const float lx0 = flipH ? (src.x + src.w - x1) : (x0 - src.x);
        const float lx1 = flipH ? (src.x + src.w - x0) : (x1 - src.x);
        const float ly0 = flipV ? (src.y + src.h - y1) : (y0 - src.y);
        const float ly1 = flipV ? (src.y + src.h - y0) : (y1 - src.y);
        float qx0 = X0 + lx0 * sx, qx1 = X0 + lx1 * sx, qy0 = Y0 + ly0 * sy, qy1 = Y0 + ly1 * sy;
        float u0 = U(t, (float)x0), u1 = U(t, (float)x1), v0 = V(t, (float)y0), v1 = V(t, (float)y1);
        if (flipH) std::swap(u0, u1);
        if (flipV) std::swap(v0, v1);
        SetBatch(t, blend);
        if (!rotated)
        {
            Quad(qx0, qy0, qx1, qy1, u0, v0, u1, v1, t->r, t->g, t->b, t->a, clip);
            return 0;
        }
        if (!Room(6)) return 0;
        auto rot = [&](float x, float y, float u, float v) {
            const float dx = x - pcx, dy = y - pcy;
            Put(pcx + dx * ca - dy * sa, pcy + dx * sa + dy * ca, u, v, t->r, t->g, t->b, t->a);
        };
        rot(qx0, qy0, u0, v0); rot(qx0, qy1, u0, v1); rot(qx1, qy0, u1, v0);
        rot(qx1, qy0, u1, v0); rot(qx0, qy1, u0, v1); rot(qx1, qy1, u1, v1);
        return 0;
    }

    //------------------------------------------------------------- start
    bool Start(SDL_Window* window)
    {
        g.ctx = gGcmContext;
        unsigned int offsets[2] = { 0, 0 }, pitch = 0;
        if (!g.ctx || !ReclawPS3_GpuDisplay(offsets, &pitch)) return false;
        g.display[0] = offsets[0];
        g.display[1] = offsets[1];
        g.displayPitch = pitch;
        ReclawPS3_TvSize(&g.tvW, &g.tvH);
        g.current = 0;

        g.vp = (rsxVertexProgram*)reclaw2d_vpo;
        g.fp = (rsxFragmentProgram*)reclaw2d_fpo;
        u32 size = 0;
        rsxVertexProgramGetUCode(g.vp, &g.vpUcode, &size);
        void* fpUcode = nullptr;
        rsxFragmentProgramGetUCode(g.fp, &fpUcode, &size);
        void* fpCopy = rsxMemalign(64, size);
        if (!fpCopy) return false;
        memcpy(fpCopy, fpUcode, size);
        rsxAddressToOffset(fpCopy, &g.fpOffset);
        g.uScaleBias = rsxVertexProgramGetConst(g.vp, "scaleBias");
        rsxProgramAttrib* a;
        if (!(a = rsxVertexProgramGetAttrib(g.vp, "position"))) return false;
        g.aPosition = a->index;
        if (!(a = rsxVertexProgramGetAttrib(g.vp, "texcoord"))) return false;
        g.aTexcoord = a->index;
        if (!(a = rsxVertexProgramGetAttrib(g.vp, "color"))) return false;
        g.aColor = a->index;
        if (!(a = rsxFragmentProgramGetAttrib(g.fp, "texture"))) return false;
        g.texUnit = a->index;
        if (!g.uScaleBias) return false;

        g.vbufBase = (Vertex*)rsxMemalign(128, sizeof(Vertex) * kMaxVertices * 2);
        if (!g.vbufBase || rsxAddressToOffset(g.vbufBase, &g.vbufOffset) != 0) return false;
        for (int i = 0; i < 2; ++i) { *Label(i) = 0; g.halfSeq[i] = 0; }

        g.white = NewTexture(8, 8, false);
        if (!g.white) return false;
        std::vector<Uint32> white(64, 0xFFFFFFFFu);
        Upload(g.white, nullptr, white.data(), 8);

        g.window = window;
        g.active = true;
        SDL_Log("GPU: the game draws with the RSX at %dx%d", g.tvW, g.tvH);
        return true;
    }
}

extern "C" {

int ReclawGpu_Active(void) { return g.active ? 1 : 0; }

void ReclawGpu_SetEye(int eye) { (void)eye; }

SDL_Renderer* ReclawGpu_CreateRenderer(SDL_Window* window, int index, Uint32 flags)
{
    // the first renderer is the top screen's (the game's); later ones (the
    // bottom screen) stay with SDL
    if (!g.active && !g.window && window && PortSettings::GetInt("gpu", 1) != 0)
    {
        g.window = window;
        if (Start(window)) return kGpuRenderer;
        SDL_Log("GPU: could not start the RSX drawing - software drawing");
        g.active = false;
    }
    return ReclawPS3_CreateRenderer(window, index, flags);
}

void ReclawGpu_DestroyRenderer(SDL_Renderer* renderer)
{
    if (renderer == kGpuRenderer) return;   // lives as long as the game
    SDL_DestroyRenderer(renderer);
}

int ReclawGpu_GetRendererOutputSize(SDL_Renderer* renderer, int* w, int* h)
{
    if (!IsGpu(renderer)) return SDL_GetRendererOutputSize(renderer, w, h);
    int tw, th;
    TargetSize(tw, th);
    if (w) *w = tw;
    if (h) *h = th;
    return 0;
}

void ReclawGpu_RenderPresent(SDL_Renderer* renderer);

int ReclawGpu_RenderClear(SDL_Renderer* renderer)
{
    if (!IsGpu(renderer)) return SDL_RenderClear(renderer);
    EnsureFrame();
    FlushBatch();
    Room();
    // the whole target (like SDL: viewport and clip are not used)
    rsxSetClearColor(g.ctx, ((u32)g.da << 24) | ((u32)g.dr << 16) | ((u32)g.dg << 8) | g.db);
    rsxClearSurface(g.ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
    return 0;
}

void ReclawGpu_RenderPresent(SDL_Renderer* renderer)
{
    if (!IsGpu(renderer)) { ReclawPS3_RenderPresent(renderer); return; }
    if (!g.inFrame) return;
    if (g.target) { g.target = nullptr; BindTarget(); }
    FlushBatch();
    Room();
    const int vertices = g.vused, batches = g.batches, resets = g.resets;
    g.batches = 0;
    g.resets = 0;
    rsxSetWriteBackendLabel(g.ctx, (u8)(kFrameLabel + g.half), ++g.halfSeq[g.half]);
    __asm__ volatile("sync" ::: "memory");   // the vertices and pictures written first
    ReclawPS3_GpuFlip(g.current);
    g.current ^= 1;
    g.inFrame = false;
    static int s_Frames = 0;
    if (++s_Frames % 300 == 0)
        SDL_Log("GPU: last frame %d vertices in %d draws (%d command buffer restarts)", vertices / 6 * 6, batches, resets);
}

int ReclawGpu_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 gg, Uint8 b, Uint8 a)
{
    if (!IsGpu(renderer)) return SDL_SetRenderDrawColor(renderer, r, gg, b, a);
    g.dr = r; g.dg = gg; g.db = b; g.da = a;
    return 0;
}

int ReclawGpu_GetRenderDrawColor(SDL_Renderer* renderer, Uint8* r, Uint8* gg, Uint8* b, Uint8* a)
{
    if (!IsGpu(renderer)) return SDL_GetRenderDrawColor(renderer, r, gg, b, a);
    if (r) *r = g.dr;
    if (gg) *gg = g.dg;
    if (b) *b = g.db;
    if (a) *a = g.da;
    return 0;
}

int ReclawGpu_SetRenderDrawBlendMode(SDL_Renderer* renderer, SDL_BlendMode mode)
{
    if (!IsGpu(renderer)) return SDL_SetRenderDrawBlendMode(renderer, mode);
    g.drawBlend = mode;
    return 0;
}

int ReclawGpu_RenderFillRect(SDL_Renderer* renderer, const SDL_Rect* rect)
{
    if (!IsGpu(renderer)) return SDL_RenderFillRect(renderer, rect);
    if (rect) FillLogical((float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h);
    else
    {
        int tw, th;
        TargetSize(tw, th);
        const View& v = CurView();
        FillLogical(0, 0, v.viewportSet ? v.viewport.w : tw / v.sx, v.viewportSet ? v.viewport.h : th / v.sy);
    }
    return 0;
}

int ReclawGpu_RenderDrawRect(SDL_Renderer* renderer, const SDL_Rect* rect)
{
    if (!IsGpu(renderer)) return SDL_RenderDrawRect(renderer, rect);
    if (!rect) return 0;
    const float x = (float)rect->x, y = (float)rect->y, w = (float)rect->w, h = (float)rect->h;
    FillLogical(x, y, w, 1);
    FillLogical(x, y + h - 1, w, 1);
    FillLogical(x, y, 1, h);
    FillLogical(x + w - 1, y, 1, h);
    return 0;
}

int ReclawGpu_RenderDrawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2)
{
    if (!IsGpu(renderer)) return SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    if (y1 == y2) { FillLogical((float)std::min(x1, x2), (float)y1, (float)(std::abs(x2 - x1) + 1), 1); return 0; }
    if (x1 == x2) { FillLogical((float)x1, (float)std::min(y1, y2), 1, (float)(std::abs(y2 - y1) + 1)); return 0; }
    // a slanted line: a thin quad
    EnsureFrame();
    float ax = (float)x1 + 0.5f, ay = (float)y1 + 0.5f, bx = (float)x2 + 0.5f, by = (float)y2 + 0.5f;
    ToPixels(ax, ay);
    ToPixels(bx, by);
    const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f || !Room(6)) return 0;
    const View& v = CurView();
    const float half = 0.5f * std::max(1.0f, v.sx);
    const float nx = -dy / len * half, ny = dx / len * half;
    SetBatch(g.white, g.drawBlend);
    Put(ax + nx, ay + ny, 0, 1, g.dr, g.dg, g.db, g.da);
    Put(ax - nx, ay - ny, 0, 0, g.dr, g.dg, g.db, g.da);
    Put(bx + nx, by + ny, 1, 1, g.dr, g.dg, g.db, g.da);
    Put(bx + nx, by + ny, 1, 1, g.dr, g.dg, g.db, g.da);
    Put(ax - nx, ay - ny, 0, 0, g.dr, g.dg, g.db, g.da);
    Put(bx - nx, by - ny, 1, 0, g.dr, g.dg, g.db, g.da);
    return 0;
}

int ReclawGpu_RenderDrawLines(SDL_Renderer* renderer, const SDL_Point* points, int count)
{
    if (!IsGpu(renderer)) return SDL_RenderDrawLines(renderer, points, count);
    for (int i = 0; i + 1 < count; ++i) ReclawGpu_RenderDrawLine(renderer, points[i].x, points[i].y, points[i + 1].x, points[i + 1].y);
    return 0;
}

int ReclawGpu_RenderDrawPoint(SDL_Renderer* renderer, int x, int y)
{
    if (!IsGpu(renderer)) return SDL_RenderDrawPoint(renderer, x, y);
    FillLogical((float)x, (float)y, 1, 1);
    return 0;
}

int ReclawGpu_RenderSetScale(SDL_Renderer* renderer, float sx, float sy)
{
    if (!IsGpu(renderer)) return SDL_RenderSetScale(renderer, sx, sy);
    CurView().sx = sx > 0 ? sx : 1.0f;
    CurView().sy = sy > 0 ? sy : 1.0f;
    return 0;
}

void ReclawGpu_RenderGetScale(SDL_Renderer* renderer, float* sx, float* sy)
{
    if (!IsGpu(renderer)) { SDL_RenderGetScale(renderer, sx, sy); return; }
    if (sx) *sx = CurView().sx;
    if (sy) *sy = CurView().sy;
}

int ReclawGpu_RenderSetViewport(SDL_Renderer* renderer, const SDL_Rect* rect)
{
    if (!IsGpu(renderer)) return SDL_RenderSetViewport(renderer, rect);
    View& v = CurView();
    if (rect) { v.viewport = *rect; v.viewportSet = true; }
    else v.viewportSet = false;
    return 0;
}

void ReclawGpu_RenderGetViewport(SDL_Renderer* renderer, SDL_Rect* rect)
{
    if (!IsGpu(renderer)) { SDL_RenderGetViewport(renderer, rect); return; }
    if (!rect) return;
    const View& v = CurView();
    if (v.viewportSet) { *rect = v.viewport; return; }
    int tw, th;
    TargetSize(tw, th);
    *rect = SDL_Rect{ 0, 0, (int)(tw / v.sx), (int)(th / v.sy) };
}

int ReclawGpu_RenderSetClipRect(SDL_Renderer* renderer, const SDL_Rect* rect)
{
    if (!IsGpu(renderer)) return SDL_RenderSetClipRect(renderer, rect);
    View& v = CurView();
    if (rect) { v.clip = *rect; v.clipOn = true; }
    else v.clipOn = false;
    return 0;
}

void ReclawGpu_RenderGetClipRect(SDL_Renderer* renderer, SDL_Rect* rect)
{
    if (!IsGpu(renderer)) { SDL_RenderGetClipRect(renderer, rect); return; }
    if (!rect) return;
    const View& v = CurView();
    *rect = v.clipOn ? v.clip : SDL_Rect{ 0, 0, 0, 0 };
}

SDL_bool ReclawGpu_RenderIsClipEnabled(SDL_Renderer* renderer)
{
    if (!IsGpu(renderer)) return SDL_RenderIsClipEnabled(renderer);
    return CurView().clipOn ? SDL_TRUE : SDL_FALSE;
}

int ReclawGpu_SetRenderTarget(SDL_Renderer* renderer, SDL_Texture* texture)
{
    if (!IsGpu(renderer)) return SDL_SetRenderTarget(renderer, texture);
    GpuTexture* t = IsGpuTex(texture) ? Tex(texture) : nullptr;
    // The game's frame: the screen with the game's view (kept between uses,
    // as the game expects of its own render target).
    const bool frame = t && t->alias;
    if (frame) t = nullptr;
    if (texture && !frame && (!t || !t->target)) return SDL_SetError("not a render target");
    g.gameFrameOn = frame;
    if (t == g.target) return 0;
    if (g.inFrame) FlushBatch();
    g.target = t;
    if (t) g.targetView = View();   // a fresh view for the target, like SDL
    if (g.inFrame)
    {
        BindTarget();
        // a target drawn into and then sampled: the texture cache forgets it
        rsxInvalidateTextureCache(g.ctx, GCM_INVALIDATE_TEXTURE);
        g.boundTex = nullptr;
    }
    return 0;
}

SDL_Texture* ReclawGpu_GetRenderTarget(SDL_Renderer* renderer)
{
    if (!IsGpu(renderer)) return SDL_GetRenderTarget(renderer);
    if (!g.target && g.gameFrameOn) return (SDL_Texture*)&g.gameFrame;
    return (SDL_Texture*)g.target;
}

SDL_Texture* ReclawGpu_GameFrame(void)
{
    if (!g.active) return nullptr;
    g.gameFrame.alias = true;
    g.gameFrame.access = SDL_TEXTUREACCESS_TARGET;
    g.gameFrame.w = g.tvW;
    g.gameFrame.h = g.tvH;
    return (SDL_Texture*)&g.gameFrame;
}

int ReclawGpu_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect)
{
    if (!IsGpu(renderer)) return SDL_RenderCopy(renderer, texture, srcrect, dstrect);
    if (!IsGpuTex(texture)) return -1;
    return Copy(Tex(texture), srcrect, dstrect, 0.0, nullptr, SDL_FLIP_NONE);
}

int ReclawGpu_RenderCopyEx(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect,
                           const double angle, const SDL_Point* center, const SDL_RendererFlip flip)
{
    if (!IsGpu(renderer)) return SDL_RenderCopyEx(renderer, texture, srcrect, dstrect, angle, center, flip);
    if (!IsGpuTex(texture)) return -1;
    return Copy(Tex(texture), srcrect, dstrect, angle, center, flip);
}

int ReclawGpu_RenderReadPixels(SDL_Renderer* renderer, const SDL_Rect* rect, Uint32 format, void* pixels, int pitch)
{
    if (!IsGpu(renderer)) return SDL_RenderReadPixels(renderer, rect, format, pixels, pitch);
    return SDL_SetError("GPU: cannot read the screen");
}

SDL_Texture* ReclawGpu_CreateTexture(SDL_Renderer* renderer, Uint32 format, int access, int w, int h)
{
    if (!IsGpu(renderer)) return SDL_CreateTexture(renderer, format, access, w, h);
    if (format != SDL_PIXELFORMAT_ARGB8888 && format != SDL_PIXELFORMAT_RGBA8888 && format != SDL_PIXELFORMAT_ABGR8888)
    {
        SDL_SetError("GPU: texture format not supported");
        return nullptr;
    }
    GpuTexture* t = NewTexture(w, h, access == SDL_TEXTUREACCESS_TARGET);
    if (!t) return nullptr;
    t->format = format;
    t->access = access;
    t->blend = SDL_BLENDMODE_NONE;
    return (SDL_Texture*)t;
}

SDL_Texture* ReclawGpu_CreateTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface)
{
    if (!IsGpu(renderer)) return SDL_CreateTextureFromSurface(renderer, surface);
    if (!surface) return nullptr;
    // like SDL: a colour key becomes transparency
    const bool keyed = SDL_HasColorKey(surface) == SDL_TRUE;
    const bool hasAlpha = surface->format->Amask != 0 || keyed;
    SDL_Surface* argb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!argb) return nullptr;
    GpuTexture* t = NewTexture(argb->w, argb->h, false);
    if (t)
    {
        Upload(t, nullptr, (const Uint32*)argb->pixels, argb->pitch / 4);
        t->format = SDL_PIXELFORMAT_ARGB8888;
        t->blend = hasAlpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
        Uint8 am = 255, cr = 255, cg = 255, cb = 255;
        SDL_GetSurfaceAlphaMod(surface, &am);
        SDL_GetSurfaceColorMod(surface, &cr, &cg, &cb);
        t->a = am; t->r = cr; t->g = cg; t->b = cb;
    }
    SDL_FreeSurface(argb);
    return (SDL_Texture*)t;
}

void ReclawGpu_DestroyTexture(SDL_Texture* texture)
{
    if (!IsGpuTex(texture)) { if (texture) SDL_DestroyTexture(texture); return; }
    GpuTexture* t = Tex(texture);
    if (t->alias) return;
    if (g.target == t) ReclawGpu_SetRenderTarget(kGpuRenderer, nullptr);
    // Freed when the RSX is done with every frame that may have used it
    if (g.inFrame) FlushBatch();
    t->magic = 0;
    if (g.batchTex == t) g.batchTex = nullptr;
    if (g.boundTex == t) g.boundTex = nullptr;
    // (the frame being drawn, or the last one shown, used g.half; that half
    // comes round again once the RSX has finished it)
    g.doomed[g.half].push_back(t);
}

int ReclawGpu_QueryTexture(SDL_Texture* texture, Uint32* format, int* access, int* w, int* h)
{
    if (!IsGpuTex(texture)) return SDL_QueryTexture(texture, format, access, w, h);
    GpuTexture* t = Tex(texture);
    if (format) *format = t->format;
    if (access) *access = t->access;
    if (w) *w = t->w;
    if (h) *h = t->h;
    return 0;
}

int ReclawGpu_SetTextureAlphaMod(SDL_Texture* texture, Uint8 alpha)
{
    if (!IsGpuTex(texture)) return SDL_SetTextureAlphaMod(texture, alpha);
    Tex(texture)->a = alpha;
    return 0;
}

int ReclawGpu_GetTextureAlphaMod(SDL_Texture* texture, Uint8* alpha)
{
    if (!IsGpuTex(texture)) return SDL_GetTextureAlphaMod(texture, alpha);
    if (alpha) *alpha = Tex(texture)->a;
    return 0;
}

int ReclawGpu_SetTextureColorMod(SDL_Texture* texture, Uint8 r, Uint8 gg, Uint8 b)
{
    if (!IsGpuTex(texture)) return SDL_SetTextureColorMod(texture, r, gg, b);
    GpuTexture* t = Tex(texture);
    t->r = r; t->g = gg; t->b = b;
    return 0;
}

int ReclawGpu_GetTextureColorMod(SDL_Texture* texture, Uint8* r, Uint8* gg, Uint8* b)
{
    if (!IsGpuTex(texture)) return SDL_GetTextureColorMod(texture, r, gg, b);
    GpuTexture* t = Tex(texture);
    if (r) *r = t->r;
    if (gg) *gg = t->g;
    if (b) *b = t->b;
    return 0;
}

int ReclawGpu_SetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode mode)
{
    if (!IsGpuTex(texture)) return SDL_SetTextureBlendMode(texture, mode);
    Tex(texture)->blend = mode;
    return 0;
}

int ReclawGpu_GetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode* mode)
{
    if (!IsGpuTex(texture)) return SDL_GetTextureBlendMode(texture, mode);
    if (mode) *mode = Tex(texture)->blend;
    return 0;
}

void ReclawGpu_SetSmooth(int on)
{
    g.smooth = on != 0;   // (the next draw binds its texture again if it changed)
}

int ReclawGpu_SetTextureScaleMode(SDL_Texture* texture, SDL_ScaleMode mode)
{
    if (!IsGpuTex(texture)) return SDL_SetTextureScaleMode(texture, mode);
    GpuTexture* t = Tex(texture);
    t->linear = mode != SDL_ScaleModeNearest;
    return 0;
}

int ReclawGpu_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch)
{
    if (!IsGpuTex(texture)) return SDL_UpdateTexture(texture, rect, pixels, pitch);
    GpuTexture* t = Tex(texture);
    if (t->target || !pixels) return -1;
    if (t->format != SDL_PIXELFORMAT_ARGB8888)
    {
        // RGBA / ABGR pixels: to ARGB first
        const SDL_Rect r = rect ? *rect : SDL_Rect{ 0, 0, t->w, t->h };
        std::vector<Uint32> argb((size_t)r.w * r.h);
        if (SDL_ConvertPixels(r.w, r.h, t->format, pixels, pitch, SDL_PIXELFORMAT_ARGB8888, argb.data(), r.w * 4) != 0) return -1;
        Upload(t, &r, argb.data(), r.w);
        return 0;
    }
    Upload(t, rect, (const Uint32*)pixels, pitch / 4);
    return 0;
}

int ReclawGpu_UpdateYUVTexture(SDL_Texture* texture, const SDL_Rect* rect, const Uint8* Y, int Yp, const Uint8* U, int Up, const Uint8* V, int Vp)
{
    if (!IsGpuTex(texture)) return SDL_UpdateYUVTexture(texture, rect, Y, Yp, U, Up, V, Vp);
    return SDL_SetError("GPU: no YUV textures");
}

}   // extern "C"
