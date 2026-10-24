#ifndef __EMSCRIPTEN__

#include "SdlGamepadInput.h"

#include "../../GameApp/BaseGameApp.h"
#include "../../GameApp/BaseGameLogic.h"
#include "../Events/AppEvent.h"

#include <cmath>
#include <map>

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
        bool nintendoLayout = false;
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
#ifdef __SWITCH__
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
            case SDL_CONTROLLER_BUTTON_A: out = c.nintendoLayout ? GamepadButton::B : GamepadButton::A; break;
            case SDL_CONTROLLER_BUTTON_B: out = c.nintendoLayout ? GamepadButton::A : GamepadButton::B; break;
            case SDL_CONTROLLER_BUTTON_X: out = c.nintendoLayout ? GamepadButton::Y : GamepadButton::X; break;
            case SDL_CONTROLLER_BUTTON_Y: out = c.nintendoLayout ? GamepadButton::X : GamepadButton::Y; break;

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
        if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0)
        {
            if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0)
            {
                LOG_WARNING("SDL game controller subsystem unavailable: " + std::string(SDL_GetError()));
                return;
            }
        }
        SDL_GameControllerEventState(SDL_ENABLE);

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
