#pragma once

// Input for screens that run their own loop outside the game loop (movies,
// title scroll, custom level picker). Reads the Switch pads directly through
// libnx (SDL's controller may not be opened yet at startup) plus keyboard,
// SDL game controllers on other platforms, and touch/mouse.
//
// Only input events are taken from the SDL queue; device add/remove events
// stay queued for the game.

namespace BlockingInput
{
    enum Button
    {
        None   = 0,
        Up     = 1 << 0,
        Down   = 1 << 1,
        Accept = 1 << 2,   // A / Enter / Space / tap
        Back   = 1 << 3,   // B / Escape / Backspace
        Start  = 1 << 4,   // + / controller Start
        Quit   = 1 << 5,   // window closed / app exiting
    };

    // Buttons pressed since the previous call (bitmask of Button).
    int Poll();

    // Forget anything pressed so far (call when such a screen starts).
    void Reset();

    // Drop input queued during the screen so the game does not react to it.
    void Flush();

    // False when the application is being closed (Switch applet loop).
    bool AppAlive();
}
