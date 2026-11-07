// ReClaw GPU shims (3DS, PS3): see-through pixels of a picture take the
// colour of an opaque neighbour. Smooth (bilinear) sampling mixes a sprite's
// edge with the pixels next to it; their own colour (usually black) made
// dark outlines. ARGB8888 pixels, alpha in the top byte; the result is a
// copy (out: w*h pixels, pitch w).
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

static inline const uint32_t* ReclawBleedAlpha(const uint32_t* px, int pitch, int w, int h, std::vector<uint32_t>& out)
{
    bool any = false;
    for (int y = 0; y < h && !any; ++y)
        for (int x = 0; x < w; ++x)
            if (!(px[(size_t)y * pitch + x] >> 24)) { any = true; break; }
    if (!any) return nullptr;   // opaque: nothing to do
    out.resize((size_t)w * h);
    for (int y = 0; y < h; ++y) memcpy(&out[(size_t)y * w], px + (size_t)y * pitch, (size_t)w * 4);
    auto at = [&](int x, int y) { return px[(size_t)y * pitch + x]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            if (at(x, y) >> 24) continue;
            uint32_t n;
            if (x > 0 && (at(x - 1, y) >> 24)) n = at(x - 1, y);
            else if (x + 1 < w && (at(x + 1, y) >> 24)) n = at(x + 1, y);
            else if (y > 0 && (at(x, y - 1) >> 24)) n = at(x, y - 1);
            else if (y + 1 < h && (at(x, y + 1) >> 24)) n = at(x, y + 1);
            else continue;
            out[(size_t)y * w + x] = n & 0x00FFFFFFu;
        }
    return out.data();
}
