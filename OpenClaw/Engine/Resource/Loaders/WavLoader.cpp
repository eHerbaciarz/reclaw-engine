#include "../../Util/MemStats.h"
#include "../../PortFeatures.h"
#include <cstring>
#include <algorithm>
#include <cstdint>
#include "../../Util/StartupTrace.h"
#include "WavLoader.h"

#include "../../GameApp/BaseGameApp.h"

#ifdef __EMSCRIPTEN__
#include "../../Audio/WebAudioAPI.h"
#else
#include "../LooseSoundFiles.h"
#include <map>
#endif

//=================================================================================================
// class WavResourceExtraData
//
//     This class implements the IResourceExtraData
//

// Custom deallocator for SDL stuff
void DeleteMixChunk(Mix_Chunk* sound)
{
    if (sound) { MemStats::g_SoundBytes -= sound->alen; MemStats::g_SoundCount--; }
    Mix_FreeChunk(sound);
}

WavResourceExtraData::~WavResourceExtraData()
{
    // Sound takes care of its own destruction
}

// 3DS: plain PCM WAVs converted to the mixer's format here, with linear
// interpolation. SDL's converter (a long resampling filter) took 15-225 ms a
// sound on the 3DS, a stutter every time a sound was played for the first
// time; this takes a millisecond or two. Null: not a WAV this handles (SDL
// decodes it).
static Mix_Chunk* FastDecodeWav(const unsigned char* b, uint32 size)
{
#if defined(RECLAW_FAST_WAV)
    int outRate = 0, outCh = 0;
    Uint16 outFmt = 0;
    if (!Mix_QuerySpec(&outRate, &outFmt, &outCh) || outFmt != AUDIO_S16SYS || (outCh != 1 && outCh != 2) || outRate <= 0)
        return nullptr;
    if (size < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return nullptr;
    unsigned tag = 0, ch = 0, rate = 0, bits = 0;
    const unsigned char* data = nullptr;
    uint32 dataLen = 0;
    for (uint32 i = 12; i + 8 <= size; )
    {
        const uint32 len = b[i + 4] | (b[i + 5] << 8) | (b[i + 6] << 16) | ((uint32)b[i + 7] << 24);
        if (memcmp(b + i, "fmt ", 4) == 0 && len >= 16 && i + 24 <= size)
        {
            tag = b[i + 8] | (b[i + 9] << 8);
            ch = b[i + 10] | (b[i + 11] << 8);
            rate = b[i + 12] | (b[i + 13] << 8) | (b[i + 14] << 16) | ((uint32)b[i + 15] << 24);
            bits = b[i + 22] | (b[i + 23] << 8);
        }
        else if (memcmp(b + i, "data", 4) == 0)
        {
            data = b + i + 8;
            dataLen = std::min<uint32>(len, size - (i + 8));
            break;
        }
        if (len > size) break;
        i += 8 + len + (len & 1);
    }
    if (!data || tag != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || rate < 4000 || rate > 96000) return nullptr;
    const uint32 bytesPerFrame = ch * bits / 8;
    const uint32 inFrames = dataLen / bytesPerFrame;
    if (inFrames == 0) return nullptr;
    const uint32 outFrames = (uint32)((uint64_t)inFrames * (uint32)outRate / rate);
    if (outFrames == 0) return nullptr;
    Mix_Chunk* chunk = (Mix_Chunk*)SDL_malloc(sizeof(Mix_Chunk));
    Sint16* out = (Sint16*)SDL_malloc((size_t)outFrames * outCh * sizeof(Sint16));
    if (!chunk || !out) { SDL_free(chunk); SDL_free(out); return nullptr; }
    auto sample = [&](uint32 frame, unsigned c) -> int {
        const unsigned char* p = data + (size_t)frame * bytesPerFrame + c * (bits / 8);
        return bits == 8 ? ((int)p[0] - 128) << 8 : (int)(Sint16)(p[0] | (p[1] << 8));
    };
    const uint64_t step = ((uint64_t)rate << 16) / (uint32)outRate;   // 16.16 fixed point
    uint64_t pos = 0;
    for (uint32 f = 0; f < outFrames; ++f, pos += step)
    {
        const uint32 i0 = (uint32)(pos >> 16), i1 = std::min(i0 + 1, inFrames - 1);
        const int frac = (int)(pos & 0xFFFF);
        int l = sample(i0, 0) + (int)(((int64_t)(sample(i1, 0) - sample(i0, 0)) * frac) >> 16);
        int r = ch == 2 ? sample(i0, 1) + (int)(((int64_t)(sample(i1, 1) - sample(i0, 1)) * frac) >> 16) : l;
        if (outCh == 1) out[f] = (Sint16)((l + r) / 2);
        else { out[2 * f] = (Sint16)l; out[2 * f + 1] = (Sint16)r; }
    }
    chunk->allocated = 1;
    chunk->abuf = (Uint8*)out;
    chunk->alen = outFrames * outCh * sizeof(Sint16);
    chunk->volume = MIX_MAX_VOLUME;
    return chunk;
#else
    (void)b; (void)size;
    return nullptr;
#endif
}

void WavResourceExtraData::LoadWavSound(char* rawBuffer, uint32 size)
{
#ifndef __EMSCRIPTEN__
    // Native: decode to PCM in the mixer's output format, as upstream OpenClaw
    // does. Mix_PlayChannel, Mix_SetPosition and Util::GetSoundDurationMs all
    // expect a real decoded chunk.
    Mix_Chunk* pChunk = FastDecodeWav((const unsigned char*)rawBuffer, size);
    if (!pChunk)
    {
        SDL_RWops* soundRwOps = SDL_RWFromConstMem(rawBuffer, (int)size);
        pChunk = Mix_LoadWAV_RW(soundRwOps, 1);
    }
    if (pChunk == NULL)
    {
        LOG_ERROR("Mix_LoadWAV_RW failed: " + std::string(Mix_GetError()));
#ifdef OPENCLAW_STARTUP_TRACE
        {
            // What kind of WAV it is (format tag, channels, rate, bits).
            std::string info = "sound failed (" + std::to_string(size) + " bytes";
            const unsigned char* b = (const unsigned char*)rawBuffer;
            for (uint32 i = 12; i + 24 <= size; ++i)
            {
                if (memcmp(b + i, "fmt ", 4) == 0)
                {
                    auto u16 = [&](uint32 o) { return (unsigned)(b[o] | (b[o + 1] << 8)); };
                    auto u32 = [&](uint32 o) { return (unsigned)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((unsigned)b[o + 3] << 24)); };
                    info += ", fmt size " + std::to_string(u32(i + 4)) + ", tag " + std::to_string(u16(i + 8)) +
                            ", ch " + std::to_string(u16(i + 10)) + ", rate " + std::to_string(u32(i + 12)) +
                            ", bits " + std::to_string(u16(i + 22));
                    break;
                }
            }
            for (uint32 i = 12; i + 8 <= size; ++i)
                if (memcmp(b + i, "data", 4) == 0)
                {
                    info += ", data " + std::to_string((unsigned)(b[i + 4] | (b[i + 5] << 8) | (b[i + 6] << 16) | ((unsigned)b[i + 7] << 24)));
                    break;
                }
            STARTUP_TRACE(info + ")");
        }
#endif
        _sound = nullptr;
        return;
    }
    MemStats::g_SoundBytes += pChunk->alen; MemStats::g_SoundCount++;
    _sound = shared_ptr<Mix_Chunk>(pChunk, DeleteMixChunk);
    return;
#endif

    // Create a dummy Mix_Chunk for compatibility
    _sound = shared_ptr<Mix_Chunk>(new Mix_Chunk(), DeleteMixChunk);

    // CRITICAL FIX: Copy the buffer instead of storing the pointer
    // The rawBuffer will be deleted by ResourceCache after VLoadResource returns,
    // so we need our own copy of the WAV data
    _sound->abuf = new Uint8[size];
    memcpy(_sound->abuf, rawBuffer, size);
    _sound->alen = size;
    MemStats::g_SoundBytes += size; MemStats::g_SoundCount++;
    _sound->allocated = 1; // Mark as allocated so SDL will free it

    // The actual audio loading will be handled by the Audio class when PlaySound is called
}

//=================================================================================================
// class WavResourceLoader
//
//     This class implements the IResourceLoader interface with WAV sound format
//

bool WavResourceLoader::VLoadResource(char* rawBuffer, uint32 rawSize, std::shared_ptr<ResourceHandle> handle)
{
    if (rawSize <= 0 || rawBuffer == NULL)
    {
        LOG_ERROR("Received invalid rawBuffer or its size");
        return false;
    }

    shared_ptr<WavResourceExtraData> extraData = shared_ptr<WavResourceExtraData>(new WavResourceExtraData());
    extraData->LoadWavSound(rawBuffer, rawSize);

    if (extraData->GetSound() == NULL)
    {
        LOG_ERROR("Failed to load sound. Is sound system initialized ?");
    }

    handle->SetExtraData(extraData);

    return true;
}

uint32 WavResourceLoader::VGetLoadedResourceSize(char* rawBuffer, uint32 rawSize)
{
#if defined(__EMSCRIPTEN__)
    // For Emscripten, just return the raw size since we don't need to decode
    return rawSize;
#elif defined(RECLAW_FAST_WAV)
    // 3DS, PS3: the decoded size worked out from the header (decoding it here too
    // decoded every sound twice); the cache counts it, so sounds still leave
    // the cache when it is full. Output: 22050 Hz, 16-bit stereo.
    {
        const unsigned char* b = (const unsigned char*)rawBuffer;
        unsigned ch = 1, rate = 22050, bits = 8, data = rawSize;
        for (uint32 i = 12; i + 8 <= rawSize; )
        {
            const unsigned len = b[i + 4] | (b[i + 5] << 8) | (b[i + 6] << 16) | ((unsigned)b[i + 7] << 24);
            if (memcmp(b + i, "fmt ", 4) == 0 && i + 24 <= rawSize)
            {
                ch = b[i + 10] | (b[i + 11] << 8);
                rate = b[i + 12] | (b[i + 13] << 8) | (b[i + 14] << 16) | ((unsigned)b[i + 15] << 24);
                bits = b[i + 22] | (b[i + 23] << 8);
            }
            else if (memcmp(b + i, "data", 4) == 0) { data = len; break; }
            i += 8 + len + (len & 1);
        }
        if (!ch || !rate || !bits) return rawSize * 8;
        int outRate = 22050, outCh = 2;
        Uint16 outFmt = 0;
        if (!Mix_QuerySpec(&outRate, &outFmt, &outCh) || outRate <= 0 || outCh <= 0) { outRate = 22050; outCh = 2; }
        const double frames = (double)data / (ch * (bits / 8.0));
        return (uint32)(frames * outRate / rate * 2.0 * outCh) + 64;
    }
#else
    // TODO: This is inefficent, this resource gets basically loaded twice, once just
    // to find out how much room it takes and second time to actually create it
    SDL_RWops* soundRwOps = SDL_RWFromMem((void*)rawBuffer, rawSize);
    Mix_Chunk* pChunk = Mix_LoadWAV_RW(soundRwOps, 1);
    if (pChunk == NULL)
    {
        return rawSize;
    }
    uint32 decodedSize = pChunk->alen;
    Mix_FreeChunk(pChunk);
    return decodedSize;
#endif
}

#if defined(__3DS__)
static std::map<std::string, shared_ptr<Mix_Chunk>>& KeptSounds()
{
    static std::map<std::string, shared_ptr<Mix_Chunk>> s_Kept;
    return s_Kept;
}
#endif

shared_ptr<Mix_Chunk> WavResourceLoader::LoadAndReturnSound(const char* resourceString)
{
#ifndef __EMSCRIPTEN__
    // Releases that keep the sounds in a "Sounds" folder next to CLAW.REZ.
    // Decoded chunks are kept for the rest of the session: SDL_mixer may still
    // be playing one, so they must never be freed while channels reference them.
    {
        static std::map<std::string, shared_ptr<Mix_Chunk>> s_LooseCache;
        std::string key(resourceString);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        auto cached = s_LooseCache.find(key);
        if (cached != s_LooseCache.end())
        {
            return cached->second;
        }
        std::string filePath;
        if (LooseSoundFiles::Find(resourceString, filePath))
        {
            Mix_Chunk* pChunk = Mix_LoadWAV(filePath.c_str());
            if (pChunk)
            {
                MemStats::g_SoundBytes += pChunk->alen; MemStats::g_SoundCount++;
                shared_ptr<Mix_Chunk> pSound(pChunk, DeleteMixChunk);
                s_LooseCache[key] = pSound;
                return pSound;
            }
            LOG_WARNING("Could not decode " + filePath + ": " + std::string(Mix_GetError()));
        }
    }
#endif
    Resource resource(resourceString);

    shared_ptr<ResourceHandle> handle = g_pApp->GetResourceCache()->GetHandle(&resource);
    if (!handle)
    {
        // Not every release of CLAW.REZ contains every sound (e.g. MENUBED.WAV).
        LOG_WARNING("Sound not found, skipping: " + std::string(resourceString));
        return NULL;
    }
    shared_ptr<WavResourceExtraData> extraData = std::static_pointer_cast<WavResourceExtraData>(handle->GetExtraData());

    if (!extraData)
    {
        LOG_ERROR("Could not cast type to WavResourceExtraData. Check if WavResourceLoader is registered.");
        return NULL;
    }

    return extraData->GetSound();
}

void WavResourceLoader::ClearKept()
{
#if defined(__3DS__)
    KeptSounds().clear();
#endif
}

std::shared_ptr<WavResourceLoader> WavResourceLoader::Create()
{
    return shared_ptr<WavResourceLoader>(new WavResourceLoader());
}