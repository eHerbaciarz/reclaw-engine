#pragma once
// Pixel-art upscaling filters (Options > Graphics > filter, experimental):
// the game's pictures made twice as big when they are loaded, with the edges
// worked out by the filter instead of plain bigger pixels. ARGB8888 pixels
// (alpha in the top byte); out gets (2w) x (2h) pixels.
#include <cstdint>
#include <vector>

namespace PixelFilters
{
    enum Kind
    {
        None = 0,
        Scale2x = 1,   // EPX / Scale2x: sharp, only straight and diagonal edges
        Sai2x = 2,     // 2xSaI (Kreed): smooth, blends the edges
        Xbr2x = 3,     // xBR (Hyllian, level 1): finds edges of any slope
        Count = 4
    };

    void Upscale2x(Kind kind, const uint32_t* src, int w, int h, std::vector<uint32_t>& out);
    const char* Name(Kind kind);
}
