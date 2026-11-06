#include "Lang.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include "../GameApp/BaseGameApp.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ResourceMgr.h"

#include "../Platform/Platform.h"

namespace
{
    Lang::Language g_Language = Lang::Polish;
    std::string g_Code;                                        // empty: pl / en
    std::unordered_map<std::string, std::string> g_Strings;    // English -> translation
    std::string g_StringsFor;                                  // code g_Strings was loaded for
    bool g_StringsLoaded = false;

    std::string Trim(const std::string& s)
    {
        size_t start = s.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;   // UTF-8 BOM
        size_t a = s.find_first_not_of(" \t\r", start);
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    }

    // "text" keeps its spaces at the ends; \n is a new line.
    std::string Unescape(std::string s)
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; }
            else out += s[i];
        }
        return out;
    }

    void LoadStrings()
    {
        std::string code = Lang::Code();
        if (g_StringsLoaded && g_StringsFor == code) return;
        g_StringsLoaded = true;
        g_StringsFor = code;
        g_Strings.clear();
#ifndef __EMSCRIPTEN__
        // The player's folder first, then the files bundled with the port.
        const std::string files[] = {
            Platform::LangDir(code) + "menu/strings.txt",
            Platform::BundlePath("lang/" + code + "/menu/strings.txt"),
        };
        for (const std::string& file : files)
        {
            std::ifstream in(file);
            if (!in.good()) continue;
            std::string line;
            while (std::getline(in, line))
            {
                std::string t = Trim(line);
                if (t.empty() || t[0] == '#') continue;
                size_t eq = t.find(" = ");
                if (eq == std::string::npos) continue;
                std::string key = Unescape(Trim(t.substr(0, eq)));
                std::string value = Unescape(Trim(t.substr(eq + 3)));
                if (!key.empty() && !value.empty() && !g_Strings.count(key)) g_Strings[key] = value;
            }
            LOG("Language: texts from " + file);
        }
#endif
    }
}

namespace Lang
{
    Language Get() { return g_Language; }
    void Set(Language language) { g_Language = language; g_Code.clear(); g_StringsLoaded = false; }

    std::string Code()
    {
        if (!g_Code.empty()) return g_Code;
        return g_Language == Polish ? "pl" : "en";
    }

    const char* Tr(const char* polish, const char* english)
    {
        LoadStrings();
        if (!g_Strings.empty())
        {
            auto it = g_Strings.find(english);
            if (it != g_Strings.end()) return it->second.c_str();
        }
        return g_Language == Polish ? polish : english;
    }

    void Detect(const std::string& config)
    {
        std::string value = config;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        g_Code.clear();
        g_StringsLoaded = false;   // read lang/<code>/menu/strings.txt again
        if (value == "pl" || value == "polish" || value == "polski")
        {
            g_Language = Polish;
            LOG("Language: Polish (config)");
            return;
        }
        if (value == "en" || value == "english")
        {
            g_Language = English;
            LOG("Language: English (config)");
            return;
        }

        // Another translation (lang/<code>/): English texts as the base.
        if (value.size() >= 2 && value.size() <= 5 && value != "auto" &&
            value.find_first_not_of("abcdefghijklmnopqrstuvwxyz-_") == std::string::npos)
        {
            g_Language = English;
            g_Code = value;
            LOG("Language: " + value + " (config, English base)");
            return;
        }

        // "auto": the release of the game data. The folder (data/pl/,
        // data/en/) says it; otherwise the menu picture "DIFFICULTY" does
        // (Polish "POZIOM TRUDNOSCI" is 363 px wide, English 220).
        const std::string rez = g_pApp ? g_pApp->GetGameConfig()->rezArchive : std::string();
        std::string lower = rez;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("/pl/") != std::string::npos) { g_Language = Polish; LOG("Language: Polish (data/pl)"); return; }
        if (lower.find("/en/") != std::string::npos) { g_Language = English; LOG("Language: English (data/en)"); return; }
        if (g_pApp && g_pApp->GetResourceMgr())
        {
            Resource res("/STATES/MENU/IMAGES/MAIN/OPTIONS/003_DIFFICULTY.PCX");
            shared_ptr<ResourceHandle> h = g_pApp->GetResourceMgr()->VGetHandle(&res);
            if (h && h->GetDataBuffer() && h->GetSize() > 12)
            {
                const unsigned char* d = (const unsigned char*)h->GetDataBuffer();
                int w = (d[8] | (d[9] << 8)) - (d[4] | (d[5] << 8)) + 1;   // PCX header, little-endian
                if (w == 220) { g_Language = English; LOG_WARNING("Language: English (menu pictures of the English release)"); return; }
                if (w == 363) { g_Language = Polish; LOG_WARNING("Language: Polish (menu pictures of the Polish release)"); return; }
            }
        }
        g_Language = Polish;
        LOG_WARNING("Language: Polish (default, release not identified)");
    }
}
