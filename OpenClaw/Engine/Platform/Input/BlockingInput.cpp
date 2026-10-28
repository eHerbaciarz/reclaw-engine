#include "BlockingInput.h"

#ifndef __EMSCRIPTEN__

#include <SDL2/SDL.h>

#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef __3DS__
#include <3ds.h>
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
            padConfigureInput(8, HidNpadStyleSet_NpadStandard);   // 8: keep player 2 (race) connected
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
        // Only the stick here: the buttons come from SDL below, mapped the
        // same way as in the game's own menus (so the same button confirms
        // everywhere, whatever SDL reports for the Nintendo labels).
        if (down & HidNpadButton_StickLUp) result |= Up;
        if (down & HidNpadButton_StickLDown) result |= Down;
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
        for (int i = 0; i < n; ++i)
        {
            switch (ev[i].cbutton.button)
            {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: result |= Up; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: result |= Down; break;
                case SDL_CONTROLLER_BUTTON_A: result |= Accept; break;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: result |= Back; break;
                case SDL_CONTROLLER_BUTTON_START: result |= Start; break;
                default: break;
            }
        }
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
#if defined(__SWITCH__)
        return appletMainLoop();
#elif defined(__3DS__)
        return aptMainLoop();
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
