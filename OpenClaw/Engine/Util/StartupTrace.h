#pragma once

// Startup tracing for diagnosing hangs on consoles/emulators where no debugger
// is attached. Enabled with -DOPENCLAW_STARTUP_TRACE (platform/switch:
// `make TRACE=1`). Each line is flushed immediately so the log survives a hang
// or a crash.
#ifdef OPENCLAW_STARTUP_TRACE
#include <SDL2/SDL_timer.h>
#include <cstdio>
#define STARTUP_TRACE(msg)                                                     \
  do {                                                                         \
    fprintf(stderr, "[trace %7u ms] %s\n", (unsigned)SDL_GetTicks(), (msg));   \
    fflush(stderr);                                                            \
  } while (0)
#else
#define STARTUP_TRACE(msg) do {} while (0)
#endif
