#pragma once

// The second screen of the 3DS (bottom screen, 320x240) and the Wii U
// (GamePad, 854x480): the main menu's map picture as the background, and on
// top of it the player's status - level, treasure score, health, lives,
// ammunition with the selected weapon, power-up time and, in a race, the
// race clock. The game itself runs on the top screen / the TV.
//
// Drawn with its own window and renderer; redrawn only when something on it
// changes (the 3DS renders in software).

namespace SecondScreen
{
    // Creates the window when the platform has a second screen.
    void Init();
    void Shutdown();
    bool IsActive();

    // Once per frame, after the main screen has been drawn.
    void Update();
}
