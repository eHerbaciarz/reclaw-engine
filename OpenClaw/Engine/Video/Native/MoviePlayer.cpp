#include "../../Graphics2D/CrtEffect.h"
#include "../../Util/StartupTrace.h"
#include "MoviePlayer.h"

#ifndef __EMSCRIPTEN__

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <memory>
#include <sys/stat.h>

#include <tinyxml.h>


extern "C" {
#include "../../../../ThirdParty/libsmacker/smacker.h"
}

#include "../../Logger/Logger.h"
#include "../../Platform/Input/BlockingInput.h"
#include "../../UserInterface/Subtitles.h"
#include "../../Platform/Platform.h"
#include "RemasterDecoder.h"

namespace
{
    enum PlayResult { Played, Skipped, Aborted, Missing };
    enum SkipKind { None = 0, SkipOne, AbortAll };

    struct MovieSource
    {
        std::string path;
        long offset;
        long size;
    };

    std::vector<std::string> g_Roots;
    std::string g_UserDirectory;
    std::vector<MovieSource> g_Movies;
    bool g_Indexed = false;

    // Default schedule: indexes into CLAW.FEC (file order). Overridable by
    // <userDirectory>/movies.xml.
    std::vector<int> g_StartupMovies = { 0, 1 };
    std::map<int, std::vector<int>> g_AfterLevel = {
        { 2, { 2 } }, { 4, { 3 } }, { 6, { 4 } }, { 8, { 5 } },
        { 10, { 6 } }, { 12, { 7 } }, { 13, { 8 } }, { 14, { 9 } },
    };

    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    bool IsDir(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool IsFile(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    // Case-insensitive child lookup; returns full path or "".
    std::string FindChild(const std::string& dir, const std::string& lowerName)
    {
        DIR* d = opendir(dir.c_str());
        if (!d) return "";
        std::string found;
        while (struct dirent* e = readdir(d))
        {
            if (ToLower(e->d_name) == lowerName)
            {
                found = dir + "/" + e->d_name;
                break;
            }
        }
        closedir(d);
        return found;
    }

    uint32_t ReadU32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

    // Validates a Smacker header at `offset` and returns its total size (0 if invalid).
    long SmkSizeAt(FILE* f, long offset, long fileSize)
    {
        unsigned char hdr[104];
        if (fseek(f, offset, SEEK_SET) != 0 || fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) return 0;
        if (hdr[0] != 'S' || hdr[1] != 'M' || hdr[2] != 'K' || (hdr[3] != '2' && hdr[3] != '4')) return 0;
        uint32_t w = ReadU32(hdr + 4), h = ReadU32(hdr + 8), frames = ReadU32(hdr + 12);
        uint32_t flags = ReadU32(hdr + 20), trees = ReadU32(hdr + 52);
        if (w == 0 || w > 4096 || h == 0 || h > 4096 || frames == 0 || frames > 200000) return 0;
        uint32_t n = frames + ((flags & 1) ? 1 : 0);
        unsigned long data = 0;
        std::vector<unsigned char> sizes(n * 4);
        if (fread(sizes.data(), 1, sizes.size(), f) != sizes.size()) return 0;
        for (uint32_t i = 0; i < n; ++i) data += ReadU32(&sizes[i * 4]) & ~3u;
        unsigned long total = 104 + 5ul * n + trees + data;
        if (offset + (long)total > fileSize) return 0;
        return (long)total;
    }

    // Finds the Smacker files inside a container (CLAW.FEC). They follow each
    // other with small gaps, so each search only looks a little past the last one.
    void IndexContainer(const std::string& path)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return;
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        const long kWindow = 1 << 18;
        std::vector<unsigned char> buf(kWindow + 4);
        long pos = 0;
        while (pos < fileSize && g_Movies.size() < 64)
        {
            fseek(f, pos, SEEK_SET);
            size_t got = fread(buf.data(), 1, buf.size(), f);
            if (got < 4) break;
            long found = -1, size = 0;
            for (size_t i = 0; i + 4 <= got && i < (size_t)kWindow; ++i)
            {
                if (buf[i] == 'S' && buf[i + 1] == 'M' && buf[i + 2] == 'K' && (buf[i + 3] == '2' || buf[i + 3] == '4'))
                {
                    size = SmkSizeAt(f, pos + (long)i, fileSize);
                    if (size > 0)
                    {
                        found = pos + (long)i;
                        break;
                    }
                }
            }
            if (found < 0) break;
            g_Movies.push_back({ path, found, size });
            pos = found + size;
        }
        fclose(f);
    }

    void IndexFolder(const std::string& dir)
    {
        std::vector<std::string> files;
        DIR* d = opendir(dir.c_str());
        if (!d) return;
        while (struct dirent* e = readdir(d))
        {
            std::string n = ToLower(e->d_name);
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".smk") == 0) files.push_back(dir + "/" + e->d_name);
        }
        closedir(d);
        std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return ToLower(a) < ToLower(b); });
        for (const std::string& p : files) g_Movies.push_back({ p, 0, 0 });
    }

    void ParseIndexList(const char* text, std::vector<int>& out)
    {
        out.clear();
        if (!text) return;
        std::string s(text);
        for (char& c : s) if (c == ',' || c == ';') c = ' ';
        size_t pos = 0;
        while (pos < s.size())
        {
            while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
            size_t end = pos;
            while (end < s.size() && !std::isspace((unsigned char)s[end])) ++end;
            if (end > pos) out.push_back(atoi(s.substr(pos, end - pos).c_str()));
            pos = end;
        }
    }

    // Optional override, e.g.:
    // <Movies>
    //   <Startup>0 1</Startup>            (empty element = no startup movies)
    //   <AfterLevel level="2">2</AfterLevel>
    // </Movies>
    void LoadSchedule()
    {
        std::string path = g_UserDirectory + "movies.xml";
        if (!IsFile(path)) return;
        TiXmlDocument doc;
        if (!doc.LoadFile(path.c_str()))
        {
            LOG_WARNING("movies.xml could not be parsed, using the default movie schedule");
            return;
        }
        TiXmlElement* root = doc.RootElement();
        if (!root) return;
        if (TiXmlElement* e = root->FirstChildElement("Startup")) ParseIndexList(e->GetText(), g_StartupMovies);
        bool anyLevel = false;
        std::map<int, std::vector<int>> levels;
        for (TiXmlElement* e = root->FirstChildElement("AfterLevel"); e; e = e->NextSiblingElement("AfterLevel"))
        {
            int level = 0;
            if (e->QueryIntAttribute("level", &level) != TIXML_SUCCESS) continue;
            ParseIndexList(e->GetText(), levels[level]);
            anyLevel = true;
        }
        if (anyLevel) g_AfterLevel = levels;
        LOG_WARNING("Movie schedule loaded from " + path);
    }

    void BuildIndex()
    {
        if (g_Indexed) return;
        g_Indexed = true;
        for (const std::string& root : g_Roots)
        {
            if (!IsDir(root)) continue;
            std::string fec = FindChild(root, "claw.fec");
            std::string movies = FindChild(root, "movies");
            if (fec.empty() && !movies.empty()) fec = FindChild(movies, "claw.fec");
            if (!fec.empty() && IsFile(fec))
            {
                IndexContainer(fec);
                if (!g_Movies.empty())
                {
                    LOG_WARNING("Movies: " + std::to_string(g_Movies.size()) + " found in " + fec);
                    break;
                }
            }
            if (!movies.empty() && IsDir(movies))
            {
                IndexFolder(movies);
                if (!g_Movies.empty())
                {
                    LOG_WARNING("Movies: " + std::to_string(g_Movies.size()) + " .SMK files in " + movies);
                    break;
                }
            }
        }
        LoadSchedule();
    }

    //---------------------------------------------------------------- audio
    struct AudioCtx
    {
        SDL_AudioStream* stream = nullptr;
        SDL_mutex* mutex = nullptr;
    };

    void MovieAudioEffect(int, void* buffer, int len, void* udata)
    {
        AudioCtx* ctx = (AudioCtx*)udata;
        int got = 0;
        if (SDL_LockMutex(ctx->mutex) == 0)
        {
            got = SDL_AudioStreamGet(ctx->stream, buffer, len);
            SDL_UnlockMutex(ctx->mutex);
        }
        if (got < 0) got = 0;
        if (got < len) memset((char*)buffer + got, 0, len - got);
    }

    //---------------------------------------------------------------- input
    // A / Start / Enter / tap: skip this movie. B / Select / Escape: stop the sequence.
    SkipKind SkipRequested()
    {
        int b = BlockingInput::Poll();
        if (b & (BlockingInput::Back | BlockingInput::Quit)) return AbortAll;
        if (b & (BlockingInput::Accept | BlockingInput::Start)) return SkipOne;
        return None;
    }

    void InitSkipInput() { BlockingInput::Reset(); }
    void FlushInput() { BlockingInput::Flush(); }
    bool AppAlive() { return BlockingInput::AppAlive(); }

    //---------------------------------------------------------------- playback
    PlayResult PlaySource(SDL_Renderer* r, const MovieSource& m, int index)
    {
        STARTUP_TRACE("movie: open");
        FILE* fp = fopen(m.path.c_str(), "rb");
        if (!fp) return Missing;
        // Frames are read one by one; a big buffer saves many small card reads.
        setvbuf(fp, NULL, _IOFBF, 256 * 1024);
        if (fseek(fp, m.offset, SEEK_SET) != 0)
        {
            fclose(fp);
            return Missing;
        }
        smk s = smk_open_filepointer(fp, SMK_MODE_DISK);   // owns fp from now on
        if (!s)
        {
            LOG_ERROR("Could not open movie in " + m.path + " at " + std::to_string(m.offset));
            return Missing;
        }

        unsigned long frameCount = 0, w = 0, h = 0;
        double usf = 0;
        unsigned char yScale = 0, trackMask = 0, channels[7], depth[7];
        unsigned long rate[7];
        smk_info_all(s, NULL, &frameCount, &usf);
        smk_info_video(s, &w, &h, &yScale);
        smk_info_audio(s, &trackMask, channels, depth, rate);
        if (usf <= 0) usf = 1000000.0 / 15.0;

        smk_enable_all(s, 0);
        smk_enable_video(s, 1);
        bool hasAudio = (trackMask & SMK_AUDIO_TRACK_0) && rate[0] > 0 && channels[0] > 0;
        if (hasAudio) smk_enable_audio(s, 0, 1);

        SDL_Texture* pPrevTarget = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, NULL);
        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, (int)w, (int)h);
        if (tex) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        std::vector<uint32_t> pixels(w * h);

        // Audio: decoded chunks go into an SDL_AudioStream that converts to the
        // mixer's format; an effect on a looping silent chunk feeds it out.
        AudioCtx ctx;
        Mix_Chunk* pSilence = nullptr;
        static Uint8 s_Silence[4096];
        // A channel of its own, added past the engine's channels, so paused
        // game sounds and music can simply resume afterwards.
        int baseChannels = Mix_AllocateChannels(-1);
        int movieChannel = -1;
        if (hasAudio)
        {
            int freq = 0, outCh = 0;
            Uint16 fmt = 0;
            if (Mix_QuerySpec(&freq, &fmt, &outCh))
            {
                // (libsmacker gives 16-bit samples in the CPU's byte order:
                // big-endian on the PS3 - read as little-endian they crackled)
                ctx.stream = SDL_NewAudioStream(depth[0] == 16 ? AUDIO_S16SYS : AUDIO_U8, channels[0], (int)rate[0],
                                                fmt, outCh, freq);
                ctx.mutex = SDL_CreateMutex();
            }
            if (!ctx.stream || !ctx.mutex) hasAudio = false;
        }

        auto pushAudio = [&]()
        {
            if (!hasAudio) return;
            unsigned long size = smk_get_audio_size(s, 0);
            const unsigned char* data = smk_get_audio(s, 0);
            if (size && data && SDL_LockMutex(ctx.mutex) == 0)
            {
                SDL_AudioStreamPut(ctx.stream, data, (int)size);
                SDL_UnlockMutex(ctx.mutex);
            }
        };

        auto drawFrame = [&]()
        {
            if (!tex) return;
            const unsigned char* pal = smk_get_palette(s);
            const unsigned char* src = smk_get_video(s);
            if (pal && src)
            {
                uint32_t lut[256];
                for (int i = 0; i < 256; ++i)
                    lut[i] = 0xFF000000u | (pal[i * 3] << 16) | (pal[i * 3 + 1] << 8) | pal[i * 3 + 2];
                for (unsigned long i = 0; i < w * h; ++i) pixels[i] = lut[src[i]];
                SDL_UpdateTexture(tex, NULL, pixels.data(), (int)(w * 4));
            }
            int outW = 0, outH = 0;
            SDL_GetRendererOutputSize(r, &outW, &outH);
            // The original game showed every movie full screen at 640x480,
            // so always display 4:3 (640x240 files are line-doubled even when
            // their header carries no Y-scale flag).
            (void)yScale;
            double dispW = 4.0, dispH = 3.0;
            double scale = std::min(outW / dispW, outH / dispH);
            SDL_Rect dst = { (int)((outW - dispW * scale) / 2), (int)((outH - dispH * scale) / 2),
                             (int)(dispW * scale), (int)(dispH * scale) };
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            SDL_RenderCopy(r, tex, NULL, &dst);
            Subtitles::Render(r);
            CrtEffect::BeforePresent(r);
            SDL_RenderPresent(r);
        };

#if defined(__PSL1GHT__)
        {
            // PS3: the TV takes a moment to show the picture after the game
            // starts (the video mode is set); the first movie began unseen.
            // A second and a half of black first.
            static bool s_First = true;
            if (s_First)
            {
                s_First = false;
                const Uint32 t0 = SDL_GetTicks();
                while (SDL_GetTicks() - t0 < 1500 && AppAlive())
                {
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                    SDL_RenderClear(r);
                    CrtEffect::BeforePresent(r);
                    SDL_RenderPresent(r);
                }
            }
        }
#endif
        InitSkipInput();
        SkipKind skipped = None;
        STARTUP_TRACE("movie: first frame");
        char status = smk_first(s);
        pushAudio();

        if (hasAudio)
        {
            pSilence = Mix_QuickLoad_RAW(s_Silence, sizeof(s_Silence));
            if (pSilence && Mix_AllocateChannels(baseChannels + 1) == baseChannels + 1)
            {
                movieChannel = baseChannels;
                Mix_Volume(movieChannel, MIX_MAX_VOLUME);
                if (Mix_PlayChannel(movieChannel, pSilence, -1) == -1 ||
                    !Mix_RegisterEffect(movieChannel, MovieAudioEffect, NULL, &ctx))
                {
                    LOG_WARNING("Movie audio unavailable: " + std::string(Mix_GetError()));
                }
            }
        }

        char trackName[32];
        snprintf(trackName, sizeof(trackName), "movie_%02d", index);
        Subtitles::StartTrack(trackName);

        Uint32 start = SDL_GetTicks();
        for (unsigned long frame = 0; status != SMK_ERROR; ++frame)
        {
            Uint32 due = start + (Uint32)(frame * usf / 1000.0);
            while ((Sint32)(SDL_GetTicks() - due) < 0)
            {
                if (!AppAlive()) { skipped = AbortAll; break; }
                skipped = SkipRequested();
                if (skipped != None) break;
                SDL_Delay(2);
            }
            if (skipped) break;
            drawFrame();
            if (frame + 1 >= frameCount) break;
            status = smk_next(s);
            pushAudio();
        }

        if (!skipped && hasAudio)
        {
            // Let the tail of the soundtrack play out (at most a second).
            Uint32 tailStart = SDL_GetTicks();
            while (SDL_GetTicks() - tailStart < 1000 && AppAlive() && SkipRequested() == None)
            {
                int left = 0;
                if (SDL_LockMutex(ctx.mutex) == 0)
                {
                    left = SDL_AudioStreamAvailable(ctx.stream);
                    SDL_UnlockMutex(ctx.mutex);
                }
                if (left <= 0) break;
                SDL_Delay(10);
            }
        }

        if (movieChannel >= 0)
        {
            Mix_UnregisterEffect(movieChannel, MovieAudioEffect);
            Mix_HaltChannel(movieChannel);
            Mix_AllocateChannels(baseChannels);
        }
        if (pSilence) Mix_FreeChunk(pSilence);
        if (ctx.stream) SDL_FreeAudioStream(ctx.stream);
        if (ctx.mutex) SDL_DestroyMutex(ctx.mutex);

        Subtitles::StopTrack();
        if (tex) SDL_DestroyTexture(tex);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        CrtEffect::BeforePresent(r);
        SDL_RenderPresent(r);
        SDL_SetRenderTarget(r, pPrevTarget);
        smk_close(s);
        FlushInput();
        return skipped == AbortAll ? Aborted : (skipped == SkipOne ? Skipped : Played);
    }

    //---------------------------------------------------------------- remastered movies
    // <ReClaw folder>/movies/movie_XX.mp4 (H.264 + AAC, builds with FFmpeg)
    // or movie_XX.mpg (MPEG-1 + MP2, everywhere) replace movie XX of CLAW.FEC.
    // Guidelines: docs/FILMY.md. Not on Vita / 3DS (Platform::SupportsRemasteredMovies).
    std::string RemasterPath(int index, bool* isMp4)
    {
        if (!Platform::SupportsRemasteredMovies()) return "";
        std::string dir = Platform::RootDir() + "movies";
        if (!IsDir(dir)) return "";
        char name[32];
        if (RemasterMovies::HasFfmpeg())
        {
            snprintf(name, sizeof(name), "movie_%02d.mp4", index);
            std::string p = FindChild(dir, name);
            if (!p.empty() && IsFile(p)) { *isMp4 = true; return p; }
        }
        snprintf(name, sizeof(name), "movie_%02d.mpg", index);
        std::string p = FindChild(dir, name);
        if (!p.empty() && IsFile(p)) { *isMp4 = false; return p; }
        return "";
    }

    int RemasterCount()
    {
        int count = 0;
        bool mp4 = false;
        for (int i = 0; i < 64; ++i) if (!RemasterPath(i, &mp4).empty()) count = i + 1;
        return count;
    }

    // YUV 4:2:0 -> ARGB for renderers without YUV textures.
    void YuvToArgb(const RemasterMovies::Frame& f, std::vector<uint32_t>& out)
    {
        out.resize((size_t)f.width * f.height);
        for (int y = 0; y < f.height; ++y)
        {
            const uint8_t* py = f.y + (size_t)y * f.yPitch;
            const uint8_t* pu = f.u + (size_t)(y / 2) * f.uPitch;
            const uint8_t* pv = f.v + (size_t)(y / 2) * f.vPitch;
            uint32_t* d = &out[(size_t)y * f.width];
            for (int x = 0; x < f.width; ++x)
            {
                int c = (py[x] - 16) * 298, u = pu[x / 2] - 128, v = pv[x / 2] - 128;
                int r = (c + 409 * v + 128) >> 8, g = (c - 100 * u - 208 * v + 128) >> 8, b = (c + 516 * u + 128) >> 8;
                r = r < 0 ? 0 : (r > 255 ? 255 : r);
                g = g < 0 ? 0 : (g > 255 ? 255 : g);
                b = b < 0 ? 0 : (b > 255 ? 255 : b);
                d[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }

    PlayResult PlayRemaster(SDL_Renderer* r, const std::string& path, bool isMp4, int index)
    {
        std::unique_ptr<RemasterMovies::Decoder> dec =
            isMp4 ? RemasterMovies::CreateFfmpegDecoder() : RemasterMovies::CreateMpeg1Decoder();
        if (!dec || !dec->Open(path))
        {
            LOG_WARNING("Could not open the remastered movie " + path);
            return Missing;
        }
        LOG("Remastered movie: " + path);

        SDL_Texture* pPrevTarget = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, NULL);
        SDL_Texture* tex = nullptr;
        bool yuvTexture = true;
        int texW = 0, texH = 0;
        std::vector<uint32_t> rgb;

        // Sound: as for the original movies, through an SDL_AudioStream fed
        // out by an effect on a silent looping chunk on a channel of its own.
        AudioCtx ctx;
        Mix_Chunk* pSilence = nullptr;
        static Uint8 s_Silence[4096];
        int baseChannels = Mix_AllocateChannels(-1);
        int movieChannel = -1;
        bool hasAudio = dec->AudioRate() > 0 && dec->AudioChannels() > 0;
        if (hasAudio)
        {
            int freq = 0, outCh = 0;
            Uint16 fmt = 0;
            if (Mix_QuerySpec(&freq, &fmt, &outCh))
            {
                ctx.stream = SDL_NewAudioStream(AUDIO_F32SYS, (Uint8)dec->AudioChannels(), dec->AudioRate(), fmt, outCh, freq);
                ctx.mutex = SDL_CreateMutex();
            }
            if (!ctx.stream || !ctx.mutex) hasAudio = false;
        }
        const int audioChannels = dec->AudioChannels();
        RemasterMovies::AudioSink sink = [&](const float* samples, int frames)
        {
            if (!hasAudio || frames <= 0) return;
            if (SDL_LockMutex(ctx.mutex) == 0)
            {
                SDL_AudioStreamPut(ctx.stream, samples, frames * audioChannels * (int)sizeof(float));
                SDL_UnlockMutex(ctx.mutex);
            }
        };

        InitSkipInput();
        SkipKind skipped = None;
        RemasterMovies::Frame frame;
        bool have = dec->NextFrame(frame, sink);

        if (hasAudio)
        {
            pSilence = Mix_QuickLoad_RAW(s_Silence, sizeof(s_Silence));
            if (pSilence && Mix_AllocateChannels(baseChannels + 1) == baseChannels + 1)
            {
                movieChannel = baseChannels;
                Mix_Volume(movieChannel, MIX_MAX_VOLUME);
                if (Mix_PlayChannel(movieChannel, pSilence, -1) == -1 ||
                    !Mix_RegisterEffect(movieChannel, MovieAudioEffect, NULL, &ctx))
                {
                    LOG_WARNING("Movie audio unavailable: " + std::string(Mix_GetError()));
                }
            }
        }

        char trackName[32];
        snprintf(trackName, sizeof(trackName), "movie_%02d", index);
        Subtitles::StartTrack(trackName);

        Uint32 start = SDL_GetTicks();
        int late = 0, frames = 0;
        // (for the log: how long decoding and showing a picture take)
        Uint64 decodeTicks = 0, pictureTicks = 0;
        const double perfMs = 1000.0 / (double)SDL_GetPerformanceFrequency();
        while (have)
        {
            Uint32 due = start + (Uint32)(frame.time * 1000.0);
            while ((Sint32)(SDL_GetTicks() - due) < 0)
            {
                if (!AppAlive()) { skipped = AbortAll; break; }
                skipped = SkipRequested();
                if (skipped != None) break;
                SDL_Delay(1);
            }
            if (skipped) break;
            ++frames;
            // Counted for the log: pictures shown more than two frames late.
            if ((Sint32)(SDL_GetTicks() - due) > 66) ++late;

            if (!tex || texW != frame.width || texH != frame.height)
            {
                if (tex) SDL_DestroyTexture(tex);
                texW = frame.width;
                texH = frame.height;
                tex = yuvTexture ? SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, texW, texH) : nullptr;
                if (!tex)
                {
                    yuvTexture = false;
                    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, texW, texH);
                }
                if (tex) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
            }
            const Uint64 pictureT0 = SDL_GetPerformanceCounter();
            if (tex)
            {
                if (yuvTexture)
                {
                    SDL_UpdateYUVTexture(tex, NULL, frame.y, frame.yPitch, frame.u, frame.uPitch, frame.v, frame.vPitch);
                }
                else
                {
                    YuvToArgb(frame, rgb);
                    SDL_UpdateTexture(tex, NULL, rgb.data(), texW * 4);
                }
                int outW = 0, outH = 0;
                SDL_GetRendererOutputSize(r, &outW, &outH);
                double scale = std::min((double)outW / texW, (double)outH / texH);
                SDL_Rect dst = { (int)((outW - texW * scale) / 2), (int)((outH - texH * scale) / 2),
                                 (int)(texW * scale), (int)(texH * scale) };
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                SDL_RenderClear(r);
                SDL_RenderCopy(r, tex, NULL, &dst);
                Subtitles::Render(r);
                CrtEffect::BeforePresent(r);
                SDL_RenderPresent(r);
            }
            const Uint64 decodeT0 = SDL_GetPerformanceCounter();
            pictureTicks += decodeT0 - pictureT0;
            have = dec->NextFrame(frame, sink);
            // Too slow to decode: drop pictures until we catch up.
            while (have && (Sint32)(SDL_GetTicks() - (start + (Uint32)(frame.time * 1000.0))) > 100)
            {
                have = dec->NextFrame(frame, sink);
            }
            decodeTicks += SDL_GetPerformanceCounter() - decodeT0;
        }
        if (frames > 0)
        {
            char line[200];
            snprintf(line, sizeof(line), "Remastered movie: %d pictures %dx%d, %d late; per picture: decoding %.1f ms, showing %.1f ms (incl. waiting for the screen)",
                     frames, texW, texH, late, decodeTicks * perfMs / frames, pictureTicks * perfMs / frames);
            LOG_WARNING(line);
        }
        if (frames > 0 && late * 10 > frames)
        {
            LOG_WARNING("Remastered movie played late in " + std::to_string(late) + " of " + std::to_string(frames) +
                        " frames: too heavy for this console (try a smaller .mpg)");
        }

        if (!skipped && hasAudio)
        {
            Uint32 tailStart = SDL_GetTicks();
            while (SDL_GetTicks() - tailStart < 1500 && AppAlive() && SkipRequested() == None)
            {
                int left = 0;
                if (SDL_LockMutex(ctx.mutex) == 0)
                {
                    left = SDL_AudioStreamAvailable(ctx.stream);
                    SDL_UnlockMutex(ctx.mutex);
                }
                if (left <= 0) break;
                SDL_Delay(10);
            }
        }

        if (movieChannel >= 0)
        {
            Mix_UnregisterEffect(movieChannel, MovieAudioEffect);
            Mix_HaltChannel(movieChannel);
            Mix_AllocateChannels(baseChannels);
        }
        if (pSilence) Mix_FreeChunk(pSilence);
        if (ctx.stream) SDL_FreeAudioStream(ctx.stream);
        if (ctx.mutex) SDL_DestroyMutex(ctx.mutex);

        Subtitles::StopTrack();
        if (tex) SDL_DestroyTexture(tex);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        CrtEffect::BeforePresent(r);
        SDL_RenderPresent(r);
        SDL_SetRenderTarget(r, pPrevTarget);
        FlushInput();
        return skipped == AbortAll ? Aborted : (skipped == SkipOne ? Skipped : Played);
    }

    // A movie by index: the remastered file if there is one, else CLAW.FEC.
    PlayResult PlayIndex(SDL_Renderer* r, int index)
    {
        bool isMp4 = false;
        std::string remaster = RemasterPath(index, &isMp4);
        if (!remaster.empty())
        {
            PlayResult res = PlayRemaster(r, remaster, isMp4, index);
            if (res != Missing) return res;
        }
        if (index < 0 || index >= (int)g_Movies.size()) return Missing;
        return PlaySource(r, g_Movies[index], index);
    }

    int MovieCount()
    {
        return std::max((int)g_Movies.size(), RemasterCount());
    }

    // Plays a list of movies; B / + stops the rest of the list.
    void PlayList(SDL_Renderer* r, const std::vector<int>& list)
    {
        for (int index : list)
        {
            if (!AppAlive() || index < 0 || index >= MovieCount()) continue;
            LOG("Playing movie " + std::to_string(index));
            if (PlayIndex(r, index) == Aborted) break;
        }
    }
}

namespace MoviePlayer
{
    void SetSearchRoots(const std::vector<std::string>& roots, const std::string& userDirectory)
    {
        g_Roots = roots;
        g_UserDirectory = userDirectory;
        g_Movies.clear();
        g_Indexed = false;
    }

    bool IsAvailable()
    {
        BuildIndex();
        return MovieCount() > 0;
    }

    int GetMovieCount()
    {
        BuildIndex();
        return MovieCount();
    }

    int PollSkipInput()
    {
        return (int)SkipRequested();
    }

    void FlushSkipInput()
    {
        FlushInput();
    }

    bool PlayMovie(SDL_Renderer* pRenderer, int index)
    {
        BuildIndex();
        if (!pRenderer || index < 0 || index >= MovieCount()) return false;
        LOG("Playing movie " + std::to_string(index));
        return PlayIndex(pRenderer, index) != Missing;
    }

    void PlayStartupMovies(SDL_Renderer* pRenderer)
    {
        BuildIndex();
        if (pRenderer) PlayList(pRenderer, g_StartupMovies);
    }

    void PlayAfterLevel(SDL_Renderer* pRenderer, int levelNumber)
    {
        BuildIndex();
        auto it = g_AfterLevel.find(levelNumber);
        if (pRenderer && it != g_AfterLevel.end()) PlayList(pRenderer, it->second);
    }

    void PlayReplay(SDL_Renderer* pRenderer, int lastFinishedLevel)
    {
        BuildIndex();
        if (!pRenderer) return;
        std::vector<int> list = g_StartupMovies;
        for (const auto& entry : g_AfterLevel)   // std::map: ascending level order
        {
            if (entry.first <= lastFinishedLevel)
            {
                list.insert(list.end(), entry.second.begin(), entry.second.end());
            }
        }
        PlayList(pRenderer, list);
    }
}

#else

namespace MoviePlayer
{
    void SetSearchRoots(const std::vector<std::string>&, const std::string&) { }
    bool IsAvailable() { return false; }
    void PlayStartupMovies(SDL_Renderer*) { }
    void PlayAfterLevel(SDL_Renderer*, int) { }
    void PlayReplay(SDL_Renderer*, int) { }
    bool PlayMovie(SDL_Renderer*, int) { return false; }
    int GetMovieCount() { return 0; }
    int PollSkipInput() { return 0; }
    void FlushSkipInput() { }
}

#endif
