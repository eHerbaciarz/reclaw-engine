#include <algorithm>
#include <assert.h>
#include <SDL2/SDL_image.h>
#include "Image.h"
#include "../SharedDefines.h"
#include <set>
#include <map>
#include <vector>
#include "PixelFilters.h"
#if !defined(RECLAW_GPU)
#include "../../../platform/common/reclaw_bleed.h"
#endif

namespace
{
    // The pictures made from the game's images (their sampling follows
    // Image::SetSmooth).
    std::set<SDL_Texture*>& SmoothTextures() { static std::set<SDL_Texture*> s; return s; }
    bool g_Smooth = false;
    // The filterable pictures' file and palette (Image::FilterableImages)
    struct FilterSource { std::string path; WapPal* palette; };
    std::map<Image*, FilterSource>& FilterSources() { static std::map<Image*, FilterSource> s; return s; }
    void AddSmoothTexture(SDL_Texture* t)
    {
#if !defined(RECLAW_GPU)
        if (!t) return;
        SmoothTextures().insert(t);
        SDL_SetTextureScaleMode(t, g_Smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#else
        (void)t;
#endif
    }
    void RemoveSmoothTexture(SDL_Texture* t)
    {
#if !defined(RECLAW_GPU)
        if (t) SmoothTextures().erase(t);
#else
        (void)t;
#endif
    }
}

void Image::SetSmooth(bool on)
{
    g_Smooth = on;
#if !defined(RECLAW_GPU)
    for (SDL_Texture* t : SmoothTextures())
        SDL_SetTextureScaleMode(t, on ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#endif
}

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
    FilterSources().erase(this);
    RemoveSmoothTexture(m_pMirrored);
    RemoveSmoothTexture(m_pTexture);
    if (m_pMirrored) {
        SDL_DestroyTexture(m_pMirrored);
        m_pMirrored = NULL;
    }
    if (m_pTexture) {
        SDL_DestroyTexture(m_pTexture);
        m_pTexture = NULL;
    }
}

SDL_Texture* Image::GetMirroredTexture(SDL_Renderer* pRenderer)
{
    if (m_pMirrored || !m_pTexture || !pRenderer) return m_pMirrored;
    Uint32 format = 0;
    int w = 0, h = 0;
    if (SDL_QueryTexture(m_pTexture, &format, NULL, &w, &h) != 0 || w <= 0 || h <= 0) return NULL;
    SDL_Texture* mirrored = SDL_CreateTexture(pRenderer, format, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!mirrored) return NULL;

    SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
    Uint8 alpha = 255, r = 255, g = 255, b = 255;
    SDL_GetTextureBlendMode(m_pTexture, &blend);
    SDL_GetTextureAlphaMod(m_pTexture, &alpha);
    SDL_GetTextureColorMod(m_pTexture, &r, &g, &b);
    SDL_Texture* previous = SDL_GetRenderTarget(pRenderer);
    SDL_Rect viewport, clip;
    SDL_RenderGetViewport(pRenderer, &viewport);
    const SDL_bool clipped = SDL_RenderIsClipEnabled(pRenderer);
    SDL_RenderGetClipRect(pRenderer, &clip);
    float scaleX = 1.0f, scaleY = 1.0f;
    SDL_RenderGetScale(pRenderer, &scaleX, &scaleY);

    // the pixels as they are (alpha included), mirrored, into the new texture
    SDL_SetRenderTarget(pRenderer, mirrored);
    SDL_SetTextureBlendMode(m_pTexture, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(m_pTexture, 255);
    SDL_SetTextureColorMod(m_pTexture, 255, 255, 255);
    SDL_RenderSetScale(pRenderer, 1.0f, 1.0f);
    SDL_RenderCopyEx(pRenderer, m_pTexture, NULL, NULL, 0, NULL, SDL_FLIP_HORIZONTAL);
    SDL_SetRenderTarget(pRenderer, previous);
    SDL_RenderSetScale(pRenderer, scaleX, scaleY);
    SDL_RenderSetViewport(pRenderer, &viewport);
    SDL_RenderSetClipRect(pRenderer, clipped ? &clip : NULL);

    SDL_SetTextureBlendMode(m_pTexture, blend);
    SDL_SetTextureAlphaMod(m_pTexture, alpha);
    SDL_SetTextureColorMod(m_pTexture, r, g, b);
    SDL_SetTextureBlendMode(mirrored, blend);
    m_pMirrored = mirrored;
    if (SmoothTextures().count(m_pTexture)) AddSmoothTexture(m_pMirrored);
    return m_pMirrored;
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

static SDL_Renderer* g_ScaledRenderer = NULL;
static float g_TextureScale = 1.0f;

void Image::SetScaledRenderer(SDL_Renderer* renderer, float scale)
{
    g_ScaledRenderer = renderer;
    g_TextureScale = scale;
}

#if defined(__3DS__) || defined(__PSL1GHT__)
// The picture at `scale` (below 1): each new pixel the alpha-weighted average
// of the source area it covers (transparent pixels do not darken the edges).
// The size is the one SDL draws it at: (int)(w * scale).
static SDL_Surface* ScaleDown(SDL_Surface* src, float scale)
{
    const int dw = (int)(src->w * scale), dh = (int)(src->h * scale);
    if (dw < 1 || dh < 1) return NULL;
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) return NULL;
    const float inv = 1.0f / scale;
    for (int y = 0; y < dh; ++y)
    {
        const float sy0 = y * inv, sy1 = std::min((float)src->h, (y + 1) * inv);
        Uint32* out = (Uint32*)((Uint8*)dst->pixels + y * dst->pitch);
        for (int x = 0; x < dw; ++x)
        {
            const float sx0 = x * inv, sx1 = std::min((float)src->w, (x + 1) * inv);
            float a = 0, r = 0, g = 0, b = 0, area = 0;
            for (int yy = (int)sy0; yy < sy1; ++yy)
            {
                const float wy = std::min(sy1, (float)(yy + 1)) - std::max(sy0, (float)yy);
                const Uint32* row = (const Uint32*)((const Uint8*)src->pixels + yy * src->pitch);
                for (int xx = (int)sx0; xx < sx1; ++xx)
                {
                    const float w = wy * (std::min(sx1, (float)(xx + 1)) - std::max(sx0, (float)xx));
                    const Uint32 p = row[xx];
                    const float pa = (float)(p >> 24) * w;
                    a += pa;
                    r += ((p >> 16) & 0xFF) * pa;
                    g += ((p >> 8) & 0xFF) * pa;
                    b += (p & 0xFF) * pa;
                    area += w;
                }
            }
            if (a <= 0.0f || area <= 0.0f) { out[x] = 0; continue; }
            const Uint32 A = (Uint32)std::min(255.0f, a / area + 0.5f);
            out[x] = (A << 24) | ((Uint32)(r / a + 0.5f) << 16) | ((Uint32)(g / a + 0.5f) << 8) | (Uint32)(b / a + 0.5f);
        }
    }
    return dst;
}
#endif

SDL_Texture* Image::GetTextureFromPid(WapPid* pid, SDL_Renderer* renderer)
{
    assert(pid != NULL);
    assert(renderer != NULL);
    
    uint32_t width = pid->width;
    uint32_t height = pid->height;

#if defined(__3DS__) || defined(__PSL1GHT__)
    // 3DS, PS3 (software rendering): ARGB8888 like the frame the game draws into,
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
    if (renderer == g_ScaledRenderer && g_TextureScale < 0.99f)
    {
        if (SDL_Surface* scaled = ScaleDown(surface, g_TextureScale))
        {
            SDL_FreeSurface(surface);
            surface = scaled;
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
#if defined(__SWITCH__) || defined(__vita__)
    // Smooth sampling mixes a sprite's edge with the see-through pixels next
    // to it: those take the colour of an opaque neighbour (no dark outlines).
    std::vector<WAP_ColorRGBA> bled(pid->colors, pid->colors + colorsCount);
    if (colorsCount == width * height)
    {
        auto at = [&](uint32_t x, uint32_t y) -> const WAP_ColorRGBA& { return pid->colors[y * width + x]; };
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                WAP_ColorRGBA& c = bled[y * width + x];
                if (c.a != 0) continue;
                const WAP_ColorRGBA* n = nullptr;
                if (x > 0 && at(x - 1, y).a) n = &at(x - 1, y);
                else if (x + 1 < width && at(x + 1, y).a) n = &at(x + 1, y);
                else if (y > 0 && at(x, y - 1).a) n = &at(x, y - 1);
                else if (y + 1 < height && at(x, y + 1).a) n = &at(x, y + 1);
                if (n) { c.r = n->r; c.g = n->g; c.b = n->b; }
            }
    }
    const WAP_ColorRGBA* colors = bled.data();
#else
    const WAP_ColorRGBA* colors = pid->colors;
#endif
    for (colorIdx = 0; colorIdx < colorsCount; colorIdx++)
    {
        WAP_ColorRGBA color = colors[colorIdx];
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

static int g_Filter = 0;


std::vector<Image::Source> Image::FilterableImages()
{
    std::vector<Source> v;
    for (auto& kv : FilterSources()) v.push_back(Source{ kv.first, kv.second.path, kv.second.palette });
    return v;
}

bool Image::Rebuild(WapPid* pid, SDL_Renderer* renderer)
{
    if (!pid || !renderer) return false;
    SDL_Texture* tex = NULL;
    int scale = 1;
    if (g_Filter != 0)
    {
        tex = FilteredTextureFromPid(pid, renderer, g_Filter);
        if (tex) scale = 2;
    }
    if (!tex) tex = GetTextureFromPid(pid, renderer);
    if (!tex) return false;
    RemoveSmoothTexture(m_pMirrored);
    RemoveSmoothTexture(m_pTexture);
    if (m_pMirrored) { SDL_DestroyTexture(m_pMirrored); m_pMirrored = NULL; }
    if (m_pTexture) SDL_DestroyTexture(m_pTexture);
    m_pTexture = tex;
    m_TexScale = scale;
    AddSmoothTexture(m_pTexture);
    return true;
}

void Image::SetFilter(int kind)
{
    g_Filter = (kind > 0 && kind < PixelFilters::Count) ? kind : 0;
}

int Image::Filter() { return g_Filter; }

// The picture made twice as big by a pixel filter (its colours as in
// GetTextureFromPid). Null if that cannot be done (the caller makes the plain one).
SDL_Texture* Image::FilteredTextureFromPid(WapPid* pid, SDL_Renderer* renderer, int kind)
{
    const int w = (int)pid->width, h = (int)pid->height;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || (uint32_t)(w * h) != pid->colorsCount) return NULL;
    std::vector<uint32_t> src((size_t)w * h);
    bool opaque = true;
    for (size_t i = 0; i < src.size(); ++i)
    {
        const WAP_ColorRGBA c = pid->colors[i];
        if (c.a != 255) opaque = false;
        src[i] = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
    }
    std::vector<uint32_t> big;
    PixelFilters::Upscale2x((PixelFilters::Kind)kind, src.data(), w, h, big);
    const uint32_t* pixels = big.data();
#if !defined(RECLAW_GPU)
    // (for smooth sampling: see-through pixels coloured like their neighbours;
    // the GPU shims do this themselves)
    std::vector<uint32_t> bled;
    if (const uint32_t* b = ReclawBleedAlpha(pixels, w * 2, w * 2, h * 2, bled)) pixels = b;
#endif
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom((void*)pixels, w * 2, h * 2, 32, w * 2 * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return NULL;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (texture && opaque) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    else if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

Image* Image::CreateImage(WapPid* pid, SDL_Renderer* renderer, bool filterable,
                          const char* sourcePath, WapPal* sourcePalette)
{
    Image* image = new Image();
    if (!image->Initialize(pid, renderer, filterable))
    {
        delete image;
        return NULL;
    }
    if (filterable && sourcePath) FilterSources()[image] = FilterSource{ sourcePath, sourcePalette };

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

bool Image::Initialize(WapPid* pid, SDL_Renderer* renderer, bool filterable)
{
    if (pid == NULL || renderer == NULL)
    {
        return false;
    }

    m_Width = pid->width;
    m_Height = pid->height;
    m_OffsetX = pid->offsetX;
    m_OffsetY = pid->offsetY;

    m_pTexture = NULL;
    if (filterable && g_Filter != 0)
    {
        m_pTexture = FilteredTextureFromPid(pid, renderer, g_Filter);
        if (m_pTexture) m_TexScale = 2;
    }
    if (m_pTexture == NULL) m_pTexture = GetTextureFromPid(pid, renderer);
    if (m_pTexture == NULL)
    {
        return false;
    }
    AddSmoothTexture(m_pTexture);

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