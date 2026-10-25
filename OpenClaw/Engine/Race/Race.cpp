#include "Race.h"

#include <map>

#include <tinyxml.h>

#include "RaceNet.h"
#include "RaceUi.h"

#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Actor/Actor.h"
#include "../Actor/Components/PositionComponent.h"
#include "../Actor/Components/RenderComponent.h"
#include "../Actor/Components/ControllableComponent.h"
#include "../Actor/Components/ControllerComponents/HealthComponent.h"
#include "../Events/EventMgr.h"
#include "../Events/Events.h"
#include "../UserInterface/HumanView.h"
#include "../UserInterface/Lang.h"

namespace
{
    enum class Phase
    {
        Idle,
        WaitingPeer,    // network: level loaded, waiting for the other console
        Countdown,
        Running,
        Finished,       // someone won (or the opponent left)
        Done,           // results shown, going back to the menu
    };

    const int kCountdownMs = 3000;
    const uint32_t kNoTime = 0xffffffff;
    const SDL_Color kWhite = { 255, 255, 255, 255 };
    const SDL_Color kGold = { 246, 205, 86, 255 };

    Race::Setup g_Setup;
    bool g_Active = false;
    Phase g_Phase = Phase::Idle;
    uint32_t g_Players[2] = { 0, 0 };
    uint32_t g_Ghost = 0;
    std::map<uint32_t, Point> g_Spawns;

    int g_CountdownMs = 0;
    uint32_t g_RaceMs = 0;
    uint32_t g_FinishMs[2] = { kNoTime, kNoTime };
    int g_Winner = -1;                 // 0 / 1 (network: 0 = this console)
    std::string g_EndReason;           // opponent left / connection lost
    int g_FinishedForMs = 0;
    int g_GoShownMs = 0;

    // network
    bool g_PeerReady = false;
    uint32_t g_LastPeerPacket = 0;
    int g_SendTimerMs = 0;
    int g_GoResendMs = 0;

    shared_ptr<ClawControllableComponent> GetControl(uint32_t actorId)
    {
        StrongActorPtr p = MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(actorId));
        return p ? MakeStrongPtr(p->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name)) : nullptr;
    }

    void FreezePlayers(bool frozen)
    {
        for (uint32_t id : g_Players)
        {
            if (auto pControl = GetControl(id)) pControl->SetFrozen(frozen);
        }
    }

    std::string FormatTime(uint32_t ms)
    {
        if (ms == kNoTime) return "-";
        char buf[32];
        snprintf(buf, sizeof(buf), "%u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
        return buf;
    }

    std::string PlayerName(int index)
    {
        if (g_Setup.mode == Race::Mode::Network)
        {
            return index == 0 ? Lang::Tr("Ty", "You") : Lang::Tr("Rywal", "Opponent");
        }
        return index == 0 ? Lang::Tr("Gracz 1", "Player 1") : Lang::Tr("Gracz 2", "Player 2");
    }

    // A copy of Claw's creation XML with a different name; for the ghost only
    // the position and the picture are kept.
    StrongActorPtr CreateFromClaw(BaseGameLogic* pLogic, const std::string& name, bool ghost)
    {
        StrongActorPtr pClaw = pLogic->GetClawActor();
        if (!pClaw || !pClaw->GetCreationXml()) return nullptr;
        TiXmlDocument doc;
        doc.Parse(pClaw->GetCreationXml()->c_str());
        TiXmlElement* pRoot = doc.RootElement();
        if (!pRoot) return nullptr;
        pRoot->SetAttribute("Type", name.c_str());
        if (ghost)
        {
            TiXmlElement* pChild = pRoot->FirstChildElement();
            while (pChild)
            {
                TiXmlElement* pNext = pChild->NextSiblingElement();
                std::string value = pChild->Value();
                if (value != "PositionComponent" && value != "ActorRenderComponent")
                {
                    pRoot->RemoveChild(pChild);
                }
                pChild = pNext;
            }
        }
        StrongActorPtr pActor = pLogic->VCreateActor(pRoot, NULL);
        if (pActor)
        {
            IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_New_Actor(pActor->GetGUID())));
        }
        return pActor;
    }

    void SetStartingStats(uint32_t actorId)
    {
        IEventMgr* pEventMgr = IEventMgr::Get();
        pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(actorId, PlayerStat_Score, 0, false)));
        pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(actorId, PlayerStat_Health, 100, false)));
        pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(actorId, PlayerStat_Lives, 9, false)));
        pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(actorId, PlayerStat_Bullets, 10, false)));
        pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(actorId, PlayerStat_Magic, 5, false)));
        pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(actorId, PlayerStat_Dynamite, 3, false)));
    }

    void StartCountdown()
    {
        g_Phase = Phase::Countdown;
        g_CountdownMs = kCountdownMs;
        g_RaceMs = 0;
        LOG("Race: countdown");
    }

    void Finish(int winner, const std::string& reason)
    {
        if (g_Phase == Phase::Finished || g_Phase == Phase::Done) return;
        g_Phase = Phase::Finished;
        g_Winner = winner;
        g_EndReason = reason;
        g_FinishedForMs = 0;
        FreezePlayers(true);
        LOG("Race: finished, winner " + std::to_string(winner) + " " + reason);
    }

    // ---------------------------------------------------------------- network
    void SendState(BaseGameLogic* pLogic)
    {
        StrongActorPtr pClaw = MakeStrongPtr(pLogic->VGetActor(g_Players[0]));
        if (!pClaw) return;
        shared_ptr<ActorRenderComponent> pRender =
            MakeStrongPtr(pClaw->GetComponent<ActorRenderComponent>(ActorRenderComponent::g_Name));
        if (!pRender) return;
        Point pos = pClaw->GetPositionComponent()->GetPosition();
        std::vector<uint8_t> p;
        RaceNet::PutF32(p, (float)pos.x);
        RaceNet::PutF32(p, (float)pos.y);
        RaceNet::PutStr(p, pRender->GetCurrentImageName());
        p.push_back((uint8_t)((pRender->IsMirrored() ? 1 : 0) | (pRender->IsVisible() ? 2 : 0)));
        RaceNet::PutU32(p, g_RaceMs);
        RaceNet::SendToPeer(RaceNet::Msg_State, p);
    }

    void ApplyGhostState(BaseGameLogic* pLogic, const std::vector<uint8_t>& payload)
    {
        StrongActorPtr pGhost = MakeStrongPtr(pLogic->VGetActor(g_Ghost));
        if (!pGhost) return;
        size_t pos = 0;
        float x = 0, y = 0;
        std::string image;
        if (!RaceNet::GetF32(payload, pos, x) || !RaceNet::GetF32(payload, pos, y) ||
            !RaceNet::GetStr(payload, pos, image) || pos >= payload.size())
        {
            return;
        }
        uint8_t flags = payload[pos];
        shared_ptr<ActorRenderComponent> pRender =
            MakeStrongPtr(pGhost->GetComponent<ActorRenderComponent>(ActorRenderComponent::g_Name));
        if (pRender)
        {
            if (!image.empty()) pRender->SetImage(image);
            pRender->SetMirrored((flags & 1) != 0);
            pRender->SetVisible((flags & 2) != 0);
        }
        Point p(x, y);
        pGhost->GetPositionComponent()->SetPosition(p);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Move_Actor(g_Ghost, p)));
    }

    void UpdateNetwork(BaseGameLogic* pLogic, uint32_t msDiff)
    {
        RaceNet::Message m;
        while (RaceNet::Receive(m))
        {
            g_LastPeerPacket = SDL_GetTicks();
            switch (m.type)
            {
                case RaceNet::Msg_Ready:
                    g_PeerReady = true;
                    // The joiner missed GO: send it again.
                    if (g_Setup.host && g_Phase != Phase::WaitingPeer)
                    {
                        RaceNet::SendToPeer(RaceNet::Msg_Go);
                    }
                    break;
                case RaceNet::Msg_Go:
                    if (!g_Setup.host && g_Phase == Phase::WaitingPeer) StartCountdown();
                    break;
                case RaceNet::Msg_State:
                    ApplyGhostState(pLogic, m.payload);
                    break;
                case RaceNet::Msg_Finish:
                {
                    size_t pos = 0;
                    uint32_t time = 0;
                    RaceNet::GetU32(m.payload, pos, time);
                    if (g_FinishMs[1] == kNoTime) g_FinishMs[1] = time;
                    if (g_Phase == Phase::Running || g_Phase == Phase::Countdown) Finish(1, "");
                    break;
                }
                case RaceNet::Msg_Leave:
                    if (g_Phase != Phase::Finished && g_Phase != Phase::Done)
                    {
                        Finish(0, Lang::Tr("Rywal opuścił wyścig.", "The opponent left the race."));
                    }
                    break;
                default:
                    break;
            }
        }

        g_SendTimerMs += msDiff;
        if (g_Phase == Phase::WaitingPeer)
        {
            if (g_SendTimerMs >= 300)
            {
                g_SendTimerMs = 0;
                RaceNet::SendToPeer(RaceNet::Msg_Ready);
            }
            if (g_Setup.host && g_PeerReady)
            {
                RaceNet::SendToPeer(RaceNet::Msg_Go);
                g_GoResendMs = 1000;
                StartCountdown();
            }
        }
        else if (g_Phase == Phase::Countdown || g_Phase == Phase::Running)
        {
            if (g_GoResendMs > 0) g_GoResendMs -= (int)msDiff;
            if (g_SendTimerMs >= 50)
            {
                g_SendTimerMs = 0;
                SendState(pLogic);
                // GO is repeated for a while in case the first one got lost.
                if (g_Setup.host && g_GoResendMs > 0) RaceNet::SendToPeer(RaceNet::Msg_Go);
            }
        }
        else if (g_Phase == Phase::Finished)
        {
            // Repeat our finish so the other side surely hears it.
            if (g_Winner == 0 && g_SendTimerMs >= 250)
            {
                g_SendTimerMs = 0;
                std::vector<uint8_t> p;
                RaceNet::PutU32(p, g_FinishMs[0]);
                RaceNet::SendToPeer(RaceNet::Msg_Finish, p);
            }
        }

        if (g_Phase != Phase::Finished && g_Phase != Phase::Done && g_LastPeerPacket != 0 &&
            SDL_GetTicks() - g_LastPeerPacket > 8000)
        {
            Finish(-1, Lang::Tr("Utracono połączenie z rywalem.", "Lost connection to the opponent."));
        }
    }

    void ShowResultsAndLeave()
    {
        std::vector<std::string> lines;
        if (!g_EndReason.empty()) lines.push_back(g_EndReason);
        for (int i = 0; i < 2; ++i)
        {
            lines.push_back(PlayerName(i) + ": " + FormatTime(g_FinishMs[i]));
        }
        std::string title;
        if (g_Winner < 0) title = Lang::Tr("Koniec wyścigu", "Race over");
        else if (g_Setup.mode == Race::Mode::Network) title = g_Winner == 0 ? Lang::Tr("Wygrałeś!", "You win!") : Lang::Tr("Przegrałeś!", "You lose!");
        else title = PlayerName(g_Winner) + Lang::Tr(" wygrywa!", " wins!");

        RaceUi::ShowMessage(g_pApp->GetRenderer(), title, lines);
        g_Phase = Phase::Done;
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Enter_Menu()));
    }
}

namespace Race
{
    void Configure(const Setup& setup)
    {
        End();
        g_Setup = setup;
        g_Active = setup.mode != Mode::None;
        LOG("Race configured: mode " + std::to_string((int)setup.mode) + ", level " + std::to_string(setup.level));
    }

    void End()
    {
        if (g_Active && g_Setup.mode == Mode::Network)
        {
            for (int i = 0; i < 3; ++i) RaceNet::SendToPeer(RaceNet::Msg_Leave);
            RaceNet::Close();
        }
        if (g_Active)
        {
            if (HumanView* pView = g_pApp->GetHumanView()) pView->DisableSplitScreen();
            RaceUi::FreeTextCache();
        }
        g_Active = false;
        g_Setup = Setup();
        g_Phase = Phase::Idle;
        g_Players[0] = g_Players[1] = 0;
        g_Ghost = 0;
        g_Spawns.clear();
        g_FinishMs[0] = g_FinishMs[1] = kNoTime;
        g_Winner = -1;
        g_EndReason.clear();
        g_PeerReady = false;
        g_LastPeerPacket = 0;
        g_SendTimerMs = 0;
        g_GoResendMs = 0;
        g_GoShownMs = 0;
    }

    bool IsActive() { return g_Active; }
    bool IsSplit() { return g_Active && g_Setup.mode == Mode::SplitScreen; }
    bool IsNetwork() { return g_Active && g_Setup.mode == Mode::Network; }
    const Setup& GetSetup() { return g_Setup; }

    void OnLevelLoaded(BaseGameLogic* pLogic)
    {
        if (!g_Active) return;
        StrongActorPtr pClaw = pLogic->GetClawActor();
        if (!pClaw)
        {
            LOG_ERROR("Race: no Captain in the level");
            return;
        }
        g_Players[0] = pClaw->GetGUID();
        Point spawn = pClaw->GetPositionComponent()->GetPosition();
        SetStartingStats(g_Players[0]);

        if (g_Setup.mode == Mode::SplitScreen)
        {
            StrongActorPtr p2 = CreateFromClaw(pLogic, "Claw2", false);
            if (p2)
            {
                g_Players[1] = p2->GetGUID();
                SetStartingStats(g_Players[1]);
                Point p2Spawn = spawn + Point(-30, 0);
                IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Teleport_Actor(g_Players[1], p2Spawn)));
                shared_ptr<ActorRenderComponent> pRender =
                    MakeStrongPtr(p2->GetComponent<ActorRenderComponent>(ActorRenderComponent::g_Name));
                if (pRender) pRender->SetColorMod(SDL_Color{ 165, 195, 255, 255 });
                if (HumanView* pView = g_pApp->GetHumanView()) pView->EnableSplitScreen(g_Players[1]);
                g_Spawns[g_Players[1]] = p2Spawn;
            }
            else
            {
                LOG_ERROR("Race: could not create player 2");
            }
            StartCountdown();
        }
        else
        {
            StrongActorPtr pGhost = CreateFromClaw(pLogic, "ClawGhost", true);
            if (pGhost)
            {
                g_Ghost = pGhost->GetGUID();
                shared_ptr<ActorRenderComponent> pRender =
                    MakeStrongPtr(pGhost->GetComponent<ActorRenderComponent>(ActorRenderComponent::g_Name));
                if (pRender)
                {
                    pRender->SetAlpha(150);
                    pRender->SetColorMod(SDL_Color{ 180, 220, 255, 255 });
                }
            }
            g_Phase = Phase::WaitingPeer;
            g_LastPeerPacket = SDL_GetTicks();
        }
        g_Spawns[g_Players[0]] = spawn;
        FreezePlayers(true);
    }

    void Update(BaseGameLogic* pLogic, uint32_t msDiff)
    {
        if (!g_Active) return;
        if (g_Setup.mode == Mode::Network) UpdateNetwork(pLogic, msDiff);

        switch (g_Phase)
        {
            case Phase::Countdown:
                // Keep everyone still (respawns or boss scripts may unfreeze).
                FreezePlayers(true);
                g_CountdownMs -= (int)msDiff;
                if (g_CountdownMs <= 0)
                {
                    g_Phase = Phase::Running;
                    g_GoShownMs = 800;
                    FreezePlayers(false);
                    LOG("Race: start");
                }
                break;
            case Phase::Running:
                g_RaceMs += msDiff;
                if (g_GoShownMs > 0) g_GoShownMs -= (int)msDiff;
                break;
            case Phase::Finished:
                g_FinishedForMs += (int)msDiff;
                if (g_FinishedForMs >= 2000)
                {
                    ShowResultsAndLeave();
                }
                break;
            default:
                break;
        }
    }

    uint32_t GetPlayerActor(int index)
    {
        return (index >= 0 && index < 2) ? g_Players[index] : 0;
    }

    uint32_t GetGhostActor()
    {
        return g_Active ? g_Ghost : 0;
    }

    int PlayerIndexOf(uint32_t actorId)
    {
        if (!g_Active || actorId == 0) return -1;
        if (actorId == g_Players[0]) return 0;
        if (actorId == g_Players[1]) return 1;
        return -1;
    }

    void SetSpawn(uint32_t actorId, const Point& position)
    {
        g_Spawns[actorId] = position;
    }

    bool GetSpawn(uint32_t actorId, Point& position)
    {
        auto it = g_Spawns.find(actorId);
        if (it == g_Spawns.end()) return false;
        position = it->second;
        return true;
    }

    bool OnPlayerFinished(uint32_t actorId)
    {
        if (!g_Active) return false;
        int index = PlayerIndexOf(actorId);
        if (index < 0) return true;
        if (g_Phase != Phase::Running) return true;
        g_FinishMs[index] = g_RaceMs;
        Finish(index, "");
        if (g_Setup.mode == Mode::Network)
        {
            std::vector<uint8_t> p;
            RaceNet::PutU32(p, g_RaceMs);
            RaceNet::SendToPeer(RaceNet::Msg_Finish, p);
        }
        return true;
    }

    bool IsOver()
    {
        return g_Phase == Phase::Finished || g_Phase == Phase::Done;
    }

    void RenderOverlay(SDL_Renderer* r, int viewIndex, const SDL_Rect& viewport)
    {
        if (!g_Active || g_Phase == Phase::Idle) return;
        const int w = viewport.w, h = viewport.h;
        const int cx = w / 2;

        // Timer
        if (g_Phase != Phase::WaitingPeer)
        {
            RaceUi::DrawText(r, FormatTime(g_RaceMs), 20, cx, 4, 0, kWhite);
        }

        // Player name and health (split screen: each half has its own)
        if (g_Setup.mode == Mode::SplitScreen)
        {
            uint32_t id = g_Players[viewIndex];
            RaceUi::DrawText(r, PlayerName(viewIndex), 16, 8, 4, -1, kGold);
            StrongActorPtr p = MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(id));
            if (p)
            {
                shared_ptr<HealthComponent> pHealth = MakeStrongPtr(p->GetComponent<HealthComponent>(HealthComponent::g_Name));
                if (pHealth)
                {
                    RaceUi::DrawText(r, std::string(Lang::Tr("Zdrowie ", "Health ")) + std::to_string(std::max(0, (int)pHealth->GetCurrentHealth())),
                        14, w - 8, 4, 1, kWhite);
                }
            }
        }

        // Center messages
        std::string big;
        std::string small;
        switch (g_Phase)
        {
            case Phase::WaitingPeer:
                small = Lang::Tr("Czekam na rywala...", "Waiting for the opponent...");
                break;
            case Phase::Countdown:
                big = std::to_string(std::max(1, (g_CountdownMs + 999) / 1000));
                break;
            case Phase::Running:
                if (g_GoShownMs > 0) big = Lang::Tr("Start!", "Go!");
                break;
            case Phase::Finished:
            case Phase::Done:
                if (g_Winner < 0) small = g_EndReason;
                else if (g_Setup.mode == Mode::Network) big = g_Winner == 0 ? Lang::Tr("Wygrałeś!", "You win!") : Lang::Tr("Przegrałeś!", "You lose!");
                else big = PlayerName(g_Winner) + Lang::Tr(" wygrywa!", " wins!");
                break;
            default:
                break;
        }
        if (!big.empty()) RaceUi::DrawText(r, big, big.size() <= 2 ? 64 : 32, cx, h / 2 - 50, 0, kGold);
        if (!small.empty()) RaceUi::DrawText(r, small, 18, cx, h / 2 - 12, 0, kWhite);
    }
}
