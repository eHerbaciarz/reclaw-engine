#include "../UserInterface/SecondScreen.h"
#include "Scene.h"
#include "HUDSceneNode.h"
#include "../Actor/Components/RenderComponent.h"
#include "../Graphics2D/Image.h"
#include "../GameApp/BaseGameApp.h"

bool SDL2HUDSceneNode::s_bHideAll = false;

SDL2HUDSceneNode::SDL2HUDSceneNode(const uint32 actorId,
    BaseRenderComponent* pRenderComponent,
    RenderPass renderPass,
    Point position,
    bool visible)
    : SceneNode(actorId, pRenderComponent, renderPass, position, 0)
{
    m_IsActive = visible;
}

SDL2HUDSceneNode::~SDL2HUDSceneNode()
{

}

void SDL2HUDSceneNode::VRender(Scene* pScene)
{
    // 3DS: the HUD is on the bottom screen
    if (!IsVisible(pScene) || SecondScreen::ShowsHud())
    {
        return;
    }

    HUDRenderComponent* hrc = static_cast<HUDRenderComponent*>(m_pRenderComponent);
    Point scale = g_pApp->GetScale();

    shared_ptr<Image> actorImage = MakeStrongPtr(hrc->GetCurrentImage());
    if (!actorImage)
    {
        LOG_WARNING("Trying to render actor without active image. ActorId: " + ToStr(m_Properties.GetActorId()));
        return;
    }

    int offsetX = hrc->IsMirrored() ? -actorImage->GetOffsetX() : actorImage->GetOffsetX();
    int offsetY = hrc->IsInverted() ? -actorImage->GetOffsetY() : actorImage->GetOffsetY();

    int x = (int32)(m_Properties.GetPosition().x - actorImage->GetWidth() / 2 + offsetX);
    if (hrc->IsAnchoredRight())
    {
        x += (int)((pScene->GetCamera()->GetWidth()) / scale.x);
    }
    int32 y = (int)(m_Properties.GetPosition().y - actorImage->GetHeight() / 2 + offsetY);
    if (hrc->IsAnchoredBottom())
    {
        y += (int)((pScene->GetCamera()->GetHeight()) / scale.y);
    }

    // HACK: Pistol and magic first frame has incorrect offset...
    if (hrc->IsAnchoredRight() && actorImage->GetWidth() == 52 && actorImage->GetHeight() == 14 && actorImage->GetOffsetY() == 1)
    {
        y += 2;
    }
    else if (hrc->IsAnchoredRight() && actorImage->GetWidth() == 52 && actorImage->GetHeight() == 21 && actorImage->GetOffsetY() == -2)
    {
        y += 2;
    }

    SDL_Rect renderRect =
    {
        x,
        y,
        actorImage->GetWidth(),
        actorImage->GetHeight()
    };

    SDL_Renderer* renderer = pScene->GetRenderer();
#if defined(__PSL1GHT__) || (defined(__3DS__) && !defined(RECLAW_3DS_GPU))
    // SDL's software renderer: SDL_RenderCopyEx makes and rotates a temporary
    // copy of the picture on every call, even unflipped. A plain copy, of the
    // mirrored picture (made once) when mirrored.
    if (!hrc->IsInverted())
    {
        SDL_Texture* tex = actorImage->GetTexture();
        if (hrc->IsMirrored())
        {
            if (SDL_Texture* m = actorImage->GetMirroredTexture(renderer))
            {
                Uint8 a = 255, cr = 255, cg = 255, cb = 255;
                SDL_GetTextureAlphaMod(tex, &a);
                SDL_GetTextureColorMod(tex, &cr, &cg, &cb);
                SDL_SetTextureAlphaMod(m, a);
                SDL_SetTextureColorMod(m, cr, cg, cb);
                tex = m;
            }
            else tex = NULL;
        }
        if (tex)
        {
            SDL_RenderCopy(renderer, tex, NULL, &renderRect);
            return;
        }
    }
#endif
    SDL_RenderCopyEx(renderer, actorImage->GetTexture(), NULL, &renderRect, 0, NULL,
        hrc->IsMirrored() ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}