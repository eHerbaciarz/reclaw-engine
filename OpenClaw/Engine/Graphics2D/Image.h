#ifndef IMAGE_H_
#define IMAGE_H_

#include <libwap.h>
#include <SDL2/SDL.h>
#include <stdint.h>

class Image
{
public:
    Image();
    Image(SDL_Texture* pSDLTexture);
    ~Image();

    static SDL_Texture* GetTextureFromPid(WapPid* pid, SDL_Renderer* renderer);
    static Image* CreateImage(WapPid* pid, SDL_Renderer* renderer);
    // Pass as colorKey: use the image's top-left pixel as the transparent colour.
    // Menu texts are drawn on a solid background that differs between releases
    // (black in the English CLAW.REZ, magenta in the Polish one).
    static SDL_Color ColorKeyFromTopLeft() { return SDL_Color{ 0, 0, 0, 1 }; }
    static Image* CreatePcxImage(char* rawBuffer, uint32_t size, SDL_Renderer* renderer, bool useColorKey = false, SDL_Color colorKey = { 0, 0, 0, 0 });
    static Image* CreatePngImage(char* rawBuffer, uint32_t size, SDL_Renderer* renderer);
    static Image* CreateImageFromColor(SDL_Color color, int w, int h, SDL_Renderer* pRenderer);

    inline SDL_Texture* GetTexture() { return m_pTexture; }
    inline int GetWidth() { return m_Width; }
    inline int GetHeight() { return m_Height; }
    inline int GetOffsetX() { return m_OffsetX; }
    inline int GetOffsetY() { return m_OffsetY; }

    void SetOffset(int x, int y) { m_OffsetX = x; m_OffsetY = y; }
    // The size the game places the picture by, when its texture is made
    // bigger or smaller than that (already at the size it is drawn at).
    void SetLogicalSize(int w, int h) { m_Width = w; m_Height = h; }

    SDL_Rect GetPositonRect(int32_t x, int32_t y);

private:
    bool Initialize(WapPid* pid, SDL_Renderer* renderer);
    bool Initialize(SDL_Texture* pTexture);

    SDL_Texture* m_pTexture;
    int m_Width;
    int m_Height;
    int m_OffsetX;
    int m_OffsetY;
};

#endif
