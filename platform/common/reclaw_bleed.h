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

// The game's see-through-by-dithering (a checkerboard of opaque and empty
// pixels: clouds, mist, ghosts - how 1997 drew translucency) turned into
// real half-transparency. Drawn bigger and smoothed, the checkerboard came
// out as a grainy pattern with lines at the edges of the tiles. A pixel is
// part of it when its (in-picture) side neighbours are all the opposite of it
// and its corner neighbours all the same. ARGB8888, in place.
static inline void ReclawDeDither(uint32_t* px, int pitch, int w, int h)
{
    if (w < 3 || h < 3) return;
    auto opaque = [&](int x, int y) { return (px[(size_t)y * pitch + x] >> 24) >= 128; };
    std::vector<uint8_t> mark((size_t)w * h, 0);
    bool any = false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const bool me = opaque(x, y);
            int sides = 0;
            bool ok = true;
            const int sx[4] = { -1, 1, 0, 0 }, sy[4] = { 0, 0, -1, 1 };
            for (int k = 0; k < 4 && ok; ++k)
            {
                const int nx = x + sx[k], ny = y + sy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                ++sides;
                if (opaque(nx, ny) == me) ok = false;
            }
            const int cx[4] = { -1, 1, -1, 1 }, cy[4] = { -1, -1, 1, 1 };
            for (int k = 0; k < 4 && ok; ++k)
            {
                const int nx = x + cx[k], ny = y + cy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                if (opaque(nx, ny) != me) ok = false;
            }
            if (ok && sides >= 3) { mark[(size_t)y * w + x] = me ? 1 : 2; any = true; }
        }
    if (!any) return;
    std::vector<uint32_t> orig((size_t)w * h);
    for (int y = 0; y < h; ++y) memcpy(&orig[(size_t)y * w], px + (size_t)y * pitch, (size_t)w * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const uint8_t m = mark[(size_t)y * w + x];
            if (!m) continue;
            uint32_t c = orig[(size_t)y * w + x];
            if (m == 2)
            {
                // an empty pixel: the colour of its opaque neighbours
                int r = 0, g = 0, b = 0, n = 0;
                const int sx[4] = { -1, 1, 0, 0 }, sy[4] = { 0, 0, -1, 1 };
                for (int k = 0; k < 4; ++k)
                {
                    const int nx = x + sx[k], ny = y + sy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const uint32_t o = orig[(size_t)ny * w + nx];
                    r += (o >> 16) & 0xFF; g += (o >> 8) & 0xFF; b += o & 0xFF; ++n;
                }
                if (!n) continue;
                c = ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
            }
            const uint32_t a = (c >> 24) ? (c >> 24) : 255;
            px[(size_t)y * pitch + x] = ((a / 2) << 24) | (c & 0x00FFFFFFu);
        }
}

// Both, for a picture about to become a texture: a copy with the dithering
// made half-transparent and the see-through pixels coloured; null when the
// picture is opaque (nothing to do).
static inline const uint32_t* ReclawPreparePicture(const uint32_t* px, int pitch, int w, int h, std::vector<uint32_t>& out)
{
    bool any = false;
    for (int y = 0; y < h && !any; ++y)
        for (int x = 0; x < w; ++x)
            if (!(px[(size_t)y * pitch + x] >> 24)) { any = true; break; }
    if (!any) return nullptr;
    std::vector<uint32_t> tmp((size_t)w * h);
    for (int y = 0; y < h; ++y) memcpy(&tmp[(size_t)y * w], px + (size_t)y * pitch, (size_t)w * 4);
    ReclawDeDither(tmp.data(), w, w, h);
    if (!ReclawBleedAlpha(tmp.data(), w, w, h, out)) out.swap(tmp);
    return out.data();
}
