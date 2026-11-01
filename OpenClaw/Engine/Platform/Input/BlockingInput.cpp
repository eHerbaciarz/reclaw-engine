#include "BlockingInput.h"

#ifndef __EMSCRIPTEN__

#include <SDL2/SDL.h>

#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef __3DS__
#include <3ds.h>
#endif
#ifdef __PSL1GHT__
#include <io/pad.h>
#endif

namespace
{
    // Screens shown before the game opens its controllers (the "CLAW.REZ
    // not found" screen) would get no button events: open them here.
    void EnsureControllers()
    {
        if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0)
        {
            if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return;
        }
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
        {
            if (!SDL_IsGameController(i)) continue;
            SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
            if (SDL_GameControllerFromInstanceID(id)) continue;   // already open
            SDL_GameControllerOpen(i);
        }
    }

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
#ifdef __PSL1GHT__
        // PS3: the pads read directly too (the "CLAW.REZ not found" screen
        // could not be left with SDL's controller events alone).
        {
            static bool s_init = false;
            static unsigned s_prev[MAX_PORT_NUM] = {};
            if (!s_init) { ioPadInit(7); s_init = true; }
            padInfo info;
            static Uint32 s_lastDiag = 0;
            const bool diag = SDL_GetTicks() - s_lastDiag > 1000;
            if (diag) s_lastDiag = SDL_GetTicks();
            int infoRet = ioPadGetInfo(&info);
            if (diag)
            {
                SDL_Log("pad diag: getinfo %d, max %u, connected %u, info 0x%x, status %u %u %u %u",
                        infoRet, (unsigned)info.max, (unsigned)info.connected, (unsigned)info.info,
                        (unsigned)info.status[0], (unsigned)info.status[1], (unsigned)info.status[2], (unsigned)info.status[3]);
            }
            if (infoRet == 0)
            {
                for (int i = 0; i < MAX_PORT_NUM; ++i)
                {
                    if (!info.status[i]) continue;
                    padData d;
                    int ret = ioPadGetData(i, &d);
                    if (diag || (ret == 0 && d.len > 0))
                    {
                        SDL_Log("pad diag: port %d: getdata %d, len %d, buttons %04x %04x", i, ret, (int)d.len,
                                (unsigned)d.button[2], (unsigned)d.button[3]);
                    }
                    if (ret != 0 || d.len == 0) continue;
                    unsigned now = (d.BTN_CROSS ? 1u : 0u) | (d.BTN_CIRCLE ? 2u : 0u) | (d.BTN_START ? 4u : 0u) |
                                   (d.BTN_UP ? 8u : 0u) | (d.BTN_DOWN ? 16u : 0u) | (d.BTN_SELECT ? 32u : 0u);
                    unsigned pressed = now & ~s_prev[i];
                    s_prev[i] = now;
                    if (pressed & 1u) result |= Accept;
                    if (pressed & (2u | 32u)) result |= Back;
                    if (pressed & 4u) result |= Start;
                    if (pressed & 8u) result |= Up;
                    if (pressed & 16u) result |= Down;
                }
            }
        }
#endif
        EnsureControllers();
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
