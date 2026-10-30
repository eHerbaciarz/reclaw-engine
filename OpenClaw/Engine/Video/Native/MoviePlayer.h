#pragma once

// Native cutscene player for the original Smacker (.SMK) movies.
//
// The Captain Claw CD keeps its movies in MOVIES/CLAW.FEC: a container holding
// ten Smacker files back to back. They are read straight from that file (or
// from loose .SMK files in a MOVIES folder) - nothing is bundled with the port.
//
// Remastered movies replace them one by one: <ReClaw folder>/movies/movie_XX.mp4
// (H.264 + AAC, builds with FFmpeg) or movie_XX.mpg (MPEG-1 + MP2, every
// platform but Vita / 3DS); see docs/FILMY.md.
//
// Movies are referred to by their index inside CLAW.FEC (0-9, in file order).
// Which movie plays when comes from a built-in table that can be overridden
// with movies.xml in the user directory (see MoviePlayer.cpp).
//
// Playback is blocking: the call returns when the movie ends or the player
// skips it. A / Enter / Space / a tap skips the current movie; B / + / Escape
// also skips the rest of a sequence (startup movies, replay list).

#include <string>
#include <vector>

struct SDL_Renderer;

namespace MoviePlayer
{
    // Folders where CLAW.FEC or a MOVIES folder may be. Call once at startup.
    void SetSearchRoots(const std::vector<std::string>& roots, const std::string& userDirectory);

    // True when the movie files were found.
    bool IsAvailable();

    // Startup sequence (logo / intro).
    void PlayStartupMovies(SDL_Renderer* pRenderer);

    // Movie(s) shown after finishing `levelNumber`, before the next level loads.
    void PlayAfterLevel(SDL_Renderer* pRenderer, int levelNumber);

    // "Replay movies" menu entry: startup movies plus the story movies of all
    // chapters up to and including `lastFinishedLevel`.
    void PlayReplay(SDL_Renderer* pRenderer, int lastFinishedLevel);

    // Plays one movie by index; returns false if it is not available.
    bool PlayMovie(SDL_Renderer* pRenderer, int index);

    int GetMovieCount();

    // Shared with other blocking screens (title scroll): 0 = nothing,
    // 1 = skip (A / Enter / tap), 2 = abort (B / + / Escape).
    int PollSkipInput();
    // Drops input that arrived during a blocking screen; keeps device events.
    void FlushSkipInput();
}
