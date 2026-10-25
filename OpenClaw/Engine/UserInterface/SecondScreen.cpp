#include "SecondScreen.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <map>
#include <string>

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

    const SDL_Color kGold = { 246, 205, 86, 255 };
    const SDL_Color kCream = { 250, 236, 200, 255 };
    const SDL_Color kDim = { 190, 170, 130, 255 };
    const SDL_Color kEdge = { 30, 15, 2, 255 };

    // The main menu's background (the map) from the game data, as a texture
    // of this screen's renderer.
    void LoadBackground()
    {
        if (g_pBackground || !g_pApp || !g_pApp->GetResourceCache()) return;
        Resource resource("/STATES/MENU/SCREENS/MENU.PCX");
        shared_ptr<ResourceHandle> handle = g_pApp->GetResourceCache()->GetHandle(&resource);
        if (!handle || !handle->GetDataBuffer()) return;
        SDL_RWops* rw = SDL_RWFromConstMem(handle->GetDataBuffer(), (int)handle->GetSize());
        SDL_Surface* surface = rw ? IMG_LoadPCX_RW(rw) : nullptr;
        if (rw) SDL_RWclose(rw);
        if (!surface) return;
        g_pBackground = SDL_CreateTextureFromSurface(g_pRenderer, surface);
        SDL_FreeSurface(surface);
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
        g_pWindow = SDL_CreateWindow("Captain Claw", SDL_WINDOWPOS_UNDEFINED_DISPLAY(1),
                                     SDL_WINDOWPOS_UNDEFINED_DISPLAY(1), g_Width, g_Height, SDL_WINDOW_SHOWN);
#elif defined(__WIIU__)
        g_Width = 854; g_Height = 480;
        g_pWindow = SDL_CreateWindow("Captain Claw", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
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
        if (g_pRenderer) SDL_DestroyRenderer(g_pRenderer);
        if (g_pWindow) SDL_DestroyWindow(g_pWindow);
        g_pBackground = nullptr;
        g_pFontBig = g_pFontSmall = nullptr;
        g_pRenderer = nullptr;
        g_pWindow = nullptr;
    }

    bool IsActive() { return g_pRenderer != nullptr; }

    void Update()
    {
        if (!g_pRenderer) return;
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
                if (p->HasPowerup()) powerup = ToStr(std::max(0, p->GetRemainingPowerupTime() / 1000)) + " s";
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
            // Darkened panel for readability
            SDL_SetRenderDrawBlendMode(g_pRenderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(g_pRenderer, 0, 0, 0, 120);
            SDL_Rect panel = { g_Width / 16, g_Height / 10, g_Width - g_Width / 8, g_Height - g_Height / 5 };
            SDL_RenderFillRect(g_pRenderer, &panel);

            const int x0 = panel.x + g_Width / 20, x1 = panel.x + panel.w - g_Width / 20;
            const int line = g_Height / 10;
            int y = panel.y + line / 3;
            Text(g_pFontBig, level, g_Width / 2, y, kGold, 0);
            y += line + line / 3;
            Text(g_pFontSmall, Lang::Tr("Skarby", "Treasure"), x0, y, kDim);
            Text(g_pFontSmall, score, x1, y, kCream, 1);
            y += line;
            Text(g_pFontSmall, Lang::Tr("Zdrowie", "Health"), x0, y, kDim);
            Text(g_pFontSmall, health, x1, y, kCream, 1);
            y += line;
            Text(g_pFontSmall, Lang::Tr("Życia", "Lives"), x0, y, kDim);
            Text(g_pFontSmall, lives, x1, y, kCream, 1);
            y += line;
            const char* ammoNames[3] = { Lang::Tr("Pistolet", "Pistol"), Lang::Tr("Magia", "Magic"), Lang::Tr("Dynamit", "Dynamite") };
            int colW = (x1 - x0) / 3;
            for (int i = 0; i < 3; ++i)
            {
                SDL_Color c = (i == activeAmmo) ? kGold : kDim;
                Text(g_pFontSmall, ammoNames[i], x0 + colW * i + colW / 2, y, c, 0);
                Text(g_pFontSmall, ammo[i], x0 + colW * i + colW / 2, y + line * 3 / 4, i == activeAmmo ? kGold : kCream, 0);
            }
            y += line * 2;
            if (!powerup.empty())
            {
                Text(g_pFontSmall, Lang::Tr("Moc", "Power-up"), x0, y, kDim);
                Text(g_pFontSmall, powerup, x1, y, kCream, 1);
                y += line;
            }
            if (!race.empty())
            {
                Text(g_pFontSmall, race, g_Width / 2, y, kGold, 0);
            }
        }
        SDL_RenderPresent(g_pRenderer);
    }
}
