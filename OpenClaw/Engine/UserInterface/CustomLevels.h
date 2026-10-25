#pragma once

// Custom levels: .WWD files made with the original level editor (WapWorld),
// kept in a "Custom" folder next to the game data (as in the original game,
// where they sat in Claw\Custom). Each one reuses the graphics, sounds and
// enemies of one of the 14 original levels, named in its header
// (e.g. "LEVEL5\TILES"), and has its own start position.
//
// Pick() shows a menu-styled list of the files found and blocks until the
// player chooses one (A) or goes back (B).

#include <string>
#include <vector>

struct SDL_Renderer;

namespace CustomLevels
{
    struct Entry
    {
        std::string path;       // file on disk
        std::string fileName;
        std::string levelName;  // from the WWD header (UTF-8)
        std::string author;
        int baseLevel = 0;      // original level whose assets it uses (1-14), 0 if unknown
        int startX = 0, startY = 0;
    };

    void SetSearchRoots(const std::vector<std::string>& roots);

    std::vector<Entry> List();

    // Returns true and fills `out` when the player picked a level.
    bool Pick(SDL_Renderer* pRenderer, Entry& out);
}
