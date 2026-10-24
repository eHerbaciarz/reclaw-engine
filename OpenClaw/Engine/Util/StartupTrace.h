#pragma once

// Startup tracing for diagnosing hangs on consoles/emulators where no debugger
// is attached. Enabled with -DOPENCLAW_STARTUP_TRACE (platform/switch:
// `make TRACE=1`). Each line is flushed immediately so the log survives a hang
// or a crash.
#ifdef OPENCLAW_STARTUP_TRACE
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_timer.h>
// Goes through SDL_Log so it lands in the same sink as engine errors
// (on Switch: openclaw.log + debug output; elsewhere: stderr).
#define STARTUP_TRACE(msg)                                                     \
  SDL_Log("[trace %7u ms] %s", (unsigned)SDL_GetTicks(), (msg))
#else
#define STARTUP_TRACE(msg) do {} while (0)
#endif
