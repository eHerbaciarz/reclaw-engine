#include "SecondScreen.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Actor/Actor.h"
#include "../Actor/Components/ControllerComponents/AmmoComponent.h"
#include "../Actor/Components/ControllerComponents/HealthComponent.h"
#include "../Actor/Components/ControllerComponents/LifeComponent.h"
#include "../Actor/Components/ControllerComponents/PowerupComponent.h"
#include "../Actor/Components/ControllerComponents/ScoreComponent.h"
#include "../Platform/Platform.h"
#include "../Resource/ResourceCache.h"
#include "../Race/Race.h"
#include "Lang.h"
#include "../Graphics2D/Image.h"
#include "../Resource/ResourceMgr.h"

namespace
{
    SDL_Window* g_pWindow = nullptr;
    SDL_Renderer* g_pRenderer = nullptr;
    SDL_Texture* g_pBackground = nullptr;
    TTF_Font* g_pFontBig = nullptr;
    TTF_Font* g_pFontSmall = nullptr;
    int g_Width = 0, g_Height = 0;
    std::string g_LastState;
    Uint32 g_LastDraw = 0;

    // The menu shown on this screen (3DS): a copy of the top screen's picture
    SDL_Texture* g_pMirror = nullptr;
    int g_MirrorW = 0, g_MirrorH = 0;
    std::vector<Uint32> g_MirrorPixels;
    SDL_Rect g_MirrorRect = { 0, 0, 0, 0 };   // where it is drawn here
    SDL_Rect g_MirrorSrc = { 0, 0, 0, 0 };    // the part of the top screen shown
    bool g_MirrorShown = false;

    bool InMenu()
    {
        if (!g_pApp || !g_pApp->GetGameLogic()) return false;
        return g_pApp->GetGameLogic()->GetGameState() == GameState_Menu || g_pApp->IsMenuActive();
    }

    const SDL_Color kGold = { 246, 205, 86, 255 };
    const SDL_Color kCream = { 250, 236, 200, 255 };
    const SDL_Color kDim = { 190, 170, 130, 255 };
    const SDL_Color kEdge = { 30, 15, 2, 255 };

    // The menu picture without the blue-silver CLAW title at its top: those
    // pixels (bluish or grey, where the parchment is orange-brown) are filled
    // with parchment taken from nearby, so the texture goes on.
    void RemoveLogo(SDL_Surface* s)
    {
        const int w = s->w, h = s->h;
        const int y1 = h * 32 / 100, x0 = w * 12 / 100, x1 = w * 88 / 100;
        auto px = [&](int x, int y) -> Uint32& { return ((Uint32*)((Uint8*)s->pixels + y * s->pitch))[x]; };
        std::vector<Uint8> mask((size_t)w * y1, 0);
        for (int y = 0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
            {
                Uint32 c = px(x, y);
                int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                if (b + 12 >= r) mask[(size_t)y * w + x] = 1;   // not orange-brown
            }
        // grow by 3 pixels: the letters' soft edges
        for (int pass = 0; pass < 3; ++pass)
        {
            std::vector<Uint8> grown(mask);
            for (int y = 0; y < y1; ++y)
                for (int x = 1; x < w - 1; ++x)
                {
                    if (mask[(size_t)y * w + x]) continue;
                    if (mask[(size_t)y * w + x - 1] || mask[(size_t)y * w + x + 1] ||
                        (y > 0 && mask[(size_t)(y - 1) * w + x]) || (y + 1 < y1 && mask[(size_t)(y + 1) * w + x]))
                        grown[(size_t)y * w + x] = 1;
                }
            mask.swap(grown);
        }
        // fill: a random parchment pixel from the nearest ring around it
        Uint32 seed = 12345;
        auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (int)((seed >> 16) & 0x7fff); };
        for (int y = 0; y < y1; ++y)
            for (int x = 0; x < w; ++x)
            {
                if (!mask[(size_t)y * w + x]) continue;
                for (int r = 2; r < 200; r += 2)
                {
                    int found = 0;
                    Uint32 pick = 0;
                    for (int t = 0; t < 12; ++t)
                    {
                        int sx = x + rnd() % (2 * r + 1) - r, sy = y + rnd() % (2 * r + 1) - r;
                        if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                        if (sy < y1 && mask[(size_t)sy * w + sx]) continue;
                        pick = px(sx, sy);
                        ++found;
                        break;
                    }
                    if (found) { px(x, y) = pick; break; }
                }
            }
    }

    // The main menu's background (the map) from the game data, as a texture
    // of this screen's renderer.
    void LoadBackground()
    {
        if (g_pBackground || !g_pApp || !g_pApp->GetResourceCache()) return;
        Resource resource("/STATES/MENU/SCREENS/MENU.PCX");
        shared_ptr<ResourceHandle> handle = g_pApp->GetResourceCache()->GetHandle(&resource);
        if (!handle || !handle->GetDataBuffer()) return;
        SDL_RWops* rw = SDL_RWFromConstMem(handle->GetDataBuffer(), (int)handle->GetSize());
        SDL_Surface* loaded = rw ? IMG_LoadPCX_RW(rw) : nullptr;
        if (rw) SDL_RWclose(rw);
        if (!loaded) return;
        SDL_Surface* surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(loaded);
        if (!surface) return;
        RemoveLogo(surface);
        g_pBackground = SDL_CreateTextureFromSurface(g_pRenderer, surface);
        SDL_FreeSurface(surface);
    }


    // The game's own HUD pictures (from CLAW.REZ), as textures of this screen's
    // renderer, in the current level's palette. Cleared when the level changes.
    std::map<std::string, shared_ptr<Image>> g_HudImages;
    int g_HudLevel = -1;

    shared_ptr<Image> HudImage(const std::string& path)
    {
        auto it = g_HudImages.find(path);
        if (it != g_HudImages.end()) return it->second;
        shared_ptr<Image> img;
        if (g_pApp && g_pApp->GetResourceCache())
        {
            Resource resource(path.c_str());
            shared_ptr<ResourceHandle> h = g_pApp->GetResourceCache()->GetHandle(&resource);
            if (h && h->GetDataBuffer())
            {
                std::vector<char> data(h->GetDataBuffer(), h->GetDataBuffer() + h->GetSize());
                if (WapPid* pid = WAP_PidLoadFromData(data.data(), data.size(), g_pApp->GetCurrentPalette()))
                {
                    img.reset(Image::CreateImage(pid, g_pRenderer));
                    WAP_PidDestroy(pid);
                }
            }
        }
        g_HudImages[path] = img;
        return img;
    }

    // First picture of an animated HUD element (chest, heart, weapons...).
    shared_ptr<Image> HudIcon(const std::string& folder)
    {
        auto it = g_HudImages.find(folder);
        if (it != g_HudImages.end()) return it->second;
        shared_ptr<Image> img;
        if (g_pApp && g_pApp->GetResourceCache())
        {
            std::vector<std::string> files = g_pApp->GetResourceCache()->Match(folder + "*");
            std::sort(files.begin(), files.end());
            if (!files.empty()) img = HudImage(files.front());
        }
        g_HudImages[folder] = img;
        return img;
    }

    // Draws a picture scaled, centred vertically on cy; returns its width.
    int Draw(const shared_ptr<Image>& img, int x, int cy, double scale, Uint8 alpha = 255)
    {
        if (!img || !img->GetTexture()) return 0;
        SDL_Rect dst = { x, cy - (int)(img->GetHeight() * scale) / 2,
                         (int)(img->GetWidth() * scale), (int)(img->GetHeight() * scale) };
        SDL_SetTextureAlphaMod(img->GetTexture(), alpha);
        SDL_RenderCopy(g_pRenderer, img->GetTexture(), NULL, &dst);
        SDL_SetTextureAlphaMod(img->GetTexture(), 255);
        return dst.w;
    }

    // A number in the HUD's digit pictures ("scorenumbers", "healthnumbers",
    // "smallnumbers"), with leading zeros to `digits`; returns the width.
    int Digits(const char* set, int value, int digits, int x, int cy, double scale, Uint8 alpha = 255)
    {
        std::string s = std::to_string(std::max(0, value));
        while ((int)s.size() < digits) s = "0" + s;
        int w = 0;
        for (char c : s)
        {
            shared_ptr<Image> d = HudImage(std::string("/game/images/interface/") + set + "/00" + c + ".pid");
            w += Draw(d, x + w, cy, scale, alpha);
        }
        return w;
    }

    void Text(TTF_Font* font, const std::string& s, int x, int y, SDL_Color color, int align = -1)
    {
        if (!font || s.empty()) return;
        int outline = std::max(1, TTF_FontHeight(font) / 14);
        TTF_SetFontOutline(font, outline);
        SDL_Surface* back = TTF_RenderUTF8_Blended(font, s.c_str(), kEdge);
        TTF_SetFontOutline(font, 0);
        SDL_Surface* front = TTF_RenderUTF8_Blended(font, s.c_str(), color);
        if (back && front)
        {
            SDL_Rect inner = { outline, outline, front->w, front->h };
            SDL_SetSurfaceBlendMode(front, SDL_BLENDMODE_BLEND);
            SDL_BlitSurface(front, NULL, back, &inner);
            SDL_Texture* tex = SDL_CreateTextureFromSurface(g_pRenderer, back);
            SDL_Rect dst = { x, y, back->w, back->h };
            if (align == 0) dst.x -= back->w / 2;
            else if (align > 0) dst.x -= back->w;
            SDL_RenderCopy(g_pRenderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
        }
        if (back) SDL_FreeSurface(back);
        if (front) SDL_FreeSurface(front);
    }

    template <class T>
    shared_ptr<T> Component(const StrongActorPtr& actor, const char* name)
    {
        return actor ? MakeStrongPtr(actor->GetComponent<T>(name)) : nullptr;
    }

}

namespace SecondScreen
{
    void Init()
    {
        if (!Platform::HasSecondScreen() || g_pWindow) return;
#if defined(__3DS__)
        g_Width = 320; g_Height = 240;
        g_pWindow = SDL_CreateWindow("ReClaw", SDL_WINDOWPOS_UNDEFINED_DISPLAY(1),
                                     SDL_WINDOWPOS_UNDEFINED_DISPLAY(1), g_Width, g_Height, SDL_WINDOW_SHOWN);
#elif defined(__WIIU__)
        g_Width = 854; g_Height = 480;
        g_pWindow = SDL_CreateWindow("ReClaw", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     g_Width, g_Height, SDL_WINDOW_SHOWN | SDL_WINDOW_WIIU_GAMEPAD_ONLY);
#endif
        if (!g_pWindow)
        {
            LOG_WARNING(std::string("Second screen: no window: ") + SDL_GetError());
            return;
        }
        g_pRenderer = SDL_CreateRenderer(g_pWindow, -1, 0);
        if (!g_pRenderer)
        {
            LOG_WARNING(std::string("Second screen: no renderer: ") + SDL_GetError());
            SDL_DestroyWindow(g_pWindow);
            g_pWindow = nullptr;
            return;
        }
        if (!TTF_WasInit()) TTF_Init();
        std::string font = Platform::SplashFont();
        g_pFontBig = TTF_OpenFont(font.c_str(), g_Height / 12);
        g_pFontSmall = TTF_OpenFont(font.c_str(), g_Height / 18);
        LOG("Second screen ready: " + ToStr(g_Width) + "x" + ToStr(g_Height));
    }

    void Shutdown()
    {
        if (g_pBackground) SDL_DestroyTexture(g_pBackground);
        if (g_pFontBig) TTF_CloseFont(g_pFontBig);
        if (g_pFontSmall) TTF_CloseFont(g_pFontSmall);
        g_HudImages.clear();
        if (g_pMirror) SDL_DestroyTexture(g_pMirror);
        g_pMirror = nullptr;
        if (g_pRenderer) SDL_DestroyRenderer(g_pRenderer);
        if (g_pWindow) SDL_DestroyWindow(g_pWindow);
        g_pBackground = nullptr;
        g_pFontBig = g_pFontSmall = nullptr;
        g_pRenderer = nullptr;
        g_pWindow = nullptr;
    }

    bool IsActive() { return g_pRenderer != nullptr; }

    bool ShowsHud()
    {
#if defined(__3DS__)
        return g_pRenderer != nullptr;
#else
        return false;
#endif
    }

    void DrawBackground()
    {
        SDL_SetRenderDrawColor(g_pRenderer, 0, 0, 0, 255);
        SDL_RenderClear(g_pRenderer);
        if (g_pBackground)
        {
            int tw = 0, th = 0;
            SDL_QueryTexture(g_pBackground, NULL, NULL, &tw, &th);
            double scale = std::max(g_Width / (double)tw, g_Height / (double)th);
            SDL_Rect dst = { (int)(g_Width - tw * scale) / 2, (int)(g_Height - th * scale) / 2,
                             (int)(tw * scale), (int)(th * scale) };
            SDL_RenderCopy(g_pRenderer, g_pBackground, NULL, &dst);
        }
    }

    // The menu as on the top screen (3DS), about 20 times a second.
    bool UpdateMirror()
    {
#if defined(__3DS__)
        if (!InMenu())
        {
            if (g_MirrorShown) { g_MirrorShown = false; g_LastState.clear(); }
            return false;
        }
        static Uint32 s_last = 0;
        if (SDL_GetTicks() - s_last < 50) return true;
        s_last = SDL_GetTicks();
        SDL_Renderer* top = g_pApp->GetRenderer();
        int w = 0, h = 0;
        if (SDL_Texture* target = SDL_GetRenderTarget(top)) SDL_QueryTexture(target, NULL, NULL, &w, &h);
        else SDL_GetRendererOutputSize(top, &w, &h);
        if (w <= 0 || h <= 0) return false;
        if (!g_pMirror || w != g_MirrorW || h != g_MirrorH)
        {
            if (g_pMirror) SDL_DestroyTexture(g_pMirror);
            g_pMirror = SDL_CreateTexture(g_pRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            g_MirrorW = w; g_MirrorH = h;
            g_MirrorPixels.assign((size_t)w * h, 0);
        }
        if (!g_pMirror) return false;
        float sx = 1.0f, sy = 1.0f;
        SDL_RenderGetScale(top, &sx, &sy);
        SDL_RenderSetScale(top, 1.0f, 1.0f);
        int ok = SDL_RenderReadPixels(top, NULL, SDL_PIXELFORMAT_ARGB8888, g_MirrorPixels.data(), w * 4);
        SDL_RenderSetScale(top, sx, sy);
        if (ok != 0) return false;
        SDL_UpdateTexture(g_pMirror, NULL, g_MirrorPixels.data(), w * 4);

        LoadBackground();
        DrawBackground();
        // The menu is laid out in 4:3 (640x480) in the middle of the top
        // screen: that part, filling this 4:3 screen (the 3DS: 1:1).
        int srcW = std::min(w, h * g_Width / g_Height);
        g_MirrorSrc = { (w - srcW) / 2, 0, srcW, h };
        g_MirrorRect = { 0, 0, g_Width, g_Height };
        SDL_RenderCopy(g_pRenderer, g_pMirror, &g_MirrorSrc, &g_MirrorRect);
        SDL_RenderPresent(g_pRenderer);
        g_MirrorShown = true;
        return true;
#else
        return false;
#endif
    }

    void Update()
    {
        if (!g_pRenderer) return;
        if (UpdateMirror()) return;
        // A few times per second is plenty for a status screen.
        if (SDL_GetTicks() - g_LastDraw < 200) return;

        LoadBackground();

        BaseGameLogic* pLogic = g_pApp->GetGameLogic();
        StrongActorPtr pClaw = pLogic ? pLogic->GetClawActor() : nullptr;
        const bool inGame = pLogic && pClaw && pLogic->GetGameState() == GameState_IngameRunning;

        // Gather what is shown; redraw only when it changed.
        std::string level, health, lives, score, ammo[3], powerup, race;
        int activeAmmo = -1;
        if (inGame)
        {
            if (shared_ptr<LevelData> pLevel = pLogic->GetCurrentLevelData())
            {
                level = std::string(Lang::Tr("Poziom ", "Level ")) + ToStr((int)pLevel->GetLevelNumber());
            }
            if (auto p = Component<HealthComponent>(pClaw, HealthComponent::g_Name)) health = ToStr(std::max(0, (int)p->GetHealth()));
            if (auto p = Component<LifeComponent>(pClaw, LifeComponent::g_Name)) lives = ToStr(p->GetLives());
            if (auto p = Component<ScoreComponent>(pClaw, ScoreComponent::g_Name)) score = ToStr((int)p->GetScore());
            if (auto p = Component<AmmoComponent>(pClaw, AmmoComponent::g_Name))
            {
                ammo[0] = ToStr((int)p->GetRemainingAmmo(AmmoType_Pistol));
                ammo[1] = ToStr((int)p->GetRemainingAmmo(AmmoType_Magic));
                ammo[2] = ToStr((int)p->GetRemainingAmmo(AmmoType_Dynamite));
                activeAmmo = (int)p->GetActiveAmmoType();
            }
            if (auto p = Component<PowerupComponent>(pClaw, PowerupComponent::g_Name))
            {
                if (p->HasPowerup()) powerup = ToStr(std::max(0, (int)(p->GetRemainingPowerupTime() / 1000))) + " s";
            }
            if (Race::IsActive()) race = Lang::Tr("Wyścig", "Race");
        }
        std::string state = level + "|" + health + "|" + lives + "|" + score + "|" + ammo[0] + ammo[1] + ammo[2] +
                            "|" + ToStr(activeAmmo) + "|" + powerup + "|" + race + (g_pBackground ? "b" : "");
        if (state == g_LastState) return;
        g_LastState = state;
        g_LastDraw = SDL_GetTicks();

        SDL_SetRenderDrawColor(g_pRenderer, 0, 0, 0, 255);
        SDL_RenderClear(g_pRenderer);
        if (g_pBackground)
        {
            // The map fills the screen, cropped to its aspect ratio.
            int tw = 0, th = 0;
            SDL_QueryTexture(g_pBackground, NULL, NULL, &tw, &th);
            double scale = std::max(g_Width / (double)tw, g_Height / (double)th);
            SDL_Rect dst = { (int)(g_Width - tw * scale) / 2, (int)(g_Height - th * scale) / 2,
                             (int)(tw * scale), (int)(th * scale) };
            SDL_RenderCopy(g_pRenderer, g_pBackground, NULL, &dst);
        }

        if (inGame)
        {
            // The game's HUD, moved here from the top screen (3DS): the same
            // pictures as in the level, twice the size, on a darkened panel.
            int levelNo = 0;
            if (shared_ptr<LevelData> pLevel = pLogic->GetCurrentLevelData()) levelNo = pLevel->GetLevelNumber();
            if (levelNo != g_HudLevel) { g_HudImages.clear(); g_HudLevel = levelNo; }

            SDL_SetRenderDrawBlendMode(g_pRenderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(g_pRenderer, 0, 0, 0, 130);
            SDL_Rect panel = { 10, 8, g_Width - 20, g_Height - 16 };
            SDL_RenderFillRect(g_pRenderer, &panel);

            const double k = g_Width >= 640 ? 2.5 : 1.5;   // GamePad / 3DS
            const int left = panel.x + 10;
            const int rowH = (int)(30 * k);
            int cy = panel.y + g_Height / 14;
            Text(g_pFontBig, level, g_Width / 2, cy - g_Height / 24, kGold, 0);
            cy += (int)(26 * k);

            int iconW = (int)(34 * k);
            Draw(HudIcon("/game/images/interface/treasurechest/"), left, cy, k);
            Digits("scorenumbers", atoi(score.c_str()), 8, left + iconW + 4, cy, k);
            cy += rowH;

            Draw(HudIcon("/game/images/interface/healthheart/"), left, cy, k);
            int hx = left + iconW + 4;
            hx += Digits("healthnumbers", atoi(health.c_str()), 3, hx, cy, k);
            int lx = panel.x + panel.w - (int)(78 * k);
            Draw(HudIcon("/game/images/interface/liveshead/"), lx, cy, k);
            Digits("smallnumbers", atoi(lives.c_str()), 1, lx + (int)(30 * k), cy, k * 1.3);
            cy += rowH;

            // the three weapons; the selected one bright
            const char* weapons[3] = { "pistol", "magic", "dynamite" };
            int colW = (panel.w - 20) / 3;
            for (int i = 0; i < 3; ++i)
            {
                Uint8 a = (i == activeAmmo) ? 255 : 110;
                int wx = left + colW * i;
                int iw = Draw(HudIcon(std::string("/game/images/interface/weapons/") + weapons[i] + "/"), wx, cy, k, a);
                Digits("smallnumbers", atoi(ammo[i].c_str()), 2, wx + iw + 3, cy, k * 1.2, a);
            }
            cy += rowH;

            if (!powerup.empty())
            {
                Draw(HudIcon("/game/images/interface/stopwatch/"), left, cy, k);
                Digits("scorenumbers", atoi(powerup.c_str()), 3, left + iconW + 4, cy, k);
            }
            if (!race.empty())
            {
                Text(g_pFontSmall, race, g_Width / 2, panel.y + panel.h - g_Height / 10, kGold, 0);
            }
        }
        SDL_RenderPresent(g_pRenderer);
    }

    bool HandleTouch(const SDL_Event& evt)
    {
#if defined(__3DS__)
        if (evt.type != SDL_FINGERDOWN && evt.type != SDL_FINGERUP && evt.type != SDL_FINGERMOTION) return false;
        // Only the menu on the bottom screen takes touches.
        if (!g_MirrorShown || !InMenu() || g_MirrorRect.w <= 0 || !g_pApp->GetWindow()) return true;
        int px = (int)(evt.tfinger.x * g_Width), py = (int)(evt.tfinger.y * g_Height);
        int wx = g_MirrorSrc.x + (px - g_MirrorRect.x) * g_MirrorSrc.w / g_MirrorRect.w;
        int wy = g_MirrorSrc.y + (py - g_MirrorRect.y) * g_MirrorSrc.h / g_MirrorRect.h;
        if (wx < 0 || wy < 0 || wx >= g_MirrorW || wy >= g_MirrorH) return true;
        SDL_Window* top = g_pApp->GetWindow();
        SDL_WarpMouseInWindow(top, wx, wy);   // the sword cursor follows the finger
        if (evt.type != SDL_FINGERMOTION)
        {
            SDL_Event e;
            SDL_zero(e);
            e.type = evt.type == SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.windowID = SDL_GetWindowID(top);
            e.button.button = SDL_BUTTON_LEFT;
            e.button.state = evt.type == SDL_FINGERDOWN ? SDL_PRESSED : SDL_RELEASED;
            e.button.clicks = 1;
            e.button.x = wx;
            e.button.y = wy;
            SDL_PushEvent(&e);
        }
        return true;
#else
        (void)evt;
        return false;
#endif
    }
}
