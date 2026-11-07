#include "CrtEffect.h"
#include "../Util/PortSettings.h"

#include <cmath>
#include <vector>

namespace CrtEffect
{
    namespace
    {
        SDL_Texture* g_Lines = nullptr;
        int g_LinesH = 0;
        float g_LinesScale = 0.0f;
        SDL_Texture* g_Vignette = nullptr;
        SDL_Renderer* g_Renderer = nullptr;
        const int kLinesW = 8;   // (narrow textures: 8 wide for the 3DS GPU)

        void Drop()
        {
            if (g_Lines) SDL_DestroyTexture(g_Lines);
            if (g_Vignette) SDL_DestroyTexture(g_Vignette);
            g_Lines = g_Vignette = nullptr;
            g_LinesH = 0;
        }

        // One dark band at the bottom of every row of the game's pixels
        // (every other screen line when they are smaller than 1.5 lines).
        void MakeLines(SDL_Renderer* r, int h, float scale)
        {
            if (g_Lines) SDL_DestroyTexture(g_Lines);
            g_Lines = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kLinesW, h);
            g_LinesH = h;
            g_LinesScale = scale;
            if (!g_Lines) return;
            std::vector<Uint32> px((size_t)kLinesW * h);
            for (int y = 0; y < h; ++y)
            {
                int alpha;
                if (scale < 1.5f)
                {
                    alpha = (y & 1) ? 70 : 0;
                }
                else
                {
                    // where in its game row this screen line is (0 top .. 1 bottom)
                    const float phase = std::fmod((y + 0.5f) / scale, 1.0f);
                    alpha = phase >= 0.62f ? 105 : (phase >= 0.5f ? 45 : 0);
                }
                for (int x = 0; x < kLinesW; ++x) px[(size_t)y * kLinesW + x] = (Uint32)alpha << 24;
            }
            SDL_UpdateTexture(g_Lines, NULL, px.data(), kLinesW * 4);
            SDL_SetTextureBlendMode(g_Lines, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(g_Lines, SDL_ScaleModeNearest);
        }

        void MakeVignette(SDL_Renderer* r)
        {
            const int n = 64;
            g_Vignette = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, n, n);
            if (!g_Vignette) return;
            std::vector<Uint32> px((size_t)n * n);
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    const float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
                    const float d = std::sqrt(dx * dx * 0.8f + dy * dy * 0.8f);
                    float t = (d - 0.55f) / 0.65f;
                    t = t < 0 ? 0 : (t > 1 ? 1 : t);
                    px[(size_t)y * n + x] = (Uint32)(t * t * 150.0f) << 24;
                }
            SDL_UpdateTexture(g_Vignette, NULL, px.data(), n * 4);
            SDL_SetTextureBlendMode(g_Vignette, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(g_Vignette, SDL_ScaleModeLinear);
        }
    }

    bool IsOn() { return PortSettings::GetInt("crt", 0) != 0; }

    void SetOn(bool on) { PortSettings::SetInt("crt", on ? 1 : 0); }

    void Render(SDL_Renderer* r, int w, int h, float levelScale)
    {
        if (!r || w <= 0 || h <= 0 || !IsOn()) return;
        if (r != g_Renderer) { Drop(); g_Renderer = r; }
        if (!g_Lines || g_LinesH != h || std::fabs(g_LinesScale - levelScale) > 0.001f) MakeLines(r, h, levelScale);
        if (!g_Vignette) MakeVignette(r);
        float sx = 1.0f, sy = 1.0f;
        SDL_RenderGetScale(r, &sx, &sy);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_Rect all = { 0, 0, w, h };
        if (g_Lines) SDL_RenderCopy(r, g_Lines, NULL, &all);
        if (g_Vignette) SDL_RenderCopy(r, g_Vignette, NULL, &all);
        SDL_RenderSetScale(r, sx, sy);
    }
}
