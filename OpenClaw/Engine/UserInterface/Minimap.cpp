#include "Minimap.h"

#include <algorithm>
#include <vector>

#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Actor/Actor.h"
#include "../Actor/Components/PositionComponent.h"
#include "../Actor/Components/PickupComponents/PickupComponent.h"
#include "../Race/Race.h"

namespace
{
    // One map pixel = this many level pixels.
    const double kScale = 8.0;

    struct MapRect
    {
        float x, y, w, h;
        uint8_t type;
    };

    // Sorted by x when the map is first drawn, so drawing can skip to the
    // part of the level that is on screen.
    std::vector<MapRect> g_Rects;
    bool g_Sorted = false;
    float g_MaxWidth = 0.0f;
    bool g_Visible = false;

    // Treasure and level-end positions, refreshed a few times per second.
    std::vector<Point> g_Treasure;
    std::vector<Point> g_Exits;
    uint32_t g_LastScan = 0;

    void Scan()
    {
        g_Treasure.clear();
        g_Exits.clear();
        BaseGameLogic* pLogic = g_pApp->GetGameLogic();
        if (!pLogic) return;
        for (const auto& kv : pLogic->GetActorMap())
        {
            const StrongActorPtr& pActor = kv.second;
            if (!pActor || !pActor->GetPositionComponent()) continue;
            if (shared_ptr<TreasurePickupComponent> pTreasure =
                    MakeStrongPtr(pActor->GetComponent<TreasurePickupComponent>(TreasurePickupComponent::g_Name)))
            {
                if (!pTreasure->IsPickedUp()) g_Treasure.push_back(pActor->GetPositionComponent()->GetPosition());
            }
            else if (MakeStrongPtr(pActor->GetComponent<EndLevelPickupComponent>(EndLevelPickupComponent::g_Name)))
            {
                g_Exits.push_back(pActor->GetPositionComponent()->GetPosition());
            }
        }
    }

    void FillDot(SDL_Renderer* r, double x, double y, int size)
    {
        SDL_Rect d = { (int)x - size / 2, (int)y - size / 2, size, size };
        SDL_RenderFillRect(r, &d);
    }
}

namespace Minimap
{
    void Reset()
    {
        g_Rects.clear();
        g_Sorted = false;
        g_MaxWidth = 0.0f;
        g_Treasure.clear();
        g_Exits.clear();
        g_LastScan = 0;
    }

    void AddGeometry(const Point& position, const Point& size, int collisionType)
    {
        if (collisionType == CollisionType_None) return;
        g_Rects.push_back(MapRect{ (float)position.x, (float)position.y, (float)size.x, (float)size.y, (uint8_t)collisionType });
        g_MaxWidth = std::max(g_MaxWidth, (float)size.x);
        g_Sorted = false;
    }

    void Toggle() { g_Visible = !g_Visible; }
    void SetVisible(bool visible) { g_Visible = visible; }
    bool IsVisible() { return g_Visible; }

    void Render(SDL_Renderer* r, const SDL_Rect& viewport, uint32_t centerActorId)
    {
        if (!g_Visible) return;
        BaseGameLogic* pLogic = g_pApp->GetGameLogic();
        if (!pLogic) return;
        StrongActorPtr pCenter = MakeStrongPtr(pLogic->VGetActor(centerActorId));
        if (!pCenter || !pCenter->GetPositionComponent()) return;

        if (!g_Sorted)
        {
            std::sort(g_Rects.begin(), g_Rects.end(), [](const MapRect& a, const MapRect& b) { return a.x < b.x; });
            g_Sorted = true;
        }
        if (SDL_GetTicks() - g_LastScan > 400)
        {
            g_LastScan = SDL_GetTicks();
            Scan();
        }

        const Point center = pCenter->GetPositionComponent()->GetPosition();
        // Level -> screen
        const double ox = viewport.x + viewport.w / 2.0 - center.x / kScale;
        const double oy = viewport.y + viewport.h / 2.0 - center.y / kScale;
        const double left = center.x - (viewport.w / 2.0) * kScale;
        const double right = center.x + (viewport.w / 2.0) * kScale;
        const double top = center.y - (viewport.h / 2.0) * kScale;
        const double bottom = center.y + (viewport.h / 2.0) * kScale;

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        // Slight darkening so the map reads over bright backgrounds.
        SDL_SetRenderDrawColor(r, 0, 0, 0, 70);
        SDL_RenderFillRect(r, &viewport);

        // Level geometry
        auto first = std::lower_bound(g_Rects.begin(), g_Rects.end(), (float)(left - g_MaxWidth),
            [](const MapRect& a, float x) { return a.x < x; });
        for (auto it = first; it != g_Rects.end() && it->x <= right; ++it)
        {
            const MapRect& g = *it;
            if (g.x + g.w < left || g.y > bottom || g.y + g.h < top) continue;
            switch (g.type)
            {
                case CollisionType_Solid:  SDL_SetRenderDrawColor(r, 235, 215, 160, 150); break;
                case CollisionType_Ground: SDL_SetRenderDrawColor(r, 200, 230, 255, 150); break;
                case CollisionType_Climb:  SDL_SetRenderDrawColor(r, 190, 140, 70, 170); break;
                case CollisionType_Death:  SDL_SetRenderDrawColor(r, 230, 60, 40, 170); break;
                default: continue;
            }
            SDL_Rect d = { (int)(ox + g.x / kScale), (int)(oy + g.y / kScale),
                           std::max(1, (int)(g.w / kScale + 0.5)), std::max(1, (int)(g.h / kScale + 0.5)) };
            // Platforms you can jump through are thin lines in the original
            // too; keep them at least one pixel high.
            SDL_RenderFillRect(r, &d);
        }

        // Treasure (gold) and the end of the level (green)
        SDL_SetRenderDrawColor(r, 255, 210, 40, 210);
        for (const Point& p : g_Treasure)
        {
            if (p.x < left || p.x > right || p.y < top || p.y > bottom) continue;
            FillDot(r, ox + p.x / kScale, oy + p.y / kScale, 3);
        }
        SDL_SetRenderDrawColor(r, 80, 255, 120, 230);
        for (const Point& p : g_Exits)
        {
            if (p.x < left || p.x > right || p.y < top || p.y > bottom) continue;
            FillDot(r, ox + p.x / kScale, oy + p.y / kScale, 7);
        }

        // Captains: the one this view follows blinks; in a race the other
        // one (or the network ghost) is shown in blue.
        auto drawActor = [&](uint32_t id, SDL_Color c, int size)
        {
            StrongActorPtr p = MakeStrongPtr(pLogic->VGetActor(id));
            if (!p || !p->GetPositionComponent()) return;
            Point pos = p->GetPositionComponent()->GetPosition();
            SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
            FillDot(r, ox + pos.x / kScale, oy + pos.y / kScale, size + 2);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            FillDot(r, ox + pos.x / kScale, oy + pos.y / kScale, size);
        };
        if (Race::IsActive())
        {
            for (int i = 0; i < 2; ++i)
            {
                uint32_t id = Race::GetPlayerActor(i);
                if (id && id != centerActorId) drawActor(id, SDL_Color{ 120, 170, 255, 230 }, 5);
            }
            if (uint32_t ghost = Race::GetGhostActor())
            {
                drawActor(ghost, SDL_Color{ 120, 170, 255, 200 }, 5);
            }
        }
        if ((SDL_GetTicks() / 300) % 2 == 0)
        {
            drawActor(centerActorId, SDL_Color{ 255, 255, 255, 255 }, 6);
        }
        else
        {
            drawActor(centerActorId, SDL_Color{ 255, 80, 60, 255 }, 6);
        }
    }
}
