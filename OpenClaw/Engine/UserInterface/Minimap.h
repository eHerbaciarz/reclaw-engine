#pragma once

// Level map drawn over the game, see-through like the automap in Diablo
// (toggled with Select / Minus, Tab on the keyboard). Shows the level's
// solid ground, ladders and death zones, the Captain(s), the treasure still
// lying around and the end of the level, centered on the player.
//
// The geometry is collected while the level's collision tiles are created
// (ClawPhysics::VAddStaticGeometry), so it matches what Claw can stand on.

#include <cstdint>

#include <SDL2/SDL.h>

#include "../Util/Point.h"

namespace Minimap
{
    // New level: forget the old geometry.
    void Reset();
    // A static collision rectangle of the level (collisionType: CollisionType).
    void AddGeometry(const Point& position, const Point& size, int collisionType);

    void Toggle();
    void SetVisible(bool visible);
    bool IsVisible();

    // Draws the map into `viewport` (local coordinates), centered on the actor.
    void Render(SDL_Renderer* pRenderer, const SDL_Rect& viewport, uint32_t centerActorId);
}
