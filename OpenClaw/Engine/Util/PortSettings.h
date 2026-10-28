#pragma once

// Small persistent settings of the port that the original config.xml has no
// place for (subtitles on/off, cheats unlocked...). Stored as key=value lines
// in <data folder>/port_settings.txt and written right away on every change.

#include <string>

namespace PortSettings
{
    bool GetBool(const std::string& key, bool defaultValue);
    void SetBool(const std::string& key, bool value);
    int GetInt(const std::string& key, int defaultValue);
    void SetInt(const std::string& key, int value);
}
