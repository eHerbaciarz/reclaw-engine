#include "BossModes.h"

#include <algorithm>
#include <cstdio>

#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../GameApp/GameSaves.h"
#include "../Actor/Actor.h"
#include "../Actor/Components/PositionComponent.h"
#include "../Actor/Components/ControllableComponent.h"
#include "../Actor/Components/ControllerComponents/HealthComponent.h"
#include "../Actor/Components/ControllerComponents/AmmoComponent.h"
#include "../Actor/Components/ControllerComponents/LifeComponent.h"
#include "../Actor/Components/TriggerComponents/BossStagerTriggerComponent.h"
#include "../Events/EventMgr.h"
#include "../Events/Events.h"
#include "../Platform/Platform.h"
#include "../Race/RaceUi.h"
#include "../UserInterface/Lang.h"
#include "../Util/PortSettings.h"

namespace
{
    enum class Phase
    {
        Idle,
        Approach,       // level loaded, boss intro not over yet
        Fighting,       // the stopwatch runs
        Won,            // boss down, short wait (death animation, gem)
        Lost,           // Captain died (Boss Rush) or practice attempt failed
        Leaving,        // going back to the menu / the next boss
    };

    const uint32_t kNoTime = 0xffffffff;
    const int kAfterWinMs = 3000;
    const int kAfterLossMs = 2500;
    const int kPracticeRetryShownMs = 2000;
    const int kRushRegen = 20;
    const SDL_Color kWhite = { 255, 255, 255, 255 };
    const SDL_Color kGold = { 246, 205, 86, 255 };

    const std::vector<BossModes::BossInfo> g_Bosses = {
        { 2, "Le Rauxe", true },
        { 4, "Katherine", true },
        { 6, "Wolvington", true },
        { 8, "Gabriel", true },
        { 10, "Marrow", true },
        { 12, "Aquatis", true },
        { 13, "Red Tail", true },
        { 14, "Omar", false },
    };

    BossModes::Mode g_Mode = BossModes::Mode::None;
    Phase g_Phase = Phase::Idle;
    int g_Level = 0;                    // boss level being played
    int g_PendingLevel = 0;             // load this level once in the menu
    bool g_PendingList = false;         // reopen the boss list once in the menu
    uint32_t g_FightMs = 0;
    int g_PhaseMs = 0;
    int g_Attempts = 0;                 // practice
    int g_RetryShownMs = 0;             // practice: "try again" message
    uint32_t g_LastPracticeMs = kNoTime;

    // Boss Rush
    std::vector<int> g_RushLevels;
    size_t g_RushIndex = 0;
    std::vector<uint32_t> g_RushTimes;
    CheckpointSave g_Carry;
    bool g_HaveCarry = false;

    const BossModes::BossInfo* FindBoss(int level)
    {
        for (const auto& b : g_Bosses) if (b.level == level) return &b;
        return nullptr;
    }

    std::string BossName(int level)
    {
        const BossModes::BossInfo* b = FindBoss(level);
        return b ? b->name : "?";
    }

    std::string LevelText(int level)
    {
        return std::string(Lang::Tr("poziom ", "level ")) + std::to_string(level);
    }

    // In words: the list is drawn in the game's menu letters (no digits)
    std::string LevelWords(int level)
    {
        static const char* pl[] = { "", "pierwszy", "drugi", "trzeci", "czwarty", "piąty", "szósty", "siódmy",
                                    "ósmy", "dziewiąty", "dziesiąty", "jedenasty", "dwunasty", "trzynasty", "czternasty" };
        static const char* en[] = { "", "one", "two", "three", "four", "five", "six", "seven",
                                    "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen" };
        if (level < 1 || level > 14) return LevelText(level);
        return Lang::Code() == "pl" ? std::string("poziom ") + pl[level] : std::string("level ") + en[level];
    }

    std::string FormatTime(uint32_t ms)
    {
        if (ms == kNoTime) return "-";
        char buf[32];
        snprintf(buf, sizeof(buf), "%u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
        return buf;
    }

    uint32_t GetRecord(const std::string& key)
    {
        int v = PortSettings::GetInt(key, -1);
        return v < 0 ? kNoTime : (uint32_t)v;
    }

    // Returns true for a new record.
    bool SaveRecord(const std::string& key, uint32_t ms)
    {
        uint32_t old = GetRecord(key);
        if (old != kNoTime && old <= ms) return false;
        PortSettings::SetInt(key, (int)ms);
        return true;
    }

    std::string BossRecordKey(int level) { return "boss_best_" + std::to_string(level); }
    std::string RushRecordKey() { return "rush_best_" + std::to_string(g_RushLevels.size()); }

    template <class T>
    T* ClawComponent()
    {
        if (!g_pApp || !g_pApp->GetGameLogic()) return nullptr;
        StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
        return pClaw ? pClaw->GetRawComponent<T>() : nullptr;
    }

    void FreezeClaw(bool frozen)
    {
        if (auto p = ClawComponent<ClawControllableComponent>()) p->SetFrozen(frozen);
    }

    void BackToMenu()
    {
        g_Phase = Phase::Leaving;
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Enter_Menu()));
    }

    void RememberClawStats()
    {
        g_Carry = CheckpointSave();
        g_Carry.health = 100;
        g_Carry.lives = 9;
        if (auto p = ClawComponent<HealthComponent>()) g_Carry.health = std::max<int>(1, (int)p->GetCurrentHealth());
        if (auto p = ClawComponent<AmmoComponent>())
        {
            g_Carry.bulletCount = p->GetRemainingAmmo(AmmoType_Pistol);
            g_Carry.magicCount = p->GetRemainingAmmo(AmmoType_Magic);
            g_Carry.dynamiteCount = p->GetRemainingAmmo(AmmoType_Dynamite);
        }
        // A little help before the next boss.
        g_Carry.health = std::min<int>(100, (int)g_Carry.health + kRushRegen);
        g_Carry.bulletCount += 5;
        g_Carry.magicCount += 2;
        g_Carry.dynamiteCount += 2;
        g_HaveCarry = true;
    }

    // ------------------------------------------------------------ screens
    void PracticeResult()
    {
        uint32_t time = g_LastPracticeMs;
        bool record = SaveRecord(BossRecordKey(g_Level), time);
        RaceUi::ListScreen s;
        s.title = BossName(g_Level) + Lang::Tr(" pokonany!", " defeated!");
        s.items = {
            Lang::Tr("Walcz ponownie", "Fight again"),
            Lang::Tr("Wybierz innego bossa", "Choose another boss"),
            Lang::Tr("Menu główne", "Main menu"),
        };
        s.footer = {
            std::string(Lang::Tr("Czas: ", "Time: ")) + FormatTime(time) +
                (record ? Lang::Tr("  - nowy rekord!", "  - new record!") : ""),
            std::string(Lang::Tr("Rekord: ", "Record: ")) + FormatTime(GetRecord(BossRecordKey(g_Level))),
            std::string(Lang::Tr("Próby: ", "Attempts: ")) + std::to_string(g_Attempts),
        };
        s.backCancels = true;
        int choice = RaceUi::Run(g_pApp->GetRenderer(), s);
        if (choice == 0) g_PendingLevel = g_Level;
        else if (choice == 1 || choice < 0) g_PendingList = true;
        BackToMenu();
    }

    void RushSummary(bool finished)
    {
        std::vector<std::string> lines;
        uint32_t total = 0;
        for (size_t i = 0; i < g_RushLevels.size(); ++i)
        {
            int level = g_RushLevels[i];
            std::string time;
            if (i < g_RushTimes.size())
            {
                time = FormatTime(g_RushTimes[i]);
                total += g_RushTimes[i];
            }
            else if (i == g_RushTimes.size() && !finished)
            {
                time = Lang::Tr("porażka", "defeat");
            }
            else
            {
                time = "-";
            }
            lines.push_back(BossName(level) + " (" + LevelText(level) + "): " + time);
        }
        lines.push_back("");
        lines.push_back(std::string(Lang::Tr("Razem: ", "Total: ")) + FormatTime(total));
        if (finished)
        {
            bool record = SaveRecord(RushRecordKey(), total);
            lines.push_back(std::string(Lang::Tr("Rekord: ", "Record: ")) + FormatTime(GetRecord(RushRecordKey())) +
                (record ? Lang::Tr("  - nowy rekord!", "  - new record!") : ""));
        }
        else
        {
            lines.push_back(std::string(Lang::Tr("Pokonani bossowie: ", "Bosses defeated: ")) +
                std::to_string(g_RushTimes.size()) + " / " + std::to_string(g_RushLevels.size()));
        }
        RaceUi::ShowMessage(g_pApp->GetRenderer(),
            finished ? Lang::Tr("Paruj i kontruj - ukończone!", "Parry & Thrust - complete!")
                     : Lang::Tr("Paruj i kontruj - koniec", "Parry & Thrust - over"),
            lines);
        BackToMenu();
    }

    // ------------------------------------------------------------ events
    class Listener
    {
    public:
        void FightStarted(IEventDataPtr)
        {
            if (g_Phase != Phase::Approach) return;
            g_Phase = Phase::Fighting;
            g_FightMs = 0;
            g_RetryShownMs = 0;
            ++g_Attempts;
            LOG_WARNING("Boss modes: fight started (level " + std::to_string(g_Level) + ")");
        }

        void FightEnded(IEventDataPtr pEvent)
        {
            if (g_Phase != Phase::Fighting) return;
            auto pCast = static_pointer_cast<EventData_Boss_Fight_Ended>(pEvent);
            if (pCast && pCast->GetIsBossDead())
            {
                g_Phase = Phase::Won;
                g_PhaseMs = 0;
                if (g_Mode == BossModes::Mode::Practice) g_LastPracticeMs = g_FightMs;
                else g_RushTimes.push_back(g_FightMs);
                LOG_WARNING("Boss modes: boss defeated in " + FormatTime(g_FightMs));
            }
            else if (g_Mode == BossModes::Mode::Practice)
            {
                // The Captain comes back at the boss and the fight restarts.
                g_Phase = Phase::Approach;
                g_RetryShownMs = kPracticeRetryShownMs;
            }
        }

        void ClawDied(IEventDataPtr)
        {
            if (g_Mode != BossModes::Mode::Rush) return;
            if (g_Phase == Phase::Lost || g_Phase == Phase::Leaving || g_Phase == Phase::Won) return;
            g_Phase = Phase::Lost;
            g_PhaseMs = 0;
            LOG_WARNING("Boss modes: Boss Rush lost");
        }
    };

    Listener g_Listener;
    bool g_Listening = false;

    void Listen(bool on)
    {
        if (on == g_Listening) return;
        IEventMgr* pMgr = IEventMgr::Get();
        if (on)
        {
            pMgr->VAddListener(MakeDelegate(&g_Listener, &Listener::FightStarted), EventData_Boss_Fight_Started::sk_EventType);
            pMgr->VAddListener(MakeDelegate(&g_Listener, &Listener::FightEnded), EventData_Boss_Fight_Ended::sk_EventType);
            pMgr->VAddListener(MakeDelegate(&g_Listener, &Listener::ClawDied), EventData_Claw_Died::sk_EventType);
        }
        else
        {
            pMgr->VRemoveListener(MakeDelegate(&g_Listener, &Listener::FightStarted), EventData_Boss_Fight_Started::sk_EventType);
            pMgr->VRemoveListener(MakeDelegate(&g_Listener, &Listener::FightEnded), EventData_Boss_Fight_Ended::sk_EventType);
            pMgr->VRemoveListener(MakeDelegate(&g_Listener, &Listener::ClawDied), EventData_Claw_Died::sk_EventType);
        }
        g_Listening = on;
    }

    void Begin(BossModes::Mode mode)
    {
        BossModes::End();
        g_Mode = mode;
        Listen(true);
    }
}

namespace BossModes
{
    const std::vector<BossInfo>& Bosses() { return g_Bosses; }

    bool IsBossReached(int level)
    {
        if (PortSettings::GetBool("bosses_unlocked", false)) return true;
        if (PortSettings::GetBool("boss_reached_" + std::to_string(level), false)) return true;
        // Older saves: the next level was reached, so this boss was beaten.
        if (level < 14 && g_pApp && g_pApp->GetGameLogic())
        {
            shared_ptr<GameSaveMgr> pSaves = g_pApp->GetGameLogic()->GetGameSaveMgr();
            if (pSaves && pSaves->HasLevelSave(level + 1)) return true;
        }
        return false;
    }

    void MarkBossReached(int level)
    {
        if (!FindBoss(level)) return;
        std::string key = "boss_reached_" + std::to_string(level);
        if (!PortSettings::GetBool(key, false)) PortSettings::SetBool(key, true);
    }

    bool IsGameCompleted()
    {
        return PortSettings::GetBool("game_completed", false) || PortSettings::GetBool("bosses_unlocked", false);
    }

    void MarkGameCompleted()
    {
        if (!PortSettings::GetBool("game_completed", false)) PortSettings::SetBool("game_completed", true);
    }

    void UnlockAll()
    {
        PortSettings::SetBool("bosses_unlocked", true);
    }

    int RunMenu(SDL_Renderer* pRenderer)
    {
        if (!pRenderer) return 0;
        std::vector<int> levels;      // per list row; -1 = Boss Rush
        std::vector<std::string> records;   // per row: the best time, if any
        RaceUi::ListScreen s;
        s.title = Lang::Tr("Skrzyżuj szpady ponownie", "Cross Swords Again");
        for (const auto& b : g_Bosses)
        {
            if (!IsBossReached(b.level)) continue;
            std::string item = std::string(b.name) + " - " + LevelWords(b.level);
            if (!b.available) item += Lang::Tr(" - niedostępny", " - not ready");
            // (the record under the list, for the selected boss: digits are
            // not among the menu's letters)
            uint32_t best = b.available ? GetRecord(BossRecordKey(b.level)) : kNoTime;
            records.push_back(best == kNoTime ? std::string() :
                              std::string(Lang::Tr("Rekord: ", "Record: ")) + FormatTime(best));
            s.items.push_back(item);
            levels.push_back(b.level);
        }
        bool rushOpen = IsGameCompleted();
        std::string rush = Lang::Tr("Paruj i kontruj", "Parry and thrust");
        if (!rushOpen) rush += Lang::Tr(" - ukończ grę", " - finish the game");
        s.items.push_back(rush);
        levels.push_back(-1);
        {
            int rushCount = 0;
            for (const auto& b : g_Bosses) if (b.available) ++rushCount;
            uint32_t best = rushOpen ? GetRecord("rush_best_" + std::to_string(rushCount)) : kNoTime;
            records.push_back(best == kNoTime ? std::string() :
                              std::string(Lang::Tr("Rekord: ", "Record: ")) + FormatTime(best));
        }

        if (levels.size() == 1)
        {
            s.footer.push_back(Lang::Tr("Nie dotarłeś jeszcze do żadnego bossa.", "You have not reached any boss yet."));
        }
        const size_t recordLine = s.footer.size();
        s.footer.push_back("");
        s.footer.push_back(std::string(Platform::BackButtonName()) + Lang::Tr(" - wróć", " - back"));
        s.tick = [records, recordLine](RaceUi::ListScreen& screen) -> int
        {
            if (recordLine < screen.footer.size() && screen.selected >= 0 && screen.selected < (int)records.size())
                screen.footer[recordLine] = records[screen.selected];
            return -2;
        };

        while (true)
        {
            int choice = RaceUi::Run(pRenderer, s);
            if (choice < 0 || choice >= (int)levels.size()) return 0;
            s.selected = choice;
            int level = levels[choice];
            if (level > 0)
            {
                const BossInfo* b = FindBoss(level);
                if (!b || !b->available)
                {
                    RaceUi::ShowMessage(pRenderer, b ? b->name : "",
                        { Lang::Tr("Walka z tym bossem nie jest jeszcze gotowa.", "This boss fight is not ready yet.") });
                    continue;
                }
                Begin(Mode::Practice);
                g_Level = level;
                g_Attempts = 0;
                LOG_WARNING("Boss practice: " + BossName(level));
                return level;
            }
            if (!rushOpen)
            {
                RaceUi::ShowMessage(pRenderer, Lang::Tr("Paruj i kontruj", "Parry & Thrust"),
                    { Lang::Tr("Ukończ całą grę, by odblokować walkę", "Finish the whole game to unlock fighting"),
                      Lang::Tr("ze wszystkimi bossami pod rząd.", "all the bosses in a row.") });
                continue;
            }
            Begin(Mode::Rush);
            for (const auto& b : g_Bosses) if (b.available) g_RushLevels.push_back(b.level);
            if (g_RushLevels.empty()) { End(); return 0; }
            g_RushIndex = 0;
            g_Level = g_RushLevels[0];
            LOG_WARNING("Boss Rush: " + std::to_string(g_RushLevels.size()) + " bosses");
            return g_Level;
        }
    }

    bool IsActive() { return g_Mode != Mode::None; }
    Mode GetMode() { return g_Mode; }

    void FillStartStats(CheckpointSave& stats)
    {
        stats.checkpointIdx = 0;
        stats.score = 0;
        stats.lives = 9;
        if (g_Mode == Mode::Rush && g_HaveCarry)
        {
            stats.health = g_Carry.health;
            stats.bulletCount = g_Carry.bulletCount;
            stats.magicCount = g_Carry.magicCount;
            stats.dynamiteCount = g_Carry.dynamiteCount;
        }
        else if (g_Mode == Mode::Rush)
        {
            stats.health = 100;
            stats.bulletCount = 30;
            stats.magicCount = 15;
            stats.dynamiteCount = 10;
        }
        else
        {
            stats.health = 100;
            stats.bulletCount = 50;
            stats.magicCount = 25;
            stats.dynamiteCount = 25;
        }
    }

    void OnLevelLoaded(BaseGameLogic* pLogic, uint32_t clawId, Point& spawnPosition)
    {
        if (!IsActive() || !pLogic) return;
        g_Phase = Phase::Approach;
        g_FightMs = 0;
        g_PhaseMs = 0;
        g_RetryShownMs = 0;

        for (const auto& kv : pLogic->GetActorMap())
        {
            Actor* pActor = kv.second.get();
            if (pActor && pActor->GetRawComponent<BossStagerTriggerComponent>())
            {
                // The stager is a narrow trigger where the Captain walks in;
                // standing on it starts the boss intro straight away.
                // A little above it: the Captain drops onto the floor instead
                // of starting half inside it (the stager's point is near the
                // floor line).
                spawnPosition = pActor->GetPositionComponent()->GetPosition() + Point(0, -48);
                IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Teleport_Actor(clawId, spawnPosition)));
                LOG_WARNING("Boss modes: Captain placed at the boss (" + std::to_string((int)spawnPosition.x) + ", " +
                    std::to_string((int)spawnPosition.y) + ")");
                return;
            }
        }
        LOG_ERROR("Boss modes: no boss stager in level " + std::to_string(g_Level) + ", starting at the level start");
    }

    void Update(BaseGameLogic* pLogic, uint32_t msDiff)
    {
        if (!IsActive()) return;

        // Lives never run out (a Boss Rush ends on the first death anyway).
        if (auto p = ClawComponent<LifeComponent>())
        {
            if (p->GetLives() < 5) p->AddLives(9 - p->GetLives());
        }
        if (g_RetryShownMs > 0) g_RetryShownMs -= (int)msDiff;

        switch (g_Phase)
        {
            case Phase::Fighting:
                g_FightMs += msDiff;
                break;
            case Phase::Won:
                g_PhaseMs += (int)msDiff;
                if (g_PhaseMs < kAfterWinMs) break;
                if (g_Mode == Mode::Practice)
                {
                    PracticeResult();
                }
                else if (g_RushIndex + 1 < g_RushLevels.size())
                {
                    RememberClawStats();
                    ++g_RushIndex;
                    g_PendingLevel = g_RushLevels[g_RushIndex];
                    BackToMenu();
                }
                else
                {
                    RushSummary(true);
                }
                break;
            case Phase::Lost:
                FreezeClaw(true);
                g_PhaseMs += (int)msDiff;
                if (g_PhaseMs >= kAfterLossMs) RushSummary(false);
                break;
            default:
                break;
        }
    }

    void OnMenu()
    {
        if (!IsActive()) return;
        if (g_PendingLevel > 0)
        {
            g_Level = g_PendingLevel;
            g_PendingLevel = 0;
            g_Phase = Phase::Idle;
            IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Menu_Boss_Modes(g_Level)));
        }
        else if (g_PendingList)
        {
            g_PendingList = false;
            End();
            IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Menu_Boss_Modes(0)));
        }
        else if (g_Phase != Phase::Idle)
        {
            // Left through the pause menu or the mode is over.
            End();
        }
    }

    bool OnLevelEndPickup()
    {
        if (!IsActive()) return false;
        if (g_Phase == Phase::Won) g_PhaseMs = kAfterWinMs;
        return true;
    }

    void RenderOverlay(SDL_Renderer* r, const SDL_Rect& viewport)
    {
        if (!IsActive() || g_Phase == Phase::Idle) return;
        const int cx = viewport.w / 2;
        const int h = viewport.h;

        uint32_t shown = g_FightMs;
        if (g_Mode == Mode::Rush)
        {
            for (uint32_t t : g_RushTimes) shown += t;
            if (g_Phase == Phase::Won || g_Phase == Phase::Leaving) shown -= g_FightMs;   // already in the list
        }
        RaceUi::DrawText(r, FormatTime(shown), 20, cx, 4, 0, kWhite);

        std::string label;
        if (g_Mode == Mode::Practice)
        {
            label = std::string(Lang::Tr("Trening: ", "Practice: ")) + BossName(g_Level);
            if (g_Attempts > 0) label += std::string(Lang::Tr(" - próba ", " - attempt ")) + std::to_string(g_Attempts);
        }
        else
        {
            label = std::string(Lang::Tr("Paruj i kontruj ", "Parry & Thrust ")) + std::to_string(g_RushIndex + 1) +
                "/" + std::to_string(g_RushLevels.size()) + ": " + BossName(g_Level);
        }
        RaceUi::DrawText(r, label, 14, cx, 28, 0, kGold);

        std::string big, small;
        if (g_Phase == Phase::Won)
        {
            big = Lang::Tr("Pokonany!", "Defeated!");
            small = FormatTime(g_Mode == Mode::Practice ? g_LastPracticeMs : g_RushTimes.back());
        }
        else if (g_Phase == Phase::Lost)
        {
            big = Lang::Tr("Porażka", "Defeat");
        }
        else if (g_RetryShownMs > 0)
        {
            small = Lang::Tr("Spróbuj jeszcze raz!", "Try again!");
        }
        if (!big.empty()) RaceUi::DrawText(r, big, 32, cx, h / 2 - 50, 0, kGold);
        if (!small.empty()) RaceUi::DrawText(r, small, 18, cx, h / 2 - 12, 0, kWhite);
    }

    void End()
    {
        Listen(false);
        g_Mode = Mode::None;
        g_Phase = Phase::Idle;
        g_Level = 0;
        g_PendingLevel = 0;
        g_FightMs = 0;
        g_PhaseMs = 0;
        g_Attempts = 0;
        g_RetryShownMs = 0;
        g_LastPracticeMs = kNoTime;
        g_RushLevels.clear();
        g_RushIndex = 0;
        g_RushTimes.clear();
        g_HaveCarry = false;
    }
}
