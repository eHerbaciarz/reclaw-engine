#include "ControlsScreen.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "GameFont.h"
#include "Lang.h"
#include "../GameApp/BaseGameApp.h"
#include "../Graphics2D/Image.h"
#include "../Platform/Platform.h"
#include "../Resource/ResourceMgr.h"
#include "../Resource/ResourceCache.h"
#include "../Util/PortSettings.h"

namespace
{
    const int kWidth = 600;
    // Two columns like the port's earlier screen, each: keys right-aligned,
    // then the action.
    const int kLeftKeys = 48;       // right edge of the left key column
    const int kRightKeys = 352;     // ... of the right one
    const int kGap = 10;            // between the keys and the action
    const int kTop = 54;            // first row
    const int kTextPercent = 55;    // action letters, against the menu buttons' size

    // Buttons per controller style (as in scripts/make_controls_image.py):
    // jump fire sword lift ammo save load rewind pause map ok back
    struct Style { const char* name; const char* b[12]; };
    const Style kStyles[] = {
        { "switch",      { "b", "a", "y", "x", "l", "zl", "zr", "r", "plus", "minus", "a", "b" } },
        { "3ds",         { "b", "a", "y", "x", "l", "zl", "zr", "r", "start", "select", "a", "b" } },
        { "wiiu",        { "b", "a", "y", "x", "l", "zl", "zr", "r", "plus", "minus", "a", "b" } },
#if defined(__vita__)
        // PS Vita: L and R (no L1 / R1, no L2 / R2)
        { "playstation", { "ps_x", "ps_o", "ps_sq", "ps_tri", "l", "l", "r", "r", "start", "select", "ps_x", "ps_o" } },
#endif
        { "playstation", { "ps_x", "ps_o", "ps_sq", "ps_tri", "l1", "l2", "r2", "r1", "start", "select", "ps_x", "ps_o" } },
        { "xbox",        { "a", "b", "x", "y", "lb", "lt", "rt", "rb", "start", "back", "a", "b" } },
        // Steam Deck: A bottom, B right; L1 / R1 bumpers, L2 / R2 triggers, the Menu and View buttons
        { "steamdeck",   { "a", "b", "x", "y", "l1", "l2", "r2", "r1", "menu", "view", "a", "b" } },
        // PortMaster handhelds: PortMaster's controller mapping follows the
        // printed labels (SDL A = the button marked A, on the right)
        { "handheld",    { "a", "b", "x", "y", "l1", "l2", "r2", "r1", "start", "select", "a", "b" } },
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

}

namespace ControlsScreen
{
    std::shared_ptr<Image> Build()
    {
        if (!GameFont::Available()) return nullptr;

        std::string styleName = Platform::ControlsStyle();
        const Style* style = &kStyles[0];
        for (const Style& s : kStyles) if (styleName == s.name) { style = &s; break; }
        const char* const* b = style->b;
        const bool experimental = PortSettings::GetBool("experimental", false);

        struct Row { std::vector<std::string> keys; const char* text; };
        std::vector<Row> left = {
            { { "left" }, Lang::Tr("W LEWO", "MOVE LEFT") },
            { { "right" }, Lang::Tr("W PRAWO", "MOVE RIGHT") },
            { { "up" }, Lang::Tr("WSPINANIE", "CLIMB") },
            { { "down" }, Lang::Tr("KUCANIE I ZEJŚCIE", "DUCK AND CLIMB DOWN") },
            { { b[0] }, Lang::Tr("SKOK", "JUMP") },
            { { b[2] }, Lang::Tr("ATAK WRĘCZ", "SWORD ATTACK") },
            { { b[1] }, Lang::Tr("STRZELANIE", "SHOOT") },
        };
        std::vector<Row> right = {
            { { b[3] }, Lang::Tr("PODNOSZENIE I RZUT", "PICK UP AND THROW") },
            { { b[3] }, Lang::Tr("TRZYMAJ: DALEKI RZUT", "HOLD: FAR THROW") },
            { { b[4] }, Lang::Tr("ZMIEŃ BROŃ", "CHANGE WEAPON") },
            { { b[8] }, Lang::Tr("PAUZA I MENU", "PAUSE AND MENU") },
            { { b[9] }, Lang::Tr("MAPA POZIOMU", "LEVEL MAP") },
        };
        if (experimental)
        {
            right.push_back({ { b[5] }, Lang::Tr("SZYBKI ZAPIS", "QUICK SAVE") });
            right.push_back({ { b[6] }, Lang::Tr("SZYBKIE WCZYTANIE", "QUICK LOAD") });
            right.push_back({ { b[7] }, Lang::Tr("TRZYMAJ: COFANIE CZASU", "HOLD: REWIND") });
        }
        right.push_back({ { b[10], b[11] }, Lang::Tr("MENU: WYBÓR I POWRÓT", "MENU: SELECT AND BACK") });

        const int rowsMax = (int)std::max(left.size(), right.size());
        const int step = rowsMax > 7 ? 21 : 25;
        Pixels canvas;
        canvas.w = kWidth;
        canvas.h = kTop + step * rowsMax + 8;
        canvas.px.assign((size_t)canvas.w * canvas.h, 0);

        // Title, full size, underlined like the original page titles.
        Pixels title;
        if (GameFont::RenderPixels(Lang::Tr("STEROWANIE", "CONTROLS"), GameFont::Look::Normal, -2, title))
        {
            GameFont::Paste(canvas, GameFont::WithShadow(title, 3, 3, 170), (kWidth - title.w) / 2, -GameFont::ExtraTop());
        }

        // Letters' baseline inside a GameFont picture.
        const int baseline = GameFont::ExtraTop() + 27;
        auto drawColumn = [&](const std::vector<Row>& rows, int keyRight, int textRight) {
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
                        keysW += (badges.empty() ? 0 : 3) + badge.w;
                        badges.push_back(badge);
                    }
                    else
                    {
                        LOG_WARNING("Controls screen: no badge " + k);
                    }
                }
                int x = keyRight - keysW;
                for (const Pixels& badge : badges)
                {
                    GameFont::Paste(canvas, badge, x, cy - badge.h / 2);
                    x += badge.w + 3;
                }
                // action, smaller when it would not fit the column
                Pixels text;
                if (GameFont::RenderPixels(row.text, GameFont::Look::Normal, -1, text))
                {
                    int percent = kTextPercent;
                    int room = textRight - (keyRight + kGap);
                    if (text.w * percent / 100 > room) percent = std::max(35, room * 100 / text.w);
                    Pixels small = GameFont::Scale(text, percent);
                    int base = baseline * percent / 100;
                    GameFont::Paste(canvas, GameFont::WithShadow(small, 2, 2, 200), keyRight + kGap, cy + 6 - base);
                }
                y += step;
            }
        };
        drawColumn(left, kLeftKeys, kRightKeys - 46);
        drawColumn(right, kRightKeys, kWidth);
        return GameFont::ToImage(canvas);
    }
}
