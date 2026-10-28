#include "GameLanguages.h"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

#include <tinyxml.h>

#include "BaseGameApp.h"
#include "../Platform/Platform.h"
#include "../Race/RaceUi.h"
#include "../UserInterface/Lang.h"

namespace
{
    std::vector<GameLanguages::Entry> g_Languages;
    std::string g_Current;
    std::string g_ConfigPath;

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    bool IsDir(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool IsFile(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    // The game data inside a language folder: CLAW.REZ (any case) or Assets.
    std::string FindData(const std::string& dir)
    {
        std::string rez, assets;
        DIR* d = opendir(dir.c_str());
        if (!d) return "";
        while (struct dirent* e = readdir(d))
        {
            std::string name = e->d_name;
            std::string low = Lower(name);
            if (low == "claw.rez" && IsFile(dir + name)) rez = dir + name;
            else if (low == "assets" && IsDir(dir + name)) assets = dir + name;
        }
        closedir(d);
        return !rez.empty() ? rez : assets;
    }

    bool SaveChoice(const std::string& code)
    {
        if (g_ConfigPath.empty()) return false;
        TiXmlDocument doc(g_ConfigPath.c_str());
        if (!doc.LoadFile()) return false;
        TiXmlElement* root = doc.RootElement();
        if (!root) return false;
        TiXmlElement* assets = root->FirstChildElement("Assets");
        if (!assets)
        {
            assets = new TiXmlElement("Assets");
            root->LinkEndChild(assets);
        }
        TiXmlElement* lang = assets->FirstChildElement("Language");
        if (!lang)
        {
            lang = new TiXmlElement("Language");
            assets->LinkEndChild(lang);
        }
        lang->Clear();
        lang->LinkEndChild(new TiXmlText(code.c_str()));
        bool ok = doc.SaveFile();
        LOG(std::string("Language ") + code + (ok ? " saved to " : " NOT saved to ") + g_ConfigPath);
        return ok;
    }
}

namespace GameLanguages
{
    void Setup(GameOptions* pOptions, const std::string& configPath)
    {
        g_Languages.clear();
        g_Current.clear();
        g_ConfigPath = configPath;
#ifndef __EMSCRIPTEN__
        const std::string base = Platform::GameDir();
        if (DIR* d = opendir(base.c_str()))
        {
            while (struct dirent* e = readdir(d))
            {
                std::string name = e->d_name;
                if (name.empty() || name[0] == '.') continue;
                std::string dir = base + name + "/";
                if (!IsDir(dir) || Lower(name) == "assets") continue;
                std::string data = FindData(dir);
                if (!data.empty()) g_Languages.push_back(Entry{ Lower(name), data });
            }
            closedir(d);
        }
        std::sort(g_Languages.begin(), g_Languages.end(),
                  [](const Entry& a, const Entry& b) { return a.code < b.code; });
#endif
        if (g_Languages.empty() || !pOptions)
        {
            LOG("Languages: no data/<language>/ folders, using the data as found");
            return;
        }

        std::string wanted = Lower(pOptions->language);
        const Entry* pick = nullptr;
        for (const Entry& e : g_Languages) if (e.code == wanted) pick = &e;
        if (!pick) for (const Entry& e : g_Languages) if (e.code == "en") pick = &e;
        if (!pick) pick = &g_Languages[0];

        g_Current = pick->code;
        pOptions->assetsFolder = "";
        pOptions->rezArchive = pick->data;
        std::string list;
        for (const Entry& e : g_Languages) list += (list.empty() ? "" : ", ") + e.code;
        LOG("Languages: " + list + " - playing " + g_Current + " (" + pick->data + ")");

        // Remember the language, so it stays the same on the next start.
        if (wanted != g_Current)
        {
            pOptions->language = g_Current;
            SaveChoice(g_Current);
        }
    }

    const std::vector<Entry>& Available() { return g_Languages; }
    const std::string& Current() { return g_Current; }
    bool CanChoose() { return g_Languages.size() > 1; }

    std::string DisplayName(const std::string& code)
    {
        static const char* kNames[][2] = {
            { "pl", "Polski" }, { "en", "English" }, { "de", "Deutsch" }, { "fr", "Français" },
            { "es", "Español" }, { "it", "Italiano" }, { "cs", "Čeština" }, { "sk", "Slovenčina" },
            { "hu", "Magyar" }, { "nl", "Nederlands" }, { "pt", "Português" }, { "sv", "Svenska" },
            { "fi", "Suomi" }, { "da", "Dansk" }, { "no", "Norsk" }, { "ru", "Русский" },
        };
        for (const auto& n : kNames) if (code == n[0]) return n[1];
        std::string up = code;
        std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return (char)std::toupper(c); });
        return up;
    }

    void RunMenu()
    {
        if (!g_pApp || g_Languages.empty()) return;
        SDL_Renderer* r = g_pApp->GetRenderer();
        // The language that will be used on the next start (may differ from
        // the one being played after an earlier change).
        std::string chosen = g_pApp->GetGameConfig() ? Lower(g_pApp->GetGameConfig()->language) : g_Current;
        if (chosen.empty()) chosen = g_Current;

        RaceUi::ListScreen s;
        s.title = Lang::Tr("Język", "Language");
        for (size_t i = 0; i < g_Languages.size(); ++i)
        {
            const std::string& code = g_Languages[i].code;
            std::string item = DisplayName(code);
            if (code == chosen) item = "> " + item + " <";
            s.items.push_back(item);
            if (code == chosen) s.selected = (int)i;
        }
        s.footer.push_back(Lang::Tr("Gra uruchomi się w wybranym języku przy następnym starcie.",
                                    "The game starts in the chosen language next time."));
        s.footer.push_back(std::string(Platform::BackButtonName()) + Lang::Tr(" - wróć", " - back"));
        int choice = RaceUi::Run(r, s);
        if (choice < 0 || choice >= (int)g_Languages.size()) return;
        const std::string& code = g_Languages[choice].code;
        if (code == chosen) return;

        if (g_pApp->GetGameConfig()) g_pApp->GetGameConfig()->language = code;
        bool saved = SaveChoice(code);
        if (code == g_Current)
        {
            RaceUi::ShowMessage(r, DisplayName(code),
                { Lang::Tr("Zostaje obecny język.", "The current language stays.") });
            return;
        }
        std::vector<std::string> lines = {
            Lang::Tr("Język zmieni się po ponownym uruchomieniu gry.",
                     "The language changes when you restart the game."),
        };
        if (!saved) lines.push_back(Lang::Tr("Nie udało się zapisać config.xml!", "Could not write config.xml!"));
        RaceUi::ShowMessage(r, DisplayName(code), lines);
    }
}
