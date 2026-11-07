// PS3: SDL's PSL1GHT renderer only copies pictures (no blending, no render
// targets, no alpha), so the game draws with SDL's software renderer into a
// picture in memory, and only that finished picture goes to the screen
// through the PSL1GHT renderer (ps3_sdl_screen.cpp). Force-included in C++
// files with ps3_cxx_compat.h: every SDL_CreateRenderer / SDL_RenderPresent
// call of the game goes through these.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Renderer* ReclawPS3_CreateRenderer(struct SDL_Window* window, int index, unsigned int flags);
void ReclawPS3_RenderPresent(struct SDL_Renderer* renderer);
/* The size the game draws at (960x540 on a 1080p TV, 854x480 otherwise). */
void ReclawPS3_GameSize(int* w, int* h);
/* The TV's size (the display buffers). */
void ReclawPS3_TvSize(int* w, int* h);
/* RSX drawing (platform/ps3/gpu): the display buffers without the software
   screen. Fills their RSX offsets and pitch; 0 on failure. */
int ReclawPS3_GpuDisplay(unsigned int offsets[2], unsigned int* pitch);
/* Waits until the buffer last flipped away from is off the screen. */
void ReclawPS3_GpuWaitFlip(void);
/* Queues a flip to display buffer `index`. */
void ReclawPS3_GpuFlip(int index);
/* The RSX command buffer back to its start (waits for the RSX). */
void ReclawPS3_ResetCommandBuffer(void);
#ifdef __cplusplus
}
#endif
#ifndef RECLAW_PS3_SCREEN_IMPL
#define SDL_CreateRenderer ReclawPS3_CreateRenderer
#define SDL_RenderPresent ReclawPS3_RenderPresent
#endif
