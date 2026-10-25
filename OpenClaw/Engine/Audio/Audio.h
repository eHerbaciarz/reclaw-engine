#ifndef AUDIO_H_
#define AUDIO_H_

// The old Windows build played MIDI music through an RPC server (MidiProc);
// native builds (incl. MinGW) use SDL_mixer + TinySoundFont everywhere.
#if defined(_WIN32) && !defined(OPENCLAW_NATIVE)
#define OPENCLAW_MIDI_RPC 1
#endif

#include <stdint.h>
#include <SDL2/SDL_mixer.h>
#include <memory>

#include "../GameApp/BaseGameApp.h"
#include "IAudioSystem.h"

class Audio
{
public:
    Audio();
    ~Audio();

    bool Initialize(const GameOptions& config);
    void Terminate();

    bool PlaySound(const char* soundData, size_t soundSize, const SoundProperties& soundProperties);
    bool PlaySound(Mix_Chunk* sound, const SoundProperties& soundProperties);
    void SetSoundVolume(int volumePercentage); 

    void PlayMusic(const char* musicData, size_t musicSize, bool looping);
    // Native builds: play a decoded WAV (e.g. MENUBED.WAV) as background music.
    // Audio keeps a reference so the resource cache cannot free it mid-song.
    void PlayMusicChunk(std::shared_ptr<Mix_Chunk> pChunk, bool looping);
    void PauseMusic();
    void ResumeMusic();
    void StopMusic();
    void SetMusicVolume(int volumePercentage);

    void StopAllSounds();

    void PauseAllSounds();
    void ResumeAllSounds();

    bool IsSoundActive() { return m_bSoundOn; }
    bool IsMusicActive() { return m_bMusicOn; }

    void SetSoundActive(bool active);
    void SetMusicActive(bool active);

    int GetSoundVolume();
    int GetMusicVolume();
    
    // Audio system access
    IAudioSystem* GetAudioSystem() { return m_audioSystem.get(); }

private:
    //##### Methods #####//
    bool InitializeMidiRPC(const std::string& midiRpcServerPath);
    bool InitializeMidiRPCServer(const std::string& midiRpcServerPath);
    bool InitializeMidiRPCClient();
    bool IsRPCServerListening();

    void TerminateMidiRPC();

    //##### Members #####//
    bool m_bIsServerInitialized;
    bool m_bIsClientInitialized;
    bool m_bIsMidiRpcInitialized;
    bool m_bIsAudioInitialized;
    unsigned char* m_RpcBindingString;
    int m_SoundVolume;
    int m_MusicVolume;
    bool m_bSoundOn;
    bool m_bMusicOn;
    
    // Modular audio system
    std::unique_ptr<IAudioSystem> m_audioSystem;

    std::shared_ptr<Mix_Chunk> m_pMusicChunk;
};

#endif