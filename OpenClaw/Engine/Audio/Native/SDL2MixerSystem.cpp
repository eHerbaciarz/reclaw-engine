#ifndef __EMSCRIPTEN__

#include "SDL2MixerSystem.h"

#include "../../Logger/Logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>

// TinySoundFont reads Ogg-compressed SF3 samples through stb_vorbis when the
// stb_vorbis declarations are visible before tsf.h is compiled.
#define STB_VORBIS_HEADER_ONLY
#include "../../../../ThirdParty/TinySoundFont/stb_vorbis.c"
#define TSF_IMPLEMENTATION
#include "../../../../ThirdParty/TinySoundFont/tsf.h"
#define TML_IMPLEMENTATION
#include "../../../../ThirdParty/TinySoundFont/tml.h"
#undef STB_VORBIS_HEADER_ONLY
#include "../../../../ThirdParty/TinySoundFont/stb_vorbis.c"

namespace
{
    // MIDI channel 10 (index 9) is the General MIDI percussion channel.
    const int kDrumChannel = 9;
    // Headroom for the synth so dense passages do not clip against effects.
    // Applied through tsf_set_volume (which overrides tsf_set_output's gain).
    const float kSynthHeadroom = 0.25f; // about -12 dB
    // Group tag upstream OpenClaw uses for LocalAmbientSoundComponent.
    const int kAmbientGroupTag = 1;
    const int kAmbientReservedChannels = 16;
    // Channel right after the ambient group, reserved for WAV music.
    const int kMusicChannel = kAmbientReservedChannels;

    struct SynthLock
    {
        explicit SynthLock(SDL_mutex *m) : m_m(m) { if (m_m) SDL_LockMutex(m_m); }
        ~SynthLock() { if (m_m) SDL_UnlockMutex(m_m); }
        SDL_mutex *m_m;
    };

    bool IsStandardMidi(const std::vector<char> &bytes)
    {
        return bytes.size() >= 4 && std::memcmp(bytes.data(), "MThd", 4) == 0;
    }

    float ToSynthVolume(float v)
    {
        v = std::max(0.0f, std::min(1.0f, v));
        // tsf_set_volume divides by the volume; keep it strictly positive.
        return std::max(0.00001f, v * kSynthHeadroom);
    }

    int ToMixVolume(float v)
    {
        v = std::max(0.0f, std::min(1.0f, v));
        return (int)(v * MIX_MAX_VOLUME + 0.5f);
    }
}

SDL2MixerSystem::SDL2MixerSystem(const SDL2MixerConfig &config) : m_Config(config) {}

SDL2MixerSystem::~SDL2MixerSystem() { Shutdown(); }

bool SDL2MixerSystem::Initialize()
{
    if (m_Initialized)
    {
        return true;
    }

    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        LOG_ERROR("SDL audio subsystem unavailable: " + std::string(SDL_GetError()));
        return false;
    }

    if (Mix_OpenAudio(m_Config.frequency, AUDIO_S16SYS, m_Config.outputChannels, m_Config.chunkSize) != 0)
    {
        LOG_ERROR("Mix_OpenAudio failed: " + std::string(Mix_GetError()));
        return false;
    }

    Uint16 format = 0;
    if (!Mix_QuerySpec(&m_OutputFrequency, &format, &m_OutputChannels))
    {
        m_OutputFrequency = m_Config.frequency;
        m_OutputChannels = m_Config.outputChannels;
    }

    Mix_AllocateChannels(std::max(m_Config.mixingChannels, kMusicChannel + 1 + 8));
    if (Mix_ReserveChannels(kMusicChannel + 1) != kMusicChannel + 1)
    {
        LOG_WARNING("Could not reserve ambient sound channels: " + std::string(Mix_GetError()));
    }
    Mix_GroupChannels(0, kAmbientReservedChannels - 1, kAmbientGroupTag);

    m_pSynthMutex = SDL_CreateMutex();

    // Decoding the SF3 soundfont (Ogg samples) takes a while on handheld CPUs,
    // so it happens in the background; music requested earlier starts as soon
    // as the synth is ready.
    m_pLoaderThread = SDL_CreateThread(&SDL2MixerSystem::SoundFontLoaderThread, "SoundFontLoader", this);
    if (!m_pLoaderThread)
    {
        LOG_WARNING("Could not start soundfont loader thread, loading synchronously");
        SoundFontLoaderThread(this);
    }

    m_Initialized = true;
    LOG("SDL2_mixer audio initialized: " + std::to_string(m_OutputFrequency) + " Hz, " +
        std::to_string(m_OutputChannels) + " channels");
    return true;
}

int SDL2MixerSystem::SoundFontLoaderThread(void *userData)
{
    SDL2MixerSystem *self = static_cast<SDL2MixerSystem *>(userData);

    tsf *pFont = nullptr;
    std::string usedPath;
    for (const std::string &path : self->m_Config.soundFontPaths)
    {
        // Read through SDL_RWops so platform-specific paths (e.g. Switch romfs:/)
        // work the same as regular files.
        SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw)
        {
            continue;
        }
        Sint64 size = SDL_RWsize(rw);
        std::vector<char> bytes(size > 0 ? (size_t)size : 0);
        size_t read = bytes.empty() ? 0 : SDL_RWread(rw, bytes.data(), 1, bytes.size());
        SDL_RWclose(rw);
        if (read != bytes.size() || bytes.empty())
        {
            continue;
        }

        pFont = tsf_load_memory(bytes.data(), (int)bytes.size());
        if (pFont)
        {
            usedPath = path;
            break;
        }
        LOG_WARNING("Failed to parse soundfont: " + path);
    }

    {
        SynthLock lock(self->m_pSynthMutex);
        if (pFont)
        {
            tsf_set_output(pFont, self->m_OutputChannels >= 2 ? TSF_STEREO_INTERLEAVED : TSF_MONO,
                           self->m_OutputFrequency, 0.0f);
            tsf_set_max_voices(pFont, 256);
            self->m_pSoundFont = pFont;
            self->ResetSynthChannels();
        }
        self->m_SoundFontReady = true;
    }

    if (pFont)
    {
        LOG("Music soundfont loaded: " + usedPath);
    }
    else
    {
        LOG_WARNING("No music soundfont found - level music (XMI) will be silent. "
                    "Expected music/soundfont.sf3 next to the game assets.");
    }
    return 0;
}

void SDL2MixerSystem::Shutdown()
{
    if (!m_Initialized)
    {
        return;
    }

    if (m_pLoaderThread)
    {
        SDL_WaitThread(m_pLoaderThread, nullptr);
        m_pLoaderThread = nullptr;
    }

    StopMusic();
    Mix_HookMusic(nullptr, nullptr);
    m_HookInstalled = false;
    Mix_HaltChannel(-1);

    for (auto &kv : m_Sounds)
    {
        Mix_FreeChunk(kv.second);
    }
    m_Sounds.clear();
    m_MusicData.clear();

    {
        SynthLock lock(m_pSynthMutex);
        if (m_pMidi)
        {
            tml_free(m_pMidi);
            m_pMidi = m_pMidiCursor = nullptr;
        }
        if (m_pSoundFont)
        {
            tsf_close(m_pSoundFont);
            m_pSoundFont = nullptr;
        }
    }

    Mix_CloseAudio();
    if (m_pSynthMutex)
    {
        SDL_DestroyMutex(m_pSynthMutex);
        m_pSynthMutex = nullptr;
    }
    m_Initialized = false;
}

//----------------------------------------------------------------------------
// Sound effects
//----------------------------------------------------------------------------

bool SDL2MixerSystem::LoadSound(const std::string &name, const char *data, size_t size)
{
    if (!data || size == 0)
    {
        return false;
    }
    Mix_Chunk *pChunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(data, (int)size), 1);
    if (!pChunk)
    {
        LOG_ERROR("Mix_LoadWAV_RW failed for " + name + ": " + std::string(Mix_GetError()));
        return false;
    }
    auto it = m_Sounds.find(name);
    if (it != m_Sounds.end())
    {
        Mix_FreeChunk(it->second);
    }
    m_Sounds[name] = pChunk;
    return true;
}

bool SDL2MixerSystem::PlaySound(const std::string &name, float volume)
{
    if (!m_SoundEnabled)
    {
        return false;
    }
    auto it = m_Sounds.find(name);
    if (it == m_Sounds.end())
    {
        return false;
    }
    Mix_VolumeChunk(it->second, ToMixVolume(volume));
    return Mix_PlayChannel(-1, it->second, 0) != -1;
}

bool SDL2MixerSystem::PlaySoundWithPath(const std::string &originalPath, const char *data, size_t size,
                                        float volume, int loops)
{
    if (!m_SoundEnabled)
    {
        return false;
    }
    if (m_Sounds.find(originalPath) == m_Sounds.end() && !LoadSound(originalPath, data, size))
    {
        return false;
    }
    Mix_Chunk *pChunk = m_Sounds[originalPath];
    Mix_VolumeChunk(pChunk, ToMixVolume(volume));
    return Mix_PlayChannel(-1, pChunk, loops) != -1;
}

void SDL2MixerSystem::StopSound(const std::string &name)
{
    auto it = m_Sounds.find(name);
    if (it == m_Sounds.end())
    {
        return;
    }
    for (int ch = 0; ch < Mix_AllocateChannels(-1); ++ch)
    {
        if (Mix_Playing(ch) && Mix_GetChunk(ch) == it->second)
        {
            Mix_HaltChannel(ch);
        }
    }
}

void SDL2MixerSystem::HaltEffectChannels()
{
    for (int ch = 0; ch < Mix_AllocateChannels(-1); ++ch)
    {
        if (ch != kMusicChannel)
        {
            Mix_HaltChannel(ch);
        }
    }
}

void SDL2MixerSystem::StopAllSounds() { HaltEffectChannels(); }

void SDL2MixerSystem::SetSoundVolume(float volume)
{
    // Per-play volume already folds in the global sound volume (see Audio.cpp);
    // it is stored here for GetSoundVolume().
    m_SoundVolume = std::max(0.0f, std::min(1.0f, volume));
}

void SDL2MixerSystem::SetSoundEnabled(bool enabled)
{
    m_SoundEnabled = enabled;
    if (!enabled)
    {
        HaltEffectChannels();
    }
}

//----------------------------------------------------------------------------
// Music
//----------------------------------------------------------------------------

bool SDL2MixerSystem::LoadMusic(const std::string &name, const char *data, size_t size)
{
    if (!data || size == 0)
    {
        return false;
    }
    m_MusicData[name] = std::vector<char>(data, data + size);
    return true;
}

bool SDL2MixerSystem::PlayMusic(const std::string &name, bool looping)
{
    auto it = m_MusicData.find(name);
    if (it == m_MusicData.end())
    {
        return false;
    }
    std::vector<char> bytes = std::move(it->second);
    m_MusicData.erase(it);

    StopMusic();

    if (IsStandardMidi(bytes))
    {
        tml_message *pMidi = tml_load_memory(bytes.data(), (int)bytes.size());
        if (!pMidi)
        {
            LOG_ERROR("Could not parse MIDI music: " + name);
            return false;
        }

        {
            SynthLock lock(m_pSynthMutex);
            m_pMidi = pMidi;
            m_pMidiCursor = pMidi;
            m_MidiTimeMs = 0.0;
            m_MidiLooping = looping;
            m_MidiPlaying = true;
            m_MidiPaused = false;
            ResetSynthChannels();
        }

        if (!m_HookInstalled)
        {
            Mix_HookMusic(&SDL2MixerSystem::MusicHook, this);
            m_HookInstalled = true;
        }
        return true;
    }

    // Streamed music (e.g. MENUBED.WAV). A custom music hook would replace
    // SDL_mixer's own music player, so it must be removed first.
    if (m_HookInstalled)
    {
        Mix_HookMusic(nullptr, nullptr);
        m_HookInstalled = false;
    }

    m_StreamMusicBytes = std::move(bytes);
    m_pStreamMusic = Mix_LoadMUS_RW(SDL_RWFromConstMem(m_StreamMusicBytes.data(), (int)m_StreamMusicBytes.size()), 1);
    if (!m_pStreamMusic)
    {
        LOG_ERROR("Mix_LoadMUS_RW failed for " + name + ": " + std::string(Mix_GetError()));
        m_StreamMusicBytes.clear();
        return false;
    }
    ApplyMusicVolume();
    if (Mix_PlayMusic(m_pStreamMusic, looping ? -1 : 1) != 0)
    {
        LOG_ERROR("Mix_PlayMusic failed: " + std::string(Mix_GetError()));
        return false;
    }
    return true;
}

bool SDL2MixerSystem::PlayMusicChunk(Mix_Chunk *pChunk, bool looping)
{
    StopMusic();
    if (!pChunk)
    {
        return false;
    }
    ApplyMusicVolume();
    if (Mix_PlayChannel(kMusicChannel, pChunk, looping ? -1 : 0) == -1)
    {
        LOG_ERROR("Failed to play music chunk: " + std::string(Mix_GetError()));
        return false;
    }
    m_ChunkMusicActive = true;
    return true;
}

void SDL2MixerSystem::StopMusicLocked()
{
    if (m_pMidi)
    {
        tml_free(m_pMidi);
        m_pMidi = m_pMidiCursor = nullptr;
    }
    m_MidiPlaying = false;
    m_MidiPaused = false;
    ResetSynthChannels();
}

void SDL2MixerSystem::StopMusic()
{
    {
        SynthLock lock(m_pSynthMutex);
        StopMusicLocked();
    }

    if (m_pStreamMusic)
    {
        Mix_HaltMusic();
        Mix_FreeMusic(m_pStreamMusic);
        m_pStreamMusic = nullptr;
    }
    m_StreamMusicBytes.clear();

    if (m_ChunkMusicActive)
    {
        Mix_HaltChannel(kMusicChannel);
        m_ChunkMusicActive = false;
    }
}

void SDL2MixerSystem::PauseMusic()
{
    {
        SynthLock lock(m_pSynthMutex);
        m_MidiPaused = true;
    }
    if (m_pStreamMusic)
    {
        Mix_PauseMusic();
    }
    if (m_ChunkMusicActive)
    {
        Mix_Pause(kMusicChannel);
    }
}

void SDL2MixerSystem::ResumeMusic()
{
    {
        SynthLock lock(m_pSynthMutex);
        m_MidiPaused = false;
    }
    if (m_pStreamMusic)
    {
        Mix_ResumeMusic();
    }
    if (m_ChunkMusicActive)
    {
        Mix_Resume(kMusicChannel);
    }
}

bool SDL2MixerSystem::IsMusicPlaying() const
{
    if (m_ChunkMusicActive)
    {
        return Mix_Playing(kMusicChannel) != 0 && Mix_Paused(kMusicChannel) == 0;
    }
    if (m_pStreamMusic)
    {
        return Mix_PlayingMusic() != 0 && Mix_PausedMusic() == 0;
    }
    return m_MidiPlaying && !m_MidiPaused;
}

void SDL2MixerSystem::SetMusicVolume(float volume)
{
    m_MusicVolume = std::max(0.0f, std::min(1.0f, volume));
    ApplyMusicVolume();
}

void SDL2MixerSystem::SetMusicEnabled(bool enabled)
{
    // Like the web build: disabling music mutes it but keeps the track alive,
    // so enabling it again resumes where it was.
    m_MusicEnabled = enabled;
    ApplyMusicVolume();
}

void SDL2MixerSystem::ApplyMusicVolume()
{
    const float effective = m_MusicEnabled ? m_MusicVolume : 0.0f;
    Mix_VolumeMusic(ToMixVolume(effective));
    if (m_Initialized || m_ChunkMusicActive)
    {
        Mix_Volume(kMusicChannel, ToMixVolume(effective));
    }
    SynthLock lock(m_pSynthMutex);
    if (m_pSoundFont)
    {
        tsf_set_volume(m_pSoundFont, ToSynthVolume(effective));
    }
}

void SDL2MixerSystem::ResetSynthChannels()
{
    // Caller holds m_pSynthMutex.
    if (!m_pSoundFont)
    {
        return;
    }
    tsf_reset(m_pSoundFont);
    for (int ch = 0; ch < 16; ++ch)
    {
        tsf_channel_set_presetnumber(m_pSoundFont, ch, 0, ch == kDrumChannel);
        tsf_channel_midi_control(m_pSoundFont, ch, 121, 0); // reset all controllers
    }
    tsf_set_volume(m_pSoundFont, ToSynthVolume(m_MusicEnabled ? m_MusicVolume : 0.0f));
}

void SDL2MixerSystem::MusicHook(void *userData, Uint8 *stream, int len)
{
    static_cast<SDL2MixerSystem *>(userData)->RenderMidi(stream, len);
}

void SDL2MixerSystem::RenderMidi(Uint8 *stream, int len)
{
    // Output format is AUDIO_S16SYS (requested in Mix_OpenAudio).
    std::memset(stream, 0, (size_t)len);

    SynthLock lock(m_pSynthMutex);
    if (!m_pSoundFont || !m_MidiPlaying || m_MidiPaused || !m_pMidi)
    {
        return;
    }

    const int channels = m_OutputChannels >= 2 ? 2 : 1;
    const int frameBytes = (int)sizeof(short) * channels;
    int framesLeft = len / frameBytes;
    short *out = reinterpret_cast<short *>(stream);
    const double msPerFrame = 1000.0 / (double)m_OutputFrequency;

    while (framesLeft > 0)
    {
        int block = std::min(framesLeft, (int)TSF_RENDER_EFFECTSAMPLEBLOCK);

        m_MidiTimeMs += block * msPerFrame;
        for (; m_pMidiCursor && m_MidiTimeMs >= m_pMidiCursor->time; m_pMidiCursor = m_pMidiCursor->next)
        {
            const tml_message *msg = m_pMidiCursor;
            switch (msg->type)
            {
                case TML_PROGRAM_CHANGE:
                    tsf_channel_set_presetnumber(m_pSoundFont, msg->channel, msg->program, msg->channel == kDrumChannel);
                    break;
                case TML_NOTE_ON:
                    tsf_channel_note_on(m_pSoundFont, msg->channel, msg->key, msg->velocity / 127.0f);
                    break;
                case TML_NOTE_OFF:
                    tsf_channel_note_off(m_pSoundFont, msg->channel, msg->key);
                    break;
                case TML_PITCH_BEND:
                    tsf_channel_set_pitchwheel(m_pSoundFont, msg->channel, msg->pitch_bend);
                    break;
                case TML_CONTROL_CHANGE:
                    tsf_channel_midi_control(m_pSoundFont, msg->channel, msg->control, msg->control_value);
                    break;
                default:
                    break;
            }
        }

        tsf_render_short(m_pSoundFont, out, block, 0);
        out += block * channels;
        framesLeft -= block;

        if (!m_pMidiCursor)
        {
            if (m_MidiLooping)
            {
                // Restart the song; silence notes that were still held.
                m_pMidiCursor = m_pMidi;
                m_MidiTimeMs = 0.0;
                tsf_note_off_all(m_pSoundFont);
            }
            else if (tsf_active_voice_count(m_pSoundFont) == 0)
            {
                m_MidiPlaying = false;
                break;
            }
        }
    }
}

#endif // !__EMSCRIPTEN__
