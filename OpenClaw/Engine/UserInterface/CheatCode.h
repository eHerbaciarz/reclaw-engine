#pragma once
#include <string>

// Secret code in the pause menu: hold R (keyboard: right Shift) and press
// Down, Down, Left, Right, Up. A subtitle answers ("Nie dość, że pirat to
// jeszcze oszust") and from then on the pause menu has a "Cheats" entry
// (remembered in port_settings.txt).

#include <SDL2/SDL.h>

namespace CheatCode
{
    enum class Dir { Up, Down, Left, Right };

    bool IsUnlocked();

    // R held (gamepad right shoulder / keyboard right Shift)
    void SetModifierHeld(bool held);
    bool IsModifierHeld();

    // A direction pressed while the modifier is held. Returns true when this
    // press completed the code (the message is already shown and the code is
    // unlocked).
    bool FeedDirection(Dir dir);

    // The "Cheats" screen: toggles and one-off cheats for the current game.
    void RunCheatsScreen(SDL_Renderer* pRenderer);
    // Cheats page items: is a cheat on ("invincible", "ammo", "jump",
    // "experimental"), and toggle / use one.
    bool IsOn(const std::string& name);
    void Apply(const std::string& action);
}
