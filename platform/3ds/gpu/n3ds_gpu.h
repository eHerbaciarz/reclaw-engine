// ReClaw 3DS: the game's drawing on the 3DS's GPU (citro3d).
//
// SDL has only a software renderer on the 3DS: the CPU drew every pixel
// (about 50 ms a frame). This header is force-included into the game's C++
// files (not into SDL): every SDL render call of the game goes to
// ReclawGpu_* (n3ds_gpu.cpp). The top screen's renderer draws with the GPU;
// any other renderer (the bottom screen's SecondScreen) and its textures go
// on to SDL as before. The SDL headers, included after this, declare the
// ReclawGpu_* functions with SDL's own signatures.
#pragma once

#ifndef RECLAW_GPU_IMPL
#define SDL_CreateRenderer ReclawGpu_CreateRenderer
#define SDL_DestroyRenderer ReclawGpu_DestroyRenderer
#define SDL_GetRendererOutputSize ReclawGpu_GetRendererOutputSize
#define SDL_RenderClear ReclawGpu_RenderClear
#define SDL_RenderPresent ReclawGpu_RenderPresent
#define SDL_SetRenderDrawColor ReclawGpu_SetRenderDrawColor
#define SDL_GetRenderDrawColor ReclawGpu_GetRenderDrawColor
#define SDL_SetRenderDrawBlendMode ReclawGpu_SetRenderDrawBlendMode
#define SDL_RenderFillRect ReclawGpu_RenderFillRect
#define SDL_RenderDrawRect ReclawGpu_RenderDrawRect
#define SDL_RenderDrawLine ReclawGpu_RenderDrawLine
#define SDL_RenderDrawLines ReclawGpu_RenderDrawLines
#define SDL_RenderDrawPoint ReclawGpu_RenderDrawPoint
#define SDL_RenderSetScale ReclawGpu_RenderSetScale
#define SDL_RenderGetScale ReclawGpu_RenderGetScale
#define SDL_RenderSetViewport ReclawGpu_RenderSetViewport
#define SDL_RenderGetViewport ReclawGpu_RenderGetViewport
#define SDL_RenderSetClipRect ReclawGpu_RenderSetClipRect
#define SDL_RenderGetClipRect ReclawGpu_RenderGetClipRect
#define SDL_RenderIsClipEnabled ReclawGpu_RenderIsClipEnabled
#define SDL_SetRenderTarget ReclawGpu_SetRenderTarget
#define SDL_GetRenderTarget ReclawGpu_GetRenderTarget
#define SDL_RenderCopy ReclawGpu_RenderCopy
#define SDL_RenderCopyEx ReclawGpu_RenderCopyEx
#define SDL_RenderReadPixels ReclawGpu_RenderReadPixels
#define SDL_CreateTexture ReclawGpu_CreateTexture
#define SDL_CreateTextureFromSurface ReclawGpu_CreateTextureFromSurface
#define SDL_DestroyTexture ReclawGpu_DestroyTexture
#define SDL_QueryTexture ReclawGpu_QueryTexture
#define SDL_SetTextureAlphaMod ReclawGpu_SetTextureAlphaMod
#define SDL_GetTextureAlphaMod ReclawGpu_GetTextureAlphaMod
#define SDL_SetTextureColorMod ReclawGpu_SetTextureColorMod
#define SDL_GetTextureColorMod ReclawGpu_GetTextureColorMod
#define SDL_SetTextureBlendMode ReclawGpu_SetTextureBlendMode
#define SDL_GetTextureBlendMode ReclawGpu_GetTextureBlendMode
#define SDL_SetTextureScaleMode ReclawGpu_SetTextureScaleMode
#define SDL_UpdateTexture ReclawGpu_UpdateTexture
#define SDL_UpdateYUVTexture ReclawGpu_UpdateYUVTexture
#endif

#ifdef __cplusplus
extern "C" {
#endif
// True once the top screen draws with the GPU.
int ReclawGpu_Active(void);
// Stereo 3D: which eye the screen drawing goes to (0 left, 1 right).
void ReclawGpu_SetEye(int eye);
// The game's frame as a render target: drawing goes straight to the screen,
// but with a scale and viewport of its own, as with a real render target (code
// that selects no target and draws by the screen's size gets scale 1).
// Copying it, or destroying it, does nothing. Null without the GPU.
struct SDL_Texture* ReclawGpu_GameFrame(void);
#ifdef __cplusplus
}
#endif
