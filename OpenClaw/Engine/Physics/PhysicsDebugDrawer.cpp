#include "PhysicsDebugDrawer.h"
#include "../SharedDefines.h"
#include "../Scene/SceneNodes.h"

#if !defined(OPENCLAW_NO_SDL2_GFX)
#include <SDL2/SDL2_gfxPrimitives.h>
#else
// Ports without SDL2_gfx: the two outlines the physics debug view draws.
#include <cmath>
static int polygonRGBA(SDL_Renderer* r, const Sint16* vx, const Sint16* vy, int n, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    if (n < 2) return 0;
    std::vector<SDL_Point> pts(n + 1);
    for (int i = 0; i < n; ++i) pts[i] = SDL_Point{ vx[i], vy[i] };
    pts[n] = pts[0];
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    return SDL_RenderDrawLines(r, pts.data(), n + 1);
}
static int circleRGBA(SDL_Renderer* r, Sint16 x, Sint16 y, Sint16 rad, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    const int kSegments = 24;
    SDL_Point pts[kSegments + 1];
    for (int i = 0; i <= kSegments; ++i)
    {
        double a = i * 2.0 * M_PI / kSegments;
        pts[i] = SDL_Point{ (int)(x + rad * std::cos(a)), (int)(y + rad * std::sin(a)) };
    }
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    return SDL_RenderDrawLines(r, pts, kSegments + 1);
}
#endif

// Conversion warnings from Box2D -> SDL2_gfx
#pragma warning(disable: 4244)

PhysicsDebugDrawer::PhysicsDebugDrawer()
{
    SetFlags(b2Draw::e_shapeBit);
}

void PhysicsDebugDrawer::DrawPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color)
{
    Point cameraPos = m_pCamera->GetPosition();

    std::vector<int16> vaX;
    std::vector<int16> vaY;

    vaX.reserve(vertexCount);
    vaY.reserve(vertexCount);
    for (int32 vertexIdx = 0; vertexIdx < vertexCount; vertexIdx++)
    {
        b2Vec2 worldPos = MetersToPixels(vertices[vertexIdx]);

        vaX.push_back(worldPos.x - cameraPos.x);
        vaY.push_back(worldPos.y - cameraPos.y);
    }

    polygonRGBA(m_pRenderer, vaX.data(), vaY.data(), vertexCount, color.r * 255, color.g * 255, color.b * 255, color.a * 255);
}

// This is ~20 times more cpu taxing than DrawPolygon
void PhysicsDebugDrawer::DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color)
{
    // For now I guess
    DrawPolygon(vertices, vertexCount, color);

    /*Point cameraPos = m_pCamera->GetPosition();

    std::vector<int16> vaX;
    std::vector<int16> vaY;

    for (int32 vertexIdx = 0; vertexIdx < vertexCount; vertexIdx++)
    {
        vaX.push_back(vertices[vertexIdx].x - cameraPos.x);
        vaY.push_back(vertices[vertexIdx].y - cameraPos.y);
    }

    filledPolygonRGBA(m_pRenderer, vaX.data(), vaY.data(), vertexCount, color.r * 255, color.g * 255, color.b * 255, (color.a * 255) * 0.5);*/
}

void PhysicsDebugDrawer::DrawCircle(const b2Vec2& center, float32 radius, const b2Color& color)
{
    Point cameraPos = m_pCamera->GetPosition();
    b2Vec2 worldPos = MetersToPixels(center);
    float worldRadius = MetersToPixels(radius);

    circleRGBA(m_pRenderer, worldPos.x - cameraPos.x, worldPos.y - cameraPos.y, worldRadius, color.r * 255, color.g * 255, color.b * 255, color.a * 255);
}

void PhysicsDebugDrawer::DrawSolidCircle(const b2Vec2& center, float32 radius, const b2Vec2& axis, const b2Color& color)
{
    DrawCircle(center, radius, color);
}

void PhysicsDebugDrawer::DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color)
{

}

void PhysicsDebugDrawer::DrawTransform(const b2Transform& xf)
{

}

void PhysicsDebugDrawer::DrawPoint(const b2Vec2& p, float32 size, const b2Color& color)
{
    SDL_SetRenderDrawColor(m_pRenderer, color.r * 255, color.g, color.b * 255, color.a * 255);

    SDL_RenderDrawPoint(m_pRenderer, p.x, p.y);
}

void PhysicsDebugDrawer::PrepareForDraw(SDL_Renderer* pRenderer, std::shared_ptr<CameraNode> pCamera)
{
    assert(pRenderer);
    assert(pCamera);

    m_pRenderer = pRenderer;
    m_pCamera = pCamera;
}