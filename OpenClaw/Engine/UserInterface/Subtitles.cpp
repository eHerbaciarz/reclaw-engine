#include "../Graphics2D/ViewportRestore.h"
#include "Subtitles.h"

#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "../GameApp/BaseGameApp.h"
#include "../Platform/Platform.h"
#include "../Resource/ResourceMgr.h"
#include "../Resource/ResourceCache.h"
#include "../Util/PortSettings.h"
#include "Lang.h"

namespace
{
    struct Cue
    {
        uint32_t startMs = 0;
        uint32_t endMs = 0;
        std::string text;
    };

    bool g_SettingLoaded = false;
    bool g_Enabled = true;

    // Free text (cheat message)
    std::string g_Text;
    uint32_t g_TextUntil = 0;

    // Running SRT track
    std::vector<Cue> g_Track;
    uint32_t g_TrackStart = 0;
    bool g_TrackRunning = false;

    // name -> parsed track (empty vector = no such track)
    std::map<std::string, std::vector<Cue>> g_TrackCache;
    bool g_ZipIndexed = false;
    std::map<std::string, std::string> g_ZipFiles;   // lower-case name -> path in ASSETS.ZIP

    TTF_Font* g_pFont = nullptr;
    int g_FontPx = 0;
    std::string g_DrawnText;
    SDL_Texture* g_pTexture = nullptr;
    SDL_Renderer* g_pTextureRenderer = nullptr;
    int g_TexW = 0, g_TexH = 0;

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // "00:01:02,345" -> ms
    bool ParseTime(const std::string& s, uint32_t& out)
    {
        int h = 0, m = 0, sec = 0, ms = 0;
        char sep1, sep2, sep3;
        std::istringstream in(s);
        if (!(in >> h >> sep1 >> m >> sep2 >> sec >> sep3 >> ms)) return false;
        out = (uint32_t)(((h * 60 + m) * 60 + sec) * 1000 + ms);
        return true;
    }

    std::string StripTags(const std::string& s)
    {
        std::string out;
        bool inTag = false;
        for (char c : s)
        {
            if (c == '<' || c == '{') inTag = true;
            else if ((c == '>' || c == '}') && inTag) inTag = false;
            else if (!inTag) out += c;
        }
        return out;
    }

    std::vector<Cue> ParseSrt(std::string data)
    {
        std::vector<Cue> cues;
        if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
            data.erase(0, 3);   // UTF-8 BOM
        std::istringstream in(data);
        std::string line;
        Cue cue;
        enum { Index, Times, Text } state = Index;
        auto flush = [&]()
        {
            if (state == Text && !cue.text.empty()) cues.push_back(cue);
            cue = Cue();
            state = Index;
        };
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty())
            {
                flush();
                continue;
            }
            size_t arrow = line.find("-->");
            if (state != Text && arrow != std::string::npos)
            {
                if (ParseTime(line.substr(0, arrow), cue.startMs) && ParseTime(line.substr(arrow + 3), cue.endMs))
                    state = Text;
                continue;
            }
            if (state == Text)
            {
                if (!cue.text.empty()) cue.text += "\n";
                cue.text += StripTags(line);
            }
            // otherwise: the cue number
        }
        flush();
        return cues;
    }

    void IndexZip()
    {
        if (g_ZipIndexed || !g_pApp || !g_pApp->GetResourceMgr()) return;
        g_ZipIndexed = true;
        if (!g_pApp->GetResourceMgr()->VHasResourceCache(CUSTOM_RESOURCE)) return;
        std::shared_ptr<ResourceCache> pCache = g_pApp->GetResourceMgr()->VGetResourceCacheFromName(CUSTOM_RESOURCE);
        if (!pCache) return;
        for (const std::string& path : pCache->Match("*subtitles/*.srt"))
        {
            std::string p = Lower(path);
            size_t at = p.find("subtitles/");
            std::string name = p.substr(at + 10);
            name = name.substr(0, name.size() - 4);
            g_ZipFiles[name] = path;
        }
        LOG("Subtitles: " + ToStr((int)g_ZipFiles.size()) + " tracks in ASSETS.ZIP");
    }

    bool ReadFile(const std::string& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.good()) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    const std::vector<Cue>& FindTrack(const std::string& rawName)
    {
        std::string name = Lower(rawName);
        auto it = g_TrackCache.find(name);
        if (it != g_TrackCache.end()) return it->second;

        // lang/<code>/subtitles/movies/movie_01.srt for "movie_01",
        // lang/<code>/subtitles/dialogue/<sound path>.srt for "sounds/<sound path>";
        // the current language first, then English.
        std::string rel;
        if (name.compare(0, 7, "sounds/") == 0) rel = "subtitles/dialogue/" + name.substr(7) + ".srt";
        else if (name.compare(0, 6, "movie_") == 0) rel = "subtitles/movies/" + name + ".srt";
        else rel = "subtitles/" + name + ".srt";
        std::vector<std::string> codes = { Lang::Code() };
        if (codes[0] != "en") codes.push_back("en");
        const std::string lang = codes[0];
        const std::string names[2] = { name + "_" + lang, name };
        std::string data;
        bool found = false;
        // Remastered movies: also next to them, movies/movie_01_pl.srt or
        // movies/movie_01.srt
        if (name.compare(0, 6, "movie_") == 0)
        {
            const std::string dir = Platform::RootDir() + "movies/";
            found = ReadFile(dir + name + "_" + lang + ".srt", data) || ReadFile(dir + name + ".srt", data);
        }
        for (const std::string& code : codes)
        {
            if (found) break;
            if (ReadFile(Platform::LangDir(code) + rel, data) ||
                ReadFile(Platform::BundlePath("lang/" + code + "/" + rel), data))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            IndexZip();
            for (const std::string& n : names)
            {
                auto z = g_ZipFiles.find(n);
                if (z == g_ZipFiles.end()) continue;
                Resource resource(z->second);
                std::shared_ptr<ResourceHandle> h = g_pApp->GetResourceMgr()->VGetHandle(&resource, CUSTOM_RESOURCE);
                if (h && h->GetDataBuffer())
                {
                    data.assign(h->GetDataBuffer(), h->GetSize());
                    found = true;
                    break;
                }
            }
        }
        std::vector<Cue> cues;
        if (found)
        {
            cues = ParseSrt(data);
            LOG("Subtitles: track " + name + " (" + ToStr((int)cues.size()) + " lines)");
        }
        return g_TrackCache.emplace(name, cues).first->second;
    }

    std::string CurrentText()
    {
        uint32_t now = SDL_GetTicks();
        if (!g_Text.empty() && (int32_t)(g_TextUntil - now) > 0) return g_Text;
        if (g_TrackRunning)
        {
            uint32_t t = now - g_TrackStart;
            bool anyLeft = false;
            for (const Cue& c : g_Track)
            {
                if (t >= c.startMs && t < c.endMs) return c.text;
                if (c.endMs > t) anyLeft = true;
            }
            if (!anyLeft) g_TrackRunning = false;
        }
        return std::string();
    }

    bool EnsureFont(int px)
    {
        if (g_pFont && g_FontPx == px) return true;
        if (!TTF_WasInit() && TTF_Init() != 0) return false;
        if (g_pFont) TTF_CloseFont(g_pFont);
        std::string path = Platform::BundlePath("subtitles.ttf");
        g_pFont = TTF_OpenFont(path.c_str(), px);
        if (!g_pFont) g_pFont = TTF_OpenFont(Platform::SplashFont().c_str(), px);
        g_FontPx = px;
        g_DrawnText.clear();   // re-render with the new size
        return g_pFont != nullptr;
    }

    // White text with a black outline, lines centred; one texture per text.
    SDL_Texture* TextTexture(SDL_Renderer* r, const std::string& text, int maxWidth)
    {
        if (g_pTexture && g_DrawnText == text && g_pTextureRenderer == r) return g_pTexture;
        if (g_pTexture) SDL_DestroyTexture(g_pTexture);
        g_pTexture = nullptr;
        g_DrawnText = text;
        g_pTextureRenderer = r;

        // split into lines, then wrap long lines
        std::vector<std::string> lines;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            std::string current;
            std::istringstream words(line);
            std::string word;
            while (words >> word)
            {
                std::string attempt = current.empty() ? word : current + " " + word;
                int w = 0;
                TTF_SizeUTF8(g_pFont, attempt.c_str(), &w, NULL);
                if (w > maxWidth && !current.empty())
                {
                    lines.push_back(current);
                    current = word;
                }
                else current = attempt;
            }
            if (!current.empty()) lines.push_back(current);
        }
        if (lines.empty()) return nullptr;

        const int outline = std::max(1, g_FontPx / 12);
        const int lineH = TTF_FontLineSkip(g_pFont);
        std::vector<SDL_Surface*> rendered;
        int width = 0;
        for (const std::string& l : lines)
        {
            TTF_SetFontOutline(g_pFont, outline);
            SDL_Surface* back = TTF_RenderUTF8_Blended(g_pFont, l.c_str(), SDL_Color{ 0, 0, 0, 255 });
            TTF_SetFontOutline(g_pFont, 0);
            SDL_Surface* front = TTF_RenderUTF8_Blended(g_pFont, l.c_str(), SDL_Color{ 255, 255, 255, 255 });
            if (back && front)
            {
                SDL_Rect inner = { outline, outline, front->w, front->h };
                SDL_SetSurfaceBlendMode(front, SDL_BLENDMODE_BLEND);
                SDL_BlitSurface(front, NULL, back, &inner);
            }
            if (front) SDL_FreeSurface(front);
            if (back)
            {
                rendered.push_back(back);
                width = std::max(width, back->w);
            }
        }
        if (rendered.empty()) return nullptr;
        int height = lineH * (int)(rendered.size() - 1) + rendered.back()->h;
        SDL_Surface* all = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
        if (all)
        {
            SDL_FillRect(all, NULL, SDL_MapRGBA(all->format, 0, 0, 0, 0));
            for (size_t i = 0; i < rendered.size(); ++i)
            {
                SDL_Rect dst = { (width - rendered[i]->w) / 2, (int)i * lineH, rendered[i]->w, rendered[i]->h };
                SDL_SetSurfaceBlendMode(rendered[i], SDL_BLENDMODE_NONE);
                SDL_BlitSurface(rendered[i], NULL, all, &dst);
            }
            g_pTexture = SDL_CreateTextureFromSurface(r, all);
            g_TexW = width;
            g_TexH = height;
            SDL_FreeSurface(all);
        }
        for (SDL_Surface* s : rendered) SDL_FreeSurface(s);
        return g_pTexture;
    }
}

namespace Subtitles
{
    bool IsEnabled()
    {
        if (!g_SettingLoaded)
        {
            g_Enabled = PortSettings::GetBool("subtitles", true);
            g_SettingLoaded = true;
        }
        return g_Enabled;
    }

    void SetEnabled(bool enabled)
    {
        g_Enabled = enabled;
        g_SettingLoaded = true;
        PortSettings::SetBool("subtitles", enabled);
        if (!enabled) StopTrack();
    }

    void ShowText(const std::string& text, uint32_t ms)
    {
        g_Text = text;
        g_TextUntil = SDL_GetTicks() + ms;
    }

    bool StartTrack(const std::string& name)
    {
        if (!IsEnabled()) return false;
        const std::vector<Cue>& cues = FindTrack(name);
        if (cues.empty()) return false;
        g_Track = cues;
        g_TrackStart = SDL_GetTicks();
        g_TrackRunning = true;
        return true;
    }

    void StopTrack()
    {
        g_TrackRunning = false;
        g_Track.clear();
    }

    void OnSoundPlayed(const std::string& soundPath)
    {
        if (!IsEnabled() || soundPath.empty()) return;
        std::string p = Lower(soundPath);
        while (!p.empty() && p[0] == '/') p.erase(0, 1);
        size_t dot = p.rfind('.');
        if (dot != std::string::npos) p.erase(dot);
        StartTrack("sounds/" + p);
    }

    void Render(SDL_Renderer* r)
    {
        if (!r) return;
        std::string text = CurrentText();
        if (text.empty()) return;

        // Draw in target pixels, whatever scale / viewport the game set.
        float sx = 1.0f, sy = 1.0f;
        SDL_RenderGetScale(r, &sx, &sy);
        SDL_Rect viewport;
        SDL_RenderGetViewport(r, &viewport);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_RenderSetViewport(r, NULL);

        int w = 0, h = 0;
        if (SDL_Texture* target = SDL_GetRenderTarget(r)) SDL_QueryTexture(target, NULL, NULL, &w, &h);
        else SDL_GetRendererOutputSize(r, &w, &h);

        if (w > 0 && h > 0 && EnsureFont(std::max(10, h / 20)))
        {
            if (SDL_Texture* tex = TextTexture(r, text, w * 9 / 10))
            {
                SDL_Rect dst = { (w - g_TexW) / 2, h - g_TexH - h / 16, g_TexW, g_TexH };
                SDL_RenderCopy(r, tex, NULL, &dst);
            }
        }

        SDL_RenderSetScale(r, sx, sy);
        ReclawRestoreViewport(r, viewport, sx, sy);
    }

    void FreeTextures()
    {
        if (g_pTexture) SDL_DestroyTexture(g_pTexture);
        g_pTexture = nullptr;
        g_pTextureRenderer = nullptr;
        g_DrawnText.clear();
    }
}
