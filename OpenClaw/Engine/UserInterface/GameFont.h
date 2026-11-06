#pragma once

// Menu texts made of the game's own menu letters.
//
// The original game has no font file: every menu text is a picture. For the
// Polish and English releases, GameFont cuts single letters out of their menu
// pictures (in the player's CLAW.REZ, at start-up) and puts new texts together from them,
// so menu items the port adds look like the original ones. Nothing from the
// game is stored in the port: only where the letters are (GameFont.cpp).
//
// Letters available (Polish release): A-Z without Q X, Ą Ć Ę Ó Ś Ź Ż Ń, and
// for names Í Ö Š Č; ':' '.' '·' '–' and space; no digits. English release:
// A-Z without Q X, Ö Ż, the same marks; other marked letters are drawn plain. Lower case is drawn as
// upper case. L, H, V and the extra marks are put together from other letters.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Image;

namespace GameFont
{
    enum class Look
    {
        Normal,     // gold letters (an inactive menu item)
        Active,     // brighter letters with a light glow (selected item)
        Disabled,   // grey
    };

    // True when the menu pictures of a known release (Polish, English) are there.
    bool Available();

    // Rows the pictures have above the top of an original 35-pixel menu
    // picture (room for accents); menu items draw them that much higher.
    int ExtraTop();

    // underline: index of the letter to underline (-1 none, -2 the whole
    // text). Lines are separated by '|' and centred. nullptr when unavailable.
    std::shared_ptr<Image> Render(const std::string& utf8, Look look, int underline = 0);

    // The same as pixels (RGBA, 0xAABBGGRR) for composing bigger pictures.
    struct Pixels
    {
        int w = 0, h = 0;
        std::vector<uint32_t> px;
    };
    bool RenderPixels(const std::string& utf8, Look look, int underline, Pixels& out);
    std::shared_ptr<Image> ToImage(Pixels& pixels);

    // For composing screens: a smaller copy (percent of the size, pixels
    // averaged) and drawing one picture over another.
    Pixels Scale(const Pixels& p, int percent);
    Pixels Resize(const Pixels& p, int w, int h);
    void Paste(Pixels& dst, const Pixels& src, int x, int y);
    // A soft dark shadow under the letters, moved by (dx, dy), like the one
    // in the original menu pictures (for small texts, where the letters'
    // own shadow shrinks away). The picture grows by dx, dy.
    Pixels WithShadow(const Pixels& p, int dx, int dy, int alpha);
}
