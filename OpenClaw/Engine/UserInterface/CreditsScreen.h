#pragma once

// Main menu > Credits drawn in the game's own menu letters (GameFont, Polish
// release), like the other menu pages. Other releases keep the picture made
// by scripts/make_credits_image.py.

#include <memory>

class Image;

namespace CreditsScreen
{
    // nullptr when GameFont is not available (the menu keeps its picture).
    std::shared_ptr<Image> Build();
}
