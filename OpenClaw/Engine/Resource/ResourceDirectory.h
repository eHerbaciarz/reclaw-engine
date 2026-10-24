#pragma once

// Game data stored as a folder instead of CLAW.REZ.
//
// Some releases (e.g. the Polish "Kapitan Pazur") ship the contents of
// CLAW.REZ unpacked: an "Assets" folder with CLAW/, GAME/, LEVEL1..14/,
// STATES/ and so on. This class exposes such a folder through the same
// IResourceFile interface as the REZ archive. Resource names use the archive
// convention ("/level1/tiles/action/000.pid", lower case); the folder may use
// any letter case.

#include <map>
#include <string>
#include <vector>

#include "ResourceCache.h"

class ResourceDirectory : public IResourceFile
{
public:
    explicit ResourceDirectory(const std::string& rootPath);
    virtual ~ResourceDirectory() { }

    // True if `path` looks like an unpacked CLAW.REZ (a folder with CLAW/ or LEVEL1/).
    static bool LooksLikeAssetsFolder(const std::string& path);

    virtual bool VOpen();
    virtual std::string VGetName() const { return m_RootPath; }
    virtual int32 VGetRawResourceSize(Resource* r);
    virtual int32 VGetRawResource(Resource* r, char* outBuffer);
    virtual int32 VGetNumResources() const { return (int32)m_Files.size(); }
    virtual std::string VGetResourceName(int32 num) const;
    virtual bool VIsUsingDevelopmentDIrectories() const { return false; }
    virtual std::vector<std::string> GetAllFilesInDirectory(const char* directoryPath);

private:
    struct Entry
    {
        std::string name;      // "/level1/tiles/action/000.pid"
        std::string filePath;  // real path on disk
        int32 size;
    };

    void Scan(const std::string& dir, const std::string& relative, int depth);
    const Entry* Find(const std::string& resourceName) const;

    std::string m_RootPath;
    std::vector<Entry> m_Files;
    std::map<std::string, size_t> m_ByName;                     // name -> index
    std::map<std::string, std::vector<std::string>> m_ByDir;    // "/level1/tiles/action" -> names
};
