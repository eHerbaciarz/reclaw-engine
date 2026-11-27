#pragma once
// What the memory goes to (reclaw.log, level loading and the perf lines):
// the low-memory consoles (3DS, PS2, Dreamcast) need to know what to cut.
#include <cstdint>
#include <string>

namespace MemStats
{
    extern int64_t g_TextureBytes;   // pictures' textures, width x height x bytes per pixel
    extern int64_t g_TextureCount;
    extern int64_t g_SoundBytes;     // decoded sounds (Mix_Chunk)
    extern int64_t g_SoundCount;

    // "textures 812 (23456 KB), sounds 120 (4567 KB), cache 345 KB, actors 3485, bodies 512"
    std::string Report();
}
