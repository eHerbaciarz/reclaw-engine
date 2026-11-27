#pragma once
#include <SDL2/SDL.h>

// The physical key of a key event. The scancode SDL put in the event comes
// first: with SDL2 over SDL3 (sdl2-compat, MSYS2's SDL2 on Windows) turning
// the key code back into a scancode is not reliable (Esc did nothing there).
static inline SDL_Scancode ReclawEventScancode(const SDL_Keysym& keysym)
{
    if (keysym.scancode != SDL_SCANCODE_UNKNOWN) return keysym.scancode;
    return SDL_GetScancodeFromKey(keysym.sym);
}
