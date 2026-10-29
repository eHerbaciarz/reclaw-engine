#pragma once

// Menu texts made of the game's own menu letters.
//
// The original game has no font file: every menu text is a picture. For the
// Polish release, GameFont cuts single letters out of its menu pictures (in
// the player's CLAW.REZ, at start-up) and puts new texts together from them,
// so menu items the port adds look like the original ones. Nothing from the
// game is stored in the port: only where the letters are (GameFont.cpp).
//
// Letters available: A-Z without H Q V X, Ą Ć Ę Ó Ś Ź Ż Ń, digits none,
// ':' '.' and space. Lower case is drawn as upper case.

#include <memory>
#include <string>

class Image;

namespace GameFont
{
    enum class Look
    {
        Normal,     // gold letters (an inactive menu item)
        Active,     // brighter letters with a light glow (selected item)
        Disabled,   // grey
    };

    // True when the menu pictures of the Polish release are there.
    bool Available();

    // Rows the pictures have above the top of an original 35-pixel menu
    // picture (room for accents); menu items draw them that much higher.
    int ExtraTop();

    // underline: index of the letter to underline (-1 none, -2 the whole
    // text). Lines are separated by '|' and centred. nullptr when unavailable.
    std::shared_ptr<Image> Render(const std::string& utf8, Look look, int underline = 0);
}
