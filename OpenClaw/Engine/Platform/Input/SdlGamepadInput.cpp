#ifndef __EMSCRIPTEN__

#include "SdlGamepadInput.h"

#include "../../GameApp/BaseGameApp.h"
#include "../../GameApp/BaseGameLogic.h"
#include "../Events/AppEvent.h"

#include <cmath>
#include <map>
#include <cstring>

#ifdef __PSL1GHT__
#include "Ps3Pad.h"

namespace Ps3Pad
{
    namespace
    {
        bool g_Init = false;
        padData g_Last[MAX_PORT_NUM];
        bool g_Have[MAX_PORT_NUM] = {};
        Uint32 g_ReadAt = 0;
        padInfo g_Info;
        bool g_InfoOk = false;
    }

    bool State(int port, padData& out)
    {
        if (port < 0 || port >= MAX_PORT_NUM) return false;
        if (!g_Init)
        {
            ioPadInit(7);
            g_Init = true;
            memset(g_Last, 0, sizeof(g_Last));
        }
        // Read at most once per millisecond tick: both readers see the same data.
        const Uint32 now = SDL_GetTicks();
        if (now != g_ReadAt || !g_InfoOk)
        {
            g_ReadAt = now;
            g_InfoOk = ioPadGetInfo(&g_Info) == 0;
            for (int i = 0; g_InfoOk && i < MAX_PORT_NUM; ++i)
            {
                if (!g_Info.status[i]) { g_Have[i] = false; continue; }
                padData d;
                if (ioPadGetData(i, &d) == 0 && d.len > 0) { g_Last[i] = d; g_Have[i] = true; }
            }
        }
        if (!g_InfoOk || !g_Info.status[port] || !g_Have[port]) return false;
        out = g_Last[port];
        return true;
    }
}
#endif

namespace
{
    // Sticks: matches the deadzone the browser bridge applied before calling
    // into C++ (see Build_Release/gamepad-bridge.js).
    const float kStickDeadzone = 0.20f;
    // Analog triggers are reported to the engine as buttons.
    const float kTriggerPressThreshold = 0.50f;

    struct OpenController
    {
        SDL_GameController *pController = nullptr;
        int playerIndex = 0;
        bool nintendoLayout = false;   // Nintendo menus: the right face button confirms
        bool labelLayout = false;      // SDL's A/B/X/Y are the printed labels (not positions)
        float leftX = 0.0f, leftY = 0.0f;
        bool leftTriggerDown = false, rightTriggerDown = false;
    };

    std::map<SDL_JoystickID, OpenController> g_Controllers;
    SDL_JoystickID g_LastUsed = -1;

    // Button-up must reuse the mapping chosen at button-down, otherwise a
    // press that started in a menu (A/B swapped) and was released in gameplay
    // would release the wrong engine button and leave it stuck.
    std::map<std::pair<SDL_JoystickID, int>, GamepadButton> g_PressedMapping;

    bool IsNintendoController(SDL_GameController *pController)
    {
        // Nintendo consoles report their buttons by the printed labels.
        // PlayStation consoles (Vita, PS3) are positional like Xbox pads:
        // cross = bottom = A (confirm / jump), circle = right = B (back / fire).
#if defined(__SWITCH__) || defined(__3DS__) || defined(__WIIU__)
        (void)pController;
        return true;
#elif SDL_VERSION_ATLEAST(2, 0, 12)
        switch (SDL_GameControllerGetType(pController))
        {
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
                return true;
            default:
                return false;
        }
#else
        (void)pController;
        return false;
#endif
    }

    // Does SDL name the face buttons by their labels on this controller?
    // 3DS / Wii U: yes (their SDL mappings follow the labels). Switch:
    // devkitPro's SDL 2.28 maps the console's pad by position like an Xbox
    // pad (a = bottom "B", b = right "A", x = left "Y", y = top "X").
    bool UsesButtonLabels(SDL_GameController *pController, bool nintendo)
    {
#if defined(__SWITCH__)
        (void)pController; (void)nintendo;
        return false;
#elif defined(__3DS__) || defined(__WIIU__)
        (void)pController; (void)nintendo;
        return true;
#else
        (void)pController;
        return nintendo;
#endif
    }

    void OpenDevice(int deviceIndex)
    {
        if (!SDL_IsGameController(deviceIndex))
        {
            return;
        }

        SDL_GameController *pController = SDL_GameControllerOpen(deviceIndex);
        if (!pController)
        {
            LOG_WARNING("Could not open game controller: " + std::string(SDL_GetError()));
            return;
        }

        SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pController));
        if (g_Controllers.count(id))
        {
            // Already open (SDL reports both an initial device and an ADDED event).
            SDL_GameControllerClose(pController);
            return;
        }

        OpenController entry;
        entry.pController = pController;
        entry.playerIndex = (int)g_Controllers.size();
        entry.nintendoLayout = IsNintendoController(pController);
        entry.labelLayout = UsesButtonLabels(pController, entry.nintendoLayout);
        g_Controllers[id] = entry;
        if (g_LastUsed < 0)
        {
            g_LastUsed = id;
        }

        const char *name = SDL_GameControllerName(pController);
        LOG(std::string("Game controller opened: ") + (name ? name : "unknown") +
            (entry.nintendoLayout ? " (Nintendo layout)" : ""));
    }

    bool IsMenuContext()
    {
        if (!g_pApp || !g_pApp->GetGameLogic())
        {
            return true;
        }
        return g_pApp->GetGameLogic()->GetGameState() == GameState_Menu || g_pApp->IsMenuActive();
    }

    // Returns false for SDL buttons the engine has no use for.
    bool MapButton(const OpenController &c, int sdlButton, bool inMenu, GamepadButton &out)
    {
        switch (sdlButton)
        {
            // Face buttons. SDL's A/B/X/Y follow the Xbox layout by position on
            // non-Nintendo pads, and the printed labels on Nintendo pads.
            case SDL_CONTROLLER_BUTTON_A: out = c.labelLayout ? GamepadButton::B : GamepadButton::A; break;
            case SDL_CONTROLLER_BUTTON_B: out = c.labelLayout ? GamepadButton::A : GamepadButton::B; break;
            case SDL_CONTROLLER_BUTTON_X: out = c.labelLayout ? GamepadButton::Y : GamepadButton::X; break;
            case SDL_CONTROLLER_BUTTON_Y: out = c.labelLayout ? GamepadButton::X : GamepadButton::Y; break;

            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  out = GamepadButton::LeftBumper; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: out = GamepadButton::RightBumper; break;
            case SDL_CONTROLLER_BUTTON_BACK:          out = GamepadButton::Back; break;
            case SDL_CONTROLLER_BUTTON_START:         out = GamepadButton::Start; break;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:     out = GamepadButton::LeftStick; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    out = GamepadButton::RightStick; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:       out = GamepadButton::DPadUp; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     out = GamepadButton::DPadDown; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     out = GamepadButton::DPadLeft; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    out = GamepadButton::DPadRight; break;
            case SDL_CONTROLLER_BUTTON_GUIDE:         out = GamepadButton::Guide; break;
            default:
                return false;
        }

        // Nintendo convention in menus: right face button ("A") confirms,
        // bottom face button ("B") goes back.
        if (c.nintendoLayout && inMenu)
        {
            if (out == GamepadButton::A) out = GamepadButton::B;
            else if (out == GamepadButton::B) out = GamepadButton::A;
        }
        return true;
    }

    float NormalizeAxis(Sint16 raw)
    {
        float v = raw < 0 ? raw / 32768.0f : raw / 32767.0f;
        return std::max(-1.0f, std::min(1.0f, v));
    }

    // Radial deadzone, rescaled so movement starts smoothly at its edge.
    void ApplyDeadzone(float &x, float &y)
    {
        float mag = std::sqrt(x * x + y * y);
        if (mag < kStickDeadzone)
        {
            x = y = 0.0f;
            return;
        }
        float scaled = std::min(1.0f, (mag - kStickDeadzone) / (1.0f - kStickDeadzone));
        x = x / mag * scaled;
        y = y / mag * scaled;
    }

    void MarkGamepadActive(SDL_JoystickID id)
    {
        g_LastUsed = id;
        if (g_pApp)
        {
            g_pApp->SetLastInputSource(BaseGameApp::LastInput_Gamepad);
        }
    }
}

namespace SdlGamepadInput
{
    void Initialize()
    {
#ifdef __PSL1GHT__
        // PS3: the pads are read directly (PollDirect), not through SDL.
        return;
#endif
        if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0)
        {
            if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0)
            {
                LOG_WARNING("SDL game controller subsystem unavailable: " + std::string(SDL_GetError()));
                return;
            }
        }
        SDL_GameControllerEventState(SDL_ENABLE);

#if defined(__PSL1GHT__)
        // The PSL1GHT SDL port has no controller mapping of its own (its
        // joystick GUID is the start of the name "PS1LIGHT Controller").
        // Positional like every PlayStation pad: cross = A (confirm / jump),
        // circle = B (back / attack), square = X, triangle = Y.
        SDL_GameControllerAddMapping(
            "5053314c4947485420436f6e74726f6c,PS1LIGHT Controller,"
            "a:b9,b:b10,x:b8,y:b11,back:b7,start:b4,leftstick:b6,rightstick:b5,"
            "leftshoulder:b13,rightshoulder:b12,lefttrigger:b15,righttrigger:b14,"
            "dpleft:b0,dpdown:b1,dpright:b2,dpup:b3,leftx:a0,lefty:a1,rightx:a2,righty:a3,");
#endif

#if defined(__PS4__)
        // PacBrew's PS4 SDL port reports the DualShock 4 as a plain joystick
        // ("Sony DualShock 4 V2"; GUID = start of the name). Positional:
        // cross = A, circle = B; Options = Start (pause), touch pad = Back (map).
        SDL_GameControllerAddMapping(
            "536f6e79204475616c53686f636b2034,Sony DualShock 4 V2,"
            "a:b0,b:b1,x:b2,y:b3,start:b4,guide:b5,back:b6,leftstick:b7,rightstick:b8,"
            "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
            "lefttrigger:a4,righttrigger:a5,leftx:a0,lefty:a1,rightx:a2,righty:a3,");
#endif

        for (int i = 0; i < SDL_NumJoysticks(); ++i)
        {
            OpenDevice(i);
        }
    }

    void Shutdown()
    {
        for (auto &kv : g_Controllers)
        {
            SDL_GameControllerClose(kv.second.pController);
        }
        g_Controllers.clear();
        g_PressedMapping.clear();
        g_LastUsed = -1;
    }

    bool HasController()
    {
#ifdef __PSL1GHT__
        padData d;
        return Ps3Pad::State(0, d);
#endif
        return !g_Controllers.empty();
    }

    bool Translate(const SDL_Event &sdlEvent, AppEvent &out, bool &hasEvent)
    {
        hasEvent = false;

        switch (sdlEvent.type)
        {
            case SDL_CONTROLLERDEVICEADDED:
            {
                OpenDevice(sdlEvent.cdevice.which);
                out = AppEvent::MakeGamepadConnected((int)g_Controllers.size() - 1);
                hasEvent = true;
                return true;
            }

            case SDL_CONTROLLERDEVICEREMOVED:
            {
                auto it = g_Controllers.find(sdlEvent.cdevice.which);
                if (it != g_Controllers.end())
                {
                    out = AppEvent::MakeGamepadDisconnected(it->second.playerIndex);
                    hasEvent = true;
                    SDL_GameControllerClose(it->second.pController);
                    g_Controllers.erase(it);
                }
                if (g_LastUsed == sdlEvent.cdevice.which)
                {
                    g_LastUsed = g_Controllers.empty() ? -1 : g_Controllers.begin()->first;
                }
                return true;
            }

            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
            {
                auto it = g_Controllers.find(sdlEvent.cbutton.which);
                if (it == g_Controllers.end())
                {
                    return true;
                }
                const bool down = sdlEvent.type == SDL_CONTROLLERBUTTONDOWN;
                const auto key = std::make_pair(sdlEvent.cbutton.which, (int)sdlEvent.cbutton.button);

                GamepadButton button;
                if (down)
                {
                    if (!MapButton(it->second, sdlEvent.cbutton.button, IsMenuContext(), button))
                    {
                        return true;
                    }
                    g_PressedMapping[key] = button;
                    MarkGamepadActive(sdlEvent.cbutton.which);
                }
                else
                {
                    auto pressed = g_PressedMapping.find(key);
                    if (pressed == g_PressedMapping.end())
                    {
                        return true;
                    }
                    button = pressed->second;
                    g_PressedMapping.erase(pressed);
                }

                out = AppEvent();
                out.type = down ? AppEventType::GamepadButtonDown : AppEventType::GamepadButtonUp;
                out.gamepadButton.gamepadIndex = it->second.playerIndex;
                out.gamepadButton.button = button;
                out.gamepadButton.value = down ? 1.0f : 0.0f;
                hasEvent = true;
                return true;
            }

            case SDL_CONTROLLERAXISMOTION:
            {
                auto it = g_Controllers.find(sdlEvent.caxis.which);
                if (it == g_Controllers.end())
                {
                    return true;
                }
                OpenController &c = it->second;
                const float value = NormalizeAxis(sdlEvent.caxis.value);

                // Triggers become LeftTrigger/RightTrigger button presses.
                if (sdlEvent.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                    sdlEvent.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                {
                    const bool isLeft = sdlEvent.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
                    bool &state = isLeft ? c.leftTriggerDown : c.rightTriggerDown;
                    const bool nowDown = value >= kTriggerPressThreshold;
                    if (nowDown == state)
                    {
                        return true;
                    }
                    state = nowDown;
                    if (nowDown)
                    {
                        MarkGamepadActive(sdlEvent.caxis.which);
                    }
                    out = AppEvent();
                    out.type = nowDown ? AppEventType::GamepadButtonDown : AppEventType::GamepadButtonUp;
                    out.gamepadButton.gamepadIndex = c.playerIndex;
                    out.gamepadButton.button = isLeft ? GamepadButton::LeftTrigger : GamepadButton::RightTrigger;
                    out.gamepadButton.value = nowDown ? value : 0.0f;
                    hasEvent = true;
                    return true;
                }

                if (sdlEvent.caxis.axis != SDL_CONTROLLER_AXIS_LEFTX &&
                    sdlEvent.caxis.axis != SDL_CONTROLLER_AXIS_LEFTY)
                {
                    return true; // Right stick is unused by the engine.
                }

                const bool isX = sdlEvent.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX;
                float rawX = isX ? value : NormalizeAxis(SDL_GameControllerGetAxis(c.pController, SDL_CONTROLLER_AXIS_LEFTX));
                float rawY = isX ? NormalizeAxis(SDL_GameControllerGetAxis(c.pController, SDL_CONTROLLER_AXIS_LEFTY)) : value;
                ApplyDeadzone(rawX, rawY);

                float &previous = isX ? c.leftX : c.leftY;
                const float current = isX ? rawX : rawY;
                if (current == previous)
                {
                    return true; // Still inside the deadzone, nothing changed.
                }
                previous = current;
                if (current > 0.2f || current < -0.2f)
                {
                    MarkGamepadActive(sdlEvent.caxis.which);
                }

                out = AppEvent();
                out.type = AppEventType::GamepadAxis;
                out.gamepadAxis.gamepadIndex = c.playerIndex;
                out.gamepadAxis.axis = isX ? GamepadAxis::LeftStickX : GamepadAxis::LeftStickY;
                out.gamepadAxis.value = current;
                hasEvent = true;
                return true;
            }

            default:
                return false;
        }
    }

#ifdef __PSL1GHT__
    void PollDirect(void (*emit)(const AppEvent &, void *), void *ctx)
    {
        struct Port { unsigned buttons = 0; float x = 0, y = 0; bool l2 = false, r2 = false; };
        static Port s_Ports[2];
        static std::map<std::pair<int, int>, GamepadButton> s_Pressed;
        OpenController c;   // PlayStation pads: positional (cross = A)
        for (int port = 0; port < 2; ++port)
        {
            padData d;
            if (!Ps3Pad::State(port, d)) continue;
            Port &p = s_Ports[port];
            const struct { unsigned bit; int sdl; } kButtons[] = {
                { (unsigned)d.BTN_CROSS, SDL_CONTROLLER_BUTTON_A },
                { (unsigned)d.BTN_CIRCLE, SDL_CONTROLLER_BUTTON_B },
                { (unsigned)d.BTN_SQUARE, SDL_CONTROLLER_BUTTON_X },
                { (unsigned)d.BTN_TRIANGLE, SDL_CONTROLLER_BUTTON_Y },
                { (unsigned)d.BTN_L1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
                { (unsigned)d.BTN_R1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
                { (unsigned)d.BTN_SELECT, SDL_CONTROLLER_BUTTON_BACK },
                { (unsigned)d.BTN_START, SDL_CONTROLLER_BUTTON_START },
                { (unsigned)d.BTN_L3, SDL_CONTROLLER_BUTTON_LEFTSTICK },
                { (unsigned)d.BTN_R3, SDL_CONTROLLER_BUTTON_RIGHTSTICK },
                { (unsigned)d.BTN_UP, SDL_CONTROLLER_BUTTON_DPAD_UP },
                { (unsigned)d.BTN_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN },
                { (unsigned)d.BTN_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT },
                { (unsigned)d.BTN_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
            };
            c.playerIndex = port;
            unsigned now = 0;
            for (int i = 0; i < (int)(sizeof(kButtons) / sizeof(kButtons[0])); ++i)
            {
                if (kButtons[i].bit) now |= 1u << i;
                const bool was = (p.buttons >> i) & 1u, is = kButtons[i].bit != 0;
                if (was == is) continue;
                const auto key = std::make_pair(port, kButtons[i].sdl);
                GamepadButton button;
                if (is)
                {
                    if (!MapButton(c, kButtons[i].sdl, IsMenuContext(), button)) continue;
                    s_Pressed[key] = button;
                    MarkGamepadActive(-(port + 2));
                }
                else
                {
                    auto it = s_Pressed.find(key);
                    if (it == s_Pressed.end()) continue;
                    button = it->second;
                    s_Pressed.erase(it);
                }
                AppEvent e;
                e.type = is ? AppEventType::GamepadButtonDown : AppEventType::GamepadButtonUp;
                e.gamepadButton.gamepadIndex = port;
                e.gamepadButton.button = button;
                e.gamepadButton.value = is ? 1.0f : 0.0f;
                emit(e, ctx);
            }
            p.buttons = now;

            const bool triggers[2] = { d.BTN_L2 != 0, d.BTN_R2 != 0 };
            bool *states[2] = { &p.l2, &p.r2 };
            for (int t = 0; t < 2; ++t)
            {
                if (triggers[t] == *states[t]) continue;
                *states[t] = triggers[t];
                if (triggers[t]) MarkGamepadActive(-(port + 2));
                AppEvent e;
                e.type = triggers[t] ? AppEventType::GamepadButtonDown : AppEventType::GamepadButtonUp;
                e.gamepadButton.gamepadIndex = port;
                e.gamepadButton.button = t == 0 ? GamepadButton::LeftTrigger : GamepadButton::RightTrigger;
                e.gamepadButton.value = triggers[t] ? 1.0f : 0.0f;
                emit(e, ctx);
            }

            // Left stick (0..255, 128 in the middle).
            float x = ((int)d.ANA_L_H - 128) / 127.0f, y = ((int)d.ANA_L_V - 128) / 127.0f;
            x = std::max(-1.0f, std::min(1.0f, x));
            y = std::max(-1.0f, std::min(1.0f, y));
            ApplyDeadzone(x, y);
            const float values[2] = { x, y };
            float *prev[2] = { &p.x, &p.y };
            for (int a = 0; a < 2; ++a)
            {
                if (values[a] == *prev[a]) continue;
                *prev[a] = values[a];
                if (values[a] > 0.2f || values[a] < -0.2f) MarkGamepadActive(-(port + 2));
                AppEvent e;
                e.type = AppEventType::GamepadAxis;
                e.gamepadAxis.gamepadIndex = port;
                e.gamepadAxis.axis = a == 0 ? GamepadAxis::LeftStickX : GamepadAxis::LeftStickY;
                e.gamepadAxis.value = values[a];
                emit(e, ctx);
            }
        }
    }
#endif

    void Rumble(int durationMs, float weakMagnitude, float strongMagnitude)
    {
#if SDL_VERSION_ATLEAST(2, 0, 9)
        auto it = g_Controllers.find(g_LastUsed);
        if (it == g_Controllers.end())
        {
            return;
        }
        auto toU16 = [](float m) {
            m = std::max(0.0f, std::min(1.0f, m));
            return (Uint16)(m * 0xFFFF);
        };
        // SDL's low-frequency motor is the "strong" one.
        SDL_GameControllerRumble(it->second.pController, toU16(strongMagnitude), toU16(weakMagnitude),
                                 (Uint32)std::max(0, durationMs));
#else
        (void)durationMs; (void)weakMagnitude; (void)strongMagnitude;
#endif
    }
}

#endif // !__EMSCRIPTEN__
