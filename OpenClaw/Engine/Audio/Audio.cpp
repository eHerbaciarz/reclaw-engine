#include <SDL2/SDL.h>
#include <fstream>
#include <vector>

#include "Audio.h"
#include "../Platform/Platform.h"
#include "../Events/EventMgr.h"
#include "../Events/Events.h"
#include "AudioSystemFactory.h"
#ifndef __EMSCRIPTEN__
#include "Native/SDL2MixerSystem.h"
#endif

#include <iostream>

#ifdef OPENCLAW_MIDI_RPC
#include <windows.h>
#include "../../../MidiProc/midiproc.h"
#endif

#ifdef PlaySound
#undef PlaySound
#endif

using namespace std;

const uint32_t MIDI_RPC_MAX_HANDSHAKE_TRIES = 250;

//############################################
//################# API ######################
//############################################

Audio::Audio()
    :
    m_bIsServerInitialized(false),
    m_bIsClientInitialized(false),
    m_bIsMidiRpcInitialized(false),
    m_bIsAudioInitialized(false),
    m_RpcBindingString(NULL),
    m_SoundVolume(0),
    m_MusicVolume(0),
    m_bSoundOn(true),
    m_bMusicOn(true)
{

}

Audio::~Audio()
{
    Terminate();
}

bool Audio::Initialize(const GameOptions& config)
{
#ifdef __EMSCRIPTEN__
    // Create the appropriate audio system for the current platform
    auto audioSystemType = AudioSystemFactory::GetRecommendedAudioSystemType();
    m_audioSystem = AudioSystemFactory::CreateAudioSystem(audioSystemType);
#else
    // Native: SDL_mixer for effects, TinySoundFont for XMI level music.
    SDL2MixerConfig mixerConfig;
    mixerConfig.frequency = (int)config.frequency;
#if defined(__vita__) || defined(__3DS__)
    // Handhelds mix at 22 kHz (the game's own sounds are 22 kHz): half the
    // work for the mixer and the music synth, no crackling in busy levels.
    // Also for configs written by older versions (44100).
    mixerConfig.frequency = std::min(mixerConfig.frequency, 22050);
#endif
    mixerConfig.outputChannels = (int)config.soundChannels;
    mixerConfig.chunkSize = (int)config.chunkSize;
    mixerConfig.mixingChannels = (int)config.mixingChannels;
    mixerConfig.soundFontPaths = {
        config.assetsFolder + "music/soundfont.sf3",
        "music/soundfont.sf3",
        Platform::BundlePath("music/soundfont.sf3"),
    };
    m_audioSystem.reset(new SDL2MixerSystem(mixerConfig));
#endif
    
    if (!m_audioSystem) {
        LOG_ERROR("Failed to create audio system");
        return false;
    }
    
    // Initialize the audio system
    if (!m_audioSystem->Initialize()) {
        LOG_ERROR("Failed to initialize audio system");
        return false;
    }

    // Ensure volumes are within valid range (0-100)
    m_SoundVolume = max(0, min(100, config.soundVolume));
    m_MusicVolume = max(0, min(100, config.musicVolume));
    m_bSoundOn = config.soundOn;
    m_bMusicOn = config.musicOn;

    // Convert 0-100 range to 0.0-1.0 for audio system
    float soundVol = static_cast<float>(m_SoundVolume) / 100.0f;
    float musicVol = static_cast<float>(m_MusicVolume) / 100.0f;
    
    m_audioSystem->SetSoundVolume(soundVol);
    m_audioSystem->SetMusicVolume(musicVol);
    m_audioSystem->SetSoundEnabled(m_bSoundOn);
    m_audioSystem->SetMusicEnabled(m_bMusicOn);

    m_bIsAudioInitialized = true;
    return true;
}

void Audio::Terminate()
{
#ifdef OPENCLAW_MIDI_RPC
    TerminateMidiRPC();
#endif //OPENCLAW_MIDI_RPC
}

struct _MusicInfo
{
    _MusicInfo(const char* pData, size_t size, bool isLooping, int volume)
    {
        pMusicData = pData;
        musicSize = size;
        looping = isLooping;
        musicVolume = volume;
    }

    const char* pMusicData;
    size_t musicSize;
    bool looping;
    int musicVolume;
};

static int SetupPlayMusicThread(void* pData)
{
    _MusicInfo* pMusicInfo = (_MusicInfo*)pData;

#ifdef OPENCLAW_MIDI_RPC
    RpcTryExcept
    {
        MidiRPC_PrepareNewSong();
        MidiRPC_AddChunk(pMusicInfo->musicSize, (byte*)pMusicInfo->pMusicData);
        MidiRPC_PlaySong(pMusicInfo->looping);

        if (pMusicInfo->musicVolume == -1)
        {
            MidiRPC_PauseSong();
        }
        else
        {
            MidiRPC_ResumeSong();
            MidiRPC_ChangeVolume(pMusicInfo->musicVolume);
        }
    }
        RpcExcept(1)
    {
        //__LOG_ERROR("Audio::SetMusicVolume: Failed due to RPC exception");
    }
    RpcEndExcept;
#else
    SDL_RWops* pRWops = SDL_RWFromMem((void*)pMusicInfo->pMusicData, pMusicInfo->musicSize);
    Mix_Music* pMusic = Mix_LoadMUS_RW(pRWops, 0);
    if (!pMusic) {
        LOG_ERROR("Mix_LoadMUS_RW: " + std::string(Mix_GetError()));
    }
    Mix_PlayMusic(pMusic, pMusicInfo->looping ? -1 : 0);

    if (pMusicInfo->musicVolume == -1)
    {
        Mix_PauseMusic();
    }
    else
    {
        Mix_ResumeMusic();
    }
#endif //OPENCLAW_MIDI_RPC

    SAFE_DELETE(pMusicInfo);

    return 0;
}

void Audio::PlayMusic(const char* musicData, size_t musicSize, bool looping)
{
#if !defined(__EMSCRIPTEN__) && !defined(OPENCLAW_MIDI_RPC)
    if (m_audioSystem && m_audioSystem->LoadMusic("music", musicData, musicSize))
    {
        m_audioSystem->PlayMusic("music", looping);
    }
    return;
#endif

    _MusicInfo* pMusicInfo = new _MusicInfo(musicData, musicSize, looping, m_bMusicOn ? m_MusicVolume : -1);

// Playing music track takes ALOT of time for some reason so play it in another thread
#ifdef __EMSCRIPTEN__
    SetupPlayMusicThread((void*)pMusicInfo);
#else
    SDL_Thread* pThread = SDL_CreateThread(SetupPlayMusicThread, "SetupPlayMusicThread", (void*)pMusicInfo);
    SDL_DetachThread(pThread);
#endif
}

void Audio::PlayMusicChunk(std::shared_ptr<Mix_Chunk> pChunk, bool looping)
{
#if !defined(__EMSCRIPTEN__) && !defined(OPENCLAW_MIDI_RPC)
    SDL2MixerSystem* pMixer = static_cast<SDL2MixerSystem*>(m_audioSystem.get());
    if (!pMixer || !pChunk) {
        return;
    }
    // Stop first so the previous chunk is no longer referenced by SDL_mixer
    // before our reference to it is dropped.
    pMixer->StopMusic();
    m_pMusicChunk = pChunk;
    pMixer->PlayMusicChunk(m_pMusicChunk.get(), looping);
#else
    (void)pChunk; (void)looping;
#endif
}

void Audio::PauseMusic()
{
#ifdef OPENCLAW_MIDI_RPC
    RpcTryExcept
    {
        MidiRPC_PauseSong();
    }
        RpcExcept(1)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Audio::PauseMusic: Failed due to RPC exception");
    }
    RpcEndExcept
#elif defined(__EMSCRIPTEN__)
    Mix_PauseMusic();
#else
    if (m_audioSystem) {
        m_audioSystem->PauseMusic();
    }
#endif //OPENCLAW_MIDI_RPC
}

void Audio::ResumeMusic()
{
#ifdef OPENCLAW_MIDI_RPC
    RpcTryExcept
    {
        MidiRPC_ResumeSong();
        MidiRPC_ChangeVolume(m_MusicVolume);
    }
        RpcExcept(1)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Audio::ResumeMusic: Failed due to RPC exception");
    }
    RpcEndExcept
#elif defined(__EMSCRIPTEN__)
    Mix_ResumeMusic();
#else
    if (m_audioSystem) {
        m_audioSystem->ResumeMusic();
    }
#endif //OPENCLAW_MIDI_RPC
}

void Audio::StopMusic()
{
    // On WASM the active music plays through the Web Audio system (window.musicSource),
    // not SDL_mixer. Route the stop through the audio system so that source is torn down.
    if (m_audioSystem) {
        m_audioSystem->StopMusic();
    }

#ifdef OPENCLAW_MIDI_RPC
    RpcTryExcept
    {
        MidiRPC_StopSong();
    }
        RpcExcept(1)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AudioMgr::StopMusic: Failed due to RPC exception");
    }
    RpcEndExcept
#elif defined(__EMSCRIPTEN__)
    Mix_HaltMusic();
#endif //OPENCLAW_MIDI_RPC
}

void Audio::SetMusicVolume(int volumePercentage)
{
    // Ensure volume is within valid range (0-100)
    volumePercentage = max(0, min(100, volumePercentage));
    m_MusicVolume = volumePercentage;

    // Convert 0-100 range to 0.0-1.0 for audio system
    float volume = static_cast<float>(volumePercentage) / 100.0f;
    if (m_audioSystem) {
        m_audioSystem->SetMusicVolume(volume);
    }
}

int Audio::GetMusicVolume()
{
    return m_MusicVolume; // Already in 0-100 range
}

bool Audio::PlaySound(const char* soundData, size_t soundSize, const SoundProperties& soundProperties)
{
    SDL_RWops* soundRwOps = SDL_RWFromMem((void*)soundData, soundSize);
    Mix_Chunk* soundChunk = Mix_LoadWAV_RW(soundRwOps, 1);

    bool played = PlaySound(soundChunk, soundProperties);
#if !defined(__EMSCRIPTEN__) && !defined(OPENCLAW_MIDI_RPC)
    // This one-shot chunk is not owned by the resource cache; at least free it
    // when it could not be played. (Nothing in the game uses this overload.)
    if (soundChunk && !played) {
        Mix_FreeChunk(soundChunk);
    }
#endif
    return played;
}
bool Audio::PlaySound(Mix_Chunk* sound, const SoundProperties& soundProperties)
{
    if (!m_bSoundOn) {
        return false;
    }

#ifdef __EMSCRIPTEN__
    // Emscripten/SDL2 Mixer direct path
    if (sound) {
        // Calculate volume (0-128 for SDL_mixer)
        int volume = (int)((soundProperties.volume / 100.0f) * (m_SoundVolume / 100.0f) * MIX_MAX_VOLUME);
        Mix_VolumeChunk(sound, volume);
        
        std::cout << "Audio::PlaySound: Playing channel volume=" << volume << std::endl;

        if(Mix_PlayChannel(-1, sound, 0) == -1) {
            std::cout << "Audio::PlaySound: Mix_PlayChannel failed: " << Mix_GetError() << std::endl;
             return false;
        }
        return true;
    }
    return false;
#elif !defined(OPENCLAW_MIDI_RPC)
    if (!sound) {
        return false;
    }
    int chunkVolume = (int)((soundProperties.volume / 100.0f) * (m_SoundVolume / 100.0f) * MIX_MAX_VOLUME);
    Mix_VolumeChunk(sound, chunkVolume);

    int channel = Mix_PlayChannel(-1, sound, soundProperties.loops);
    if (channel == -1) {
        LOG_ERROR("Failed to play chunk: " + std::string(Mix_GetError()));
        return false;
    }
    if (soundProperties.angle != 0 || soundProperties.distance != 0) {
        Mix_SetPosition(channel, soundProperties.angle, soundProperties.distance);
    } else {
        // Channels are reused; clear positioning left over from a previous sound.
        Mix_SetPosition(channel, 0, 0);
    }
    return true;
#else
    if (!m_audioSystem) {
        return false;
    }
    
    // Calculate volume (0.0 to 1.0)
    float volume = (static_cast<float>(soundProperties.volume) / 100.0f) * (static_cast<float>(m_SoundVolume) / 100.0f);
    
    // Use the sound data directly with our audio system
    if (sound && sound->abuf) {
        // Load the sound data into our audio system
        m_audioSystem->LoadSound("wav_sound", (const char*)sound->abuf, sound->alen);
        return m_audioSystem->PlaySound("wav_sound", volume);
    }
    
    return false;
#endif
}

void Audio::SetSoundVolume(int volumePercentage)
{
    // Ensure volume is within valid range (0-100)
    volumePercentage = max(0, min(100, volumePercentage));
    m_SoundVolume = volumePercentage;

    // Convert 0-100 range to 0.0-1.0 for audio system
    float volume = static_cast<float>(volumePercentage) / 100.0f;
    if (m_audioSystem) {
        m_audioSystem->SetSoundVolume(volume);
    }
}

int Audio::GetSoundVolume()
{
    return m_SoundVolume; // Already in 0-100 range
}

void Audio::StopAllSounds()
{
    if (m_audioSystem) {
        m_audioSystem->StopAllSounds();
    }
    StopMusic();
}

void Audio::PauseAllSounds()
{
    Mix_Pause(-1);
#ifdef OPENCLAW_MIDI_RPC
    MidiRPC_PauseSong();
#endif //OPENCLAW_MIDI_RPC
}

void Audio::ResumeAllSounds()
{
    Mix_Resume(-1);
#ifdef OPENCLAW_MIDI_RPC
    MidiRPC_ResumeSong();
#endif //OPENCLAW_MIDI_RPC
}

void Audio::SetSoundActive(bool active)
{
    m_bSoundOn = active;

    if (m_audioSystem) {
        m_audioSystem->SetSoundEnabled(m_bSoundOn);
        if (!m_bSoundOn) {
            m_audioSystem->StopAllSounds();
        }
    }
}

void Audio::SetMusicActive(bool active)
{
    m_bMusicOn = active;

    if (m_audioSystem) {
        // SetMusicEnabled mutes/unmutes via the gain node while leaving the music
        // source alive. Do NOT StopMusic() on disable: that tears down the source
        // and gain node, so re-enabling has nothing left to unmute and music never
        // resumes (the bug where toggling music off then on again killed it).
        m_audioSystem->SetMusicEnabled(m_bMusicOn);
    }
}

#ifdef OPENCLAW_MIDI_RPC
//############################################
//############## MIDI RPC ####################
//############################################

bool Audio::InitializeMidiRPC(const std::string& midiRpcServerPath)
{
    if (!InitializeMidiRPCServer(midiRpcServerPath))
    {
        return false;
    }

    if (!InitializeMidiRPCClient())
    {
        return false;
    }

    return true;
}

bool Audio::InitializeMidiRPCServer(const std::string& midiRpcServerPath)
{
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    BOOL doneCreateProc = CreateProcess(midiRpcServerPath.c_str(), NULL, NULL, NULL, FALSE,
                                           0, NULL, NULL, &si, &pi);
    if (doneCreateProc)
    {
        m_bIsServerInitialized = true;
        LOG("MIDI RPC Server started. [" + std::string(midiRpcServerPath) + "]");
    }
    else
    {
        LOG_ERROR("FAILED to start RPC MIDI Server. [" + std::string(midiRpcServerPath) + "]");
    }

    return (doneCreateProc != 0);
}

bool Audio::InitializeMidiRPCClient()
{
    RPC_STATUS rpcStatus;

    if (!m_bIsServerInitialized)
    {
        LOG_ERROR("Failed to initialize RPC MIDI Client - server was was not initialized");
        return false;
    }

    rpcStatus = RpcStringBindingCompose(NULL,
                                       (RPC_CSTR)("ncalrpc"),
                                       NULL,
                                       (RPC_CSTR)("2d4dc2f9-ce90-4080-8a00-1cb819086970"),
                                       NULL,
                                       &m_RpcBindingString);

    if (rpcStatus != 0)
    {
        LOG_ERROR("Failed to initialize RPC MIDI Client - RPC binding composition failed");
        return false;
    }

    rpcStatus = RpcBindingFromStringBinding(m_RpcBindingString, &hMidiRPCBinding);

    if (rpcStatus != 0)
    {
        LOG_ERROR("Failed to initialize RPC MIDI Client - RPC client binding failed");
        return false;
    }

    LOG("RPC Client successfully initialized");

    m_bIsClientInitialized = true;

    bool isServerListening = IsRPCServerListening();
    if (!isServerListening)
    {
        LOG_ERROR("Handshake between RPC Server and Client failed");
        return false;
    }
    else
    {
        LOG("RPC Server and Client successfully handshaked");
    }

    return true;
}

bool Audio::IsRPCServerListening()
{
    if (!m_bIsClientInitialized || !m_bIsServerInitialized)
    {
        return false;
    }

    uint16_t tries = 0;
    while (RpcMgmtIsServerListening(hMidiRPCBinding) != RPC_S_OK)
    {
        SDL_Delay(10);
        if (tries++ >= MIDI_RPC_MAX_HANDSHAKE_TRIES)
        {
            return false;
        }
    }

    return true;
}

void Audio::TerminateMidiRPC()
{
    RpcTryExcept
    {
        MidiRPC_StopServer();
    }
    RpcExcept(1)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Audio::TerminateMidiRPC: Failed due to RPC exception");
    }
    RpcEndExcept;
}
#endif //OPENCLAW_MIDI_RPC
