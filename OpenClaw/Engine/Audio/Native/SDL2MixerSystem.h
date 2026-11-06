#pragma once

// Native IAudioSystem implementation (desktop Linux, Nintendo Switch).
//
// Sound effects:  SDL_mixer channels, same model as upstream OpenClaw
//                 (channels 0-15 reserved as group 1 for local ambient sounds).
// Level music:    the game's XMIDI tracks, converted to standard MIDI by libwap,
//                 rendered with TinySoundFont through Mix_HookMusic using the
//                 same GeneralUser GS soundfont the web build ships. This keeps
//                 music independent of whether the platform's SDL_mixer was
//                 built with a MIDI backend (devkitPro's Switch build is not
//                 guaranteed to have one).
// WAV music:      e.g. the main menu's MENUBED.WAV — plain Mix_PlayMusic.

#ifndef __EMSCRIPTEN__

#include "../IAudioSystem.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <map>
#include <string>
#include <vector>

struct tsf;
struct tml_message;

struct SDL2MixerConfig
{
    int frequency = 44100;
    int outputChannels = 2;
    int chunkSize = 2048;
    int mixingChannels = 24;
    // Candidate soundfont locations, tried in order.
    std::vector<std::string> soundFontPaths;
};

class SDL2MixerSystem final : public IAudioSystem
{
public:
    explicit SDL2MixerSystem(const SDL2MixerConfig &config);
    ~SDL2MixerSystem() override;

    bool Initialize() override;
    void Shutdown() override;

    // Sound effects (by name, decoded from WAV file bytes)
    bool LoadSound(const std::string &name, const char *data, size_t size) override;
    bool PlaySound(const std::string &name, float volume = 1.0f) override;
    bool PlaySoundWithPath(const std::string &originalPath, const char *data, size_t size,
                           float volume = 1.0f, int loops = 0) override;
    void StopSound(const std::string &name) override;
    void StopAllSounds() override;

    // Music: data is either a standard MIDI file ("MThd") or anything
    // SDL_mixer can stream (WAV/OGG).
    bool LoadMusic(const std::string &name, const char *data, size_t size) override;
    bool PlayMusic(const std::string &name, bool looping = false) override;
    void StopMusic() override;
    void PauseMusic() override;
    void ResumeMusic() override;

    // Native-only: play an already decoded chunk (e.g. MENUBED.WAV) as music
    // on a dedicated channel. The caller keeps the chunk alive while it plays.
    bool PlayMusicChunk(Mix_Chunk *pChunk, bool looping);

    void SetSoundVolume(float volume) override;
    void SetMusicVolume(float volume) override;
    float GetSoundVolume() const override { return m_SoundVolume; }
    float GetMusicVolume() const override { return m_MusicVolume; }

    void SetSoundEnabled(bool enabled) override;
    void SetMusicEnabled(bool enabled) override;
    bool IsSoundEnabled() const override { return m_SoundEnabled; }
    bool IsMusicEnabled() const override { return m_MusicEnabled; }

    bool IsInitialized() const override { return m_Initialized; }
    bool IsMusicPlaying() const override;

    int OutputFrequency() const { return m_OutputFrequency; }
    int OutputChannels() const { return m_OutputChannels; }

private:
    static void MusicHook(void *userData, Uint8 *stream, int len);
    static int SoundFontLoaderThread(void *userData);

    void RenderMidi(Uint8 *stream, int len);
    void ApplyMusicVolume();
    void StopMusicLocked();
    void ResetSynthChannels();
    void HaltEffectChannels();

    SDL2MixerConfig m_Config;
    bool m_Initialized = false;

    int m_OutputFrequency = 44100;
    int m_OutputChannels = 2;

    float m_SoundVolume = 1.0f;
    float m_MusicVolume = 1.0f;
    bool m_SoundEnabled = true;
    bool m_MusicEnabled = true;

    std::map<std::string, Mix_Chunk *> m_Sounds;
    std::map<std::string, std::vector<char>> m_MusicData;

    // --- Chunk music on the dedicated music channel ---
    bool m_ChunkMusicActive = false;

    // --- Streamed (WAV/OGG) music ---
    Mix_Music *m_pStreamMusic = nullptr;
    std::vector<char> m_StreamMusicBytes; // must outlive m_pStreamMusic

    // --- MIDI synth (guarded by m_pSynthMutex; touched from the audio thread) ---
    SDL_mutex *m_pSynthMutex = nullptr;
    SDL_Thread *m_pLoaderThread = nullptr;
    tsf *m_pSoundFont = nullptr;
    bool m_SoundFontReady = false;   // loader finished (successfully or not)
    tml_message *m_pMidi = nullptr;  // whole song
    tml_message *m_pMidiCursor = nullptr;
    double m_MidiTimeMs = 0.0;
    bool m_MidiLooping = false;
    bool m_MidiPlaying = false;      // a song is selected and not stopped
    bool m_MidiPaused = false;
    bool m_HookInstalled = false;
};

#endif // !__EMSCRIPTEN__
