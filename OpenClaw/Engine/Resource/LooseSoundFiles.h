#pragma once

// Sounds stored as loose files next to CLAW.REZ.
//
// Some releases of Captain Claw (e.g. the digital re-releases) ship the game's
// sounds in a "Sounds" folder beside CLAW.REZ instead of inside the archive.
// The engine asks for sounds by their archive path ("/CLAW/SOUNDS/GUNSHOT.WAV");
// this index maps those paths onto files found in such a folder.
//
// Matching is deliberately tolerant because folder layouts differ between
// releases: case-insensitive, any "sounds" path segment is ignored, and as a
// last resort a file name that is unique in the folder matches on its own.

#include <string>
#include <vector>

namespace LooseSoundFiles
{
    // Directories that may contain the sounds; scanned lazily on first use.
    void SetSearchRoots(const std::vector<std::string>& roots);

    // Returns true and the file path if `resourcePath` maps to a loose file.
    bool Find(const std::string& resourcePath, std::string& outFilePath);
}
