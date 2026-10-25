#include "BlockingInput.h"

#ifndef __EMSCRIPTEN__

#include <SDL2/SDL.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace
{
#ifdef __SWITCH__
    PadState g_Pad;
    bool g_PadReady = false;

    void EnsurePad()
    {
        if (!g_PadReady)
        {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&g_Pad);
            g_PadReady = true;
            padUpdate(&g_Pad);
        }
    }
#endif
}

namespace BlockingInput
{
    int Poll()
    {
        int result = None;
#ifdef __SWITCH__
        EnsurePad();
        padUpdate(&g_Pad);
        u64 down = padGetButtonsDown(&g_Pad);
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) result |= Up;
        if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) result |= Down;
        if (down & HidNpadButton_A) result |= Accept;
        if (down & HidNpadButton_B) result |= Back;
        if (down & HidNpadButton_Plus) result |= Start;
#endif
        SDL_PumpEvents();
        SDL_Event ev[16];
        int n = SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYDOWN);
        for (int i = 0; i < n; ++i)
        {
            switch (ev[i].key.keysym.sym)
            {
                case SDLK_UP: result |= Up; break;
                case SDLK_DOWN: result |= Down; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: result |= Accept; break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: result |= Back; break;
                default: break;
            }
        }
        n = SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONDOWN);
#ifndef __SWITCH__
        // On Switch the same presses were already read from the pad above.
        for (int i = 0; i < n; ++i)
        {
            switch (ev[i].cbutton.button)
            {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: result |= Up; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: result |= Down; break;
                case SDL_CONTROLLER_BUTTON_A: result |= Accept; break;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: result |= Back; break;
                case SDL_CONTROLLER_BUTTON_START: result |= Start; break;
                default: result |= Accept; break;
            }
        }
#else
        (void)n;
#endif
        if (SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_FINGERDOWN, SDL_FINGERDOWN) > 0) result |= Accept;
        if (SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONDOWN) > 0) result |= Accept;
        if (SDL_PeepEvents(ev, 16, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) > 0) result |= Quit;
        if (!AppAlive()) result |= Quit;
        return result;
    }

    void Reset()
    {
#ifdef __SWITCH__
        EnsurePad();
        padUpdate(&g_Pad);   // buttons held now do not count as new presses
#endif
        Flush();
    }

    void Flush()
    {
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_KEYDOWN, SDL_TEXTINPUT);
        SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEWHEEL);
        SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_JOYBUTTONUP);
        SDL_FlushEvents(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERBUTTONUP);
        SDL_FlushEvents(SDL_FINGERDOWN, SDL_FINGERMOTION);
    }

    bool AppAlive()
    {
#ifdef __SWITCH__
        return appletMainLoop();
#else
        return true;
#endif
    }
}

#else

namespace BlockingInput
{
    int Poll() { return None; }
    void Reset() { }
    void Flush() { }
    bool AppAlive() { return true; }
}

#endif
