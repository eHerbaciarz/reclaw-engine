#pragma once

// Startup tracing for diagnosing hangs on consoles/emulators where no debugger
// is attached. Enabled with -DOPENCLAW_STARTUP_TRACE (platform/switch:
// `make TRACE=1`). Each line is flushed immediately so the log survives a hang
// or a crash.
#ifdef OPENCLAW_STARTUP_TRACE
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_timer.h>
#include <string>
// The message may be a C string or a std::string.
inline const char* StartupTraceText(const char* s) { return s ? s : ""; }
inline const char* StartupTraceText(const std::string& s) { return s.c_str(); }
// Goes through SDL_Log so it lands in the same sink as engine errors
// (on Switch: reclaw.log + debug output; elsewhere: stderr).
#if defined(__3DS__)
// 3DS: with the heap in use (it is small: ~90 MB)
#include <malloc.h>
#define STARTUP_TRACE(msg)                                                     \
  SDL_Log("[trace %7u ms, heap %6d KB] %s", (unsigned)SDL_GetTicks(),        \
          (int)(mallinfo().uordblks / 1024), StartupTraceText(msg))
#elif defined(__PSL1GHT__)
// PS3: the heap in use and the memory the system still has for the game
#include <malloc.h>
#include <ppu-lv2.h>
inline unsigned StartupTraceFreeKB()
{
  struct { u32 total, available; } info = { 0, 0 };
  lv2syscall1(352, (u64)&info);   // sys_memory_get_user_memory_size
  return info.available / 1024;
}
#define STARTUP_TRACE(msg)                                                     \
  SDL_Log("[trace %7u ms, heap %6d KB, free %6u KB] %s", (unsigned)SDL_GetTicks(), \
          (int)(mallinfo().uordblks / 1024), StartupTraceFreeKB(), StartupTraceText(msg))
#else
#define STARTUP_TRACE(msg)                                                     \
  SDL_Log("[trace %7u ms] %s", (unsigned)SDL_GetTicks(), StartupTraceText(msg))
#endif
#else
#define STARTUP_TRACE(msg) do {} while (0)
#endif
