// ReClaw PS3: the game's drawing on the RSX.
//
// The game drew with SDL's software renderer on the PPU (15-25 ms a frame on
// a real PS3). This header is force-included into the game's C++ files (not
// into SDL), after ps3_cxx_compat.h: every SDL render call of the game goes
// to ReclawGpu_* (ps3_gpu.cpp), which draws with the RSX. With the RSX off
// (port_settings.txt gpu=0) the calls go on to SDL and the software screen
// (ps3_sdl_screen.cpp). Same interface as the 3DS's (platform/3ds/gpu).
#pragma once

#ifndef RECLAW_GPU_IMPL
#undef SDL_CreateRenderer
#undef SDL_RenderPresent
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
// Options: every picture sampled smoothly (1) or as set for it (0: sharp
// pixels unless the game asked for smooth sampling).
void ReclawGpu_SetSmooth(int on);
#ifdef __cplusplus
}
#endif
