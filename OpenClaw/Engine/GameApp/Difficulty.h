#ifndef __RECLAW_DIFFICULTY_H__
#define __RECLAW_DIFFICULTY_H__

// Options > Difficulty. Every difficulty has its own saves file, so progress
// made on one does not carry over to another (SAVES.XML is the normal one,
// as before). "difficulty" in port_settings.txt.

#include <string>
#include "../Util/PortSettings.h"

namespace Difficulty
{
    enum Level
    {
        SuperEasy = 0,   // Easy, and the traps that shoot are gone
        Easy = 1,
        Normal = 2,      // the original game
        Hard = 3,
    };

    inline int Get()
    {
        int d = PortSettings::GetInt("difficulty", Normal);
        return (d >= SuperEasy && d <= Hard) ? d : Normal;
    }

    inline void Set(int d)
    {
        if (d < SuperEasy || d > Hard) d = Normal;
        PortSettings::SetInt("difficulty", d);
    }

    inline const char* Name(int d)
    {
        switch (d)
        {
            case SuperEasy: return "super easy";
            case Easy: return "easy";
            case Hard: return "hard";
            default: return "normal";
        }
    }

    // "SAVES.XML" -> "SAVES_EASY.XML" (normal: unchanged)
    inline std::string SavesFile(const std::string& base)
    {
        const char* suffix = "";
        switch (Get())
        {
            case SuperEasy: suffix = "_SUPEREASY"; break;
            case Easy: suffix = "_EASY"; break;
            case Hard: suffix = "_HARD"; break;
            default: return base;
        }
        size_t dot = base.rfind('.');
        if (dot == std::string::npos) return base + suffix;
        return base.substr(0, dot) + suffix + base.substr(dot);
    }

    // Damage Claw takes (not falls into death tiles)
    inline double DamageTakenMultiplier()
    {
        switch (Get())
        {
            case SuperEasy: case Easy: return 0.5;
            case Hard: return 1.5;
            default: return 1.0;
        }
    }

    // Health Claw gets from food and potions
    inline double HealMultiplier()
    {
        return Get() <= Easy ? 1.5 : 1.0;
    }

    // Lives at the start of a new game
    inline int StartLives()
    {
        switch (Get())
        {
            case SuperEasy: case Easy: return 9;
            case Hard: return 3;
            default: return 6;
        }
    }

    inline double BossHealthMultiplier()
    {
        switch (Get())
        {
            case SuperEasy: return 0.5;
            case Easy: return 0.75;
            case Hard: return 1.25;
            default: return 1.0;
        }
    }

    // Super easy: no traps that shoot (dart traps, cannons)
    inline bool ShootingTrapsOff()
    {
        return Get() == SuperEasy;
    }
}

#endif
