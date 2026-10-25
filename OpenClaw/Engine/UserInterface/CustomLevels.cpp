#include "CustomLevels.h"

#ifndef __EMSCRIPTEN__

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include <libwap.h>

#include "../GameApp/BaseGameApp.h"
#include "../Audio/Audio.h"
#include "../Graphics2D/Image.h"
#include "../Resource/ResourceMgr.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Loaders/PcxLoader.h"
#include "../Resource/Loaders/PidLoader.h"
#include "../Resource/Loaders/WavLoader.h"
#include "../Platform/Input/BlockingInput.h"

namespace
{
    std::vector<std::string> g_Roots;

    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    bool IsDir(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    std::string FindChild(const std::string& dir, const std::string& lowerName)
    {
        DIR* d = opendir(dir.c_str());
        if (!d) return "";
        std::string found;
        while (struct dirent* e = readdir(d))
        {
            if (ToLower(e->d_name) == lowerName)
            {
                found = dir + "/" + e->d_name;
                break;
            }
        }
        closedir(d);
        return found;
    }

    // WWD header strings are Windows code page text (Polish levels: CP1250).
    std::string Cp1250ToUtf8(const char* s, size_t maxLen)
    {
        static const struct { unsigned char c; unsigned short u; } kPolish[] = {
            { 0xA5, 0x0104 }, { 0xB9, 0x0105 }, { 0xC6, 0x0106 }, { 0xE6, 0x0107 }, { 0xCA, 0x0118 },
            { 0xEA, 0x0119 }, { 0xA3, 0x0141 }, { 0xB3, 0x0142 }, { 0xD1, 0x0143 }, { 0xF1, 0x0144 },
            { 0xD3, 0x00D3 }, { 0xF3, 0x00F3 }, { 0x8C, 0x015A }, { 0x9C, 0x015B }, { 0x8F, 0x0179 },
            { 0x9F, 0x017A }, { 0xAF, 0x017B }, { 0xBF, 0x017C },
        };
        std::string out;
        for (size_t i = 0; i < maxLen && s[i]; ++i)
        {
            unsigned char c = (unsigned char)s[i];
            unsigned int u = c;
            if (c >= 0x80)
            {
                for (const auto& m : kPolish)
                    if (m.c == c) u = m.u;
            }
            if (u < 0x80) out += (char)u;
            else if (u < 0x800) { out += (char)(0xC0 | (u >> 6)); out += (char)(0x80 | (u & 0x3F)); }
            else { out += (char)(0xE0 | (u >> 12)); out += (char)(0x80 | ((u >> 6) & 0x3F)); out += (char)(0x80 | (u & 0x3F)); }
        }
        // trim
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
        size_t start = out.find_first_not_of(" \t");
        return start == std::string::npos ? std::string() : out.substr(start);
    }

    // "LEVEL5\TILES", "LEVEL12/PALETTES/MAIN.PAL" -> 5 / 12
    int LevelFromPath(const char* s, size_t maxLen)
    {
        std::string p = ToLower(std::string(s, strnlen(s, maxLen)));
        size_t pos = 0;
        while ((pos = p.find("level", pos)) != std::string::npos)
        {
            pos += 5;
            int n = 0, digits = 0;
            while (pos < p.size() && std::isdigit((unsigned char)p[pos]) && digits < 3)
            {
                n = n * 10 + (p[pos] - '0');
                ++pos;
                ++digits;
            }
            if (digits > 0 && n >= 1 && n <= 14) return n;
        }
        return 0;
    }

    bool ReadHeader(const std::string& path, CustomLevels::Entry& e)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        unsigned char h[1524];
        size_t got = fread(h, 1, sizeof(h), f);
        fclose(f);
        if (got < 728) return false;
        uint32_t sig = h[0] | (h[1] << 8) | (h[2] << 16) | ((uint32_t)h[3] << 24);
        if (sig != 1524) return false;
        const char* c = (const char*)h;
        e.levelName = Cp1250ToUtf8(c + 16, 64);
        e.author = Cp1250ToUtf8(c + 80, 64);
        e.baseLevel = LevelFromPath(c + 464, 128);                 // image directory
        if (!e.baseLevel) e.baseLevel = LevelFromPath(c + 592, 128); // palette
        if (!e.baseLevel && got >= 1524)
            for (int i = 0; i < 4 && !e.baseLevel; ++i) e.baseLevel = LevelFromPath(c + 884 + 128 * i, 128);
        memcpy(&e.startX, h + 720, 4);
        memcpy(&e.startY, h + 724, 4);
        return true;
    }

    //---------------------------------------------------------------- picker drawing
    const int kScreenW = 640, kScreenH = 480;
    const int kListTop = 205, kRowH = 34, kVisibleRows = 5;
    const int kBackY = 385;

    struct View
    {
        double scale = 1;
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
        int w = 0, h = 0;   // in output pixels
    };

    // Gold menu-style text: dark outline under a gold fill.
    Text MakeText(SDL_Renderer* r, TTF_Font* font, const std::string& s, SDL_Color fill, SDL_Color edge, int outline)
    {
        Text t;
        if (s.empty() || !font) return t;
        TTF_SetFontOutline(font, outline);
        SDL_Surface* back = TTF_RenderUTF8_Blended(font, s.c_str(), edge);
        TTF_SetFontOutline(font, 0);
        SDL_Surface* front = TTF_RenderUTF8_Blended(font, s.c_str(), fill);
        if (back && front)
        {
            SDL_Rect dst = { outline, outline, front->w, front->h };
            SDL_SetSurfaceBlendMode(front, SDL_BLENDMODE_BLEND);
            SDL_BlitSurface(front, NULL, back, &dst);
            t.tex = SDL_CreateTextureFromSurface(r, back);
            t.w = back->w;
            t.h = back->h;
        }
        if (back) SDL_FreeSurface(back);
        if (front) SDL_FreeSurface(front);
        return t;
    }

    void FreeText(Text& t)
    {
        if (t.tex) SDL_DestroyTexture(t.tex);
        t.tex = nullptr;
    }

    TTF_Font* OpenFont(int px)
    {
        const char* paths[] = {
#ifdef __SWITCH__
            "romfs:/splash.ttf",
#endif
            "splash.ttf",
            "ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf",
            "../../ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf",
        };
        for (const char* p : paths)
            if (TTF_Font* f = TTF_OpenFont(p, px)) return f;
        return nullptr;
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

    shared_ptr<Image> LoadMenuPcx(const char* path, bool colorKey)
    {
        Resource r(path);
        if (!g_pApp->GetResourceMgr()->VGetHandle(&r)) return nullptr;
        return colorKey ? PcxResourceLoader::LoadAndReturnImage(path, true, Image::ColorKeyFromTopLeft())
                        : PcxResourceLoader::LoadAndReturnImage(path);
    }

    std::vector<shared_ptr<Image>> LoadCoins()
    {
        std::vector<shared_ptr<Image>> coins;
        WapPal* pal = nullptr;
        Resource pcxRes("/states/menu/screens/menu.pcx");
        shared_ptr<ResourceHandle> h = g_pApp->GetResourceCache()->GetHandle(&pcxRes);
        if (h && h->GetSize() > 769 && (uint8_t)h->GetDataBuffer()[h->GetSize() - 769] == 0x0C)
            pal = WAP_PalLoadFromData(h->GetDataBuffer() + h->GetSize() - 768, 768);
        if (!pal) return coins;
        for (int i = 1; i <= 9; ++i)
        {
            char path[64];
            snprintf(path, sizeof(path), "/states/menu/images/coins/frame%03d.pid", i);
            Resource r(path);
            if (!g_pApp->GetResourceMgr()->VGetHandle(&r)) continue;
            if (shared_ptr<Image> img = PidResourceLoader::LoadAndReturnImage(path, pal)) coins.push_back(img);
        }
        WAP_PalDestroy(pal);
        return coins;
    }
}

namespace CustomLevels
{
    void SetSearchRoots(const std::vector<std::string>& roots)
    {
        g_Roots = roots;
    }

    std::vector<Entry> List()
    {
        std::vector<Entry> out;
        std::vector<std::string> seen;
        for (const std::string& root : g_Roots)
        {
            std::string dir = FindChild(root, "custom");
            if (dir.empty() || !IsDir(dir)) continue;
            DIR* d = opendir(dir.c_str());
            if (!d) continue;
            while (struct dirent* de = readdir(d))
            {
                std::string name = de->d_name;
                std::string lower = ToLower(name);
                if (lower.size() < 5 || lower.compare(lower.size() - 4, 4, ".wwd") != 0) continue;
                if (std::find(seen.begin(), seen.end(), lower) != seen.end()) continue;
                Entry e;
                e.path = dir + "/" + name;
                e.fileName = name;
                if (!ReadHeader(e.path, e)) continue;
                seen.push_back(lower);
                out.push_back(e);
            }
            closedir(d);
        }
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return ToLower(a.fileName) < ToLower(b.fileName); });
        return out;
    }

    bool Pick(SDL_Renderer* r, Entry& out)
    {
        if (!r) return false;
        std::vector<Entry> levels = List();
        LOG("Custom levels found: " + ToStr((int)levels.size()));

        if (!TTF_WasInit() && TTF_Init() != 0) return false;

        int outW = 0, outH = 0;
        SDL_Texture* pPrevTarget = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, NULL);
        SDL_GetRendererOutputSize(r, &outW, &outH);
        View v;
        v.scale = std::min(outW / (double)kScreenW, outH / (double)kScreenH);
        v.offX = (int)((outW - kScreenW * v.scale) / 2);
        v.offY = (int)((outH - kScreenH * v.scale) / 2);

        TTF_Font* font = OpenFont((int)std::lround(25 * v.scale));
        TTF_Font* small = OpenFont((int)std::lround(13 * v.scale));
        if (!font || !small)
        {
            LOG_ERROR("Custom level picker: font not found");
            if (font) TTF_CloseFont(font);
            if (small) TTF_CloseFont(small);
            SDL_SetRenderTarget(r, pPrevTarget);
            return false;
        }

        shared_ptr<Image> pBackground = LoadMenuPcx("/STATES/MENU/SCREENS/MENU.PCX", false);
        shared_ptr<Image> pTitle = LoadMenuPcx("/STATES/MENU/IMAGES/MAIN/SINGLEPLAYER/005_LOADCUSTOM.PCX", true);
        shared_ptr<Image> pBack = LoadMenuPcx("/STATES/MENU/IMAGES/MAIN/SINGLEPLAYER/012_PREVIOUSMENU.PCX", true);
        shared_ptr<Image> pBackActive = LoadMenuPcx("/STATES/MENU/IMAGES/MAIN/SINGLEPLAYER/013_PREVIOUSMENU.PCX", true);
        std::vector<shared_ptr<Image>> coins = LoadCoins();

        const SDL_Color gold = { 222, 182, 84, 255 }, goldLight = { 255, 238, 160, 255 };
        const SDL_Color edge = { 40, 20, 4, 255 }, parchment = { 250, 230, 180, 255 };
        int outline = std::max(1, (int)std::lround(1.5 * v.scale));

        std::vector<Text> normal, active;
        for (const Entry& e : levels)
        {
            std::string label = e.levelName.empty() ? e.fileName : e.levelName;
            normal.push_back(MakeText(r, font, label, gold, edge, outline));
            active.push_back(MakeText(r, font, label, goldLight, edge, outline));
        }
        Text empty;
        if (levels.empty())
            empty = MakeText(r, small, "No .WWD files in the Custom folder (e.g. /switch/claw/Custom)", parchment, edge, 1);

        // Selection: 0..n-1 levels, n = back.
        int count = (int)levels.size();
        int sel = count > 0 ? 0 : count;
        int scroll = 0;
        Uint32 coinTimer = SDL_GetTicks();
        int coinFrame = 0;
        bool picked = false, done = false;
        std::vector<Text> details(count);

        BlockingInput::Reset();
        while (!done)
        {
            int in = BlockingInput::Poll();
            if (in & BlockingInput::Quit) break;
            if (in & BlockingInput::Up)
            {
                sel = (sel + count) % (count + 1);
                PlayMenuSound("/STATES/MENU/SOUNDS/SELECT.WAV");
            }
            if (in & BlockingInput::Down)
            {
                sel = (sel + 1) % (count + 1);
                PlayMenuSound("/STATES/MENU/SOUNDS/SELECT.WAV");
            }
            if (in & BlockingInput::Back) done = true;
            if (in & BlockingInput::Accept)
            {
                PlayMenuSound("/STATES/MENU/SOUNDS/CLICK.WAV");
                if (sel < count)
                {
                    out = levels[sel];
                    picked = true;
                }
                done = true;
            }
            if (sel < count)
            {
                if (sel < scroll) scroll = sel;
                if (sel >= scroll + kVisibleRows) scroll = sel - kVisibleRows + 1;
            }

            if (SDL_GetTicks() - coinTimer >= 80)
            {
                coinTimer = SDL_GetTicks();
                if (!coins.empty()) coinFrame = (coinFrame + 1) % (int)coins.size();
            }

            // ---- draw
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            if (pBackground && pBackground->GetTexture())
            {
                SDL_Rect bg = v.Map(0, 0, kScreenW, kScreenH);
                SDL_RenderCopy(r, pBackground->GetTexture(), NULL, &bg);
            }
            if (pTitle && pTitle->GetTexture())
            {
                SDL_Rect t = v.Map(320 - pTitle->GetWidth() / 2.0, 161, pTitle->GetWidth(), pTitle->GetHeight());
                SDL_RenderCopy(r, pTitle->GetTexture(), NULL, &t);
            }

            SDL_Rect activeRect = { 0, 0, 0, 0 };
            for (int row = 0; row < kVisibleRows && scroll + row < count; ++row)
            {
                int i = scroll + row;
                Text& t = (i == sel) ? active[i] : normal[i];
                if (!t.tex) continue;
                double maxW = 520 * v.scale;
                double shrink = t.w > maxW ? maxW / t.w : 1.0;
                int w = (int)(t.w * shrink), h = (int)(t.h * shrink);
                SDL_Rect rowRect = v.Map(0, kListTop + row * kRowH, kScreenW, kRowH);
                SDL_Rect dst = { v.offX + (int)(kScreenW * v.scale - w) / 2, rowRect.y + (rowRect.h - h) / 2, w, h };
                SDL_RenderCopy(r, t.tex, NULL, &dst);
                if (i == sel) activeRect = dst;
            }
            // scroll hints
            if (scroll > 0 || scroll + kVisibleRows < count)
            {
                SDL_SetRenderDrawColor(r, gold.r, gold.g, gold.b, 255);
                if (scroll > 0)
                {
                    SDL_Rect a = v.Map(316, kListTop - 8, 8, 3);
                    SDL_RenderFillRect(r, &a);
                }
                if (scroll + kVisibleRows < count)
                {
                    SDL_Rect a = v.Map(316, kListTop + kVisibleRows * kRowH + 2, 8, 3);
                    SDL_RenderFillRect(r, &a);
                }
            }
            if (levels.empty() && empty.tex)
            {
                SDL_Rect dst = { v.offX + (int)(kScreenW * v.scale - empty.w) / 2, v.Map(0, 260, 0, 0).y, empty.w, empty.h };
                SDL_RenderCopy(r, empty.tex, NULL, &dst);
            }

            shared_ptr<Image> back = (sel == count && pBackActive) ? pBackActive : pBack;
            if (back && back->GetTexture())
            {
                SDL_Rect b = v.Map(320 - back->GetWidth() / 2.0, kBackY, back->GetWidth(), back->GetHeight());
                SDL_RenderCopy(r, back->GetTexture(), NULL, &b);
                if (sel == count) activeRect = b;
            }

            // details of the selected level: file, author, base level
            if (sel < count)
            {
                if (!details[sel].tex)
                {
                    const Entry& e = levels[sel];
                    std::string d = e.fileName;
                    if (!e.author.empty()) d += "  -  " + e.author;
                    if (e.baseLevel) d += "  -  LEVEL " + ToStr(e.baseLevel);
                    details[sel] = MakeText(r, small, d, parchment, edge, 1);
                }
                Text& t = details[sel];
                if (t.tex)
                {
                    SDL_Rect dst = { v.offX + (int)(kScreenW * v.scale - t.w) / 2, v.Map(0, 430, 0, 0).y, t.w, t.h };
                    SDL_RenderCopy(r, t.tex, NULL, &dst);
                }
            }

            if (!coins.empty() && activeRect.w > 0)
            {
                shared_ptr<Image> c = coins[coinFrame];
                int cw = (int)(c->GetWidth() * v.scale), ch = (int)(c->GetHeight() * v.scale);
                int gap = (int)(4 * v.scale);
                SDL_Rect l = { activeRect.x - cw - gap, activeRect.y + (activeRect.h - ch) / 2, cw, ch };
                SDL_Rect rr = { activeRect.x + activeRect.w + gap, l.y, cw, ch };
                SDL_RenderCopy(r, c->GetTexture(), NULL, &l);
                SDL_RenderCopy(r, c->GetTexture(), NULL, &rr);
            }
            SDL_RenderPresent(r);
            SDL_Delay(15);
        }

        for (Text& t : normal) FreeText(t);
        for (Text& t : active) FreeText(t);
        for (Text& t : details) FreeText(t);
        FreeText(empty);
        TTF_CloseFont(font);
        TTF_CloseFont(small);
        SDL_SetRenderTarget(r, pPrevTarget);
        BlockingInput::Flush();
        return picked;
    }
}

#else

namespace CustomLevels
{
    void SetSearchRoots(const std::vector<std::string>&) { }
    std::vector<Entry> List() { return {}; }
    bool Pick(SDL_Renderer*, Entry&) { return false; }
}

#endif
