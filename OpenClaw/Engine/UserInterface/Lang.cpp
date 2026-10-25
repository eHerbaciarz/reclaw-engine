#include "Lang.h"

#include <algorithm>

#include "../GameApp/BaseGameApp.h"
#include "../Resource/ResourceCache.h"

namespace
{
    Lang::Language g_Language = Lang::Polish;
}

namespace Lang
{
    Language Get() { return g_Language; }
    void Set(Language language) { g_Language = language; }

    void Detect(const std::string& config)
    {
        std::string value = config;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        if (value == "pl" || value == "polish" || value == "polski")
        {
            g_Language = Polish;
            LOG("Language: Polish (config)");
            return;
        }
        if (value == "en" || value == "english")
        {
            g_Language = English;
            LOG("Language: English (config)");
            return;
        }

        // TODO: fingerprint of the Polish / English REZ (rez_lang_probe.py
        // results from MK). Until then the Polish release is assumed.
        g_Language = Polish;
        LOG("Language: Polish (default, REZ not identified yet)");
    }
}
