#include "../Graphics2D/CrtEffect.h"
#include "StartupSplash.h"

// Dedication screen while the game loads - turned off for now.
// Uncomment the line below to turn it on on every platform (or build the
// Switch version with make SPLASH="..." to set the text without editing).
#ifndef OPENCLAW_SPLASH_TEXT
// #define OPENCLAW_SPLASH_TEXT "Text-Title"
#endif

#if defined(OPENCLAW_SPLASH_TEXT) && !defined(__EMSCRIPTEN__)

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "../Platform/Input/BlockingInput.h"

#include "../Logger/Logger.h"

#include "../Platform/Platform.h"

namespace
{
    const Uint32 kFadeMs = 700;
    const Uint32 kMinVisibleMs = 3500;   // from the start of the fade-in

    const SDL_Color kBackground = { 14, 8, 4, 255 };
    const SDL_Color kGold = { 236, 196, 98, 255 };
    const SDL_Color kGoldDark = { 120, 72, 20, 255 };
    const SDL_Color kShadow = { 0, 0, 0, 255 };

    SDL_Texture* g_pText = NULL;      // gold text
    SDL_Texture* g_pShadow = NULL;    // drop shadow
    SDL_Texture* g_pGlow = NULL;      // dark outline for depth
    int g_TextW = 0, g_TextH = 0;
    int g_GlowW = 0, g_GlowH = 0, g_Outline = 0;
    Uint32 g_ShownAt = 0;
    bool g_Active = false;

    SDL_Texture* MakeTexture(SDL_Renderer* r, TTF_Font* font, SDL_Color color, int* w, int* h)
    {
        SDL_Surface* s = TTF_RenderUTF8_Blended(font, OPENCLAW_SPLASH_TEXT, color);
        if (!s) return NULL;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        if (w) *w = s->w;
        if (h) *h = s->h;
        SDL_FreeSurface(s);
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        return t;
    }

    void Draw(SDL_Renderer* r, Uint8 alpha)
    {
        int outW = 0, outH = 0;
        SDL_GetRendererOutputSize(r, &outW, &outH);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kBackground.r, kBackground.g, kBackground.b, 255);
        SDL_RenderClear(r);

        // Thin gold rules above and below the text.
        int cx = outW / 2, cy = outH / 2;
        int ruleW = g_TextW * 3 / 5;
        int gap = g_TextH * 3 / 4;
        SDL_SetRenderDrawColor(r, kGold.r, kGold.g, kGold.b, (Uint8)(alpha * 3 / 5));
        SDL_Rect top = { cx - ruleW / 2, cy - gap - 1, ruleW, 2 };
        SDL_Rect bottom = { cx - ruleW / 2, cy + gap - 1, ruleW, 2 };
        SDL_RenderFillRect(r, &top);
        SDL_RenderFillRect(r, &bottom);

        int shadowOffset = g_TextH / 18 + 1;
        if (g_pShadow)
        {
            SDL_SetTextureAlphaMod(g_pShadow, (Uint8)(alpha * 2 / 3));
            SDL_Rect d = { cx - g_TextW / 2 + shadowOffset, cy - g_TextH / 2 + shadowOffset, g_TextW, g_TextH };
            SDL_RenderCopy(r, g_pShadow, NULL, &d);
        }
        if (g_pGlow)
        {
            SDL_SetTextureAlphaMod(g_pGlow, alpha);
            SDL_Rect d = { cx - g_GlowW / 2, cy - g_GlowH / 2, g_GlowW, g_GlowH };
            SDL_RenderCopy(r, g_pGlow, NULL, &d);
        }
        if (g_pText)
        {
            SDL_SetTextureAlphaMod(g_pText, alpha);
            SDL_Rect d = { cx - g_TextW / 2, cy - g_TextH / 2, g_TextW, g_TextH };
            SDL_RenderCopy(r, g_pText, NULL, &d);
        }
        CrtEffect::BeforePresent(r);
        SDL_RenderPresent(r);
    }

    // Runs a fade for `ms`; alpha goes from `from` to `to`.
    void Fade(SDL_Renderer* r, int from, int to, Uint32 ms)
    {
        Uint32 start = SDL_GetTicks();
        while (true)
        {
            if (!BlockingInput::AppAlive()) return;
            SDL_PumpEvents();   // keep the OS happy without eating game events
            Uint32 t = SDL_GetTicks() - start;
            if (t > ms) t = ms;
            int a = from + (to - from) * (int)t / (int)ms;
            Draw(r, (Uint8)a);
            if (t >= ms) break;
            SDL_Delay(8);
        }
    }

    void Release()
    {
        if (g_pText) SDL_DestroyTexture(g_pText);
        if (g_pShadow) SDL_DestroyTexture(g_pShadow);
        if (g_pGlow) SDL_DestroyTexture(g_pGlow);
        g_pText = g_pShadow = g_pGlow = NULL;
    }
}

namespace StartupSplash
{
    void Show(SDL_Renderer* r)
    {
        if (!r) return;
        if (!TTF_WasInit() && TTF_Init() != 0) return;

        int outW = 0, outH = 0;
        SDL_GetRendererOutputSize(r, &outW, &outH);
        int size = outH > 0 ? outH / 11 : 64;

        TTF_Font* font = TTF_OpenFont(Platform::SplashFont().c_str(), size);
        if (!font)
        {
            LOG_WARNING(std::string("Splash font missing: ") + Platform::SplashFont());
            return;
        }
        // Shrink to fit the width with a margin.
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, OPENCLAW_SPLASH_TEXT, &w, &h) == 0 && w > outW * 9 / 10)
        {
            TTF_CloseFont(font);
            size = size * (outW * 9 / 10) / w;
            font = TTF_OpenFont(Platform::SplashFont().c_str(), size);
            if (!font) return;
        }

        SDL_Texture* pPrevTarget = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, NULL);

        g_pText = MakeTexture(r, font, kGold, &g_TextW, &g_TextH);
        g_pShadow = MakeTexture(r, font, kShadow, NULL, NULL);
        g_Outline = size / 28 + 1;
        TTF_SetFontOutline(font, g_Outline);
        g_pGlow = MakeTexture(r, font, kGoldDark, &g_GlowW, &g_GlowH);
        TTF_SetFontOutline(font, 0);
        TTF_CloseFont(font);

        if (g_pText)
        {
            g_Active = true;
            g_ShownAt = SDL_GetTicks();
            Fade(r, 0, 255, kFadeMs);
        }
        else
        {
            Release();
        }
        SDL_SetRenderTarget(r, pPrevTarget);
    }

    void Finish(SDL_Renderer* r)
    {
        if (!g_Active || !r) return;
        g_Active = false;

        SDL_Texture* pPrevTarget = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, NULL);

        // Loading usually takes a few seconds already; only top up the rest.
        while (SDL_GetTicks() - g_ShownAt < kMinVisibleMs)
        {
            if (!BlockingInput::AppAlive()) break;
            SDL_PumpEvents();
            Draw(r, 255);
            SDL_Delay(16);
        }
        Fade(r, 255, 0, kFadeMs);

        Release();
        SDL_SetRenderTarget(r, pPrevTarget);
    }
}

#else

namespace StartupSplash
{
    void Show(SDL_Renderer*) { }
    void Finish(SDL_Renderer*) { }
}

#endif
