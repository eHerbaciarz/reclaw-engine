#include "GameFont.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include <SDL2/SDL.h>

#include "../GameApp/BaseGameApp.h"
#include "../Graphics2D/Image.h"
#include "../Resource/ResourceMgr.h"
#include "../Resource/ResourceCache.h"
#include "Lang.h"

namespace
{
    // ------------------------------------------------------------------ data
    // Menu pictures of the Polish release (size checked before use) and the
    // columns of single letters in them (from scripts/menu_letters.py output).
    struct Source { const char* path; int w, h; };
    const Source kSources[] = {
        { "/STATES/MENU/IMAGES/MAIN/001_SINGLEPLAYER.PCX", 245, 37 },               // 0 JEDEN GRACZ
        { "/STATES/MENU/IMAGES/MAIN/003_MULTIPLAYER.PCX", 393, 38 },                // 1 TRYB WIELOOSOBOWY
        { "/STATES/MENU/IMAGES/MAIN/OPTIONS/003_DIFFICULTY.PCX", 363, 35 },         // 2 POZIOM TRUDNOŚCI
        { "/STATES/MENU/IMAGES/MAIN/005_REPLAYMOVIES.PCX", 279, 36 },               // 3 ODTWÓRZ FILM
        { "/STATES/MENU/IMAGES/MAIN/SINGLEPLAYER/005_LOADCUSTOM.PCX", 307, 35 },    // 4 WCZYTAJ POZIOM
        { "/STATES/MENU/IMAGES/MAIN/SINGLEPLAYER/003_LOADGAME.PCX", 237, 35 },      // 5 WCZYTAJ GRĘ
        { "/STATES/MENU/IMAGES/MAIN/OPTIONS/007_AUDIO.PCX", 148, 35 },              // 6 DŹWIĘK
        { "/STATES/MENU/IMAGES/MAIN/008_OPTIONS.PCX", 112, 35 },                    // 7 OPCJE
        { "/STATES/MENU/IMAGES/MAIN/015_CREDITS.PCX", 156, 35 },                    // 8 TWÓRCY
        { "/STATES/MENU/IMAGES/MAIN/OPTIONS/012_PREVIOUSMENU.PCX", 158, 35 },       // 9 POWRÓT
        { "/STATES/MENU/IMAGES/MAIN/OPTIONS/AUDIO/013_MUSICON.PCX", 536, 35 },      // 10 MUZYKA:
    };
    const int kSourceCount = (int)(sizeof(kSources) / sizeof(kSources[0]));
    // Picture with an underlined first letter (J of JEDEN GRACZ): the underline.
    const int kUnderlineSource = 0;
    const int kUnderlineX0 = 3, kUnderlineX1 = 39;

    struct GlyphRef { uint32_t code; int src, x0, x1; };
    const GlyphRef kGlyphs[] = {
        { 'A', 0, 174, 195 }, { 'B', 1, 72, 93 },   { 'C', 2, 321, 343 }, { 'D', 2, 224, 250 },
        { 'E', 0, 67, 89 },   { 'F', 3, 186, 208 }, { 'G', 0, 127, 149 }, { 'I', 4, 233, 245 },
        { 'J', 5, 142, 155 }, { 'K', 6, 118, 142 }, { 'L', 3, 222, 236 }, { 'M', 2, 114, 145 },
        { 'N', 2, 251, 277 }, { 'O', 1, 261, 286 }, { 'P', 7, 29, 49 },   { 'R', 3, 135, 157 },
        { 'S', 1, 243, 260 }, { 'T', 2, 153, 172 }, { 'U', 2, 199, 223 }, { 'W', 1, 103, 133 },
        { 'Y', 8, 130, 152 }, { 'Z', 5, 56, 78 },
        { 0x00D3, 9, 104, 129 },   // Ó
        { 0x0118, 6, 94, 117 },    // Ę
        { 0x015A, 2, 303, 320 },   // Ś
        { 0x0179, 6, 29, 50 },     // Ź
        { ':', 10, 151, 158 },
    };

    // The baseline sits on row 27 of a text picture, like in the original
    // 35-pixel menu pictures, so new items line up with the original ones.
    const int kAbove = 35;          // canvas rows above the baseline
    const int kOriginalAbove = 27;  // ... in the original menu pictures
    const int kBelow = 9;           // and below (descenders, underline)
    const int kSpace = 10;          // width of a space
    const int kLineGap = 4;         // between lines
    const int kPad = 3;             // room for the glow

    // ------------------------------------------------------------- pictures
    struct Pic
    {
        int w = 0, h = 0;
        std::vector<uint32_t> px;   // RGBA, alpha 0 = transparent
        int baseline = 0;
        bool Opaque(int x, int y) const { return (px[y * w + x] >> 24) != 0; }
    };

    struct Glyph
    {
        int w = 0;
        std::vector<uint32_t> px;   // w x (kAbove + kBelow), baseline at row kAbove
    };

    bool g_Tried = false;
    bool g_Ok = false;
    std::vector<Pic> g_Pics;
    std::map<uint32_t, Glyph> g_Glyphs;
    Glyph g_Underline;              // 1 column wide, kBelow rows

    bool DecodePcx(const char* data, size_t size, Pic& pic)
    {
        const uint8_t* d = (const uint8_t*)data;
        if (size < 128 + 769 || d[0] != 10 || d[3] != 8 || d[65] != 1) return false;
        int xmin = d[4] | (d[5] << 8), ymin = d[6] | (d[7] << 8);
        int xmax = d[8] | (d[9] << 8), ymax = d[10] | (d[11] << 8);
        int bpl = d[66] | (d[67] << 8);
        pic.w = xmax - xmin + 1;
        pic.h = ymax - ymin + 1;
        if (pic.w <= 0 || pic.h <= 0 || bpl < pic.w) return false;
        const uint8_t* pal = d + size - 768;
        if (d[size - 769] != 12) return false;
        std::vector<uint8_t> idx((size_t)pic.w * pic.h);
        size_t pos = 128;
        for (int y = 0; y < pic.h; ++y)
        {
            int x = 0;
            while (x < bpl && pos < size - 769)
            {
                uint8_t b = d[pos++];
                int count = 1;
                if (b >= 0xC0)
                {
                    count = b & 0x3F;
                    b = d[pos++];
                }
                for (int i = 0; i < count && x < bpl; ++i, ++x)
                {
                    if (x < pic.w) idx[(size_t)y * pic.w + x] = b;
                }
            }
        }
        // The menu pictures key out their top-left colour (as the menus do).
        uint8_t key = idx[0];
        pic.px.resize(idx.size());
        for (size_t i = 0; i < idx.size(); ++i)
        {
            uint8_t v = idx[i];
            if (v == key || v == 0) { pic.px[i] = 0; continue; }
            pic.px[i] = 0xFF000000u | ((uint32_t)pal[v * 3 + 2] << 16) | ((uint32_t)pal[v * 3 + 1] << 8) | pal[v * 3];
        }
        return true;
    }

    // Row where most columns of the picture end (the letters' baseline).
    // Only the columns of the letters taken from this picture count (other
    // parts, like the volume slider next to MUZYKA:, end elsewhere).
    int FindBaseline(const Pic& p, int src);

    int FindBaselineAll(const Pic& p, const std::vector<std::pair<int, int>>& ranges)
    {
        std::map<int, int> votes;
        for (auto& r : ranges)
        for (int x = std::max(0, r.first); x <= std::min(p.w - 1, r.second); ++x)
        {
            for (int y = p.h - 1; y >= 0; --y)
            {
                if (p.Opaque(x, y)) { votes[y]++; break; }
            }
        }
        int best = p.h - 1, n = -1;
        for (auto& v : votes) if (v.second > n) { n = v.second; best = v.first; }
        return best;
    }

    int FindBaseline(const Pic& p, int src)
    {
        std::vector<std::pair<int, int>> ranges;
        for (const GlyphRef& r : kGlyphs)
            if (r.src == src && r.code != 'J' && r.code != 0x0118) ranges.push_back({ r.x0, r.x1 });
        if (ranges.empty()) ranges.push_back({ 0, p.w - 1 });
        return FindBaselineAll(p, ranges);
    }

    Glyph Cut(const Pic& p, int x0, int x1, bool keepBelow)
    {
        Glyph g;
        x0 = std::max(0, x0);
        x1 = std::min(p.w - 1, x1);
        g.w = x1 - x0 + 1;
        const int H = kAbove + kBelow;
        g.px.assign((size_t)g.w * H, 0);
        for (int gy = 0; gy < H; ++gy)
        {
            int y = p.baseline - kAbove + gy;
            if (y < 0 || y >= p.h) continue;
            if (!keepBelow && y > p.baseline) continue;
            for (int x = x0; x <= x1; ++x) g.px[(size_t)gy * g.w + (x - x0)] = p.px[(size_t)y * p.w + x];
        }
        return g;
    }

    // Rows of a glyph that hold pixels, split into parts by empty rows.
    std::vector<std::pair<int, int>> RowParts(const Glyph& g)
    {
        const int H = kAbove + kBelow;
        std::vector<std::pair<int, int>> parts;
        int start = -1;
        for (int y = 0; y <= H; ++y)
        {
            bool any = false;
            if (y < H)
                for (int x = 0; x < g.w && !any; ++x) any = (g.px[(size_t)y * g.w + x] >> 24) != 0;
            if (any && start < 0) start = y;
            if (!any && start >= 0) { parts.push_back({ start, y - 1 }); start = -1; }
        }
        return parts;
    }

    Glyph Rows(const Glyph& g, int y0, int y1)
    {
        Glyph out = g;
        const int H = kAbove + kBelow;
        for (int y = 0; y < H; ++y)
            if (y < y0 || y > y1)
                for (int x = 0; x < g.w; ++x) out.px[(size_t)y * g.w + x] = 0;
        return out;
    }

    // Paints `mark` over `base` with its left edge at column x, moved by dy rows.
    Glyph Combine(const Glyph& base, const Glyph& mark, int x0, int dy)
    {
        Glyph out = base;
        const int H = kAbove + kBelow;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < mark.w; ++x)
            {
                uint32_t c = mark.px[(size_t)y * mark.w + x];
                int tx = x + x0, ty = y + dy;
                if ((c >> 24) == 0 || tx < 0 || tx >= out.w || ty < 0 || ty >= H) continue;
                out.px[(size_t)ty * out.w + tx] = c;
            }
        return out;
    }

    int g_LastCropX = 0;    // left column the last Crop() removed

    // Only the columns that hold pixels (rows unchanged).
    Glyph Crop(const Glyph& g)
    {
        const int H = kAbove + kBelow;
        int x0 = g.w, x1 = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < g.w; ++x)
                if (g.px[(size_t)y * g.w + x] >> 24) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
        Glyph out;
        g_LastCropX = x0;
        if (x1 < x0) return out;
        out.w = x1 - x0 + 1;
        out.px.assign((size_t)out.w * H, 0);
        for (int y = 0; y < H; ++y)
            for (int x = x0; x <= x1; ++x) out.px[(size_t)y * out.w + x - x0] = g.px[(size_t)y * g.w + x];
        return out;
    }

    bool InkAt(const Glyph& g, int x, int y)
    {
        return x >= 0 && y >= 0 && x < g.w && y < kAbove + kBelow && (g.px[(size_t)y * g.w + x] >> 24) != 0;
    }

    int LeftEdge(const Glyph& g)
    {
        // first column with a long run of pixels (the stem / the bowl)
        const int H = kAbove + kBelow;
        for (int x = 0; x < g.w; ++x)
        {
            int n = 0;
            for (int y = 0; y < H; ++y) n += InkAt(g, x, y);
            if (n >= 8) return x;
        }
        return 0;
    }

    // Pixels of `marked` (rows y0..y1) that `plain`, lined up by the left
    // edge, does not have nearby: the extra mark of a Polish letter.
    Glyph DiffMark(const Glyph& marked, const Glyph& plain, int y0, int y1)
    {
        Glyph out = marked;
        const int H = kAbove + kBelow;
        int dx = LeftEdge(plain) - LeftEdge(marked);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < marked.w; ++x)
            {
                bool keep = y >= y0 && y <= y1 && InkAt(marked, x, y);
                if (keep)
                {
                    for (int oy = -1; oy <= 1 && keep; ++oy)
                        for (int ox = -1; ox <= 1 && keep; ++ox)
                            if (InkAt(plain, x + dx + ox, y + oy)) keep = false;
                }
                if (!keep) out.px[(size_t)y * marked.w + x] = 0;
            }
        // drop stray single pixels
        Glyph clean = out;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < out.w; ++x)
            {
                if (!InkAt(out, x, y)) continue;
                int n = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                        if ((ox || oy) && InkAt(out, x + ox, y + oy)) n++;
                if (n < 2) clean.px[(size_t)y * out.w + x] = 0;
            }
        return clean;
    }

    // A smaller copy (percent) of a cropped mark whose pixels are in rows
    // y0..y1; the bottom row stays where it was.
    Glyph Shrink(const Glyph& g, int y0, int y1, int percent)
    {
        const int H = kAbove + kBelow;
        int sh = y1 - y0 + 1;
        int nw = std::max(1, (g.w * percent + 50) / 100), nh = std::max(1, (sh * percent + 50) / 100);
        Glyph out;
        out.w = nw;
        out.px.assign((size_t)nw * H, 0);
        for (int ny = 0; ny < nh; ++ny)
            for (int nx = 0; nx < nw; ++nx)
            {
                // average of the source pixels this one covers
                int sx0 = nx * g.w / nw, sx1 = std::max(sx0 + 1, (nx + 1) * g.w / nw);
                int sy0 = ny * sh / nh, sy1 = std::max(sy0 + 1, (ny + 1) * sh / nh);
                uint32_t r = 0, gg = 0, b = 0, a = 0, n = 0;
                for (int sy = sy0; sy < sy1; ++sy)
                    for (int sx = sx0; sx < sx1; ++sx)
                    {
                        uint32_t c = g.px[(size_t)(y0 + sy) * g.w + sx];
                        uint32_t ca = c >> 24;
                        r += (c & 0xFF) * ca; gg += ((c >> 8) & 0xFF) * ca; b += ((c >> 16) & 0xFF) * ca;
                        a += ca; n++;
                    }
                if (a == 0) continue;
                uint32_t oa = a / n;
                out.px[(size_t)(y1 - nh + 1 + ny) * nw + nx] =
                    (oa << 24) | ((b / a) << 16) | ((gg / a) << 8) | (r / a);
            }
        return out;
    }

    // Columns of the main vertical stroke: those nearly as tall as the
    // tallest column (arms next to the stem are much shorter).
    std::pair<int, int> StemRange(const Glyph& g)
    {
        const int H = kAbove + kBelow;
        std::vector<int> n(g.w, 0);
        int best = 0;
        for (int x = 0; x < g.w; ++x)
        {
            for (int y = 0; y < H; ++y) n[x] += InkAt(g, x, y);
            best = std::max(best, n[x]);
        }
        int s0 = 0;
        while (s0 < g.w - 1 && n[s0] * 100 < best * 75) s0++;
        int s1 = s0;
        while (s1 + 1 < g.w && n[s1 + 1] * 100 >= best * 75) s1++;
        return { s0, s1 };
    }

    // Removes specks: groups of up to `maxPixels` touching pixels (bits of
    // the neighbouring letters or shadows caught at the edge of a cut).
    void RemoveSpecks(Glyph& g, int maxPixels)
    {
        const int H = kAbove + kBelow;
        std::vector<int> seen((size_t)g.w * H, 0);
        for (int y0 = 0; y0 < H; ++y0)
            for (int x0 = 0; x0 < g.w; ++x0)
            {
                if (seen[(size_t)y0 * g.w + x0] || !InkAt(g, x0, y0)) continue;
                std::vector<std::pair<int, int>> group, todo{ { x0, y0 } };
                seen[(size_t)y0 * g.w + x0] = 1;
                while (!todo.empty())
                {
                    auto p = todo.back();
                    todo.pop_back();
                    group.push_back(p);
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            int x = p.first + dx, y = p.second + dy;
                            if (!InkAt(g, x, y) || seen[(size_t)y * g.w + x]) continue;
                            seen[(size_t)y * g.w + x] = 1;
                            todo.push_back({ x, y });
                        }
                }
                if ((int)group.size() <= maxPixels)
                    for (auto& p : group) g.px[(size_t)p.second * g.w + p.first] = 0;
            }
    }

    int TopRow(const Glyph& g)
    {
        auto parts = RowParts(g);
        return parts.empty() ? kAbove : parts.front().first;
    }

    void Build()
    {
        g_Tried = true;
        if (Lang::Code() != "pl" || !g_pApp || !g_pApp->GetResourceMgr()) return;
        g_Pics.resize(kSourceCount);
        for (int i = 0; i < kSourceCount; ++i)
        {
            Resource res(kSources[i].path);
            shared_ptr<ResourceHandle> h = g_pApp->GetResourceMgr()->VGetHandle(&res);
            if (!h || !h->GetDataBuffer() || !DecodePcx(h->GetDataBuffer(), h->GetSize(), g_Pics[i]))
            {
                LOG_WARNING(std::string("GameFont: cannot read ") + kSources[i].path);
                return;
            }
            if (g_Pics[i].w != kSources[i].w || g_Pics[i].h != kSources[i].h)
            {
                LOG_WARNING(std::string("GameFont: different picture (not the Polish release?): ") + kSources[i].path);
                return;
            }
            g_Pics[i].baseline = FindBaseline(g_Pics[i], i);
        }

        for (const GlyphRef& r : kGlyphs)
        {
            bool below = r.code == 'J' || r.code == 0x0118;   // J, Ę go under the line
            g_Glyphs[r.code] = Cut(g_Pics[r.src], r.x0, r.x1, below);
            if (r.code != ':') RemoveSpecks(g_Glyphs[r.code], 8);
        }

        // L: in the menus it always stands with its foot under the next letter,
        // so a cut L loses the foot. It is made from E instead: the stem and
        // the bottom arm, without the top and middle arms.
        {
            const Glyph& e = g_Glyphs['E'];
            const int H = kAbove + kBelow;
            int stemEnd = StemRange(e).second;
            int probe = std::min(e.w - 3, stemEnd + 6), foot = 0;
            for (int y = kAbove; y > 0 && InkAt(e, probe, y); --y) foot++;
            Glyph l = e;
            for (int y = 0; y < H; ++y)
                for (int x = stemEnd + 1; x < e.w; ++x)
                    if (y < kAbove - foot - 1) l.px[(size_t)y * e.w + x] = 0;
            RemoveSpecks(l, 12);
            g_Glyphs['L'] = l;
        }

        // Marks for the letters the menus do not have: what Ę has more than E
        // (the hook), what Ś has more than S (the accent), the colon's dots.
        Glyph hook = Crop(DiffMark(g_Glyphs[0x0118], g_Glyphs['E'], kAbove - 8, kAbove + kBelow - 1));
        const int hookX = g_LastCropX;
        Glyph acute = Crop(DiffMark(g_Glyphs[0x015A], g_Glyphs['S'], 0, TopRow(g_Glyphs['S']) + 1));
        LOG_WARNING("GameFont: hook " + ToStr(hook.w) + " px, accent " + ToStr(acute.w) + " px");
        if (hook.w > 0)
        {
            // Ą: the hook under the right foot of A, as far right as on Ę
            const Glyph& e = g_Glyphs[0x0118];
            int fromRight = e.w - (hookX + hook.w);
            g_Glyphs[0x0104] = Combine(g_Glyphs['A'], hook, g_Glyphs['A'].w - hook.w - std::max(0, fromRight - 1), 0);
        }
        if (acute.w > 0)
        {
            g_Glyphs[0x0106] = Combine(g_Glyphs['C'], acute, (g_Glyphs['C'].w - acute.w) / 2 + 2, 0);
            g_Glyphs[0x0143] = Combine(g_Glyphs['N'], acute, (g_Glyphs['N'].w - acute.w) / 2 + 1, 0);
        }
        const Glyph& colon = g_Glyphs[':'];
        auto colonParts = RowParts(colon);
        if (colonParts.size() >= 2)
        {
            Glyph dotTop = Crop(Rows(colon, colonParts[0].first, colonParts[0].second));
            Glyph dotLow = Crop(Rows(colon, colonParts.back().first, colonParts.back().second));
            Glyph period;
            period.w = dotLow.w + 2;
            period.px.assign((size_t)period.w * (kAbove + kBelow), 0);
            g_Glyphs['.'] = Combine(period, dotLow, 1, 0);
            // Ż: the dot moved up to just above the capitals
            int capTop = TopRow(g_Glyphs['Z']);
            int dy = (capTop - 2) - colonParts[0].second;
            Glyph dot = Shrink(dotTop, colonParts[0].first, colonParts[0].second, 70);
            g_Glyphs[0x017B] = Combine(g_Glyphs['Z'], dot, (g_Glyphs['Z'].w - dot.w) / 2, dy);
        }

        // Letters for names (credits screen) the menus do not have.
        const int H = kAbove + kBelow;
        auto inkCount = [&](const Glyph& g, int x) { int n = 0; for (int y = 0; y < H; ++y) n += InkAt(g, x, y); return n; };
        auto mirror = [&](const Glyph& g) {
            Glyph m = g;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < g.w; ++x) m.px[(size_t)y * g.w + x] = g.px[(size_t)y * g.w + (g.w - 1 - x)];
            return m;
        };
        auto blank = [&](int w) { Glyph g; g.w = w; g.px.assign((size_t)w * H, 0); return g; };
        // A horizontal stroke of the font: the rows of T's top bar (a column
        // a third of the way in, clear of the stem and the end serifs).
        std::vector<uint32_t> bar;
        {
            const Glyph& t = g_Glyphs['T'];
            int x = t.w * 3 / 10, y = TopRow(t);
            while (y < H && !InkAt(t, x, y)) y++;
            for (; y < H && InkAt(t, x, y); ++y) bar.push_back(t.px[(size_t)y * t.w + x]);
            LOG_WARNING("GameFont: bar " + ToStr((int)bar.size()) + " rows");
        }
        const int capTop = TopRow(g_Glyphs['O']);
        const int capMid = (capTop + kAbove) / 2;
        if (!bar.empty())
        {
            // H: two I stems joined by the bar, a little above the middle.
            // the I without its side spacing; the serifs of the two stems
            // nearly touch, like in the font's own N and U
            Glyph i = Crop(g_Glyphs['I']);
            std::pair<int, int> stem = StemRange(i);
            const int gap = 3;
            Glyph h = blank(i.w * 2 + gap + 2);
            h = Combine(h, i, 1, 0);
            h = Combine(h, i, 1 + i.w + gap, 0);
            int from = 1 + stem.second + 1, to = 1 + i.w + gap + stem.first - 1;
            int top = capMid - (int)bar.size() / 2 - 1;
            for (int k = 0; k < (int)bar.size(); ++k)
                for (int x = from; x <= to; ++x) h.px[(size_t)(top + k) * h.w + x] = bar[k];
            g_Glyphs['H'] = h;
            // Dash (–, -): the bar, short, in the middle.
            Glyph dash = blank(20);
            top = capMid - (int)bar.size() / 2;
            for (int k = 0; k < (int)bar.size(); ++k)
                for (int x = 3; x < dash.w - 3; ++x) dash.px[(size_t)(top + k) * dash.w + x] = bar[k];
            g_Glyphs[0x2013] = dash;
            g_Glyphs['-'] = dash;
        }
        {
            // V: the upper part of Y (a V on a stem), stretched down to the
            // baseline; the serif rows at the top keep their size.
            const Glyph& y = g_Glyphs['Y'];
            const int top = TopRow(y);
            auto runsAt = [&](int row) {
                int runs = 0;
                bool in = false;
                for (int x = 0; x < y.w; ++x)
                {
                    bool ink = InkAt(y, x, row);
                    if (ink && !in) runs++;
                    in = ink;
                }
                return runs;
            };
            // the fork: first row (below the serifs) where the arms have met
            int fork = -1;
            for (int row = top + 4; row < kAbove; ++row)
                if (runsAt(row) == 1 && runsAt(row - 1) >= 2) { fork = row; break; }
            if (fork > top + 6)
            {
                const int keep = 4;
                Glyph v = blank(y.w);
                for (int row = top; row <= kAbove; ++row)
                {
                    int src = row;
                    if (row >= top + keep)
                        src = top + keep + (row - top - keep) * (fork - top - keep) / std::max(1, kAbove - top - keep);
                    for (int x = 0; x < y.w; ++x) v.px[(size_t)row * v.w + x] = y.px[(size_t)src * y.w + x];
                }
                g_Glyphs['V'] = v;
            }
            LOG_WARNING("GameFont: V fork at " + ToStr(fork) + ", top " + ToStr(top));
        }
        if (acute.w > 0)
        {
            // Í, and a caron (Š, Č) made of the accent and its mirror image.
            g_Glyphs[0x00CD] = Combine(g_Glyphs['I'], acute, (g_Glyphs['I'].w - acute.w) / 2 + 1, 0);
            Glyph caron = blank(acute.w * 2 - 2);
            caron = Combine(caron, mirror(acute), 0, 0);
            caron = Combine(caron, acute, acute.w - 2, 0);
            int top = TopRow(acute), bottom = top;
            auto parts = RowParts(acute);
            if (!parts.empty()) bottom = parts.back().second;
            caron = Crop(Shrink(caron, top, bottom, 75));
            g_Glyphs[0x0160] = Combine(g_Glyphs['S'], caron, (g_Glyphs['S'].w - caron.w) / 2, 0);
            g_Glyphs[0x010C] = Combine(g_Glyphs['C'], caron, (g_Glyphs['C'].w - caron.w) / 2 + 1, 0);
        }
        if (colonParts.size() >= 2)
        {
            Glyph dotTop = Crop(Rows(colon, colonParts[0].first, colonParts[0].second));
            Glyph dot = Shrink(dotTop, colonParts[0].first, colonParts[0].second, 65);
            int dy = (capTop - 2) - colonParts[0].second;
            // Ö: two dots over O
            Glyph o = g_Glyphs['O'];
            int mid = o.w / 2;
            o = Combine(o, dot, mid - dot.w - 2, dy);
            o = Combine(o, dot, mid + 2, dy);
            g_Glyphs[0x00D6] = o;
            // middle dot (between names): the colon's top dot at half height
            Glyph mdot = blank(dotTop.w + 8);
            int ddy = capMid - (colonParts[0].first + colonParts[0].second) / 2;
            g_Glyphs[0x00B7] = Combine(mdot, dotTop, 4, ddy);
        }

        // Underline: one column from the middle of the original one.
        const Pic& u = g_Pics[kUnderlineSource];
        g_Underline.w = 1;
        g_Underline.px.assign(kBelow, 0);
        int ux = (kUnderlineX0 + kUnderlineX1) / 2;
        for (int y = 1; y < kBelow; ++y)
        {
            int py = u.baseline + y;
            if (py < u.h) g_Underline.px[y] = u.px[(size_t)py * u.w + ux];
        }

        int underlineRows = 0;
        for (uint32_t c : g_Underline.px) underlineRows += (c >> 24) != 0;
        if (underlineRows == 0 && !bar.empty())
        {
            for (int k = 0; k < (int)bar.size() && 2 + k < kBelow; ++k) g_Underline.px[2 + k] = bar[k];
            underlineRows = -(int)bar.size();
        }
        LOG_WARNING("GameFont: underline " + ToStr(underlineRows) + " rows (negative: made from the bar)");

        g_Ok = true;
        LOG_WARNING("GameFont: menu letters of the game ready (" + ToStr((int)g_Glyphs.size()) + ")");
    }

    std::vector<uint32_t> Decode(const std::string& s)
    {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < s.size();)
        {
            uint8_t c = (uint8_t)s[i];
            uint32_t cp;
            int n;
            if (c < 0x80) { cp = c; n = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
            else { cp = c & 0x07; n = 4; }
            for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
            i += n;
            // upper case
            if (cp >= 'a' && cp <= 'z') cp -= 32;
            switch (cp)
            {
                case 0x0105: cp = 0x0104; break;   // ą
                case 0x0107: cp = 0x0106; break;   // ć
                case 0x0119: cp = 0x0118; break;   // ę
                case 0x0142: cp = 'L'; break;      // ł (no Ł in the menus)
                case 0x0141: cp = 'L'; break;
                case 0x0144: cp = 0x0143; break;   // ń
                case 0x00F3: cp = 0x00D3; break;   // ó
                case 0x015B: cp = 0x015A; break;   // ś
                case 0x017A: cp = 0x0179; break;   // ź
                case 0x017C: cp = 0x017B; break;   // ż
                case 0x00ED: cp = 0x00CD; break;   // í
                case 0x00F6: cp = 0x00D6; break;   // ö
                case 0x0161: cp = 0x0160; break;   // š
                case 0x010D: cp = 0x010C; break;   // č
                default: break;
            }
            out.push_back(cp);
        }
        return out;
    }

    uint32_t Blend(uint32_t dst, uint32_t src)
    {
        uint32_t sa = src >> 24;
        if (sa == 255) return src;
        if (sa == 0) return dst;
        uint32_t da = dst >> 24;
        uint32_t oa = sa + da * (255 - sa) / 255;
        if (oa == 0) return 0;
        uint32_t out = oa << 24;
        for (int sh = 0; sh <= 16; sh += 8)
        {
            uint32_t sc = (src >> sh) & 0xFF, dc = (dst >> sh) & 0xFF;
            uint32_t c = (sc * sa + dc * da * (255 - sa) / 255) / oa;
            out |= (std::min<uint32_t>(c, 255)) << sh;
        }
        return out;
    }
}

namespace GameFont
{
    int ExtraTop() { return kAbove - kOriginalAbove; }

    bool Available()
    {
        if (!g_Tried) Build();
        return g_Ok;
    }

    bool RenderPixels(const std::string& utf8, Look look, int underline, Pixels& out)
    {
        if (!Available()) return false;

        // Lines of glyph codes.
        std::vector<std::vector<uint32_t>> lines(1);
        for (uint32_t cp : Decode(utf8))
        {
            if (cp == '|' || cp == '\n') lines.emplace_back();
            else lines.back().push_back(cp);
        }
        auto lineWidth = [](const std::vector<uint32_t>& l) {
            int w = 0;
            for (uint32_t cp : l)
            {
                auto it = g_Glyphs.find(cp);
                w += (it != g_Glyphs.end()) ? it->second.w : kSpace;
            }
            return w;
        };
        int textW = 1;
        for (auto& l : lines) textW = std::max(textW, lineWidth(l));
        const int GH = kAbove + kBelow;
        const int W = textW + 2 * kPad;
        const int H = (int)lines.size() * GH + ((int)lines.size() - 1) * kLineGap + kPad;
        std::vector<uint32_t> px((size_t)W * H, 0);

        int letterIndex = 0;
        for (size_t li = 0; li < lines.size(); ++li)
        {
            int x = kPad + (textW - lineWidth(lines[li])) / 2;
            int top = (int)li * (GH + kLineGap);
            int lineStart = x;
            for (uint32_t cp : lines[li])
            {
                auto it = g_Glyphs.find(cp);
                if (it == g_Glyphs.end()) { x += kSpace; continue; }
                const Glyph& g = it->second;
                for (int y = 0; y < GH; ++y)
                    for (int gx = 0; gx < g.w; ++gx)
                    {
                        uint32_t c = g.px[(size_t)y * g.w + gx];
                        if (c >> 24) px[(size_t)(top + y) * W + x + gx] = Blend(px[(size_t)(top + y) * W + x + gx], c);
                    }
                if (underline == letterIndex)
                {
                    for (int y = 1; y < kBelow; ++y)
                        for (int gx = 1; gx < g.w - 1; ++gx)
                        {
                            uint32_t c = g_Underline.px[y];
                            size_t at = (size_t)(top + kAbove + y) * W + x + gx;
                            if (c >> 24) px[at] = c;
                        }
                }
                x += g.w;
                letterIndex++;
            }
            if (underline == -2)
            {
                for (int y = 1; y < kBelow; ++y)
                    for (int ux = lineStart + 1; ux < x - 1; ++ux)
                    {
                        uint32_t c = g_Underline.px[y];
                        if (c >> 24) px[(size_t)(top + kAbove + y) * W + ux] = c;
                    }
            }
        }

        if (look == Look::Disabled)
        {
            for (uint32_t& c : px)
            {
                if (!(c >> 24)) continue;
                uint32_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
                uint32_t l = (r * 30 + g * 59 + b * 11) / 100 * 55 / 100;
                uint32_t a = (c >> 24) * 8 / 10;
                c = (a << 24) | (l << 16) | (l << 8) | l;
            }
        }
        else if (look == Look::Active)
        {
            // Light glow around the letters, then the letters a bit brighter.
            std::vector<uint8_t> a((size_t)W * H), glow((size_t)W * H, 0);
            for (size_t i = 0; i < px.size(); ++i) a[i] = (uint8_t)(px[i] >> 24);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    int m = 0;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            int xx = x + dx, yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                            int v = a[(size_t)yy * W + xx];
                            if (dx * dx + dy * dy > 5) v = v / 2;
                            m = std::max(m, v);
                        }
                    glow[(size_t)y * W + x] = (uint8_t)m;
                }
            std::vector<uint32_t> out((size_t)W * H, 0);
            for (size_t i = 0; i < px.size(); ++i)
            {
                uint32_t g = glow[i] * 85 / 100;
                uint32_t halo = (g << 24) | (0xD8u << 16) | (0xF4u << 8) | 0xFFu;   // RGBA little end: R=FF G=F4 B=D8
                uint32_t c = px[i];
                if (c >> 24)
                {
                    uint32_t r = std::min<uint32_t>(255, (c & 0xFF) * 115 / 100 + 30);
                    uint32_t gg = std::min<uint32_t>(255, ((c >> 8) & 0xFF) * 115 / 100 + 30);
                    uint32_t b = std::min<uint32_t>(255, ((c >> 16) & 0xFF) * 115 / 100 + 25);
                    c = (c & 0xFF000000u) | (b << 16) | (gg << 8) | r;
                }
                out[i] = Blend(halo, c);
            }
            px.swap(out);
        }

        out.w = W;
        out.h = H;
        out.px.swap(px);
        return true;
    }

    std::shared_ptr<Image> ToImage(Pixels& p)
    {
        // RGBA bytes (R first) -> texture
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(p.px.data(), p.w, p.h, 32, p.w * 4, SDL_PIXELFORMAT_ABGR8888);
        if (!surface) return nullptr;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(g_pApp->GetRenderer(), surface);
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        return std::shared_ptr<Image>(new Image(texture));
    }

    std::shared_ptr<Image> Render(const std::string& utf8, Look look, int underline)
    {
        Pixels p;
        if (!RenderPixels(utf8, look, underline, p)) return nullptr;
        return ToImage(p);
    }

    Pixels Scale(const Pixels& p, int percent)
    {
        Pixels out;
        out.w = std::max(1, p.w * percent / 100);
        out.h = std::max(1, p.h * percent / 100);
        out.px.assign((size_t)out.w * out.h, 0);
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x)
            {
                int sx0 = x * p.w / out.w, sx1 = std::max(sx0 + 1, (x + 1) * p.w / out.w);
                int sy0 = y * p.h / out.h, sy1 = std::max(sy0 + 1, (y + 1) * p.h / out.h);
                uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int sy = sy0; sy < sy1; ++sy)
                    for (int sx = sx0; sx < sx1; ++sx)
                    {
                        uint32_t c = p.px[(size_t)sy * p.w + sx], ca = c >> 24;
                        r += (c & 0xFF) * ca; g += ((c >> 8) & 0xFF) * ca; b += ((c >> 16) & 0xFF) * ca;
                        a += ca; n++;
                    }
                if (!a) continue;
                out.px[(size_t)y * out.w + x] = ((a / n) << 24) | ((b / a) << 16) | ((g / a) << 8) | (r / a);
            }
        return out;
    }

    void Paste(Pixels& dst, const Pixels& src, int x0, int y0)
    {
        for (int y = 0; y < src.h; ++y)
        {
            int ty = y0 + y;
            if (ty < 0 || ty >= dst.h) continue;
            for (int x = 0; x < src.w; ++x)
            {
                int tx = x0 + x;
                if (tx < 0 || tx >= dst.w) continue;
                uint32_t c = src.px[(size_t)y * src.w + x];
                uint32_t sa = c >> 24;
                if (!sa) continue;
                uint32_t& d = dst.px[(size_t)ty * dst.w + tx];
                if (sa == 255 || !(d >> 24)) { d = c; continue; }
                uint32_t o = 0xFF000000u;
                for (int sh = 0; sh <= 16; sh += 8)
                {
                    uint32_t sc = (c >> sh) & 0xFF, dc = (d >> sh) & 0xFF;
                    o |= ((sc * sa + dc * (255 - sa)) / 255) << sh;
                }
                d = o;
            }
        }
    }

    Pixels WithShadow(const Pixels& p, int dx, int dy, int alpha)
    {
        Pixels out;
        out.w = p.w + dx + 1;
        out.h = p.h + dy + 1;
        out.px.assign((size_t)out.w * out.h, 0);
        // shadow: the letters' alpha, blurred a little, dark brown
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x)
            {
                int sum = 0, n = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        int sx = x - dx + ox, sy = y - dy + oy;
                        int w = (ox == 0 && oy == 0) ? 4 : 1;
                        n += w;
                        if (sx >= 0 && sy >= 0 && sx < p.w && sy < p.h) sum += w * (int)(p.px[(size_t)sy * p.w + sx] >> 24);
                    }
                uint32_t a = (uint32_t)(sum / n * alpha / 255);
                if (a) out.px[(size_t)y * out.w + x] = (a << 24) | (0x08u << 16) | (0x10u << 8) | 0x18u;
            }
        Paste(out, p, 0, 0);
        return out;
    }
}
