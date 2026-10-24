#include "LooseSoundFiles.h"

#include "../Logger/Logger.h"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <map>
#include <sys/stat.h>

namespace
{
    std::vector<std::string> g_Roots;
    bool g_Indexed = false;

    // normalized path -> file;  basename -> files (for the unique-name fallback)
    std::map<std::string, std::string> g_ByPath;
    std::map<std::string, std::vector<std::string>> g_ByName;

    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // "/CLAW/SOUNDS/GUNSHOT.WAV" -> "claw/gunshot.wav"
    std::string Normalize(const std::string& path)
    {
        std::string lower = ToLower(path);
        std::replace(lower.begin(), lower.end(), '\\', '/');
        std::string result;
        size_t pos = 0;
        while (pos <= lower.size())
        {
            size_t next = lower.find('/', pos);
            if (next == std::string::npos) next = lower.size();
            std::string segment = lower.substr(pos, next - pos);
            if (!segment.empty() && segment != "sounds")
            {
                if (!result.empty()) result += '/';
                result += segment;
            }
            pos = next + 1;
        }
        return result;
    }

    std::string BaseName(const std::string& normalized)
    {
        size_t slash = normalized.find_last_of('/');
        return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
    }

    bool IsDirectory(const std::string& path)
    {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    void Scan(const std::string& dir, const std::string& relative, int depth, size_t& count)
    {
        if (depth > 8) return;
        DIR* pDir = opendir(dir.c_str());
        if (!pDir) return;
        while (struct dirent* pEntry = readdir(pDir))
        {
            std::string name = pEntry->d_name;
            if (name == "." || name == "..") continue;
            std::string full = dir + "/" + name;
            std::string rel = relative.empty() ? name : relative + "/" + name;
            if (IsDirectory(full))
            {
                Scan(full, rel, depth + 1, count);
            }
            else if (ToLower(name).size() > 4 && ToLower(name).compare(ToLower(name).size() - 4, 4, ".wav") == 0)
            {
                std::string key = Normalize(rel);
                g_ByPath.emplace(key, full);
                g_ByName[BaseName(key)].push_back(full);
                ++count;
            }
        }
        closedir(pDir);
    }

    void BuildIndex()
    {
        g_Indexed = true;
        for (const std::string& root : g_Roots)
        {
            if (!IsDirectory(root)) continue;
            size_t count = 0;
            Scan(root, "", 0, count);
            if (count > 0)
            {
                LOG_WARNING("Using " + std::to_string(count) + " loose sound files from " + root);
            }
        }
    }
}

namespace LooseSoundFiles
{
    void SetSearchRoots(const std::vector<std::string>& roots)
    {
        g_Roots = roots;
        g_Indexed = false;
        g_ByPath.clear();
        g_ByName.clear();
    }

    bool Find(const std::string& resourcePath, std::string& outFilePath)
    {
        if (!g_Indexed) BuildIndex();
        if (g_ByPath.empty()) return false;

        std::string key = Normalize(resourcePath);
        auto it = g_ByPath.find(key);
        if (it != g_ByPath.end())
        {
            outFilePath = it->second;
            return true;
        }

        auto byName = g_ByName.find(BaseName(key));
        if (byName != g_ByName.end() && byName->second.size() == 1)
        {
            outFilePath = byName->second.front();
            return true;
        }
        return false;
    }
}
