#include <cstdio>
#include "../Util/StartupTrace.h"
#include "../Graphics2D/CrtEffect.h"
#include "RaceUi.h"

#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "../GameApp/BaseGameApp.h"
#include "../Audio/Audio.h"
#include "../Graphics2D/Image.h"
#include "../Resource/ResourceMgr.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Loaders/PcxLoader.h"
#include "../Resource/Loaders/WavLoader.h"
#include "../Platform/Input/BlockingInput.h"
#include "../Platform/Platform.h"
#include "../UserInterface/Lang.h"

namespace
{
    const int kScreenW = 640, kScreenH = 480;
    const SDL_Color kGold = { 222, 182, 84, 255 };
    const SDL_Color kGoldLight = { 255, 238, 160, 255 };
    const SDL_Color kEdge = { 40, 20, 4, 255 };
    const SDL_Color kParchment = { 250, 230, 180, 255 };

    struct View
    {
        double scale = 1.0;
        int offX = 0, offY = 0;
        SDL_Rect Map(double x, double y, double w, double h) const
        {
            return SDL_Rect{ offX + (int)std::lround(x * scale), offY + (int)std::lround(y * scale),
                             (int)std::lround(w * scale), (int)std::lround(h * scale) };
        }
    };

    struct Text
    {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
    };

    TTF_Font* OpenFont(int px)
    {
        static std::map<int, TTF_Font*> s_Fonts;
        auto it = s_Fonts.find(px);
        if (it != s_Fonts.end()) return it->second;
        if (!TTF_WasInit() && TTF_Init() != 0) return nullptr;
        const std::string fontPath = Platform::SplashFont();
        const char* paths[] = { fontPath.c_str() };
        TTF_Font* font = nullptr;
        for (const char* p : paths)
        {
            if ((font = TTF_OpenFont(p, px)) != nullptr) break;
        }
        s_Fonts[px] = font;
        return font;
    }

    Text MakeText(SDL_Renderer* r, TTF_Font* font, const std::string& s, SDL_Color fill, SDL_Color edge, int outline)
    {
        Text t;
        if (s.empty() || !font) return t;
        // The edge: the text in the edge colour drawn around itself, not
        // FreeType's outline (the PS3 closed the game in it with this font)
        SDL_Surface* front = TTF_RenderUTF8_Blended(font, s.c_str(), fill);
        SDL_Surface* edgeText = TTF_RenderUTF8_Blended(font, s.c_str(), edge);
        SDL_Surface* back = nullptr;
        if (front && edgeText)
        {
            back = SDL_CreateRGBSurfaceWithFormat(0, front->w + 2 * outline, front->h + 2 * outline, 32, SDL_PIXELFORMAT_ARGB8888);
        }
        if (back)
        {
            SDL_FillRect(back, NULL, 0);
            SDL_SetSurfaceBlendMode(edgeText, SDL_BLENDMODE_BLEND);
            for (int dy = -outline; dy <= outline; ++dy)
                for (int dx = -outline; dx <= outline; ++dx)
                {
                    if (dx * dx + dy * dy > outline * outline + outline) continue;
                    SDL_Rect at = { outline + dx, outline + dy, edgeText->w, edgeText->h };
                    SDL_BlitSurface(edgeText, NULL, back, &at);
                }
            SDL_Rect dst = { outline, outline, front->w, front->h };
            SDL_SetSurfaceBlendMode(front, SDL_BLENDMODE_BLEND);
            SDL_BlitSurface(front, NULL, back, &dst);
            t.tex = SDL_CreateTextureFromSurface(r, back);
            t.w = back->w;
            t.h = back->h;
        }
        if (back) SDL_FreeSurface(back);
        if (front) SDL_FreeSurface(front);
        if (edgeText) SDL_FreeSurface(edgeText);
        return t;
    }

    void FreeText(Text& t)
    {
        if (t.tex) SDL_DestroyTexture(t.tex);
        t = Text();
    }

    void PlayMenuSound(const char* path)
    {
        if (!g_pApp->GetAudio() || !g_pApp->GetAudio()->IsSoundActive()) return;
        Resource r(path);
        if (!g_pApp->GetResourceMgr()->VGetHandle(&r)) return;
        static std::vector<shared_ptr<Mix_Chunk>> s_Keep;
        shared_ptr<Mix_Chunk> chunk = WavResourceLoader::LoadAndReturnSound(path);
        if (!chunk) return;
        if (s_Keep.size() < 8) s_Keep.push_back(chunk);
        Mix_PlayChannel(-1, chunk.get(), 0);
    }

    shared_ptr<Image> LoadMenuPcx(const char* path)
    {
        Resource r(path);
        if (!g_pApp->GetResourceMgr()->VGetHandle(&r)) return nullptr;
        return PcxResourceLoader::LoadAndReturnImage(path);
    }

    // Everything a list screen draws, built once per text change.
    struct Canvas
    {
        SDL_Renderer* r = nullptr;
        View v;
        TTF_Font* title = nullptr;
        TTF_Font* item = nullptr;
        TTF_Font* small = nullptr;
        int outline = 1;
        shared_ptr<Image> background;

        bool Begin(SDL_Renderer* renderer)
        {
            r = renderer;
            int outW = 0, outH = 0;
            SDL_SetRenderTarget(r, NULL);
            SDL_RenderSetViewport(r, NULL);
            SDL_RenderSetScale(r, 1.0f, 1.0f);
            SDL_GetRendererOutputSize(r, &outW, &outH);
            v.scale = std::min(outW / (double)kScreenW, outH / (double)kScreenH);
            v.offX = (int)((outW - kScreenW * v.scale) / 2);
            v.offY = (int)((outH - kScreenH * v.scale) / 2);
            title = OpenFont((int)std::lround(30 * v.scale));
            item = OpenFont((int)std::lround(22 * v.scale));
            small = OpenFont((int)std::lround(14 * v.scale));
            outline = std::max(1, (int)std::lround(1.5 * v.scale));
            background = LoadMenuPcx("/STATES/MENU/SCREENS/MENU.PCX");
            return title && item && small;
        }

        void Clear()
        {
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            if (background && background->GetTexture())
            {
                SDL_Rect bg = v.Map(0, 0, kScreenW, kScreenH);
                SDL_RenderCopy(r, background->GetTexture(), NULL, &bg);
            }
        }

        // Centered line at design y; returns its screen rect.
        // Texts made once per screen (not every frame: each one is a new
        // texture, cleaned and uploaded on the PS3 / 3DS)
        std::map<std::string, Text> texts;
        ~Canvas()
        {
            for (auto& kv : texts) FreeText(kv.second);
        }

        SDL_Rect Line(TTF_Font* font, const std::string& s, double y, SDL_Color fill)
        {
            char key[64];
            snprintf(key, sizeof(key), "%p %02x%02x%02x%02x ", (void*)font, fill.r, fill.g, fill.b, fill.a);
            auto it = texts.find(key + s);
            if (it == texts.end())
            {
                if (texts.size() > 64) { for (auto& kv : texts) FreeText(kv.second); texts.clear(); }
                it = texts.insert(std::make_pair(key + s, MakeText(r, font, s, fill, kEdge, outline))).first;
            }
            const Text& t = it->second;
            SDL_Rect dst = { 0, 0, 0, 0 };
            if (t.tex)
            {
                double maxW = 560 * v.scale;
                double shrink = t.w > maxW ? maxW / t.w : 1.0;
                int w = (int)(t.w * shrink), h = (int)(t.h * shrink);
                dst = { v.offX + (int)(kScreenW * v.scale - w) / 2, v.Map(0, y, 0, 0).y - h / 2, w, h };
                SDL_RenderCopy(r, t.tex, NULL, &dst);
            }
            return dst;
        }
    };

    std::map<std::pair<std::string, int>, Text> g_TextCache;
}

namespace RaceUi
{
    int Run(SDL_Renderer* pRenderer, ListScreen& screen)
    {
        if (!pRenderer) return -1;
        SDL_Texture* pPrevTarget = SDL_GetRenderTarget(pRenderer);
        float prevScaleX = 1.0f, prevScaleY = 1.0f;
        SDL_RenderGetScale(pRenderer, &prevScaleX, &prevScaleY);
        Canvas c;
        STARTUP_TRACE("list screen: fonts");
        const bool canvasOk = c.Begin(pRenderer);
        STARTUP_TRACE(canvasOk ? "list screen: fonts ready" : "list screen: no font");
        if (!canvasOk)
        {
            LOG_ERROR("Race screen: font not found");
            SDL_SetRenderTarget(pRenderer, pPrevTarget);
            SDL_RenderSetScale(pRenderer, prevScaleX, prevScaleY);
            return -1;
        }

        int result = -2;
        int scroll = 0;
        BlockingInput::Reset();
        while (result == -2)
        {
            int count = (int)screen.items.size();
            if (count > 0)
            {
                screen.selected = std::max(0, std::min(screen.selected, count - 1));
            }

            int in = BlockingInput::Poll();
            if (in & BlockingInput::Quit) { result = -1; break; }
            if (count > 0 && (in & BlockingInput::Up))
            {
                screen.selected = (screen.selected + count - 1) % count;
                PlayMenuSound("/STATES/MENU/SOUNDS/SELECT.WAV");
            }
            if (count > 0 && (in & BlockingInput::Down))
            {
                screen.selected = (screen.selected + 1) % count;
                PlayMenuSound("/STATES/MENU/SOUNDS/SELECT.WAV");
            }
            if ((in & BlockingInput::Back) && screen.backCancels) { result = -1; break; }
            if ((in & BlockingInput::Accept) && count > 0)
            {
                PlayMenuSound("/STATES/MENU/SOUNDS/CLICK.WAV");
                result = screen.selected;
                break;
            }
            if (screen.tick)
            {
                int r = screen.tick(screen);
                if (r != -2) { result = r; break; }
            }

            // ---- draw
            static bool s_TracedDraw = false;
            const bool traceDraw = !s_TracedDraw;
            s_TracedDraw = true;
            if (traceDraw) STARTUP_TRACE("list screen: background");
            c.Clear();
            if (traceDraw) STARTUP_TRACE("list screen: title");
            c.Line(c.title, screen.title, 150, kGold);
            if (traceDraw) STARTUP_TRACE("list screen: items");
            const double top = 205, rowH = 32;
            const int rows = 7;
            if (screen.selected < scroll) scroll = screen.selected;
            if (screen.selected >= scroll + rows) scroll = screen.selected - rows + 1;
            scroll = std::max(0, std::min(scroll, std::max(0, (int)screen.items.size() - rows)));
            for (int row = 0; row < rows && scroll + row < (int)screen.items.size(); ++row)
            {
                int i = scroll + row;
                c.Line(c.item, screen.items[i], top + row * rowH, i == screen.selected ? kGoldLight : kGold);
            }
            if (scroll > 0) c.Line(c.small, "^", top - 22, kGold);
            if (scroll + rows < (int)screen.items.size()) c.Line(c.small, "v", top + rows * rowH - 12, kGold);
            double y = top + std::min<int>((int)screen.items.size(), rows) * rowH + 14;
            for (const std::string& line : screen.footer)
            {
                c.Line(c.small, line, y, kParchment);
                y += 20;
            }
            if (traceDraw) STARTUP_TRACE("list screen: drawn, showing");
            CrtEffect::BeforePresent(pRenderer);
            SDL_RenderPresent(pRenderer);
            {
                static bool s_FirstShown = false;
                if (!s_FirstShown) { s_FirstShown = true; STARTUP_TRACE("list screen: first frame shown"); }
            }
            SDL_Delay(15);
        }

        SDL_SetRenderTarget(pRenderer, pPrevTarget);
        SDL_RenderSetScale(pRenderer, prevScaleX, prevScaleY);
        BlockingInput::Flush();
        return result;
    }

    void ShowMessage(SDL_Renderer* pRenderer, const std::string& title, const std::vector<std::string>& lines)
    {
        ListScreen s;
        s.title = title;
        s.footer = lines;
        s.footer.push_back("");
        s.footer.push_back(std::string(Platform::AcceptButtonName()) + Lang::Tr(" - dalej", " - continue"));
        s.items.clear();
        s.backCancels = true;
        Uint32 start = SDL_GetTicks();
        s.tick = [start](ListScreen&) -> int
        {
            // Accept closes it (no items, so poll here); ignore the first
            // moment so a held button does not skip the message.
            if (SDL_GetTicks() - start < 400) { BlockingInput::Reset(); return -2; }
            return -2;
        };
        // Items are empty: Accept is handled by giving one invisible item.
        s.items.push_back("");
        Run(pRenderer, s);
    }

    void DrawText(SDL_Renderer* pRenderer, const std::string& text, int px, int x, int y, int align, SDL_Color fill)
    {
        if (text.empty()) return;
        auto key = std::make_pair(text + "#" + std::to_string(fill.r) + "," + std::to_string(fill.g), px);
        auto it = g_TextCache.find(key);
        if (it == g_TextCache.end())
        {
            if (g_TextCache.size() > 96) FreeTextCache();
            Text t = MakeText(pRenderer, OpenFont(px), text, fill, kEdge, std::max(1, px / 14));
            it = g_TextCache.insert(std::make_pair(key, t)).first;
        }
        const Text& t = it->second;
        if (!t.tex) return;
        SDL_Rect dst = { x, y, t.w, t.h };
        if (align == 0) dst.x -= t.w / 2;
        else if (align > 0) dst.x -= t.w;
        SDL_RenderCopy(pRenderer, t.tex, NULL, &dst);
    }

    void FreeTextCache()
    {
        for (auto& kv : g_TextCache) FreeText(kv.second);
        g_TextCache.clear();
    }
}
