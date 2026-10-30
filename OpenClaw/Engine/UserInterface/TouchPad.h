#pragma once

// On-screen controls for touch screens (Android): a D-pad on the left, the
// action buttons on the right (jump, shoot, sword, lift) with Claw himself as
// their icons - frames of his own animations from the player's CLAW.REZ,
// played while the button is held - plus weapon change, map and pause.
//
// Shown while playing (not in menus) on a touch screen; touches on it become
// gamepad buttons (AppEvents), so everything the pad does works the same.
// Hidden while a real controller is being used; a touch brings it back.

#include <SDL2/SDL.h>

namespace TouchPad
{
    // Takes finger events (and the mouse events SDL makes from them) while
    // the pad is active. True = the event was used.
    bool HandleEvent(const SDL_Event& evt);

    // Draws the pad over the game (HumanView, after the HUD).
    void Render(SDL_Renderer* renderer);

    // A real controller was used: hide the pad until the next touch.
    void OnControllerUsed();

    // Releases every held button (level change, pause menu).
    void ReleaseAll();

    bool IsEnabled();
}
