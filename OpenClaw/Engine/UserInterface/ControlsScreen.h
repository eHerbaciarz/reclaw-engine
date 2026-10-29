#pragma once

// Options > Controls drawn like the original controls screen: a title and one
// row per action - the controller buttons right-aligned in the key column,
// the action in the game's own menu letters (GameFont, Polish release). The
// button badges are the port's own pictures (CONTROLS/BADGES/*.png, made by
// scripts/make_controls_image.py). Experimental actions (save states,
// rewind) are listed only when experimental modes are on.

#include <memory>

class Image;

namespace ControlsScreen
{
    // nullptr when GameFont is not available (the menu keeps its picture).
    std::shared_ptr<Image> Build();
}
