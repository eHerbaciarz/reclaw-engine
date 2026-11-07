#pragma once
// ReClaw: what each console port turns on, in one place (instead of the same
// list of platforms repeated through the engine). Included by SharedDefines.h.

// Actors that only animate or wait to be touched (scenery, pickups) are not
// updated, and their physics bodies switched off, when far from the screen.
#if defined(__vita__) || defined(__3DS__) || defined(__PSL1GHT__)
#define RECLAW_PAUSE_FAR_SCENERY 1
#endif

// Ordinary enemies far from the screen wait too (bodies switched off); the
// "far" margin is half a screen instead of a whole one (the CPU is the limit).
#if defined(__3DS__) || defined(__PSL1GHT__)
#define RECLAW_PAUSE_FAR_ENEMIES 1
#endif

// Own WAV decoder (SDL's resampler took 15-225 ms a sound on these CPUs),
// decoded sizes estimated from the header, slow file loads logged.
#if defined(__3DS__) || defined(__PSL1GHT__)
#define RECLAW_FAST_WAV 1
#endif

// Box2D with its recommended solver iterations (8 / 3 instead of 10 / 8).
#if defined(__vita__) || defined(__3DS__) || defined(__PSL1GHT__)
#define RECLAW_LIGHT_PHYSICS 1
#endif

// Smoothing and view zoom through SDL's own (GPU) renderer; the 3DS and PS3
// do them in their GPU layers (RECLAW_GPU).
#if defined(__SWITCH__) || defined(__vita__)
#define RECLAW_SDL_SMOOTH 1
#endif
