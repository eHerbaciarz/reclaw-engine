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
#ifdef __cplusplus
}
#endif
#ifndef RECLAW_PS3_SCREEN_IMPL
#define SDL_CreateRenderer ReclawPS3_CreateRenderer
#define SDL_RenderPresent ReclawPS3_RenderPresent
#endif
