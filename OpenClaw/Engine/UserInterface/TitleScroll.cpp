#include "TitleScroll.h"

#ifndef __EMSCRIPTEN__

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "../GameApp/BaseGameApp.h"
#include "../Audio/Audio.h"
#include "../Graphics2D/Image.h"
#include "../Resource/ResourceMgr.h"
#include "../Resource/Loaders/PcxLoader.h"
#include "../Resource/Loaders/WavLoader.h"
#include "../Video/Native/MoviePlayer.h"

namespace
{
    // Layout of the original 640x480 screen.
    const int kScreenW = 640;
    const int kScreenH = 480;
    const int kRollD = 30;          // roll diameter
    const int kRollOverhang = 8;    // rolls stick out a little above/below the paper

    // Timing (ms).
    const Uint32 kUnrollMs = 1100;
    const Uint32 kHoldMs = 2300;
    const Uint32 kRollUpMs = 800;

    const char* kTitlePath = "/STATES/ATTRACT/SCREENS/TITLE.PCX";
    const char* kPaperPath = "/STATES/ROLLER/SCREENS/PAPER.PCX";
    const char* kUnrollSound = "/STATES/ATTRACT/SOUNDS/UNROLL.WAV";
    const char* kTitleSound = "/STATES/ATTRACT/SOUNDS/TITLE.WAV";
    const char* kRollUpSound = "/STATES/ATTRACT/SOUNDS/ROLLUP.WAV";

    const double kPi = 3.14159265358979323846;

    bool HasResource(const char* path)
    {
        Resource r(path);
        return g_pApp->GetResourceMgr()->VGetHandle(&r) != nullptr;
    }

    SDL_Surface* LoadPaperSurface()
    {
        Resource r(kPaperPath);
        shared_ptr<ResourceHandle> h = g_pApp->GetResourceMgr()->VGetHandle(&r);
        if (!h || !h->GetDataBuffer()) return nullptr;
        SDL_Surface* raw = IMG_LoadPCX_RW(SDL_RWFromConstMem(h->GetDataBuffer(), (int)h->GetSize()));
        if (!raw) return nullptr;
        SDL_Surface* argb = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(raw);
        return argb;
    }

    void PlaySound(const char* path)
    {
        if (!g_pApp->GetAudio() || !g_pApp->GetAudio()->IsSoundActive() || !HasResource(path)) return;
        static std::vector<shared_ptr<Mix_Chunk>> s_Keep;   // chunks must outlive playback
        shared_ptr<Mix_Chunk> chunk = WavResourceLoader::LoadAndReturnSound(path);
        if (!chunk) return;
        s_Keep.push_back(chunk);
        Mix_PlayChannel(-1, chunk.get(), 0);
    }

    double EaseOut(double t) { return 1.0 - (1.0 - t) * (1.0 - t); }
    double EaseIn(double t) { return t * t; }

    struct Roll
    {
        SDL_Texture* tex = nullptr;
        std::vector<Uint32> pixels;
        int h = kScreenH + 2 * kRollOverhang;
    };

    // Renders one roll: the paper texture wrapped around a cylinder, shaded by
    // the angle to the viewer; `turn` (in paper pixels) rotates it.
    void UpdateRoll(Roll& roll, SDL_Surface* paper, double turn)
    {
        const int w = kRollD;
        const double radius = kRollD / 2.0;
        const Uint32* src = (const Uint32*)paper->pixels;
        const int pw = paper->w, ph = paper->h, pitch = paper->pitch / 4;
        for (int x = 0; x < w; ++x)
        {
            double u = ((x + 0.5) / w) * 2.0 - 1.0;          // -1..1 across the roll
            double theta = std::asin(std::max(-1.0, std::min(1.0, u)));
            double light = 0.28 + 0.72 * std::cos(theta);     // darker towards the edges
            light *= 1.0 - 0.25 * std::max(0.0, u);           // light from the left
            int sx = (int)std::floor(theta * radius + turn);
            sx = ((sx % pw) + pw) % pw;
            for (int y = 0; y < roll.h; ++y)
            {
                int sy = y % ph;
                Uint32 c = src[sy * pitch + sx];
                int r = (int)(((c >> 16) & 0xFF) * light);
                int g = (int)(((c >> 8) & 0xFF) * light);
                int b = (int)((c & 0xFF) * light);
                roll.pixels[y * w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
        // rounded ends: darken the first/last rows
        for (int y = 0; y < 3; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                roll.pixels[y * w + x] = (roll.pixels[y * w + x] >> 1) & 0xFF7F7F7Fu;
                roll.pixels[(roll.h - 1 - y) * w + x] = (roll.pixels[(roll.h - 1 - y) * w + x] >> 1) & 0xFF7F7F7Fu;
            }
        }
        SDL_UpdateTexture(roll.tex, NULL, roll.pixels.data(), w * 4);
    }

    struct View
    {
        double scale;
        int offX, offY;
        SDL_Rect Map(double x, double y, double w, double h) const
        {
            return SDL_Rect{ offX + (int)std::lround(x * scale), offY + (int)std::lround(y * scale),
                             (int)std::lround(w * scale), (int)std::lround(h * scale) };
        }
    };

    void DrawFrame(SDL_Renderer* r, const View& v, SDL_Texture* title, Roll& left, Roll& right,
                   SDL_Surface* paper, double half)
    {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);

        const double cx = kScreenW / 2.0;
        if (half > 0.5)
        {
            // Visible part of the map between the rolls.
            int sx = (int)std::floor(cx - half);
            int sw = (int)std::ceil(2 * half);
            SDL_Rect src = { std::max(0, sx), 0, std::min(kScreenW, sx + sw) - std::max(0, sx), kScreenH };
            SDL_Rect dst = v.Map(src.x, 0, src.w, kScreenH);
            SDL_RenderCopy(r, title, &src, &dst);

            // Soft shadow on the paper next to each roll.
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            for (int i = 0; i < 10; ++i)
            {
                SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(90 - i * 9));
                SDL_Rect l = v.Map(cx - half + i, 0, 1, kScreenH);
                SDL_Rect rr = v.Map(cx + half - 1 - i, 0, 1, kScreenH);
                SDL_RenderFillRect(r, &l);
                SDL_RenderFillRect(r, &rr);
            }
        }

        // The rolls turn as paper comes off them (opposite directions).
        UpdateRoll(left, paper, half);
        UpdateRoll(right, paper, -half);
        SDL_Rect l = v.Map(cx - half - kRollD / 2.0, -kRollOverhang, kRollD, left.h);
        SDL_Rect rr = v.Map(cx + half - kRollD / 2.0, -kRollOverhang, kRollD, right.h);
        SDL_RenderCopy(r, left.tex, NULL, &l);
        SDL_RenderCopyEx(r, right.tex, NULL, &rr, 0.0, NULL, SDL_FLIP_HORIZONTAL);
        SDL_RenderPresent(r);
    }
}

namespace TitleScroll
{
    void Play(SDL_Renderer* r)
    {
        if (!r || !HasResource(kTitlePath) || !HasResource(kPaperPath)) return;

        shared_ptr<Image> pTitle = PcxResourceLoader::LoadAndReturnImage(kTitlePath);
        SDL_Surface* paper = LoadPaperSurface();
        if (!pTitle || !pTitle->GetTexture() || !paper || paper->w <= 0 || paper->h <= 0)
        {
            if (paper) SDL_FreeSurface(paper);
            return;
        }

        SDL_Texture* pPrevTarget = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, NULL);

        Roll left, right;
        for (Roll* roll : { &left, &right })
        {
            roll->pixels.resize(kRollD * roll->h);
            roll->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kRollD, roll->h);
            if (roll->tex) SDL_SetTextureScaleMode(roll->tex, SDL_ScaleModeLinear);
        }
        SDL_SetTextureScaleMode(pTitle->GetTexture(), SDL_ScaleModeLinear);

        int outW = 0, outH = 0;
        SDL_GetRendererOutputSize(r, &outW, &outH);
        View v;
        v.scale = std::min(outW / (double)kScreenW, outH / (double)kScreenH);
        v.offX = (int)((outW - kScreenW * v.scale) / 2);
        v.offY = (int)((outH - kScreenH * v.scale) / 2);

        enum Phase { Unroll, Hold, RollUp, Done } phase = Unroll;
        Uint32 phaseStart = SDL_GetTicks();
        double half = 0;
        PlaySound(kUnrollSound);

        while (phase != Done && left.tex && right.tex)
        {
#ifdef __SWITCH__
            if (!appletMainLoop()) break;
#endif
            int input = MoviePlayer::PollSkipInput();
            if (input == 2) break;
            Uint32 now = SDL_GetTicks();
            double t;
            switch (phase)
            {
                case Unroll:
                    t = std::min(1.0, (now - phaseStart) / (double)kUnrollMs);
                    half = EaseOut(t) * (kScreenW / 2.0);
                    if (t >= 1.0 || input == 1)
                    {
                        half = kScreenW / 2.0;
                        phase = (input == 1) ? RollUp : Hold;
                        phaseStart = now;
                        PlaySound(phase == Hold ? kTitleSound : kRollUpSound);
                    }
                    break;
                case Hold:
                    if (now - phaseStart >= kHoldMs || input == 1)
                    {
                        phase = RollUp;
                        phaseStart = now;
                        PlaySound(kRollUpSound);
                    }
                    break;
                case RollUp:
                    t = std::min(1.0, (now - phaseStart) / (double)kRollUpMs);
                    half = (1.0 - EaseIn(t)) * (kScreenW / 2.0);
                    if (t >= 1.0) phase = Done;
                    break;
                default:
                    break;
            }
            Uint32 frameStart = SDL_GetTicks();
            DrawFrame(r, v, pTitle->GetTexture(), left, right, paper, half);
            Uint32 spent = SDL_GetTicks() - frameStart;
            if (spent < 15) SDL_Delay(15 - spent);   // ~60 fps without vsync
        }

        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        SDL_RenderPresent(r);
        if (left.tex) SDL_DestroyTexture(left.tex);
        if (right.tex) SDL_DestroyTexture(right.tex);
        SDL_FreeSurface(paper);
        SDL_SetRenderTarget(r, pPrevTarget);
        MoviePlayer::FlushSkipInput();
    }
}

#else

namespace TitleScroll
{
    void Play(SDL_Renderer*) { }
}

#endif
