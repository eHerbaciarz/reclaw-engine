#pragma once

// Language of the texts this port adds (save state messages, controls,
// credits, race screens). The original game's own texts are images in
// CLAW.REZ; the port's texts follow the language of that REZ.
//
// Lang::Detect() picks it from the REZ (or from <Language> in config.xml:
// "auto", "pl" or "en"); until it runs, Polish is used.

#include <string>

namespace Lang
{
    enum Language
    {
        Polish,
        English,
    };

    Language Get();
    void Set(Language language);

    // config: value of <Language> ("auto"/"", "pl", "en").
    void Detect(const std::string& config);

    // The text for the current language.
    inline const char* Tr(const char* polish, const char* english)
    {
        return Get() == Polish ? polish : english;
    }
}
