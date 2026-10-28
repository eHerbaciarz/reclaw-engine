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

    const int kAbove = 32;          // canvas rows above the baseline
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
    int FindBaseline(const Pic& p)
    {
        std::map<int, int> votes;
        for (int x = 0; x < p.w; ++x)
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

    // Paints `mark` (its own rows) over `base`, centred horizontally plus dx,
    // moved by dy rows.
    Glyph Combine(const Glyph& base, const Glyph& mark, int dx, int dy)
    {
        Glyph out = base;
        const int H = kAbove + kBelow;
        int ox = (base.w - mark.w) / 2 + dx;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < mark.w; ++x)
            {
                uint32_t c = mark.px[(size_t)y * mark.w + x];
                int tx = x + ox, ty = y + dy;
                if ((c >> 24) == 0 || tx < 0 || tx >= out.w || ty < 0 || ty >= H) continue;
                out.px[(size_t)ty * out.w + tx] = c;
            }
        return out;
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
            g_Pics[i].baseline = FindBaseline(g_Pics[i]);
        }

        for (const GlyphRef& r : kGlyphs)
        {
            bool below = r.code == 'J' || r.code == 0x0118;   // J, Ę go under the line
            g_Glyphs[r.code] = Cut(g_Pics[r.src], r.x0, r.x1, below);
        }

        // Marks for the letters the menus do not have.
        const Glyph& colon = g_Glyphs[':'];
        auto colonParts = RowParts(colon);
        if (colonParts.size() >= 2)
        {
            Glyph dotTop = Rows(colon, colonParts[0].first, colonParts[0].second);
            Glyph dotLow = Rows(colon, colonParts.back().first, colonParts.back().second);
            g_Glyphs['.'] = dotLow;
            // Ż: Z with a dot above it (the dot moved up to above the capitals)
            int capTop = TopRow(g_Glyphs['Z']);
            int dy = (capTop - 3) - colonParts[0].second;
            g_Glyphs[0x017B] = Combine(g_Glyphs['Z'], dotTop, 0, dy);
        }
        // Ć, Ń: the accent of Ś
        const Glyph& s = g_Glyphs[0x015A];
        auto sParts = RowParts(s);
        if (sParts.size() >= 2)
        {
            Glyph acute = Rows(s, sParts[0].first, sParts[0].second);
            g_Glyphs[0x0106] = Combine(g_Glyphs['C'], acute, 0, 0);
            g_Glyphs[0x0143] = Combine(g_Glyphs['N'], acute, 0, 0);
            if (!g_Glyphs.count(0x0179)) g_Glyphs[0x0179] = Combine(g_Glyphs['Z'], acute, 0, 0);
        }
        // Ą: the hook of Ę under A (rows below the baseline)
        const Glyph& e = g_Glyphs[0x0118];
        Glyph hook = Rows(e, kAbove + 1, kAbove + kBelow - 1);
        g_Glyphs[0x0104] = Combine(g_Glyphs['A'], hook, (g_Glyphs['A'].w - e.w) / 2 + 2, 0);

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
    bool Available()
    {
        if (!g_Tried) Build();
        return g_Ok;
    }

    std::shared_ptr<Image> Render(const std::string& utf8, Look look, int underline)
    {
        if (!Available()) return nullptr;

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
        const int H = (int)lines.size() * GH + ((int)lines.size() - 1) * kLineGap + 2 * kPad;
        std::vector<uint32_t> px((size_t)W * H, 0);

        int letterIndex = 0;
        for (size_t li = 0; li < lines.size(); ++li)
        {
            int x = kPad + (textW - lineWidth(lines[li])) / 2;
            int top = kPad + (int)li * (GH + kLineGap);
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

        // RGBA bytes (R first) -> texture
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), W, H, 32, W * 4, SDL_PIXELFORMAT_ABGR8888);
        if (!surface) return nullptr;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(g_pApp->GetRenderer(), surface);
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        return std::shared_ptr<Image>(new Image(texture));
    }
}
