#include "CrtEffect.h"
#include "../Util/PortSettings.h"
#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Race/RaceUi.h"

#include <algorithm>
#include <cmath>
#include <string>
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

    bool FpsCounterOn() { return PortSettings::GetInt("fps_counter", 0) != 0; }

    void SetFpsCounter(bool on) { PortSettings::SetInt("fps_counter", on ? 1 : 0); }

    namespace
    {
        // Frames shown in the last half second (every picture passes BeforePresent).
        int CountFrame()
        {
            static Uint32 s_Start = 0;
            static int s_Frames = 0, s_Fps = 0;
            const Uint32 now = SDL_GetTicks();
            if (!s_Start) s_Start = now;
            ++s_Frames;
            if (now - s_Start >= 500)
            {
                s_Fps = (int)(s_Frames * 1000.0 / (now - s_Start) + 0.5);
                s_Frames = 0;
                s_Start = now;
            }
            return s_Fps;
        }

        void DrawFps(SDL_Renderer* r, int w, int h, int left)
        {
            const int fps = CountFrame();
            if (!FpsCounterOn()) return;
            const int px = std::max(10, h / 36);
            const SDL_Color c = fps >= 55 ? SDL_Color{ 140, 240, 120, 255 } : (fps >= 28 ? SDL_Color{ 250, 220, 90, 255 } : SDL_Color{ 250, 110, 90, 255 });
            RaceUi::DrawText(r, std::to_string(fps) + " FPS", px, left + px / 2, h - px * 2, -1, c);
        }
    }

    void BeforePresent(SDL_Renderer* r)
    {
        if (!r || !g_pApp || r != g_pApp->GetRenderer()) return;
#ifdef __EMSCRIPTEN__
        const bool classic = false;   // (the web page sizes its canvas itself)
#else
        const bool classic = BaseGameApp::AspectClassic();
#endif
        const bool crt = IsOn();
        const bool fps = FpsCounterOn();
        if (!classic && !crt && !fps) { CountFrame(); return; }
        int w = 0, h = 0;
        if (SDL_Texture* target = SDL_GetRenderTarget(r)) SDL_QueryTexture(target, NULL, NULL, &w, &h);
        else SDL_GetRendererOutputSize(r, &w, &h);
        if (w <= 0 || h <= 0) return;
        float sx = 1.0f, sy = 1.0f;
        SDL_RenderGetScale(r, &sx, &sy);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_RenderSetViewport(r, NULL);
        if (classic)
        {
            const int w43 = (h * 4 / 3) & ~1;
            if (w43 + 8 < w)
            {
                const int bar = (w - w43) / 2;
                SDL_Rect left = { 0, 0, bar, h }, right = { w - bar, 0, bar, h };
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                SDL_RenderFillRect(r, &left);
                SDL_RenderFillRect(r, &right);
            }
        }
        if (fps)
        {
            int left = 0;
            if (classic)
            {
                const int w43 = (h * 4 / 3) & ~1;
                if (w43 + 8 < w) left = (w - w43) / 2;
            }
            DrawFps(r, w, h, left);
        }
        if (crt)
        {
            // the level: its pixels' size; menus, movies: made for 640x480
            float scale = h / 480.0f;
            if (BaseGameLogic* logic = g_pApp->GetGameLogic())
                if (logic->GetGameState() == GameState_IngameRunning || logic->GetGameState() == GameState_IngamePaused)
                    scale = (float)g_pApp->GetScale().x;
            Render(r, w, h, scale);
        }
        SDL_RenderSetScale(r, sx, sy);
    }
}
