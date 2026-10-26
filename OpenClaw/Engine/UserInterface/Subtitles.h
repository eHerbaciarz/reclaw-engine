#pragma once

// Subtitles at the bottom of the screen, like in the movies.
//
//  - Text from the game itself (e.g. the message of the cheat code): always
//    shown, whatever the subtitles option says.
//  - SRT tracks for the movies and for spoken lines (sounds): shown only when
//    subtitles are switched on (Options, saved in port_settings.txt).
//
// Where tracks are looked for (first match wins, {lang} = pl / en):
//   <data folder>/subtitles/<name>_{lang}.srt, <data folder>/subtitles/<name>.srt
//   the same in the folder of CLAW.REZ
//   ASSETS.ZIP /SUBTITLES/<name>_{lang}.srt,   ASSETS.ZIP /SUBTITLES/<name>.srt
// Names (lower case):
//   movies:  movie_00 ... movie_09 (order of the movies in CLAW.FEC)
//   sounds:  sounds/<path of the sound in CLAW.REZ without extension>,
//            e.g. sounds/level2/sounds/raux/le000001 for /LEVEL2/SOUNDS/RAUX/LE000001.WAV
// A sound's track starts when the sound starts playing.

#include <string>
#include <SDL2/SDL.h>

namespace Subtitles
{
    bool IsEnabled();
    void SetEnabled(bool enabled);

    // Text shown for `ms` milliseconds regardless of the option.
    void ShowText(const std::string& text, uint32_t ms);

    // Starts the SRT track `name` (only when subtitles are on and the track
    // exists). Returns true when a track was started.
    bool StartTrack(const std::string& name);
    void StopTrack();

    // Called for every sound effect / voice that starts playing.
    void OnSoundPlayed(const std::string& soundPath);

    // Draws the current text over whatever is on the render target.
    void Render(SDL_Renderer* pRenderer);

    // Forget cached textures (the renderer changes / shuts down).
    void FreeTextures();
}
