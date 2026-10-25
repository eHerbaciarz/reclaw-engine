#pragma once

// Race mode (main menu > Multiplayer): two Captains race through a level,
// the first one to reach its end wins.
//
//   Split screen - both players on one console, the screen split into a left
//                  (player 1, first controller) and right half (player 2,
//                  second controller). Both Captains live in the same world:
//                  treasure, crates and enemies are shared. They cannot hurt
//                  each other.
//   Network      - two consoles (or emulators) on the same Wi-Fi / LAN. Each
//                  runs its own copy of the level; the other player is shown
//                  as a see-through "ghost" Captain.
//
// Lives are unlimited, a dead Captain comes back at his own last checkpoint,
// save states are off and the level is not saved.

#include <cstdint>
#include <string>

#include <SDL2/SDL.h>

#include "../Util/Point.h"

class BaseGameLogic;
class Actor;

namespace Race
{
    enum class Mode
    {
        None,
        SplitScreen,
        Network,
    };

    struct Setup
    {
        Mode mode = Mode::None;
        int level = 1;
        bool host = true;              // network: this console created the race
    };

    // Lobby picked a race: the next level load starts it.
    void Configure(const Setup& setup);
    // Race over or abandoned (back to the menu).
    void End();

    bool IsActive();
    bool IsSplit();
    bool IsNetwork();
    const Setup& GetSetup();

    // Level loaded: creates player 2 / the ghost, starts the countdown.
    void OnLevelLoaded(BaseGameLogic* pLogic);
    // Every game logic step (countdown, network, timers).
    void Update(BaseGameLogic* pLogic, uint32_t msDiff);

    // Captains taking part (actor GUIDs; 0 when absent). Index 0 = player 1.
    uint32_t GetPlayerActor(int index);
    // 0 / 1 for a racing Captain, -1 otherwise.
    int PlayerIndexOf(uint32_t actorId);

    // Respawn point of a Captain (checkpoints are per player).
    void SetSpawn(uint32_t actorId, const Point& position);
    bool GetSpawn(uint32_t actorId, Point& position);

    // A Captain touched the end of the level. Returns true when the race
    // handled it (the level must not finish the normal way).
    bool OnPlayerFinished(uint32_t actorId);

    // Draws the race overlay (timer, countdown, player name, results) for one
    // view. viewIndex: 0 = left / full screen, 1 = right half.
    void RenderOverlay(SDL_Renderer* pRenderer, int viewIndex, const SDL_Rect& viewport);

    // Text for the race results; empty while the race goes on.
    bool IsOver();
}
