#include <algorithm>
#include <assert.h>
#include <SDL2/SDL_image.h>
#include "Image.h"
#include "../SharedDefines.h"

Image::Image()
    :
    m_Width(0),
    m_Height(0),
    m_OffsetX(0),
    m_OffsetY(0),
    m_pTexture(NULL)
{
    
}

Image::Image(SDL_Texture* pSDLTexture)
    :
    m_pTexture(pSDLTexture),
    m_OffsetX(0),
    m_OffsetY(0)
{
    assert(pSDLTexture != NULL);
    SDL_QueryTexture(pSDLTexture, NULL, NULL, &m_Width, &m_Height);
}

Image::~Image()
{
    if (m_pTexture) {
        SDL_DestroyTexture(m_pTexture);
        m_pTexture = NULL;
    }
}

SDL_Rect Image::GetPositonRect(int32_t x, int32_t y)
{
    int positionX = x - m_Width / 2 + m_OffsetX;
    int positionY = y - m_Height / 2 + m_OffsetY;

    SDL_Rect rect = { positionX, positionY, m_Width, m_Height };

    return rect;
}

static void PutPixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch (bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        }
        else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
}

SDL_Texture* Image::GetTextureFromPid(WapPid* pid, SDL_Renderer* renderer)
{
    assert(pid != NULL);
    assert(renderer != NULL);
    
    uint32_t width = pid->width;
    uint32_t height = pid->height;

#if defined(__3DS__)
    // 3DS (software rendering): ARGB8888 like the frame the game draws into,
    // so SDL blits with its fast paths; images without transparent pixels
    // (most tiles) are copied without blending.
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface != NULL);
    bool opaque = true;
    {
        uint32_t colorsCount = std::min<uint32_t>(pid->colorsCount, width * height);
        for (uint32_t i = 0; i < colorsCount; i++)
        {
            WAP_ColorRGBA c = pid->colors[i];
            if (c.a != 255) opaque = false;
            Uint32* row = (Uint32*)((Uint8*)surface->pixels + (i / width) * surface->pitch);
            row[i % width] = ((Uint32)c.a << 24) | ((Uint32)c.r << 16) | ((Uint32)c.g << 8) | c.b;
        }
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    assert(texture != NULL);
    if (texture && opaque) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
#else
    SDL_Surface* surface = Util::CreateRGBSurface(0, width, height, 32);
    assert(surface != NULL);

    uint32_t colorIdx;
    uint32_t colorsCount = pid->colorsCount;
    for (colorIdx = 0; colorIdx < colorsCount; colorIdx++)
    {
        WAP_ColorRGBA color = pid->colors[colorIdx];
        uint32_t x = colorIdx % width;
        uint32_t y = colorIdx / width;

        PutPixel(surface, x, y, SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a));
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    assert(texture != NULL);
#endif

    SDL_FreeSurface(surface);

    return texture;
}

Image* Image::CreateImage(WapPid* pid, SDL_Renderer* renderer)
{
    Image* image = new Image();
    if (!image->Initialize(pid, renderer))
    {
        delete image;
        return NULL;
    }

    return image;
}

Image* Image::CreatePcxImage(char* rawBuffer, uint32_t size, SDL_Renderer* renderer, bool useColorKey, SDL_Color colorKey)
{
    Image* pImage = new Image();
    SDL_RWops* pRWops = SDL_RWFromMem((void*)rawBuffer, size);
    SDL_Surface* pSurface = IMG_LoadPCX_RW(pRWops);
    if (pSurface == NULL)
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        return NULL;
    }

    if (useColorKey && colorKey.a == 1 && colorKey.r == 0 && colorKey.g == 0 && colorKey.b == 0
        && pSurface->w > 0 && pSurface->h > 0)
    {
        // Key on the raw value of the top-left pixel (palette index for 8-bit PCX).
        Uint32 key = 0;
        if (SDL_LockSurface(pSurface) == 0)
        {
            const Uint8* p = (const Uint8*)pSurface->pixels;
            switch (pSurface->format->BytesPerPixel)
            {
                case 1: key = p[0]; break;
                case 2: key = *(const Uint16*)p; break;
                case 3: key = SDL_BYTEORDER == SDL_BIG_ENDIAN ? (p[0] << 16 | p[1] << 8 | p[2])
                                                               : (p[0] | p[1] << 8 | p[2] << 16); break;
                default: key = *(const Uint32*)p; break;
            }
            SDL_UnlockSurface(pSurface);
        }
        SDL_SetColorKey(pSurface, SDL_TRUE, key);
    }
    else if (useColorKey)
    {
        SDL_SetColorKey(pSurface, SDL_TRUE, SDL_MapRGB(pSurface->format, colorKey.r, colorKey.g, colorKey.b));
    }

    SDL_Texture* pTexture = SDL_CreateTextureFromSurface(renderer, pSurface);
    SDL_FreeSurface(pSurface);

    if (pTexture == NULL)
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        return NULL;
    }

    if (!pImage->Initialize(pTexture))
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        SDL_DestroyTexture(pTexture);
        return NULL;
    }

    return pImage;
}

Image* Image::CreatePngImage(char* rawBuffer, uint32_t size, SDL_Renderer* renderer)
{
    Image* pImage = new Image();
    SDL_RWops* pRWops = SDL_RWFromMem((void*)rawBuffer, size);
    SDL_Surface* pSurface = IMG_LoadPNG_RW(pRWops);
    if (pSurface == NULL)
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        return NULL;
    }

    SDL_Texture* pTexture = SDL_CreateTextureFromSurface(renderer, pSurface);
    SDL_FreeSurface(pSurface);

    if (pTexture == NULL)
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        return NULL;
    }

    if (!pImage->Initialize(pTexture))
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        SDL_DestroyTexture(pTexture);
        return NULL;
    }

    return pImage;
}

Image* Image::CreateImageFromColor(SDL_Color color, int w, int h, SDL_Renderer* pRenderer)
{
    Image* pImage = new Image();

    SDL_Surface* pSurface = SDL_CreateRGBSurface(0, w, h, 32, 0, 0, 0, 0);
    SDL_FillRect(pSurface, NULL, SDL_MapRGB(pSurface->format, color.r, color.g, color.b));
    SDL_Texture* pTextureRect = SDL_CreateTextureFromSurface(pRenderer, pSurface);

    SDL_FreeSurface(pSurface);

    if (pTextureRect == NULL)
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        return NULL;
    }

    if (!pImage->Initialize(pTextureRect))
    {
        LOG_ERROR(IMG_GetError());
        delete pImage;
        SDL_DestroyTexture(pTextureRect);
        return NULL;
    }

    return pImage;
}

bool Image::Initialize(WapPid* pid, SDL_Renderer* renderer)
{
    if (pid == NULL || renderer == NULL)
    {
        return false;
    }

    m_Width = pid->width;
    m_Height = pid->height;
    m_OffsetX = pid->offsetX;
    m_OffsetY = pid->offsetY;

    m_pTexture = GetTextureFromPid(pid, renderer);
    if (m_pTexture == NULL)
    {
        return false;
    }

    return true;
}

bool Image::Initialize(SDL_Texture* pTexture)
{
    if (pTexture == NULL)
    {
        return false;
    }

    SDL_QueryTexture(pTexture, NULL, NULL, &m_Width, &m_Height);
    m_OffsetX = m_OffsetY = 0;
    m_pTexture = pTexture;

    return true;
}