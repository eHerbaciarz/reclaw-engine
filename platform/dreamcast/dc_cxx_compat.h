// Dreamcast (KallistiOS, sh-elf): the toolchain's libstdc++ has no
// std::to_string for floating point numbers (its C99 stdio bits are off);
// the engine uses them for logs and XML.
#pragma once
#ifdef __cplusplus
#include <string>
#include <cstdio>
namespace std
{
    inline string to_string(double v) { char b[64]; snprintf(b, sizeof(b), "%f", v); return b; }
    inline string to_string(float v) { return to_string((double)v); }
    inline string to_string(long double v) { return to_string((double)v); }
}
#endif
