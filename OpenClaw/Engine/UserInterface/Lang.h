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

    // config: value of <Language> ("auto"/"", "pl", "en", or the code of a
    // translation in lang/<code>/, e.g. "de": English plus that translation).
    void Detect(const std::string& config);

    // Folder name of the current language in lang/: "pl", "en", "de"...
    std::string Code();

    // The text for the current language. lang/<code>/menu/strings.txt can
    // replace any of them: lines "English text = translation" (the English
    // text is the key; \n for a new line; "quotes" keep spaces at the ends;
    // # starts a comment). scripts/make_lang_strings.py writes the list.
    const char* Tr(const char* polish, const char* english);
}
