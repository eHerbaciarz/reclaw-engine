#ifndef __EMSCRIPTEN__

#include "../../GameApp/BaseGameApp.h"
#include "SDL2MixerSystem.h"

#include "../../Logger/Logger.h"
#include "../../Util/StartupTrace.h"
#include "../../Platform/Platform.h"
#include "SoundFontTrim.h"
#include "../../Util/PortSettings.h"

#include <algorithm>
#include <cstring>
#include <fstream>

// TinySoundFont reads Ogg-compressed SF3 samples through stb_vorbis when the
// stb_vorbis declarations are visible before tsf.h is compiled.
// stb_vorbis has its own uint32/int32 typedefs; SharedDefines.h has others
// (uint32_t is "unsigned long" on the 3DS), so stb_vorbis gets its own names.
#define uint8 stbv_uint8
#define int8 stbv_int8
#define uint16 stbv_uint16
#define int16 stbv_int16
#define uint32 stbv_uint32
#define int32 stbv_int32
#define STB_VORBIS_HEADER_ONLY
#include "../../../../ThirdParty/TinySoundFont/stb_vorbis.c"
#if defined(__PSL1GHT__) || defined(__3DS__)
// Little memory: the music uses a soundfont trimmed to the game's
// instruments (SoundFontTrim), with its samples kept in 16 bits.
// TODO: the other platforms too, once tested on these two.
#define RECLAW_SMALL_SOUNDFONT 1
#define TSF_SAMPLES_INT16
#endif
#define TSF_IMPLEMENTATION
#include "../../../../ThirdParty/TinySoundFont/tsf.h"
#define TML_IMPLEMENTATION
#include "../../../../ThirdParty/TinySoundFont/tml.h"
#undef STB_VORBIS_HEADER_ONLY
#include "../../../../ThirdParty/TinySoundFont/stb_vorbis.c"
#undef uint8
#undef int8
#undef uint16
#undef int16
#undef uint32
#undef int32

#if (defined(__PSL1GHT__) || defined(__3DS__)) && defined(OPENCLAW_STARTUP_TRACE)
namespace { void PostMixDiag(void *userData, Uint8 *stream, int len); }
#endif
#if defined(__PSL1GHT__)
#include <sys/thread.h>
namespace
{
    // PS3: SDL's audio thread runs at priority 1500, below the game's own
    // thread (1000; lower is more urgent), and SDL_SetThreadPriority does
    // nothing on PSL1GHT. When the game thread was busy (a teleport loading
    // the new area) the mixing waited and the sound crackled. Raised from
    // inside the audio thread on its first call.
    void RaiseAudioThreadPriority(int, void *, int, void *)
    {
        static bool s_Done = false;
        if (s_Done) return;
        s_Done = true;
        sys_ppu_thread_t self;
        if (sysThreadGetId(&self) == 0) sysThreadSetPriority(self, 500);
    }
}
#endif
namespace
{
    // MIDI channel 10 (index 9) is the General MIDI percussion channel.
    const int kDrumChannel = 9;
    // Headroom for the synth so dense passages do not clip against effects.
    // Applied through tsf_set_volume (which overrides tsf_set_output's gain).
    const float kSynthHeadroom = 0.25f; // about -12 dB
#if defined(__PSL1GHT__) || defined(__3DS__)
    // PS3, 3DS: everything quieter (-6 dB): many sounds at once
    // summed past the maximum and crackled (clipped samples in the log).
    const float kMasterHeadroom = 0.5f;
#else
    const float kMasterHeadroom = 1.0f;
#endif
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
        // (music without the mixer headroom: it was too quiet next to the sounds)
        return std::max(0.00001f, v * kSynthHeadroom);
    }

    int ToMixVolume(float v)
    {
        v = std::max(0.0f, std::min(1.0f, v)) * kMasterHeadroom;
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

#if defined(__3DS__)
    // If the sound start crashed the previous launch (the marker file was left
    // behind), start silently with SDL's dummy driver instead of crashing again.
    const std::string audioMarker = Platform::DataDir() + "audio_start.flag";
    bool silent = false;
    if (FILE* f = fopen(audioMarker.c_str(), "rb"))
    {
        fclose(f);
        silent = true;
        remove(audioMarker.c_str());   // next launch tries the sound again
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        LOG_WARNING("Sound start crashed last time - starting without sound");
    }
    else if (FILE* f = fopen(audioMarker.c_str(), "wb"))
    {
        fputs("sound start in progress\n", f);
        fclose(f);
    }
#endif

    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        LOG_ERROR("SDL audio subsystem unavailable: " + std::string(SDL_GetError()));
        return false;
    }

    STARTUP_TRACE("Mix_OpenAudio");
#if defined(__PSL1GHT__)
    // PS3: short mixer calls. SDL's PSL1GHT audio driver keeps only 8 blocks
    // of 5 ms; mixing 93 ms at once (2048 samples) with many sounds took up
    // to 18 ms in one go and the sound crackled now and then.
    if (m_Config.chunkSize > 512) m_Config.chunkSize = 512;
#endif
#if defined(__PSL1GHT__)
    // PS3: exactly the asked format (the system's own is 8 channels: every
    // decoded sound would take 4x the memory); SDL converts at the end.
    if (Mix_OpenAudioDevice(m_Config.frequency, AUDIO_S16SYS, m_Config.outputChannels, m_Config.chunkSize, NULL, 0) != 0)
#else
    if (Mix_OpenAudio(m_Config.frequency, AUDIO_S16SYS, m_Config.outputChannels, m_Config.chunkSize) != 0)
#endif
    {
        LOG_ERROR("Mix_OpenAudio failed: " + std::string(Mix_GetError()));
#if defined(__3DS__)
        remove(audioMarker.c_str());
#endif
        return false;
    }
#if defined(__3DS__)
    // Let the mixing thread run a moment: a crash there also leaves the marker.
    if (!silent)
    {
        STARTUP_TRACE("audio running, checking");
        SDL_Delay(400);
        remove(audioMarker.c_str());
    }
    STARTUP_TRACE("audio ok");
#endif

    Uint16 format = 0;
    if (!Mix_QuerySpec(&m_OutputFrequency, &format, &m_OutputChannels))
    {
        m_OutputFrequency = m_Config.frequency;
        m_OutputChannels = m_Config.outputChannels;
    }
    STARTUP_TRACE("audio out: " + std::to_string(m_OutputFrequency) + " Hz, " + std::to_string(m_OutputChannels) +
                  " channels, format 0x" + [](unsigned f) { char b[8]; snprintf(b, sizeof(b), "%x", f); return std::string(b); }(format));

    // At least 64: 17 are reserved (ambient group + music), effects need the rest.
    Mix_AllocateChannels(std::max(m_Config.mixingChannels, 64));
    if (Mix_ReserveChannels(kMusicChannel + 1) != kMusicChannel + 1)
    {
        LOG_WARNING("Could not reserve ambient sound channels: " + std::string(Mix_GetError()));
    }
    Mix_GroupChannels(0, kAmbientReservedChannels - 1, kAmbientGroupTag);

    m_pSynthMutex = SDL_CreateMutex();
#if (defined(__PSL1GHT__) || defined(__3DS__)) && defined(OPENCLAW_STARTUP_TRACE)
    Mix_SetPostMix(PostMixDiag, this);
#endif
#if defined(__PSL1GHT__)
    Mix_RegisterEffect(MIX_CHANNEL_POST, RaiseAudioThreadPriority, nullptr, nullptr);
#endif
#ifdef RECLAW_SMALL_SOUNDFONT
    SoundFontTrim::SetFullPaths(m_Config.soundFontPaths);
#endif

    // Decoding the SF3 soundfont (Ogg samples) takes a while on handheld CPUs,
    // so it happens in the background; music requested earlier starts as soon
    // as the synth is ready.
    // stb_vorbis allocates its decode scratch buffers with alloca(); the
    // default pthread stack on the Switch is far too small for that.
    m_pLoaderThread = SDL_CreateThreadWithStackSize(&SDL2MixerSystem::SoundFontLoaderThread, "SoundFontLoader",
                                                    4 * 1024 * 1024, this);
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
    STARTUP_TRACE("soundfont loader started");
#if defined(__3DS__)
    // The game's own core: making the small soundfont must not take its time.
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
#endif

    tsf *pFont = nullptr;
    std::string usedPath;
#if defined(__PSL1GHT__)
    // PS3: wait until the menu is loaded, so the memory check below sees
    // what the game itself needs (both at once ran out of memory).
    for (int i = 0; i < 300 && (!g_pApp || !g_pApp->GetGameLogic()); ++i) SDL_Delay(100);
    SDL_Delay(3000);
#endif
    std::vector<std::string> paths = self->m_Config.soundFontPaths;
#if defined(__3DS__)
    // The synthesizer takes ~20% of a core: level music only on a New 3DS
    // (the audio thread is on its third core) unless port_settings.txt says
    // otherwise (music=0 / music=1).
    if (!Platform::LevelMusicOn())
    {
        paths.clear();
        LOG_WARNING("3DS: level music off (port_settings.txt: music=1 turns it on)");
    }
#endif
#ifdef RECLAW_SMALL_SOUNDFONT
    // The small soundfont (made at the first start from the full one) instead.
    {
        std::vector<std::string> small;
        for (const std::string &path : paths)
        {
            SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
            if (!rw) continue;
            SDL_RWclose(rw);
            STARTUP_TRACE("soundfont: small soundfont of " + path);
            if (SoundFontTrim::MakeSmall(path, 120000)) small.push_back(SoundFontTrim::SmallPath(path));
            break;
        }
#if defined(__3DS__)
        paths = small;   // the full one does not fit the 3DS at all
#else
        paths.insert(paths.begin(), small.begin(), small.end());
#endif
    }
#endif
    std::vector<Sint64> failedSizes;   // the same file under another path: not again
    for (const std::string &path : paths)
    {
        // Read through SDL_RWops so platform-specific paths (e.g. Switch romfs:/)
        // work the same as regular files.
        SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw)
        {
            continue;
        }
        Sint64 size = SDL_RWsize(rw);
        if (size <= 0 || std::find(failedSizes.begin(), failedSizes.end(), size) != failedSizes.end())
        {
            SDL_RWclose(rw);
            continue;
        }

        // TinySoundFont decodes every sample into RAM as floats: about 16x a
        // compressed .sf3, 2x an .sf2. Not trying it without the memory
        // (on the PS3 the attempt ran the whole game out of memory).
        {
            long freeKB = Platform::FreeMemoryKB();
            bool sf3 = path.size() > 4 && (path.compare(path.size() - 4, 4, ".sf3") == 0 || path.compare(path.size() - 4, 4, ".SF3") == 0);
#ifdef TSF_SAMPLES_INT16
            long needKB = (long)(size / 1024) * (sf3 ? 8 : 1) + (sf3 ? 8 : 2) * 1024;
#else
            long needKB = (long)(size / 1024) * (sf3 ? 16 : 3) + 8 * 1024;
#endif
            if (freeKB >= 0 && freeKB < needKB)
            {
                SDL_RWclose(rw);
                LOG_WARNING("Soundfont " + path + " needs about " + std::to_string(needKB / 1024) + " MB, only " +
                            std::to_string(freeKB / 1024) + " MB free - skipped (a smaller .sf2 soundfont would fit)");
                failedSizes.push_back(size);
                continue;
            }
        }
        STARTUP_TRACE("soundfont: read " + path + " (" + std::to_string((long long)size / 1024) + " KB), parsing");
        {
            // Read straight from the file: no second copy of it in memory.
            struct tsf_stream stream = {
                rw,
                [](void *data, void *ptr, unsigned int n) -> int { return (int)SDL_RWread((SDL_RWops *)data, ptr, 1, n); },
                [](void *data, unsigned int n) -> int { return SDL_RWseek((SDL_RWops *)data, n, RW_SEEK_CUR) >= 0 ? 1 : 0; },
            };
            pFont = tsf_load(&stream);
            SDL_RWclose(rw);
        }
        STARTUP_TRACE(pFont ? "soundfont: parsed" : "soundfont: parse failed");
        if (pFont)
        {
            usedPath = path;
            break;
        }
        LOG_WARNING("Failed to parse soundfont: " + path);
        failedSizes.push_back(size);
    }

    {
        SynthLock lock(self->m_pSynthMutex);
        if (pFont)
        {
            tsf_set_output(pFont, self->m_OutputChannels >= 2 ? TSF_STEREO_INTERLEAVED : TSF_MONO,
                           self->m_OutputFrequency, 0.0f);
#if defined(__vita__) || defined(__3DS__)
            // Handhelds: the synth runs in the audio thread; fewer voices
            // keep it in time (the level music rarely needs more).
            tsf_set_max_voices(pFont, 48);
#else
            tsf_set_max_voices(pFont, 256);
#endif
            self->m_pSoundFont = pFont;
            self->ResetSynthChannels();
        }
        self->m_SoundFontReady = true;
    }
    STARTUP_TRACE(pFont ? "soundfont loaded" : "soundfont NOT found");

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

#if (defined(__PSL1GHT__) || defined(__3DS__)) && defined(OPENCLAW_STARTUP_TRACE)
namespace
{
    // PS3 sound diagnostics (every ~5 s): clipped samples after mixing, late
    // mixer calls (the gap since the last one longer than 1.5x its length),
    // and the time the mixing itself takes.
    Uint64 g_MixStart = 0;
    void PostMixDiag(void *userData, Uint8 *stream, int len)
    {
        SDL2MixerSystem *self = static_cast<SDL2MixerSystem *>(userData);
        static Uint64 s_Last = 0, s_WindowStart = 0, s_MixTicks = 0, s_MaxMix = 0;
        static unsigned s_Calls = 0, s_Late = 0, s_Clipped = 0, s_Samples = 0;
        static int s_Peak = 0;
        const Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
        const Sint16 *p = (const Sint16 *)stream;
        const int n = len / 2;
        for (int i = 0; i < n; ++i)
        {
            const int v = p[i] < 0 ? -p[i] : p[i];
            if (v > s_Peak) s_Peak = v;
            if (v >= 32700) ++s_Clipped;
        }
        s_Samples += n;
        const double bufferSecs = (double)n / std::max(1, self->OutputChannels()) / std::max(1, self->OutputFrequency());
        if (s_Last && (double)(now - s_Last) / freq > bufferSecs * 1.5) ++s_Late;
        s_Last = now;
        if (g_MixStart)
        {
            s_MixTicks += now - g_MixStart;
            s_MaxMix = std::max(s_MaxMix, now - g_MixStart);
        }
        ++s_Calls;
        if (!s_WindowStart) s_WindowStart = now;
        if ((double)(now - s_WindowStart) / freq >= 5.0)
        {
            SDL_Log("sound mix: %u calls (%.1f ms each), %u late, peak %d, %u of %u samples clipped, mixing %.2f ms avg %.2f ms max",
                    s_Calls, bufferSecs * 1000.0, s_Late, s_Peak, s_Clipped, s_Samples,
                    s_MixTicks * 1000.0 / freq / std::max(1u, s_Calls), s_MaxMix * 1000.0 / freq);
            s_WindowStart = now; s_Calls = s_Late = s_Clipped = s_Samples = 0; s_Peak = 0; s_MixTicks = s_MaxMix = 0;
        }
        g_MixStart = 0;
    }
}
#endif

void SDL2MixerSystem::MusicHook(void *userData, Uint8 *stream, int len)
{
#if (defined(__PSL1GHT__) || defined(__3DS__)) && defined(OPENCLAW_STARTUP_TRACE)
    g_MixStart = SDL_GetPerformanceCounter();
#endif
#if (defined(__PSL1GHT__) || defined(__3DS__)) && defined(OPENCLAW_STARTUP_TRACE)
    // PS3: is the music still running (log every ~10 s)?
    static Uint32 s_Last = 0;
    static unsigned s_Calls = 0;
    ++s_Calls;
    const Uint32 t0 = SDL_GetTicks();
    if (t0 - s_Last > 10000) { s_Last = t0; SDL_Log("music synth: %u blocks", s_Calls); }
#endif
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
