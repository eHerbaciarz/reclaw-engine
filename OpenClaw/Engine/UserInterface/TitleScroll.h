#pragma once

// The original game's title screen: a parchment map with the "Captain Claw"
// title (STATES/ATTRACT/SCREENS/TITLE.PCX) unrolls from the middle to both
// sides, stays for a moment, rolls up again and the main menu appears.
//
// The rolls are drawn procedurally from the paper texture of the original
// (STATES/ROLLER/SCREENS/PAPER.PCX) with cylinder shading; sounds are the
// original UNROLL / TITLE / ROLLUP effects. Everything comes from the game's
// own data - if any of it is missing the screen is simply skipped.
//
// Blocking; A skips to the rolled-up phase, B / + skips it entirely.

struct SDL_Renderer;

namespace TitleScroll
{
    void Play(SDL_Renderer* pRenderer);

    // The main menu unrolling after the title (drawn over each frame of the
    // menu until it is open; UNROLL sound).
    void StartMenuUnroll();
    void RenderMenuUnroll(SDL_Renderer* pRenderer);
}
