// ReClaw 3DS: the game's drawing on the GPU (citro3d). See n3ds_gpu.h.
//
// - The top screen's renderer is ours: a pointer to g_RendererTag. Every
//   other renderer (the bottom screen) and its textures go to SDL.
// - Textures: power-of-two GPU textures in linear memory (pictures over
//   512 px are split into pieces), 16-bit where the picture allows it
//   (opaque: RGB565, on/off transparency: RGBA5551, else RGBA8).
//   Render targets: one texture in VRAM.
// - Drawing: quads (two triangles) collected per texture and blend mode and
//   drawn with one call; clipping (viewport, clip rect) is done on the CPU.
// - A frame starts at the first drawing after SDL_RenderPresent and ends
//   (C3D_FrameEnd) at SDL_RenderPresent. Textures destroyed during a frame
//   are freed when the GPU is done with it.
//
// Texture memory: the first row in memory is the picture's top (texture
// coordinate v = 1), so a picture's row y is stored at row y and sampled at
// v = 1 - y / potH. Render targets drawn with a y-down projection come out the
// same way, so pictures and targets are sampled alike. (Checked on a New 3DS:
// storing rows bottom-up showed every picture upside down and pictures
// smaller than their texture only as a strip.)

#define RECLAW_GPU_IMPL
#include "n3ds_gpu.h"

#include <3ds.h>
#include <citro3d.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "reclaw2d_shbin.h"
#include "../../../OpenClaw/Engine/Util/PortSettings.h"

namespace
{
    const uint32_t kMagic = 0x52475055;   // "RGPU"
    const int kMaxChunk = 512;
    const int kScreenW = 400, kScreenH = 240;
    const int kMaxVertices = 6 * 12000;
    const u32 kDisplayTransferFlags =
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    struct Chunk
    {
        C3D_Tex tex;
        int x = 0, y = 0, w = 0, h = 0;   // part of the picture
    };

    struct GpuTexture
    {
        uint32_t magic = kMagic;
        Uint32 format = SDL_PIXELFORMAT_ARGB8888;
        int access = SDL_TEXTUREACCESS_STATIC;
        int w = 0, h = 0;
        std::vector<Chunk> chunks;
        C3D_RenderTarget* target = nullptr;
        SDL_BlendMode blend = SDL_BLENDMODE_NONE;
        Uint8 r = 255, g = 255, b = 255, a = 255;
        bool linear = false;
        bool alias = false;   // the game's frame (ReclawGpu_GameFrame): the screen itself
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
        C3D_RenderTarget* screen[2] = { nullptr, nullptr };
        int eye = 0;
        DVLB_s* dvlb = nullptr;
        shaderProgram_s program;
        int uProjection = -1;
        Vertex* vbuf = nullptr;
        int vused = 0, batchStart = 0;
        bool overflowLogged = false, cmdFullLogged = false, unpresentedLogged = false;
        int batches = 0;
        // what the pending vertices are drawn with
        C3D_Tex* batchTex = nullptr;
        SDL_BlendMode batchBlend = SDL_BLENDMODE_INVALID;
        GpuTexture* target = nullptr;
        bool targetBound = false;
        View mainView, targetView;
        // The game's frame is selected (ReclawGpu_GameFrame): drawing goes to
        // the screen, with the game's own scale and viewport (gameView).
        GpuTexture gameFrame;
        bool gameFrameOn = false;
        View gameView;
        Uint8 dr = 0, dg = 0, db = 0, da = 255;
        SDL_BlendMode drawBlend = SDL_BLENDMODE_NONE;
        bool inFrame = false;
        GpuTexture* white = nullptr;
        std::vector<GpuTexture*> doomed;
    } g;

    bool IsGpu(SDL_Renderer* r) { return r == kGpuRenderer && g.active; }
    bool IsGpuTex(SDL_Texture* t) { return t && *(const uint32_t*)t == kMagic; }
    GpuTexture* Tex(SDL_Texture* t) { return (GpuTexture*)t; }
    View& CurView() { return g.target ? g.targetView : (g.gameFrameOn ? g.gameView : g.mainView); }

    int Pot(int v)
    {
        int p = 8;
        while (p < v) p <<= 1;
        return p;
    }

    void TargetSize(int& w, int& h)
    {
        if (g.target) { w = g.target->w; h = g.target->h; }
        else { w = kScreenW; h = kScreenH; }
    }

    //------------------------------------------------------------- textures
    inline u32 Morton(u32 x, u32 y)
    {
        return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    }

    // Texel index of picture pixel (x, y) in a potW x potH texture.
    inline u32 TexelIndex(int x, int y, int potW, int potH)
    {
        (void)potH;
        const u32 r = (u32)y;
        return ((r >> 3) * (u32)(potW >> 3) + ((u32)x >> 3)) * 64 + Morton((u32)x & 7, r & 7);
    }

    // ARGB8888 pixels into one chunk's texture.
    void UploadChunk(Chunk& c, const Uint32* src, int pitchPixels, int rx, int ry, int rw, int rh)
    {
        const int potW = c.tex.width, potH = c.tex.height;
        const GPU_TEXCOLOR fmt = c.tex.fmt;
        for (int y = ry; y < ry + rh; ++y)
        {
            const Uint32* row = src + (size_t)(y - ry) * pitchPixels;
            for (int x = rx; x < rx + rw; ++x)
            {
                const Uint32 p = row[x - rx];
                const u32 a = p >> 24, r = (p >> 16) & 0xFF, gg = (p >> 8) & 0xFF, b = p & 0xFF;
                const u32 i = TexelIndex(x - c.x, y - c.y, potW, potH);
                if (fmt == GPU_RGBA8)
                    ((u32*)c.tex.data)[i] = (r << 24) | (gg << 16) | (b << 8) | a;
                else if (fmt == GPU_RGB565)
                    ((u16*)c.tex.data)[i] = (u16)(((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3));
                else
                    ((u16*)c.tex.data)[i] = (u16)(((r >> 3) << 11) | ((gg >> 3) << 6) | ((b >> 3) << 1) | (a >= 128 ? 1 : 0));
            }
        }
    }

    void FlushChunk(Chunk& c)
    {
        GSPGPU_FlushDataCache(c.tex.data, c.tex.size);
    }

    // The picture's pixels (or part of them) into its chunks.
    void Upload(GpuTexture* t, const SDL_Rect* rect, const Uint32* pixels, int pitchPixels)
    {
        SDL_Rect r = rect ? *rect : SDL_Rect{ 0, 0, t->w, t->h };
        for (Chunk& c : t->chunks)
        {
            const int x0 = std::max(r.x, c.x), y0 = std::max(r.y, c.y);
            const int x1 = std::min(r.x + r.w, c.x + c.w), y1 = std::min(r.y + r.h, c.y + c.h);
            if (x1 <= x0 || y1 <= y0) continue;
            UploadChunk(c, pixels + (size_t)(y0 - r.y) * pitchPixels + (x0 - r.x), pitchPixels, x0, y0, x1 - x0, y1 - y0);
            FlushChunk(c);
        }
    }

    void ApplyFilter(GpuTexture* t)
    {
        const GPU_TEXTURE_FILTER_PARAM f = t->linear ? GPU_LINEAR : GPU_NEAREST;
        for (Chunk& c : t->chunks) C3D_TexSetFilter(&c.tex, f, f);
    }

    GpuTexture* NewTexture(int w, int h, GPU_TEXCOLOR fmt, bool target)
    {
        if (w <= 0 || h <= 0) return nullptr;
        GpuTexture* t = new GpuTexture();
        t->w = w;
        t->h = h;
        if (target)
        {
            // one texture (targets of the game are at most 1024)
            const int tw = std::min(1024, Pot(w)), th = std::min(1024, Pot(h));
            Chunk c;
            c.w = std::min(w, tw);
            c.h = std::min(h, th);
            if (!C3D_TexInitVRAM(&c.tex, tw, th, GPU_RGBA8)) { delete t; return nullptr; }
            C3D_TexSetWrap(&c.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            t->chunks.push_back(c);
            t->target = C3D_RenderTargetCreateFromTex(&t->chunks[0].tex, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE(-1));
            if (!t->target) { C3D_TexDelete(&t->chunks[0].tex); delete t; return nullptr; }
            ApplyFilter(t);
            return t;
        }
        for (int y = 0; y < h; y += kMaxChunk)
        {
            for (int x = 0; x < w; x += kMaxChunk)
            {
                Chunk c;
                c.x = x; c.y = y;
                c.w = std::min(kMaxChunk, w - x);
                c.h = std::min(kMaxChunk, h - y);
                if (!C3D_TexInit(&c.tex, Pot(c.w), Pot(c.h), fmt))
                {
                    for (Chunk& d : t->chunks) C3D_TexDelete(&d.tex);
                    delete t;
                    SDL_Log("GPU: no memory for a %dx%d texture", w, h);
                    return nullptr;
                }
                memset(c.tex.data, 0, c.tex.size);
                C3D_TexSetWrap(&c.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                t->chunks.push_back(c);
            }
        }
        ApplyFilter(t);
        return t;
    }

    void FreeTexture(GpuTexture* t)
    {
        if (t->target) C3D_RenderTargetDelete(t->target);
        for (Chunk& c : t->chunks) C3D_TexDelete(&c.tex);
        t->magic = 0;
        delete t;
    }

    //------------------------------------------------------------- drawing
    // Words left in the GPU command buffer (it overflowing stops the 3DS:
    // svcBreak in GPUCMD_AddInternal).
    u32 CmdWordsLeft()
    {
        u32 size = 0, offset = 0;
        GPUCMD_GetBuffer(nullptr, &size, &offset);
        return size > offset ? size - offset : 0;
    }

    void FlushBatch()
    {
        const int count = g.vused - g.batchStart;
        if (count > 0 && g.batchTex && CmdWordsLeft() < 2048)
        {
            // full: the rest of this frame is left out instead
            if (!g.cmdFullLogged) { SDL_Log("GPU: command buffer full, drawings left out"); g.cmdFullLogged = true; }
        }
        else if (count > 0 && g.batchTex)
        {
            C3D_TexBind(0, g.batchTex);
            switch (g.batchBlend)
            {
                case SDL_BLENDMODE_BLEND:
                    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
                    break;
                case SDL_BLENDMODE_ADD:
                    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
                    break;
                case SDL_BLENDMODE_MOD:
                    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ZERO, GPU_ONE);
                    break;
                default:
                    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                    break;
            }
            C3D_DrawArrays(GPU_TRIANGLES, g.batchStart, count);
            ++g.batches;
        }
        g.batchStart = g.vused;
    }

    void BindTarget()
    {
        FlushBatch();
        C3D_RenderTarget* rt = g.target ? g.target->target : g.screen[g.eye];
        C3D_FrameDrawOn(rt);
        C3D_Mtx proj;
        if (g.target)
        {
            const int tw = g.target->chunks[0].tex.width, th = g.target->chunks[0].tex.height;
            Mtx_Ortho(&proj, 0.0f, (float)tw, (float)th, 0.0f, 1.0f, -1.0f, true);
        }
        else
        {
            Mtx_OrthoTilt(&proj, 0.0f, (float)kScreenW, (float)kScreenH, 0.0f, 1.0f, -1.0f, true);
        }
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g.uProjection, &proj);
        g.targetBound = true;
    }

    void EnsureFrame()
    {
        if (g.inFrame) return;
        C3D_FrameBegin(0);   // waits until the GPU is done with the last frame
        for (GpuTexture* t : g.doomed) FreeTexture(t);
        g.doomed.clear();
        g.inFrame = true;
        g.vused = g.batchStart = 0;
        g.batchTex = nullptr;
        g.batchBlend = SDL_BLENDMODE_INVALID;
        C3D_BufInfo* buf = C3D_GetBufInfo();
        BufInfo_Init(buf);
        BufInfo_Add(buf, g.vbuf, sizeof(Vertex), 3, 0x210);
        BindTarget();
    }

    void SetBatch(C3D_Tex* tex, SDL_BlendMode blend)
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

    // The texture coordinate of picture column x / row y in a chunk.
    inline float U(const Chunk& c, float x) { return (x - c.x) / c.tex.width; }
    inline float V(const Chunk& c, float y) { return 1.0f - (y - c.y) / c.tex.height; }

    void FillPixels(float x0, float y0, float x1, float y1)
    {
        EnsureFrame();
        SetBatch(&g.white->chunks[0].tex, g.drawBlend);
        const SDL_Rect clip = ClipArea();
        Quad(x0, y0, x1, y1, 0.0f, 1.0f, 1.0f, 0.0f, g.dr, g.dg, g.db, g.da, clip);
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
        for (Chunk& c : t->chunks)
        {
            // the part of the source in this chunk
            const int x0 = std::max(src.x, c.x), y0 = std::max(src.y, c.y);
            const int x1 = std::min(src.x + src.w, c.x + c.w), y1 = std::min(src.y + src.h, c.y + c.h);
            if (x1 <= x0 || y1 <= y0) continue;
            // where it goes (flips mirror the position within the source)
            const float lx0 = flipH ? (src.x + src.w - x1) : (x0 - src.x);
            const float lx1 = flipH ? (src.x + src.w - x0) : (x1 - src.x);
            const float ly0 = flipV ? (src.y + src.h - y1) : (y0 - src.y);
            const float ly1 = flipV ? (src.y + src.h - y0) : (y1 - src.y);
            float qx0 = X0 + lx0 * sx, qx1 = X0 + lx1 * sx, qy0 = Y0 + ly0 * sy, qy1 = Y0 + ly1 * sy;
            float u0 = U(c, (float)x0), u1 = U(c, (float)x1), v0 = V(c, (float)y0), v1 = V(c, (float)y1);
            if (flipH) std::swap(u0, u1);
            if (flipV) std::swap(v0, v1);
            SetBatch(&c.tex, blend);
            if (!rotated)
            {
                Quad(qx0, qy0, qx1, qy1, u0, v0, u1, v1, t->r, t->g, t->b, t->a, clip);
                continue;
            }
            if (!Room(6)) return 0;
            auto rot = [&](float x, float y, float u, float v) {
                const float dx = x - pcx, dy = y - pcy;
                Put(pcx + dx * ca - dy * sa, pcy + dx * sa + dy * ca, u, v, t->r, t->g, t->b, t->a);
            };
            rot(qx0, qy0, u0, v0); rot(qx0, qy1, u0, v1); rot(qx1, qy0, u1, v0);
            rot(qx1, qy0, u1, v0); rot(qx0, qy1, u0, v1); rot(qx1, qy1, u1, v1);
        }
        return 0;
    }

    //------------------------------------------------------------- start
    bool Start(SDL_Window* window)
    {
        if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4)) return false;
        g.screen[0] = C3D_RenderTargetCreate(kScreenH, kScreenW, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
        g.screen[1] = C3D_RenderTargetCreate(kScreenH, kScreenW, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
        if (!g.screen[0] || !g.screen[1]) return false;
        C3D_RenderTargetSetOutput(g.screen[0], GFX_TOP, GFX_LEFT, kDisplayTransferFlags);
        C3D_RenderTargetSetOutput(g.screen[1], GFX_TOP, GFX_RIGHT, kDisplayTransferFlags);

        g.dvlb = DVLB_ParseFile((u32*)reclaw2d_shbin, reclaw2d_shbin_size);
        if (!g.dvlb) return false;
        shaderProgramInit(&g.program);
        shaderProgramSetVsh(&g.program, &g.dvlb->DVLE[0]);
        C3D_BindProgram(&g.program);
        g.uProjection = shaderInstanceGetUniformLocation(g.program.vertexShader, "projection");

        C3D_AttrInfo* attr = C3D_GetAttrInfo();
        AttrInfo_Init(attr);
        AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 2);           // position
        AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);           // texture coordinates
        AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4);   // colour

        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        for (int i = 1; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));

        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C3D_CullFace(GPU_CULL_NONE);

        g.vbuf = (Vertex*)linearAlloc(sizeof(Vertex) * kMaxVertices);
        if (!g.vbuf) return false;

        g.white = NewTexture(8, 8, GPU_RGBA8, false);
        if (!g.white) return false;
        std::vector<Uint32> white(64, 0xFFFFFFFFu);
        Upload(g.white, nullptr, white.data(), 8);

        g.window = window;
        g.active = true;
        SDL_Log("GPU: the top screen draws with the GPU (citro3d)");
        return true;
    }
}

extern "C" {

int ReclawGpu_Active(void) { return g.active ? 1 : 0; }

void ReclawGpu_SetEye(int eye)
{
    eye = eye ? 1 : 0;
    if (!g.active || eye == g.eye) return;
    g.eye = eye;
    if (g.inFrame && !g.target) BindTarget();
}

SDL_Renderer* ReclawGpu_CreateRenderer(SDL_Window* window, int index, Uint32 flags)
{
    // the first renderer is the top screen's (the game's); later ones (the
    // bottom screen) stay with SDL
    if (!g.active && !g.window && window && PortSettings::GetInt("gpu", 1) != 0)
    {
        g.window = window;
        if (Start(window)) return kGpuRenderer;
        SDL_Log("GPU: could not start citro3d - software drawing");
        g.active = false;
    }
    return SDL_CreateRenderer(window, index, flags & ~(Uint32)SDL_RENDERER_ACCELERATED);
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
    // The game sometimes draws whole frames without showing them (a level
    // frame drawn twice between two presents, during the teleport's fade).
    // Software SDL just draws over them; here every one of them stays in the
    // GPU's command list until a present, and enough of them overflowed it
    // (a crash entering a portal). A screen clear after a lot was drawn
    // shows that frame first.
    if (g.inFrame && !g.target && g.eye == 0)
    {
        u32 size = 0, offset = 0;
        GPUCMD_GetBuffer(nullptr, &size, &offset);
        if (offset > size / 4 || g.vused > kMaxVertices / 4)
        {
            if (!g.unpresentedLogged) { SDL_Log("GPU: frames drawn without a present, one shown at a clear"); g.unpresentedLogged = true; }
            ReclawGpu_RenderPresent(renderer);
        }
    }
    EnsureFrame();
    int tw, th;
    TargetSize(tw, th);
    const SDL_BlendMode keep = g.drawBlend;
    g.drawBlend = SDL_BLENDMODE_NONE;
    SetBatch(&g.white->chunks[0].tex, SDL_BLENDMODE_NONE);
    const SDL_Rect all = { 0, 0, tw, th };
    Quad(0, 0, (float)tw, (float)th, 0.0f, 1.0f, 1.0f, 0.0f, g.dr, g.dg, g.db, g.da, all);
    g.drawBlend = keep;
    return 0;
}

void ReclawGpu_RenderPresent(SDL_Renderer* renderer)
{
    if (!IsGpu(renderer)) { SDL_RenderPresent(renderer); return; }
    if (!g.inFrame) return;
    FlushBatch();
    GSPGPU_FlushDataCache(g.vbuf, sizeof(Vertex) * std::max(1, g.vused));
    const int vertices = g.vused, batches = g.batches;
    g.batches = 0;
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    g.inFrame = false;
    static int s_Frames = 0;
    if (++s_Frames % 300 == 0)
        SDL_Log("GPU: last frame %d vertices in %d draws (commands %.0f%% full), CPU %.1f ms, GPU %.1f ms; linear free %lu KB, VRAM free %lu KB",
                vertices / 6 * 6, batches, C3D_GetCmdBufUsage() * 100.0f,
                C3D_GetProcessingTime(), C3D_GetDrawingTime(), (unsigned long)(linearSpaceFree() / 1024), (unsigned long)(vramSpaceFree() / 1024));
    g.targetBound = false;
    g.eye = 0;
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
    SetBatch(&g.white->chunks[0].tex, g.drawBlend);
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
    if (g.inFrame) BindTarget();
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
    g.gameFrame.w = kScreenW;
    g.gameFrame.h = kScreenH;
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
    // the top screen's last frame (the left eye), as ARGB8888
    if (g.target || g.inFrame || format != SDL_PIXELFORMAT_ARGB8888) return SDL_SetError("GPU: cannot read now");
    static u32* s_Linear = nullptr;
    if (!s_Linear) s_Linear = (u32*)linearAlloc(kScreenW * kScreenH * 4);
    if (!s_Linear) return -1;
    C3D_SyncDisplayTransfer((u32*)g.screen[0]->frameBuf.colorBuf, GX_BUFFER_DIM(kScreenH, kScreenW),
                            s_Linear, GX_BUFFER_DIM(kScreenH, kScreenW), kDisplayTransferFlags);
    GSPGPU_InvalidateDataCache(s_Linear, kScreenW * kScreenH * 4);
    const SDL_Rect r = rect ? *rect : SDL_Rect{ 0, 0, kScreenW, kScreenH };
    for (int y = 0; y < r.h; ++y)
    {
        Uint32* out = (Uint32*)((Uint8*)pixels + (size_t)y * pitch);
        for (int x = 0; x < r.w; ++x)
        {
            const int sx = r.x + x, sy = r.y + y;
            if (sx < 0 || sy < 0 || sx >= kScreenW || sy >= kScreenH) { out[x] = 0; continue; }
            const u32 p = s_Linear[(kScreenH - 1 - sy) + kScreenH * sx];   // 0xRRGGBBAA
            out[x] = ((p & 0xFF) << 24) | (p >> 8);
        }
    }
    return 0;
}

SDL_Texture* ReclawGpu_CreateTexture(SDL_Renderer* renderer, Uint32 format, int access, int w, int h)
{
    if (!IsGpu(renderer)) return SDL_CreateTexture(renderer, format, access, w, h);
    if (format != SDL_PIXELFORMAT_ARGB8888 && format != SDL_PIXELFORMAT_RGBA8888 && format != SDL_PIXELFORMAT_ABGR8888)
    {
        SDL_SetError("GPU: texture format not supported");
        return nullptr;
    }
    if (g.inFrame && access == SDL_TEXTUREACCESS_TARGET) FlushBatch();
    GpuTexture* t = NewTexture(w, h, GPU_RGBA8, access == SDL_TEXTUREACCESS_TARGET);
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
    // the smallest format that keeps the picture
    bool opaque = true, binary = true;
    for (int y = 0; y < argb->h && binary; ++y)
    {
        const Uint32* row = (const Uint32*)((const Uint8*)argb->pixels + (size_t)y * argb->pitch);
        for (int x = 0; x < argb->w; ++x)
        {
            const Uint32 a = row[x] >> 24;
            if (a != 255) opaque = false;
            if (a != 0 && a != 255) { binary = false; break; }
        }
    }
    const GPU_TEXCOLOR fmt = opaque ? GPU_RGB565 : (binary ? GPU_RGBA5551 : GPU_RGBA8);
    GpuTexture* t = NewTexture(argb->w, argb->h, fmt, false);
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
    // Freed when the GPU is done with it: also after a frame was handed over
    // (C3D_FrameEnd), the GPU may still be drawing it; its memory reused at
    // once showed as garbage (the loading screen's progress bar).
    if (g.inFrame) FlushBatch();
    t->magic = 0;
    g.doomed.push_back(t);
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

int ReclawGpu_SetTextureScaleMode(SDL_Texture* texture, SDL_ScaleMode mode)
{
    if (!IsGpuTex(texture)) return SDL_SetTextureScaleMode(texture, mode);
    GpuTexture* t = Tex(texture);
    t->linear = mode != SDL_ScaleModeNearest;
    ApplyFilter(t);
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
