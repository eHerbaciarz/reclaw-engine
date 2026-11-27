#ifndef __AUTOPLAY_H__
#define __AUTOPLAY_H__
// Test runs on the desktop build: RECLAW_AUTOPLAY=<script> plays a scripted
// input sequence and logs Claw's movement (reclaw.log / stderr). See Autoplay.cpp.
#include <SDL2/SDL.h>
#include <cstdint>
namespace Autoplay
{
    bool Active();
    void Tick(uint32_t ms);                 // once per fixed logic step, before the update
    void AfterRender(SDL_Renderer *r);      // after the frame is composed (screenshots)
}
#endif
