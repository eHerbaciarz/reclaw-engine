#pragma once
// A fixture's bounding box that also works for a deactivated body (e.g. dead
// Claw): Box2D keeps no broad-phase proxies then and b2Fixture::GetAABB(0)
// asserts (it froze the game on the consoles).
#include <Box2D/Box2D.h>

inline b2AABB ReclawFixtureAABB(const b2Fixture* pFixture)
{
    if (pFixture->GetProxyCount() > 0)
    {
        return pFixture->GetAABB(0);
    }
    b2AABB aabb;
    pFixture->GetShape()->ComputeAABB(&aabb, pFixture->GetBody()->GetTransform(), 0);
    return aabb;
}
