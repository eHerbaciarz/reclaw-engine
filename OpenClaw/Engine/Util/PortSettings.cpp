#include "PortSettings.h"

#include <fstream>
#include <map>

#include "../Platform/Platform.h"

namespace
{
    std::map<std::string, std::string> g_Values;
    bool g_Loaded = false;

    std::string FilePath()
    {
        return Platform::DataDir() + "port_settings.txt";
    }

    void Load()
    {
        if (g_Loaded) return;
        g_Loaded = true;
        std::ifstream in(FilePath());
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '#') continue;
            g_Values[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }

    void Save()
    {
        std::ofstream out(FilePath(), std::ios::trunc);
        out << "# Captain Claw port settings\n";
        for (const auto& kv : g_Values) out << kv.first << "=" << kv.second << "\n";
    }
}

namespace PortSettings
{
    bool GetBool(const std::string& key, bool defaultValue)
    {
        Load();
        auto it = g_Values.find(key);
        if (it == g_Values.end()) return defaultValue;
        return it->second == "1" || it->second == "true" || it->second == "yes";
    }

    void SetBool(const std::string& key, bool value)
    {
        Load();
        g_Values[key] = value ? "1" : "0";
        Save();
    }
}
