#pragma once

// Boss modes (main menu > Single Player > "Skrzyżuj szpady ponownie" /
// "Cross Swords Again"):
//
//   Practice  - fight any boss already reached, as many times as you like.
//               Lives never run out; every win shows the time and the record.
//   Boss Rush - "Paruj i kontruj" / "Parry & Thrust", unlocked after finishing
//               the game: every boss in a row with one health bar (+20 health
//               between fights). A death ends the run. The stopwatch only runs
//               while a fight is on (from the moment the Captain gets control
//               until the boss falls), so loading, the boss intro and the pause
//               menu are not counted. At the end: the time of every boss and
//               the total.
//
// Each boss level is loaded normally and the Captain is put at the boss
// stager (the spot where the boss intro starts). Nothing is saved to the
// game save; records live in port_settings.txt.

#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "../Util/Point.h"

class BaseGameLogic;
struct CheckpointSave;

namespace BossModes
{
    enum class Mode
    {
        None,
        Practice,
        Rush,
    };

    struct BossInfo
    {
        int level;
        const char* name;
        bool available;     // the boss fight exists in the engine
    };

    // The eight bosses of the game, in order.
    const std::vector<BossInfo>& Bosses();

    // The player got to this boss in the normal game (or beat its level).
    bool IsBossReached(int level);
    void MarkBossReached(int level);
    bool IsGameCompleted();
    void MarkGameCompleted();
    // Cheats screen: unlocks every boss and the Boss Rush.
    void UnlockAll();

    // Main menu: the boss list. Returns the level to load (> 0) or 0 (back).
    int RunMenu(SDL_Renderer* pRenderer);

    bool IsActive();
    Mode GetMode();

    // Level loading: stats to start with (replaces the save game) and the
    // Captain put next to the boss.
    void FillStartStats(CheckpointSave& stats);
    void OnLevelLoaded(BaseGameLogic* pLogic, uint32_t clawId, Point& spawnPosition);
    // Every game logic step while playing (not called while paused).
    void Update(BaseGameLogic* pLogic, uint32_t msDiff);
    // Every step while the main menu is shown: starts the next boss level,
    // reopens the boss list or ends the mode.
    void OnMenu();
    // The Captain picked up the boss's gem: no score screen in boss modes.
    // Returns true when handled.
    bool OnLevelEndPickup();

    void RenderOverlay(SDL_Renderer* pRenderer, const SDL_Rect& viewport);

    void End();
}
