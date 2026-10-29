#pragma once

// Nintendo 3DS stereoscopic 3D (the 3D slider). Each frame of the level is
// drawn twice, once per eye; every layer is moved sideways by its depth:
// background planes sink into the screen, the HUD comes out of it, the action
// plane (Claw, the enemies) stays on the screen surface. Other platforms: off.

struct SDL_Renderer;
struct SDL_Window;

namespace Stereo3D
{
    // Reads the slider; true when this frame is drawn in 3D (call once per
    // frame, before drawing; only in levels, not in the menus).
    bool BeginFrame(bool inLevel);

    // Eye being drawn: -1 left, +1 right, 0 no 3D.
    int Eye();
    void SetEye(int eye);

    // Sideways move in pixels for a layer at depth (positive: behind the
    // screen, negative: in front of it; about -1..1).
    int Offset(float depth);

    // After the right eye is drawn: copies it to the right-eye screen.
    void StoreRightEye(SDL_Window* pWindow);
}
