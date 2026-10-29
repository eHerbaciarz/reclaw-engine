#include "ControlsScreen.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "GameFont.h"
#include "../GameApp/BaseGameApp.h"
#include "../Graphics2D/Image.h"
#include "../Platform/Platform.h"
#include "../Resource/ResourceMgr.h"
#include "../Resource/ResourceCache.h"
#include "../Util/PortSettings.h"

namespace
{
    const int kWidth = 600;
    const int kKeyRight = 252;      // right edge of the key column
    const int kGap = 14;            // between the keys and the action
    const int kTop = 56;            // first row
    const int kTextPercent = 62;    // action letters, against the menu buttons' size

    // Buttons per controller style (as in scripts/make_controls_image.py):
    // jump fire sword lift ammo save load rewind pause map ok back
    struct Style { const char* name; const char* b[12]; };
    const Style kStyles[] = {
        { "switch",      { "b", "a", "y", "x", "l", "zl", "zr", "r", "plus", "minus", "a", "b" } },
        { "3ds",         { "b", "a", "y", "x", "l", "zl", "zr", "r", "start", "select", "a", "b" } },
        { "wiiu",        { "b", "a", "y", "x", "l", "zl", "zr", "r", "plus", "minus", "a", "b" } },
        { "playstation", { "ps_x", "ps_o", "ps_sq", "ps_tri", "l1", "l2", "r2", "r1", "start", "select", "ps_x", "ps_o" } },
        { "xbox",        { "a", "b", "x", "y", "lb", "lt", "rt", "rb", "start", "back", "a", "b" } },
        { "pc",          { "spacja", "alt", "ctrl", "z", "shift", "f5", "f8", "f6", "esc", "tab", "enter", "esc" } },
    };

    typedef GameFont::Pixels Pixels;

    bool LoadBadge(const std::string& name, Pixels& out)
    {
        std::string path = "/controls/badges/" + name + ".png";
        Resource res(path.c_str());
        shared_ptr<ResourceHandle> h = g_pApp->GetResourceMgr()->VGetHandle(&res);
        if (!h || !h->GetDataBuffer()) return false;
        SDL_Surface* s = IMG_Load_RW(SDL_RWFromConstMem(h->GetDataBuffer(), (int)h->GetSize()), 1);
        if (!s) return false;
        SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(s);
        if (!c) return false;
        out.w = c->w;
        out.h = c->h;
        out.px.resize((size_t)c->w * c->h);
        SDL_LockSurface(c);
        for (int y = 0; y < c->h; ++y)
            memcpy(&out.px[(size_t)y * c->w], (const uint8_t*)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4);
        SDL_UnlockSurface(c);
        SDL_FreeSurface(c);
        return true;
    }

    Pixels Shrink(const Pixels& p, int percent)
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
}

namespace ControlsScreen
{
    std::shared_ptr<Image> Build()
    {
        if (!GameFont::Available()) return nullptr;

        std::string styleName = Platform::ControlsStyle();
        const Style* style = &kStyles[0];
        for (const Style& s : kStyles) if (styleName == s.name) style = &s;
        const char* const* b = style->b;
        const bool experimental = PortSettings::GetBool("experimental", false);

        struct Row { std::vector<std::string> keys; const char* text; };
        std::vector<Row> rows = {
            { { "left" }, "W LEWO" },
            { { "right" }, "W PRAWO" },
            { { "up" }, "WSPINANIE" },
            { { "down" }, "KUCANIE I ZEJŚCIE" },
            { { b[0] }, "SKOK" },
            { { b[2] }, "ATAK WRĘCZ" },
            { { b[1] }, "STRZELANIE" },
            { { b[3] }, "PODNOSZENIE I RZUT" },
            { { b[3] }, "TRZYMAJ: DALEKI RZUT" },
            { { b[4] }, "ZMIEŃ BROŃ" },
            { { b[8] }, "PAUZA I MENU" },
            { { b[9] }, "MAPA POZIOMU" },
        };
        if (experimental)
        {
            rows.push_back({ { b[5] }, "SZYBKI ZAPIS" });
            rows.push_back({ { b[6] }, "SZYBKIE WCZYTANIE" });
            rows.push_back({ { b[7] }, "TRZYMAJ: COFANIE CZASU" });
        }
        rows.push_back({ { b[10], b[11] }, "MENU: WYBÓR I POWRÓT" });

        const int step = experimental ? 16 : 20;
        Pixels canvas;
        canvas.w = kWidth;
        canvas.h = kTop + step * (int)rows.size() + 8;
        canvas.px.assign((size_t)canvas.w * canvas.h, 0);

        // Title, full size, underlined like the original page titles.
        Pixels title;
        if (GameFont::RenderPixels("STEROWANIE", GameFont::Look::Normal, -2, title))
        {
            Paste(canvas, title, (kWidth - title.w) / 2, -GameFont::ExtraTop());
        }

        // Letters' baseline inside a GameFont picture.
        const int baseline = GameFont::ExtraTop() + 27;
        int y = kTop;
        for (const Row& row : rows)
        {
            int cy = y + step / 2;
            // keys, right-aligned
            std::vector<Pixels> badges;
            int keysW = 0;
            for (const std::string& k : row.keys)
            {
                Pixels badge;
                if (LoadBadge(k, badge))
                {
                    keysW += (badges.empty() ? 0 : 4) + badge.w;
                    badges.push_back(badge);
                }
                else
                {
                    LOG_WARNING("Controls screen: no badge " + k);
                }
            }
            int x = kKeyRight - keysW;
            for (const Pixels& badge : badges)
            {
                Paste(canvas, badge, x, cy - badge.h / 2);
                x += badge.w + 4;
            }
            // action
            Pixels text;
            if (GameFont::RenderPixels(row.text, GameFont::Look::Normal, -1, text))
            {
                Pixels small = Shrink(text, kTextPercent);
                int base = baseline * kTextPercent / 100;
                Paste(canvas, small, kKeyRight + kGap, cy + 7 - base);
            }
            y += step;
        }
        return GameFont::ToImage(canvas);
    }
}
