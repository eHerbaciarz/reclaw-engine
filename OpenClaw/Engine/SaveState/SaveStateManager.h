#pragma once

// Quick save / quick load / rewind of the running level.
//
// A snapshot holds, for every actor alive at that moment: its GUID, the XML it
// was created from, the runtime state of each component and its physics body.
// Restoring works in place: actors that still exist get their state written
// back, actors that disappeared since (picked-up treasure, killed enemies,
// broken crates) are recreated with their old GUID, and actors that appeared
// since (projectiles, loot, effects) are destroyed.
//
// Requests are queued (from input handling) and carried out at the start of the
// next game logic step, when events and physics are in a consistent state.

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "StateArchive.h"

class BaseGameLogic;

class SaveStateManager
{
public:
    struct ActorRecord
    {
        uint32_t guid = 0;
        uint32_t parentGuid = 0;
        std::shared_ptr<const std::string> xml;
        std::shared_ptr<const std::string> overridesXml;
        size_t offset = 0;   // start of this actor's state in Snapshot::data
    };

    struct Snapshot
    {
        int levelNumber = -1;
        std::string customLevel;
        uint32_t clawGuid = 0;
        bool inBossFight = false;
        uint32_t timeMs = 0;
        std::vector<uint8_t> data;
        std::vector<ActorRecord> actors;
        size_t logicOffset = 0;
    };

    static SaveStateManager* Get();

    // Input side.
    void RequestSave() { m_bSaveRequested = true; }
    void RequestLoad() { m_bLoadRequested = true; }
    void SetRewindHeld(bool held) { m_bRewindHeld = held; }

    // Called by BaseGameLogic at the start of every in-game step.
    // Returns true when the step should be skipped (rewinding).
    bool OnGameStep(BaseGameLogic* pLogic, uint32_t msDiff);

    // Forget everything (new level / back to menu).
    void Reset();

    bool HasQuickSave() const { return m_pQuickSave != nullptr; }

    // Exposed for the archive's actor references.
    static class Actor* FindActor(uint32_t guid);

private:
    bool Capture(BaseGameLogic* pLogic, Snapshot& out);
    bool Restore(BaseGameLogic* pLogic, const Snapshot& snapshot);
    void ApplyStates(BaseGameLogic* pLogic, const Snapshot& snapshot, std::vector<class Actor*>& restored);
    bool SnapshotMatchesLevel(BaseGameLogic* pLogic, const Snapshot& snapshot) const;
    void ShowMessage(const std::string& text);

    bool m_bSaveRequested = false;
    bool m_bLoadRequested = false;
    bool m_bRewindHeld = false;

    std::unique_ptr<Snapshot> m_pQuickSave;

    // Rewind history: one snapshot every kRewindIntervalMs, last kRewindSeconds.
    std::deque<std::unique_ptr<Snapshot>> m_History;
    uint32_t m_SinceLastHistoryMs = 0;
    uint32_t m_RewindStepMs = 0;
};
