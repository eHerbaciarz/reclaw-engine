#pragma once

// The second screen of the 3DS (bottom screen, 320x240) and the Wii U
// (GamePad, 854x480): the main menu's map picture as the background, and on
// top of it the player's status - level, treasure score, health, lives,
// ammunition with the selected weapon, power-up time and, in a race, the
// race clock. The game itself runs on the top screen / the TV.
//
// Drawn with its own window and renderer; redrawn only when something on it
// changes (the 3DS renders in software).

#include <SDL2/SDL.h>

namespace SecondScreen
{
    // Creates the window when the platform has a second screen.
    void Init();
    void Shutdown();
    bool IsActive();
    // 3DS: the level's HUD (treasure, health, lives, weapons) is shown here
    // instead of on the top screen (less to draw there, 3D included).
    bool ShowsHud();

    // Once per frame, after the main screen has been drawn.
    void Update();

    // 3DS: in the menus the bottom screen shows the menu too, and touching it
    // works like the mouse on the menu (moves the sword cursor, picks the
    // item). Takes the touch screen's finger events; true = used.
    bool HandleTouch(const SDL_Event& evt);
}
