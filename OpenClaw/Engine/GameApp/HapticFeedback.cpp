#include "HapticFeedback.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include "../Platform/Input/SdlGamepadInput.h"
#endif

bool HapticFeedback::s_Enabled = true;

void HapticFeedback::Trigger(HapticPreset preset, int gamepadIndex)
{
#ifdef __EMSCRIPTEN__
    if (!s_Enabled) return;

    const char* presetName = nullptr;
    switch (preset)
    {
        case HapticPreset::Light:     presetName = "light"; break;
        case HapticPreset::Medium:    presetName = "medium"; break;
        case HapticPreset::Heavy:     presetName = "heavy"; break;
        case HapticPreset::Damage:    presetName = "damage"; break;
        case HapticPreset::Death:     presetName = "death"; break;
        case HapticPreset::Explosion: presetName = "explosion"; break;
        case HapticPreset::Pickup:    presetName = "pickup"; break;
        case HapticPreset::Attack:    presetName = "attack"; break;
        case HapticPreset::Jump:      presetName = "jump"; break;
        case HapticPreset::Step:      presetName = "step"; break;
        default: return;
    }

    EM_ASM({
        if (typeof triggerHaptic === 'function') {
            triggerHaptic($0, UTF8ToString($1));
        }
    }, gamepadIndex, presetName);
#else
    // Same preset table as Build_Release/gamepad-bridge.js (HAPTIC_PRESETS).
    struct Preset { int durationMs; float weak; float strong; };
    Preset p;
    switch (preset)
    {
        case HapticPreset::Light:     p = {110, 0.6f, 0.6f}; break;
        case HapticPreset::Medium:    p = {130, 0.6f, 0.75f}; break;
        case HapticPreset::Heavy:     p = {180, 0.9f, 0.95f}; break;
        case HapticPreset::Damage:    p = {240, 0.7f, 1.0f}; break;
        case HapticPreset::Death:     p = {1000, 1.0f, 1.0f}; break;
        case HapticPreset::Explosion: p = {340, 1.0f, 1.0f}; break;
        case HapticPreset::Pickup:    p = {120, 0.6f, 0.65f}; break;
        case HapticPreset::Attack:    p = {130, 0.6f, 0.85f}; break;
        case HapticPreset::Jump:      p = {120, 0.6f, 0.7f}; break;
        case HapticPreset::Step:      p = {110, 0.6f, 0.6f}; break;
        default: return;
    }
    TriggerCustom(p.durationMs, p.weak, p.strong, gamepadIndex);
#endif
}

void HapticFeedback::TriggerCustom(int durationMs, float weakMagnitude, float strongMagnitude, int gamepadIndex)
{
#ifdef __EMSCRIPTEN__
    if (!s_Enabled) return;

    EM_ASM({
        if (typeof triggerHapticCustom === 'function') {
            triggerHapticCustom($0, $1, $2, $3);
        }
    }, gamepadIndex, durationMs, weakMagnitude, strongMagnitude);
#else
    (void)gamepadIndex;
    if (!s_Enabled) return;
    SdlGamepadInput::Rumble(durationMs, weakMagnitude, strongMagnitude);
#endif
}

void HapticFeedback::SetEnabled(bool enabled)
{
    s_Enabled = enabled;

#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (typeof setHapticEnabled === 'function') {
            setHapticEnabled($0);
        }
    }, enabled ? 1 : 0);
#endif
}

bool HapticFeedback::IsEnabled()
{
    return s_Enabled;
}
