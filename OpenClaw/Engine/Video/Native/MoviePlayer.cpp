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
#include <sys/stat.h>

#include <tinyxml.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

extern "C" {
#include "../../../../ThirdParty/libsmacker/smacker.h"
}

#include "../../Logger/Logger.h"

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
#ifdef __SWITCH__
    PadState g_Pad;
    bool g_PadReady = false;
#endif

    void InitSkipInputOnce()
    {
#ifdef __SWITCH__
        if (!g_PadReady)
        {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&g_Pad);
            g_PadReady = true;
            padUpdate(&g_Pad);
        }
#endif
    }

    void InitSkipInput()
    {
#ifdef __SWITCH__
        if (!g_PadReady)
        {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&g_Pad);
            g_PadReady = true;
        }
        padUpdate(&g_Pad);   // ignore buttons already held when the movie starts
#endif
    }

    // A / Enter / Space / tap: skip this movie. B / + / Escape: stop the sequence.
    SkipKind SkipRequested()
    {
        SkipKind kind = None;
        auto take = [&kind](SkipKind k) { if (k > kind) kind = k; };
#ifdef __SWITCH__
        padUpdate(&g_Pad);
        u64 down = padGetButtonsDown(&g_Pad);
        if (down & HidNpadButton_A) take(SkipOne);
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) take(AbortAll);
#endif
        SDL_PumpEvents();
        SDL_Event ev[16];
        // Only look at input events; device add/remove events stay queued for the game.
        int n = SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYDOWN);
        for (int i = 0; i < n; ++i)
        {
            SDL_Keycode k = ev[i].key.keysym.sym;
            if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER) take(SkipOne);
            if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) take(AbortAll);
        }
        n = SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONDOWN);
        for (int i = 0; i < n; ++i)
        {
            Uint8 b = ev[i].cbutton.button;
            take((b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK) ? AbortAll : SkipOne);
        }
        if (SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_FINGERDOWN, SDL_FINGERDOWN) > 0) take(SkipOne);
        if (SDL_PeepEvents(ev, 16, SDL_GETEVENT, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONDOWN) > 0) take(SkipOne);
        if (SDL_PeepEvents(ev, 16, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) > 0) take(AbortAll);
        return kind;
    }

    void FlushInput()
    {
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_KEYDOWN, SDL_TEXTINPUT);
        SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEWHEEL);
        SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_JOYBUTTONUP);
        SDL_FlushEvents(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERBUTTONUP);
        SDL_FlushEvents(SDL_FINGERDOWN, SDL_FINGERMOTION);
    }

    bool AppAlive()
    {
#ifdef __SWITCH__
        return appletMainLoop();
#else
        return true;
#endif
    }

    //---------------------------------------------------------------- playback
    PlayResult PlaySource(SDL_Renderer* r, const MovieSource& m)
    {
        FILE* fp = fopen(m.path.c_str(), "rb");
        if (!fp) return Missing;
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
                ctx.stream = SDL_NewAudioStream(depth[0] == 16 ? AUDIO_S16LSB : AUDIO_U8, channels[0], (int)rate[0],
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
            SDL_RenderPresent(r);
        };

        InitSkipInput();
        SkipKind skipped = None;
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

        if (tex) SDL_DestroyTexture(tex);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        SDL_RenderPresent(r);
        SDL_SetRenderTarget(r, pPrevTarget);
        smk_close(s);
        FlushInput();
        return skipped == AbortAll ? Aborted : (skipped == SkipOne ? Skipped : Played);
    }

    // Plays a list of movies; B / + stops the rest of the list.
    void PlayList(SDL_Renderer* r, const std::vector<int>& list)
    {
        for (int index : list)
        {
            if (!AppAlive() || index < 0 || index >= (int)g_Movies.size()) continue;
            LOG("Playing movie " + std::to_string(index));
            if (PlaySource(r, g_Movies[index]) == Aborted) break;
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
        return !g_Movies.empty();
    }

    int GetMovieCount()
    {
        BuildIndex();
        return (int)g_Movies.size();
    }

    int PollSkipInput()
    {
        InitSkipInputOnce();
        return (int)SkipRequested();
    }

    void FlushSkipInput()
    {
        FlushInput();
    }

    bool PlayMovie(SDL_Renderer* pRenderer, int index)
    {
        BuildIndex();
        if (!pRenderer || index < 0 || index >= (int)g_Movies.size()) return false;
        LOG("Playing movie " + std::to_string(index));
        return PlaySource(pRenderer, g_Movies[index]) != Missing;
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
