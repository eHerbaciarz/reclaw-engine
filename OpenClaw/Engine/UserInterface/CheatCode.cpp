#include "CheatCode.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Actor/Actor.h"
#include "../Actor/Components/ControllerComponents/AmmoComponent.h"
#include "../Actor/Components/ControllerComponents/HealthComponent.h"
#include "../Actor/Components/ControllerComponents/LifeComponent.h"
#include "../Race/RaceUi.h"
#include "../BossModes/BossModes.h"
#include "../Util/PortSettings.h"
#include "Lang.h"
#include "Subtitles.h"

namespace
{
    typedef CheatCode::Key K;
#if defined(OPENCLAW_PORTMASTER)
    // PortMaster maps the buttons by their labels: "B, A" = shoot, jump
    const K kCode[] = { K::Up, K::Up, K::Down, K::Down, K::Left, K::Right, K::Left, K::Right, K::Fire, K::Jump };
#else
    const K kCode[] = { K::Up, K::Up, K::Down, K::Down, K::Left, K::Right, K::Left, K::Right, K::Jump, K::Fire };
#endif
    const int kCodeLength = sizeof(kCode) / sizeof(kCode[0]);
    const Uint32 kMaxGapMs = 1500;   // between two presses

    // The cheats menu: until the game is closed (the code is entered again
    // after every start)
    bool g_Unlocked = false;
    bool g_SpecialMode = false;   // "Skrzyżuj szpady ponownie" in the main menu

    std::vector<K> g_Recent;   // last presses
    Uint32 g_LastPress = 0;

    template <class T>
    shared_ptr<T> ClawComponent(const char* name)
    {
        BaseGameLogic* pLogic = g_pApp ? g_pApp->GetGameLogic() : nullptr;
        StrongActorPtr pClaw = pLogic ? pLogic->GetClawActor() : nullptr;
        return pClaw ? MakeStrongPtr(pClaw->GetComponent<T>(name)) : nullptr;
    }

    std::string OnOff(bool on)
    {
        return on ? Lang::Tr("wł.", "on") : Lang::Tr("wył.", "off");
    }
}

namespace CheatCode
{
    bool IsUnlocked()
    {
        return g_Unlocked;
    }

    // Also only until the game is closed (after the code, Cheats > bosses).
    bool IsSpecialModeOn()
    {
        return g_Unlocked && g_SpecialMode;
    }

    void Lock()
    {
        g_Unlocked = false;
        g_SpecialMode = false;
    }

    bool Feed(Key key)
    {
        if (IsUnlocked()) return false;
        Uint32 now = SDL_GetTicks();
        if (!g_Recent.empty() && now - g_LastPress > kMaxGapMs) g_Recent.clear();
        g_LastPress = now;

        g_Recent.push_back(key);
        if ((int)g_Recent.size() > kCodeLength) g_Recent.erase(g_Recent.begin());
        if ((int)g_Recent.size() < kCodeLength) return false;
        for (int i = 0; i < kCodeLength; ++i)
        {
            if (g_Recent[i] != kCode[i]) return false;
        }
        g_Recent.clear();
        g_SpecialMode = true;   // "Skrzyżuj szpady ponownie" in the main menu too
        // Shown whether subtitles are on or off.
        Subtitles::ShowText(Lang::Tr("Nie dość, że pirat to jeszcze oszust", "Not only a pirate, but a cheat as well"), 4000);
        g_Unlocked = true;
        LOG("Cheat code entered");
        return true;
    }

    int Progress()
    {
        // the longest end of the recent presses that starts the code
        for (int n = std::min((int)g_Recent.size(), kCodeLength); n > 0; --n)
        {
            bool match = true;
            for (int i = 0; i < n && match; ++i)
                match = g_Recent[g_Recent.size() - n + i] == kCode[i];
            if (match) return n;
        }
        return 0;
    }

    bool IsOn(const std::string& name)
    {
        const GameCheats* pCheats = g_pApp ? g_pApp->GetGameCheatsMutable() : nullptr;
        if (name == "experimental") return PortSettings::GetBool("experimental", false);
        if (!pCheats) return false;
        if (name == "invincible") return pCheats->clawInvincible;
        if (name == "ammo") return pCheats->clawInfiniteAmmo;
        if (name == "jump") return pCheats->clawInfiniteJump;
        return false;
    }

    void Apply(const std::string& action)
    {
        GameCheats* pCheats = g_pApp ? g_pApp->GetGameCheatsMutable() : nullptr;
        if (!pCheats) return;
        if (action == "invincible") pCheats->clawInvincible = !pCheats->clawInvincible;
        else if (action == "ammo") pCheats->clawInfiniteAmmo = !pCheats->clawInfiniteAmmo;
        else if (action == "jump") pCheats->clawInfiniteJump = !pCheats->clawInfiniteJump;
        else if (action == "health")
        {
            if (auto p = ClawComponent<HealthComponent>(HealthComponent::g_Name)) p->SetMaxHealth();
            Subtitles::ShowText(Lang::Tr("Pełne zdrowie", "Full health"), 1500);
        }
        else if (action == "fullammo")
        {
            if (auto p = ClawComponent<AmmoComponent>(AmmoComponent::g_Name))
            {
                p->SetAmmo(AmmoType_Pistol, 99);
                p->SetAmmo(AmmoType_Magic, 99);
                p->SetAmmo(AmmoType_Dynamite, 99);
            }
            Subtitles::ShowText(Lang::Tr("Pełna amunicja", "Full ammo"), 1500);
        }
        else if (action == "life")
        {
            if (auto p = ClawComponent<LifeComponent>(LifeComponent::g_Name)) p->AddLives(1);
            Subtitles::ShowText(Lang::Tr("Dodatkowe życie", "Extra life"), 1500);
        }
        else if (action == "bosses")
        {
            // The special mode: "Skrzyżuj szpady ponownie" in the main menu,
            // with every boss and Parry & Thrust.
            BossModes::UnlockAll();
            g_SpecialMode = true;
            Subtitles::ShowText(Lang::Tr("Tryb specjalny odblokowany: Skrzyżuj szpady ponownie w menu głównym",
                                         "Special mode unlocked: Cross Swords Again in the main menu"), 3500);
        }
        else if (action == "experimental")
        {
            PortSettings::SetBool("experimental", !PortSettings::GetBool("experimental", false));
            Subtitles::ShowText(Lang::Tr("Zmiana widoczna po powrocie do menu głównego",
                                         "Applies when you are back in the main menu"), 3000);
        }
    }

    void RunCheatsScreen(SDL_Renderer* pRenderer)
    {
        if (!pRenderer || !g_pApp) return;
        GameCheats* pCheats = g_pApp->GetGameCheatsMutable();
        RaceUi::ListScreen s;
        s.title = Lang::Tr("Kody", "Cheats");
        int selected = 0;
        while (true)
        {
            s.items = {
                std::string(Lang::Tr("Nieśmiertelność: ", "Invincible: ")) + OnOff(pCheats->clawInvincible),
                std::string(Lang::Tr("Nieskończona amunicja: ", "Infinite ammo: ")) + OnOff(pCheats->clawInfiniteAmmo),
                std::string(Lang::Tr("Skok bez ograniczeń: ", "Unlimited jump: ")) + OnOff(pCheats->clawInfiniteJump),
                Lang::Tr("Pełne zdrowie", "Full health"),
                Lang::Tr("Pełna amunicja", "Full ammo"),
                Lang::Tr("Dodatkowe życie", "Extra life"),
                Lang::Tr("Odblokuj tryb specjalny", "Unlock special mode"),
                std::string(Lang::Tr("Tryby eksperymentalne: ", "Experimental modes: ")) + OnOff(PortSettings::GetBool("experimental", false)),
                Lang::Tr("Wróć", "Back"),
            };
            s.selected = selected;
            int choice = RaceUi::Run(pRenderer, s);
            if (choice < 0 || choice == 8) break;
            selected = choice;
            switch (choice)
            {
                case 0: pCheats->clawInvincible = !pCheats->clawInvincible; break;
                case 1: pCheats->clawInfiniteAmmo = !pCheats->clawInfiniteAmmo; break;
                case 2: pCheats->clawInfiniteJump = !pCheats->clawInfiniteJump; break;
                case 3:
                    if (auto p = ClawComponent<HealthComponent>(HealthComponent::g_Name)) p->SetMaxHealth();
                    break;
                case 4:
                    if (auto p = ClawComponent<AmmoComponent>(AmmoComponent::g_Name))
                    {
                        p->SetAmmo(AmmoType_Pistol, 99);
                        p->SetAmmo(AmmoType_Magic, 99);
                        p->SetAmmo(AmmoType_Dynamite, 99);
                    }
                    break;
                case 5:
                    if (auto p = ClawComponent<LifeComponent>(LifeComponent::g_Name)) p->AddLives(1);
                    break;
                case 6:
                    BossModes::UnlockAll();
                    Subtitles::ShowText(Lang::Tr("Bossowie odblokowani", "Bosses unlocked"), 2500);
                    break;
                case 7:
                    PortSettings::SetBool("experimental", !PortSettings::GetBool("experimental", false));
                    Subtitles::ShowText(Lang::Tr("Zmiana widoczna po powrocie do menu głównego",
                                                 "Applies when you are back in the main menu"), 3000);
                    break;
            }
        }
    }
}
