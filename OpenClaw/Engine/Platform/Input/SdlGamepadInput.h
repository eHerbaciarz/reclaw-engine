#pragma once

// Native gamepad input for non-browser builds (desktop Linux, Nintendo Switch).
//
// In the browser build, gamepad-bridge.js reads the W3C Gamepad API and feeds
// AppEvents into the engine. Natively there is no JavaScript, so this module
// does the same job from SDL_GameController events: it opens controllers,
// converts buttons/sticks to the W3C "standard" layout that HumanView and
// ActorController already understand, and drives rumble for HapticFeedback.
//
// Button layout:
//   The engine's gamepad mapping is positional (W3C): bottom face button =
//   GamepadButton::A (jump / confirm), right = B (fire / back), left = X
//   (sword), top = Y. Nintendo controllers report buttons by their printed
//   label, so they are remapped here: physical bottom (Nintendo "B") becomes
//   W3C A, and so on. Gameplay therefore feels the same on every pad.
//   In menus, Nintendo convention (right "A" confirms, bottom "B" goes back)
//   is kept by swapping A/B while a menu is active.

#include <SDL2/SDL.h>
#include <cstdint>

struct AppEvent;

namespace SdlGamepadInput
{
    // Opens already-connected controllers. Call after SDL_Init.
    void Initialize();
    void Shutdown();

    // Translates an SDL controller event into an AppEvent. Returns true when
    // the SDL event was a controller event (consumed) — in that case
    // `outEvent` is valid only if `outHasEvent` is set (e.g. a stick motion
    // inside the deadzone that did not change state produces no AppEvent).
    bool Translate(const SDL_Event &sdlEvent, AppEvent &outEvent, bool &outHasEvent);

    // Rumble on the most recently used controller.
    void Rumble(int durationMs, float weakMagnitude, float strongMagnitude);

#ifdef __PSL1GHT__
    // PS3: the pads' changes as AppEvents (once per frame, from the event loop).
    void PollDirect(void (*emit)(const AppEvent &, void *), void *ctx);
#endif

    // True once any controller has been opened.
    bool HasController();
}
