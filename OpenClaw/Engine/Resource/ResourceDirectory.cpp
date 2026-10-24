#include "ResourceDirectory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace
{
    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // "LEVEL1\\Tiles/x.PID" -> "/level1/tiles/x.pid"; also drops a trailing '/'.
    std::string NormalizeName(const std::string& name)
    {
        std::string out = ToLower(name);
        std::replace(out.begin(), out.end(), '\\', '/');
        if (out.empty() || out[0] != '/') out.insert(out.begin(), '/');
        while (out.size() > 1 && out.back() == '/') out.pop_back();
        return out;
    }

    bool StatPath(const std::string& path, struct stat& st)
    {
        return stat(path.c_str(), &st) == 0;
    }

    bool HasChildDir(const std::string& dir, const char* lowerName)
    {
        DIR* pDir = opendir(dir.c_str());
        if (!pDir) return false;
        bool found = false;
        while (struct dirent* pEntry = readdir(pDir))
        {
            if (ToLower(pEntry->d_name) == lowerName)
            {
                struct stat st;
                found = StatPath(dir + "/" + pEntry->d_name, st) && S_ISDIR(st.st_mode);
                if (found) break;
            }
        }
        closedir(pDir);
        return found;
    }
}

ResourceDirectory::ResourceDirectory(const std::string& rootPath)
    : m_RootPath(rootPath)
{
    while (m_RootPath.size() > 1 && m_RootPath.back() == '/') m_RootPath.pop_back();
}

bool ResourceDirectory::LooksLikeAssetsFolder(const std::string& path)
{
    struct stat st;
    if (path.empty() || !StatPath(path, st) || !S_ISDIR(st.st_mode)) return false;
    return HasChildDir(path, "claw") || HasChildDir(path, "level1");
}

void ResourceDirectory::Scan(const std::string& dir, const std::string& relative, int depth)
{
    if (depth > 10) return;
    DIR* pDir = opendir(dir.c_str());
    if (!pDir) return;
    while (struct dirent* pEntry = readdir(pDir))
    {
        std::string fileName = pEntry->d_name;
        if (fileName == "." || fileName == "..") continue;
        std::string full = dir + "/" + fileName;
        std::string rel = relative + "/" + ToLower(fileName);
        struct stat st;
        if (!StatPath(full, st)) continue;
        if (S_ISDIR(st.st_mode))
        {
            Scan(full, rel, depth + 1);
        }
        else if (m_ByName.find(rel) == m_ByName.end())
        {
            m_ByName[rel] = m_Files.size();
            m_Files.push_back({ rel, full, (int32)st.st_size });
            m_ByDir[rel.substr(0, rel.find_last_of('/'))].push_back(rel);
        }
    }
    closedir(pDir);
}

bool ResourceDirectory::VOpen()
{
    m_Files.clear();
    m_ByName.clear();
    m_ByDir.clear();
    Scan(m_RootPath, "", 0);
    if (m_Files.empty())
    {
        LOG_ERROR("No game files found in assets folder: " + m_RootPath);
        return false;
    }
    LOG("Using unpacked game data: " + std::to_string(m_Files.size()) + " files in " + m_RootPath);
    return true;
}

const ResourceDirectory::Entry* ResourceDirectory::Find(const std::string& resourceName) const
{
    auto it = m_ByName.find(NormalizeName(resourceName));
    return it == m_ByName.end() ? NULL : &m_Files[it->second];
}

int32 ResourceDirectory::VGetRawResourceSize(Resource* r)
{
    const Entry* pEntry = Find(r->GetName());
    if (!pEntry)
    {
        LOG_ERROR("Could not locate: " + r->GetName() + " in assets folder: " + m_RootPath);
        return -1;
    }
    return pEntry->size;
}

int32 ResourceDirectory::VGetRawResource(Resource* r, char* outBuffer)
{
    const Entry* pEntry = Find(r->GetName());
    if (!pEntry || !outBuffer)
    {
        LOG_ERROR("Could not locate: " + r->GetName() + " in assets folder: " + m_RootPath);
        return -1;
    }
    FILE* f = fopen(pEntry->filePath.c_str(), "rb");
    if (!f)
    {
        LOG_ERROR("Could not open: " + pEntry->filePath);
        return -1;
    }
    size_t read = fread(outBuffer, 1, (size_t)pEntry->size, f);
    fclose(f);
    if (read != (size_t)pEntry->size)
    {
        LOG_ERROR("Short read: " + pEntry->filePath);
        return -1;
    }
    return pEntry->size;
}

std::string ResourceDirectory::VGetResourceName(int32 num) const
{
    if (num < 0 || num >= (int32)m_Files.size()) return "NOT_FOUND";
    return m_Files[num].name;
}

std::vector<std::string> ResourceDirectory::GetAllFilesInDirectory(const char* directoryPath)
{
    auto it = m_ByDir.find(NormalizeName(directoryPath ? directoryPath : ""));
    return it == m_ByDir.end() ? std::vector<std::string>() : it->second;
}
