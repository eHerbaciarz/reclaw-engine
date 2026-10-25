#include "SaveStateManager.h"

#include <algorithm>
#include <unordered_set>

#include <tinyxml.h>

#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Actor/Actor.h"
#include "../Actor/ActorFactory.h"
#include "../Actor/Components/PositionComponent.h"
#include "../Actor/Components/ControllableComponent.h"
#include "../Events/EventMgr.h"
#include "../Events/Events.h"
#include "../UserInterface/HumanView.h"

namespace
{
    const uint32_t kRewindIntervalMs = 250;   // one history snapshot per 250 ms
    const size_t kRewindSnapshots = 40;       // = 10 seconds
    const uint32_t kRewindStepMs = 100;       // play history back 2.5x faster
}

//=============================================================================
// StateArchive actor references
//=============================================================================

void StateArchive::ActorRef(Actor*& pActor)
{
    uint32_t guid = (!m_bLoading && pActor) ? pActor->GetGUID() : 0;
    (*this)(guid);
    if (m_bLoading)
    {
        Actor** ppActor = &pActor;
        m_Fixups.push_back([ppActor, guid]() { *ppActor = guid ? SaveStateManager::FindActor(guid) : nullptr; });
    }
}

void StateArchive::ActorRef(std::shared_ptr<Actor>& pActor)
{
    uint32_t guid = (!m_bLoading && pActor) ? pActor->GetGUID() : 0;
    (*this)(guid);
    if (m_bLoading)
    {
        std::shared_ptr<Actor>* pp = &pActor;
        m_Fixups.push_back([pp, guid]()
        {
            *pp = guid ? MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(guid)) : nullptr;
        });
    }
}

void StateArchive::ActorRef(std::weak_ptr<Actor>& pActor)
{
    std::shared_ptr<Actor> strong = pActor.lock();
    uint32_t guid = (!m_bLoading && strong) ? strong->GetGUID() : 0;
    (*this)(guid);
    if (m_bLoading)
    {
        std::weak_ptr<Actor>* pp = &pActor;
        m_Fixups.push_back([pp, guid]()
        {
            *pp = guid ? g_pApp->GetGameLogic()->VGetActor(guid) : std::weak_ptr<Actor>();
        });
    }
}

void StateArchive::ActorRefList(std::vector<std::shared_ptr<Actor>>& list)
{
    uint32_t count = (uint32_t)list.size();
    (*this)(count);
    if (m_bLoading)
    {
        if (m_bFailed || count > 100000) { m_bFailed = true; return; }
        list.assign(count, nullptr);
    }
    for (auto& p : list) ActorRef(p);
    if (m_bLoading)
    {
        // Drop references to actors that no longer exist.
        std::vector<std::shared_ptr<Actor>>* pList = &list;
        m_Fixups.push_back([pList]()
        {
            pList->erase(std::remove(pList->begin(), pList->end(), nullptr), pList->end());
        });
    }
}

//=============================================================================
// SaveStateManager
//=============================================================================

SaveStateManager* SaveStateManager::Get()
{
    static SaveStateManager s_Instance;
    return &s_Instance;
}

Actor* SaveStateManager::FindActor(uint32_t guid)
{
    StrongActorPtr p = MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(guid));
    return p.get();
}

void SaveStateManager::Reset()
{
    m_pQuickSave.reset();
    m_History.clear();
    m_bSaveRequested = m_bLoadRequested = false;
    m_SinceLastHistoryMs = 0;
    m_RewindStepMs = 0;
}

bool SaveStateManager::SnapshotMatchesLevel(BaseGameLogic* pLogic, const Snapshot& s) const
{
    shared_ptr<LevelData> pLevel = pLogic->GetCurrentLevelData();
    if (!pLevel) return false;
    if ((int)pLevel->GetLevelNumber() != s.levelNumber) return false;
    if (pLevel->GetCustomWwdPath() != s.customLevel) return false;
    StrongActorPtr pClaw = pLogic->GetClawActor();
    return pClaw && pClaw->GetGUID() == s.clawGuid;
}

void SaveStateManager::ShowMessage(const std::string& text)
{
    LOG(text);
    if (HumanView* pView = g_pApp->GetHumanView())
    {
        pView->ShowToast(text);
    }
}

bool SaveStateManager::OnGameStep(BaseGameLogic* pLogic, uint32_t msDiff)
{
    if (m_bSaveRequested)
    {
        m_bSaveRequested = false;
        std::unique_ptr<Snapshot> pSnapshot(new Snapshot());
        uint32_t start = SDL_GetTicks();
        if (Capture(pLogic, *pSnapshot))
        {
            m_pQuickSave = std::move(pSnapshot);
            LOG("Quick save: " + ToStr((int)m_pQuickSave->actors.size()) + " actors, " +
                ToStr((int)m_pQuickSave->data.size()) + " bytes, " + ToStr((int)(SDL_GetTicks() - start)) + " ms");
            ShowMessage("Zapisano");
        }
    }

    if (m_bLoadRequested)
    {
        m_bLoadRequested = false;
        if (m_pQuickSave && SnapshotMatchesLevel(pLogic, *m_pQuickSave))
        {
            uint32_t start = SDL_GetTicks();
            if (Restore(pLogic, *m_pQuickSave))
            {
                m_History.clear();
                m_SinceLastHistoryMs = 0;
                LOG("Quick load: " + ToStr((int)(SDL_GetTicks() - start)) + " ms");
                ShowMessage("Wczytano");
                return true;
            }
        }
        else
        {
            ShowMessage("Brak zapisu");
        }
    }

    if (m_bRewindHeld)
    {
        m_RewindStepMs += msDiff;
        if (m_RewindStepMs >= kRewindStepMs)
        {
            m_RewindStepMs = 0;
            // Keep the oldest snapshot so holding R longer stays at its start.
            if (!m_History.empty())
            {
                std::unique_ptr<Snapshot> pSnapshot = std::move(m_History.back());
                m_History.pop_back();
                if (SnapshotMatchesLevel(pLogic, *pSnapshot))
                {
                    Restore(pLogic, *pSnapshot);
                }
                if (m_History.empty())
                {
                    m_History.push_back(std::move(pSnapshot));
                }
            }
        }
        m_SinceLastHistoryMs = 0;
        return true;   // the world stands still while rewinding
    }
    m_RewindStepMs = kRewindStepMs;   // first step back happens at once

    m_SinceLastHistoryMs += msDiff;
    if (m_SinceLastHistoryMs >= kRewindIntervalMs)
    {
        m_SinceLastHistoryMs = 0;
        std::unique_ptr<Snapshot> pSnapshot(new Snapshot());
        if (Capture(pLogic, *pSnapshot))
        {
            m_History.push_back(std::move(pSnapshot));
            while (m_History.size() > kRewindSnapshots) m_History.pop_front();
        }
    }
    return false;
}

static bool IsClawInBossFight(const StrongActorPtr& pClaw)
{
    if (!pClaw) return false;
    shared_ptr<ClawControllableComponent> pControl =
        MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name));
    return pControl && pControl->IsInBossFight();
}

bool SaveStateManager::Capture(BaseGameLogic* pLogic, Snapshot& out)
{
    shared_ptr<LevelData> pLevel = pLogic->m_pCurrentLevel;
    StrongActorPtr pClaw = pLogic->GetClawActor();
    if (!pLevel || !pLogic->m_pPhysics || !pClaw) return false;

    pLogic->m_pPhysics->VFlushPendingChanges();

    out.levelNumber = (int)pLevel->GetLevelNumber();
    out.customLevel = pLevel->GetCustomWwdPath();
    out.clawGuid = pClaw->GetGUID();
    out.inBossFight = IsClawInBossFight(pClaw);
    out.timeMs = SDL_GetTicks();
    out.data.reserve(256 * 1024);
    out.actors.reserve(pLogic->m_ActorMap.size());

    StateArchive ar(&out.data, false);

    // Level state kept by the game logic.
    out.logicOffset = out.data.size();
    ar(pLogic->m_CurrentSpawnPosition);
    ar(pLevel->m_LootedPickupsMap);
    ar(pLevel->m_LoadedCheckpoint);

    for (auto& kv : pLogic->m_ActorMap)
    {
        Actor* pActor = kv.second.get();
        if (!pActor || !pActor->GetCreationXml()) continue;
        ActorRecord record;
        record.guid = kv.first;
        record.parentGuid = pActor->GetParentGuid();
        record.xml = pActor->GetCreationXml();
        record.overridesXml = pActor->GetCreationOverridesXml();
        record.offset = out.data.size();
        pActor->SerializeState(ar);
        pLogic->m_pPhysics->VSerializeBodyState(kv.first, ar);
        out.actors.push_back(record);
    }
    return true;
}

static TiXmlElement* ParseXml(TiXmlDocument& doc, const std::shared_ptr<const std::string>& xml)
{
    if (!xml || xml->empty()) return nullptr;
    doc.Parse(xml->c_str());
    return doc.Error() ? nullptr : doc.RootElement();
}

bool SaveStateManager::Restore(BaseGameLogic* pLogic, const Snapshot& snapshot)
{
    shared_ptr<LevelData> pLevel = pLogic->m_pCurrentLevel;
    shared_ptr<IGamePhysics> pPhysics = pLogic->m_pPhysics;
    if (!pLevel || !pPhysics) return false;
    IEventMgr* pEventMgr = IEventMgr::Get();

    // A boss fight that started after the snapshot is called off first, the
    // same way it is when Claw dies (boss music stops, boss bar hides).
    bool wasInBossFight = IsClawInBossFight(pLogic->GetClawActor());
    if (wasInBossFight && !snapshot.inBossFight)
    {
        pEventMgr->VTriggerEvent(IEventDataPtr(new EventData_Boss_Fight_Ended(false)));
    }

    // Nothing queued from the abandoned timeline may reach the restored one.
    pEventMgr->VAbortAllEvents();
    if (HumanView* pView = g_pApp->GetHumanView())
    {
        pView->CancelScreenEffects();
    }

    std::unordered_set<uint32_t> wanted;
    wanted.reserve(snapshot.actors.size() * 2);
    for (const ActorRecord& r : snapshot.actors) wanted.insert(r.guid);

    // 1. Actors created after the snapshot (projectiles, loot, effects) go away.
    std::vector<uint32_t> toDestroy;
    for (auto& kv : pLogic->m_ActorMap)
    {
        // Actors the snapshot could not have recorded are left alone.
        if (!kv.second || !kv.second->GetCreationXml()) continue;
        if (!wanted.count(kv.first)) toDestroy.push_back(kv.first);
    }
    for (uint32_t guid : toDestroy)
    {
        pEventMgr->VTriggerEvent(IEventDataPtr(new EventData_Destroy_Actor(guid)));
    }
    // Bodies queued for removal must be gone before bodies are created again
    // (an actor removed this step may be one that is recreated below).
    pPhysics->VFlushPendingChanges();

    // 2. Actors destroyed since the snapshot come back with their old GUID.
    //    Child actors (spawned by another actor while it was being created)
    //    are recreated by their parent, so they are not recreated on their own.
    std::vector<uint32_t> recreated;
    for (const ActorRecord& r : snapshot.actors)
    {
        if (pLogic->m_ActorMap.count(r.guid)) continue;
        if (r.parentGuid != 0) continue;
        TiXmlDocument doc, overridesDoc;
        TiXmlElement* pRoot = ParseXml(doc, r.xml);
        TiXmlElement* pOverrides = ParseXml(overridesDoc, r.overridesXml);
        if (!pRoot) continue;
        StrongActorPtr pActor = pLogic->m_pActorFactory->CreateActorWithGuid(pRoot, pOverrides, r.guid);
        if (!pActor)
        {
            LOG_WARNING("Save state: could not recreate actor " + ToStr((int)r.guid));
            continue;
        }
        pLogic->m_ActorMap.insert(std::make_pair(pActor->GetGUID(), pActor));
        pEventMgr->VTriggerEvent(IEventDataPtr(new EventData_New_Actor(pActor->GetGUID())));
        recreated.push_back(r.guid);
    }
    for (uint32_t guid : recreated)
    {
        if (StrongActorPtr p = MakeStrongPtr(pLogic->VGetActor(guid)))
        {
            p->OnWorldFinishedLoading();
        }
    }

    // Bodies of recreated actors must exist before their state is applied.
    pPhysics->VFlushPendingChanges();

    // 3. Write the stored state back.
    std::vector<Actor*> restored;
    ApplyStates(pLogic, snapshot, restored);

    // A boss fight that was on at the snapshot starts again (music, Claw and
    // boss flags); the stored state is then written over what the start reset.
    if (snapshot.inBossFight && !wasInBossFight)
    {
        pEventMgr->VTriggerEvent(IEventDataPtr(new EventData_Boss_Fight_Started(snapshot.clawGuid, INVALID_ACTOR_ID)));
        ApplyStates(pLogic, snapshot, restored);
    }

    for (Actor* pActor : restored)
    {
        pActor->OnStateRestored();
        if (shared_ptr<PositionComponent> pPos = pActor->GetPositionComponent())
        {
            pEventMgr->VTriggerEvent(IEventDataPtr(
                new EventData_Move_Actor(pActor->GetGUID(), pPos->GetPosition(), true)));
        }
    }

    pEventMgr->VUpdate(IEventMgr::kINFINITE);
    return true;
}

void SaveStateManager::ApplyStates(BaseGameLogic* pLogic, const Snapshot& snapshot, std::vector<Actor*>& restored)
{
    shared_ptr<LevelData> pLevel = pLogic->m_pCurrentLevel;
    shared_ptr<IGamePhysics> pPhysics = pLogic->m_pPhysics;

    std::vector<uint8_t>& data = const_cast<std::vector<uint8_t>&>(snapshot.data);
    StateArchive ar(&data, true);
    ar.Seek(snapshot.logicOffset);
    ar(pLogic->m_CurrentSpawnPosition);
    ar(pLevel->m_LootedPickupsMap);
    ar(pLevel->m_LoadedCheckpoint);

    restored.clear();
    restored.reserve(snapshot.actors.size());
    for (const ActorRecord& r : snapshot.actors)
    {
        StrongActorPtr pActor = MakeStrongPtr(pLogic->VGetActor(r.guid));
        if (!pActor) continue;
        ar.Seek(r.offset);
        pActor->SerializeState(ar);
        pPhysics->VSerializeBodyState(r.guid, ar);
        if (ar.Failed())
        {
            LOG_WARNING("Save state: data of actor " + pActor->GetName() + " is damaged");
            ar.ClearFailure();
        }
        restored.push_back(pActor.get());
    }

    // References between actors (some were recreated above).
    for (auto& fixup : ar.Fixups()) fixup();
}
