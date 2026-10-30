#pragma once
#include <string>

// Secret code, typed while playing a level (the Konami code): Up, Up, Down,
// Down, Left, Right, Left, Right, then the jump and fire buttons - B, A on
// Nintendo consoles, Cross, Circle on PlayStation (keyboard: arrows, Space,
// Alt). A subtitle answers ("Nie dość, że pirat to jeszcze oszust") and from
// then on the pause menu has a "Cheats" entry - kept in port_settings.txt
// until the saved games are deleted.

#include <SDL2/SDL.h>

namespace CheatCode
{
    enum class Key { Up, Down, Left, Right, Jump, Fire, Other };

    bool IsUnlocked();
    // Saved games deleted: the cheats are locked again.
    void Lock();

    // A button pressed during the game. Returns true when this press
    // completed the code (the message is already shown, the menu unlocked).
    bool Feed(Key key);

    // The "Cheats" screen: toggles and one-off cheats for the current game.
    void RunCheatsScreen(SDL_Renderer* pRenderer);
    // Cheats page items: is a cheat on ("invincible", "ammo", "jump",
    // "experimental"), and toggle / use one.
    bool IsOn(const std::string& name);
    void Apply(const std::string& action);
}
