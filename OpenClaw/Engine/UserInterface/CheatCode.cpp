#include "CheatCode.h"

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
    const CheatCode::Dir kCode[] = { CheatCode::Dir::Down, CheatCode::Dir::Down, CheatCode::Dir::Left,
                                     CheatCode::Dir::Right, CheatCode::Dir::Up };
    const int kCodeLength = sizeof(kCode) / sizeof(kCode[0]);
    const Uint32 kMaxGapMs = 2000;   // between two presses

    bool g_Held = false;
    std::vector<CheatCode::Dir> g_Recent;   // last presses while R is held
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
        return PortSettings::GetBool("cheats_unlocked", false);
    }

    void SetModifierHeld(bool held)
    {
        g_Held = held;
        if (!held) g_Recent.clear();
    }

    bool IsModifierHeld()
    {
        return g_Held;
    }

    bool FeedDirection(Dir dir)
    {
        Uint32 now = SDL_GetTicks();
        if (!g_Recent.empty() && now - g_LastPress > kMaxGapMs) g_Recent.clear();
        g_LastPress = now;

        g_Recent.push_back(dir);
        if ((int)g_Recent.size() > kCodeLength) g_Recent.erase(g_Recent.begin());
        if ((int)g_Recent.size() < kCodeLength) return false;
        for (int i = 0; i < kCodeLength; ++i)
        {
            if (g_Recent[i] != kCode[i]) return false;
        }
        g_Recent.clear();
        // Shown whether subtitles are on or off.
        Subtitles::ShowText(Lang::Tr("Nie dość, że pirat to jeszcze oszust", "Not only a pirate, but a cheat as well"), 4000);
        if (!IsUnlocked()) PortSettings::SetBool("cheats_unlocked", true);
        LOG("Cheat code entered");
        return true;
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
            BossModes::UnlockAll();
            Subtitles::ShowText(Lang::Tr("Bossowie i Paruj i kontruj odblokowani", "Bosses and Parry & Thrust unlocked"), 2500);
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
                Lang::Tr("Odblokuj wszystkich bossów i Paruj i kontruj", "Unlock all bosses and Parry & Thrust"),
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
