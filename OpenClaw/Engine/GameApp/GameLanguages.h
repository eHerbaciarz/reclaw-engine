#pragma once

// Language versions of the game data in the ReClaw folder:
//
//   data/pl/CLAW.REZ (+ CLAW.FEC)   Polish release
//   data/en/CLAW.REZ (+ CLAW.FEC)   English release
//   data/<code>/...                 any other release (de, fr, ...)
//
// Each sub-folder of data/ with CLAW.REZ (or an unpacked Assets folder) is
// one language; its name is the language code, also used for lang/<code>/.
// With several of them the player picks one in Options > Language; the choice
// is kept in save/config.xml (<Assets><Language>) and applied on the next
// start (the game data cannot be swapped while the game runs). The first
// start uses "en" when it is there. CLAW.REZ put straight into data/ still
// works as the only language.

#include <string>
#include <vector>

struct GameOptions;

namespace GameLanguages
{
    struct Entry
    {
        std::string code;      // folder name, lower case: "pl", "en"...
        std::string data;      // path of CLAW.REZ or of the Assets folder
    };

    // Startup, after config.xml is read: finds the language folders, picks
    // one (config, else "en", else the first) and points the options at its
    // data. A new choice is written to config.xml.
    void Setup(GameOptions* pOptions, const std::string& configPath);

    const std::vector<Entry>& Available();
    // The language being played ("" with data straight in data/).
    const std::string& Current();
    bool CanChoose();                       // more than one language

    // Name of a language in itself: "Polski", "English", "Deutsch"...
    std::string DisplayName(const std::string& code);

    // Options > Language: the list; a new choice is saved for the next start.
    void RunMenu();
    // Saves a language for the next start (Options > Language page).
    void Choose(const std::string& code);
}
