#pragma once
// Putting back a viewport read with SDL_RenderGetViewport. SDL reports "no
// viewport" as the whole target in the units of the scale of that moment;
// set again it stays fixed when the scale changes later (the menus draw at
// scale 1: they ended up cut to a corner). The whole target -> NULL.
#include <SDL2/SDL.h>

static inline void ReclawRestoreViewport(SDL_Renderer* r, const SDL_Rect& viewport, float scaleX, float scaleY)
{
    int fullW = 0, fullH = 0;
    if (SDL_Texture* target = SDL_GetRenderTarget(r)) SDL_QueryTexture(target, NULL, NULL, &fullW, &fullH);
    else SDL_GetRendererOutputSize(r, &fullW, &fullH);
    if (scaleX <= 0.0f) scaleX = 1.0f;
    if (scaleY <= 0.0f) scaleY = 1.0f;
    const bool whole = viewport.x == 0 && viewport.y == 0 &&
                       viewport.w >= (int)(fullW / scaleX) - 1 && viewport.h >= (int)(fullH / scaleY) - 1;
    SDL_RenderSetViewport(r, whole ? NULL : &viewport);
}
