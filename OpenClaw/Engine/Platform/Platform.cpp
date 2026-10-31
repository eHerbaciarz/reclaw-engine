#include "Platform.h"
#include "../GameApp/ReclawVersion.h"
#include "../Util/PortSettings.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

#if defined(__SWITCH__)
#include <switch.h>
#elif defined(__vita__)
#include <psp2/kernel/clib.h>
#include <psp2/io/stat.h>
#elif defined(__3DS__)
#include <3ds.h>
// Memory: libctru gives the linear heap (GPU / DSP memory) 32 MB by default,
// but the game draws in software and uses only ~2 MB of it. 8 MB leaves the
// rest (~24 MB more) to the normal heap, which ran out loading a level.
extern "C" { u32 __ctru_linear_heap_size = 8 * 1024 * 1024; }
#elif defined(__WIIU__)
#include <coreinit/debug.h>
#elif defined(__PS3__) || defined(__PSL1GHT__)
#include <sys/file.h>
#include <sys/process.h>
// Main thread priority and a 1 MB stack (the default is small).
SYS_PROCESS_PARAM(1001, 0x100000);
#endif

#if !defined(_WIN32)
#include <unistd.h>
#if !defined(__vita__) && !defined(__3DS__) && !defined(__WIIU__) && !defined(__SWITCH__) && !defined(__PSL1GHT__) && !defined(__PS4__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <pwd.h>
#endif
#endif

#if defined(__vita__)
#include <psp2/power.h>
// The default newlib heap of a Vita application is small; the resource cache,
// the level and the textures need much more.
extern "C" {
    int _newlib_heap_size_user = 256 * 1024 * 1024;
    unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
}
#endif

#if defined(_WIN32)
#include <direct.h>
#define PLATFORM_MKDIR(p) _mkdir(p)
#else
#define PLATFORM_MKDIR(p) mkdir(p, 0777)
#endif

namespace
{
    std::string g_RootDir;     // reclaw/
    std::string g_DataDir;     // reclaw/save/ (the player's own files)
    std::string g_BundleDir;
    std::string g_BinaryDir;
    FILE* g_pLogFile = nullptr;

    std::string WithSlash(std::string dir)
    {
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
        return dir;
    }

    void MakeDirs(const std::string& path)
    {
        // Creates every level of the path ("ux0:data/reclaw/" -> "ux0:data", ...).
        for (size_t i = 1; i < path.size(); ++i)
        {
            if (path[i] == '/' && path[i - 1] != ':')
            {
                PLATFORM_MKDIR(path.substr(0, i).c_str());
            }
        }
        PLATFORM_MKDIR(path.c_str());
    }

    bool CopyFile(const std::string& from, const std::string& to)
    {
        std::ifstream in(from, std::ios::binary);
        if (!in.good()) return false;
        std::ofstream out(to, std::ios::binary);
        out << in.rdbuf();
        return out.good();
    }

#if defined(__ANDROID__)
    // Files packed into the APK can only be read through SDL_RWops, while the
    // engine opens its files by path. The bundled files are copied out of the
    // APK into the app's internal folder (again after an update of the app).
    void ExtractAndroidBundle(const std::string& to)
    {
        static const char* kFiles[] = { "config.xml", "ASSETS.ZIP", "clacon.ttf", "console02.tga",
                                        "splash.ttf", "subtitles.ttf", "music/soundfont.sf3" };
        const std::string stamp = std::string(__DATE__ " " __TIME__);
        {
            std::ifstream in(to + "bundle.stamp");
            std::string have;
            std::getline(in, have);
            if (have == stamp) return;
        }
        MakeDirs(to + "music/");
        for (const char* name : kFiles)
        {
            SDL_RWops* src = SDL_RWFromFile(name, "rb");
            if (!src)
            {
                SDL_Log("Android bundle: %s not in the APK", name);
                continue;
            }
            std::ofstream out(to + name, std::ios::binary);
            char buf[64 * 1024];
            size_t n;
            while ((n = SDL_RWread(src, buf, 1, sizeof(buf))) > 0) out.write(buf, n);
            SDL_RWclose(src);
        }
        std::ofstream(to + "bundle.stamp") << stamp << "\n";
    }
#endif

    // One sink for every SDL_Log message on consoles: a log file in the
    // player's folder plus the system debug output (emulator logs).
    void ConsoleLogOutput(void*, int, SDL_LogPriority priority, const char* message)
    {
        static const char* kPrefix[] = { "", "VERBOSE: ", "DEBUG: ", "INFO: ", "WARN: ", "ERROR: ", "CRITICAL: " };
        const char* prefix = (priority > 0 && priority < SDL_NUM_LOG_PRIORITIES) ? kPrefix[priority] : "";
        if (g_pLogFile)
        {
            fprintf(g_pLogFile, "%s%s\n", prefix, message);
            fflush(g_pLogFile);
        }
        std::string line = std::string(prefix) + message;
#if defined(__SWITCH__)
        svcOutputDebugString(line.c_str(), line.size());
#elif defined(__vita__)
        sceClibPrintf("%s\n", line.c_str());
#elif defined(__WIIU__)
        OSReport("%s\n", line.c_str());
#else
        fprintf(stderr, "%s\n", line.c_str());
#endif
    }
}

namespace Platform
{
    const char* Name()
    {
#if defined(__SWITCH__)
        return "switch";
#elif defined(__vita__)
        return "vita";
#elif defined(__3DS__)
        return "3ds";
#elif defined(__WIIU__)
        return "wiiu";
#elif defined(__PSL1GHT__) || defined(__PS3__)
        return "ps3";
#elif defined(__PS4__)
        return "ps4";
#elif defined(__ANDROID__)
        return "android";
#elif defined(__EMSCRIPTEN__)
        return "web";
#elif defined(_WIN32)
        return "windows";
#else
        return "linux";
#endif
    }

    bool IsConsole()
    {
#if defined(__SWITCH__) || defined(__vita__) || defined(__3DS__) || defined(__WIIU__) || defined(__PSL1GHT__) || defined(__PS3__) || defined(__PS4__)
        return true;
#elif defined(OPENCLAW_PORTMASTER) || defined(OPENCLAW_STEAMDECK)
        // Linux handhelds and the Steam Deck: full screen, a gamepad, no
        // desktop to show message boxes on (the console behaviour: the log in
        // save/, errors on the game screen, config.xml seeded into save/).
        return true;
#else
        return false;
#endif
    }

    void Init(int argc, char** argv)
    {
#if defined(__vita__)
        // Applications start at 333 MHz; the game needs the full speed of the
        // CPU (444 MHz) and the faster bus and GPU clocks, like other ports.
        scePowerSetArmClockFrequency(444);
        scePowerSetBusClockFrequency(222);
        scePowerSetGpuClockFrequency(222);
        scePowerSetGpuXbarClockFrequency(166);
        // The rear touch pad is touched by accident while holding the Vita:
        // SDL ignores it (read before SDL_Init).
        SDL_setenv("VITA_DISABLE_TOUCH_BACK", "1", 1);
#elif defined(__3DS__)
        // New 3DS / 2DS XL: 804 MHz and the L2 cache (no effect on old models).
        osSetSpeedupEnable(true);
#endif
        // Folder of the binary (desktop builds look for their files there).
        if (char* base = SDL_GetBasePath())
        {
            g_BinaryDir = WithSlash(base);
            SDL_free(base);
        }
        else if (argc > 0 && argv && argv[0])
        {
            std::string p = argv[0];
            size_t slash = p.find_last_of("/\\");
            if (slash != std::string::npos) g_BinaryDir = p.substr(0, slash + 1);
        }

#if defined(__SWITCH__)
        romfsInit();
        g_RootDir = "/switch/reclaw/";
        g_BundleDir = "romfs:/";
#elif defined(__vita__)
        g_RootDir = "ux0:data/reclaw/";
        g_BundleDir = "app0:/";
#elif defined(__3DS__)
        romfsInit();
        g_RootDir = "sdmc:/3ds/reclaw/";
        g_BundleDir = "romfs:/";
#elif defined(__WIIU__)
        g_RootDir = "fs:/vol/external01/wiiu/apps/reclaw/";
        g_BundleDir = "fs:/vol/content/";   // the .wuhb's content folder
#elif defined(__PSL1GHT__) || defined(__PS3__)
        g_RootDir = "/dev_hdd0/game/RCLW00001/USRDIR/";
        g_BundleDir = g_RootDir;
#elif defined(__PS4__)
        // Homebrew convention: the app's files in /data/<name>/; the .pkg's
        // own files are mounted at /app0/.
        g_RootDir = "/data/reclaw/";
        g_BundleDir = "/app0/";
#elif defined(__ANDROID__)
        {
            // The player's folder: Android/data/<app>/files/ (reachable over
            // USB and from file managers).
            const char* external = SDL_AndroidGetExternalStoragePath();
            const char* internal = SDL_AndroidGetInternalStoragePath();
            g_RootDir = WithSlash(external ? external : (internal ? internal : "/sdcard/reclaw"));
            g_BundleDir = WithSlash(internal ? internal : g_RootDir.c_str()) + "bundle/";
            MakeDirs(g_BundleDir);
            ExtractAndroidBundle(g_BundleDir);
        }
#elif defined(_WIN32) || defined(__EMSCRIPTEN__)
        g_RootDir = g_BinaryDir;
        g_BundleDir = g_BinaryDir;
#else
        // Linux: a portable folder (data/ or save/ next to the binary) or the
        // per-user folder.
        g_BundleDir = g_BinaryDir;
        struct stat st;
        if ((stat((g_BinaryDir + "data").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ||
            (stat((g_BinaryDir + "save").c_str(), &st) == 0 && S_ISDIR(st.st_mode)))
        {
            g_RootDir = g_BinaryDir;
        }
        else
        {
            const char* home = getenv("HOME");
            if (!home)
            {
                struct passwd* pw = getpwuid(getuid());
                home = pw ? pw->pw_dir : ".";
            }
            g_RootDir = std::string(home) + "/.config/reclaw/";
        }
#endif
        g_DataDir = g_RootDir + "save/";

#if defined(__ANDROID__)
        // The tilt sensor is not a controller (it would hide the touch controls).
        SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#endif

        // Nintendo consoles: SDL reports the buttons by their printed labels
        // (A = right); the menus swap A/B for that (see SdlGamepadInput).
#ifdef SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS
        if (Buttons() == ButtonLabels::Nintendo)
        {
            SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "1");
        }
#endif

        // The folder layout (empty folders show the player where things go):
        //   data/<xx>/ CLAW.REZ, CLAW.FEC (or an unpacked Assets folder) per language
        //   movies/    remastered movies
        //   custom/    custom levels (.WWD)
        //   lang/<xx>/subtitles/movies, lang/<xx>/subtitles/dialogue, lang/<xx>/menu
        //   save/      config.xml, SAVES.XML, port_settings.txt, reclaw.log
#if !defined(__EMSCRIPTEN__)
        {
            static const char* kDirs[] = {
                "data/", "data/pl/", "data/en/", "movies/", "custom/", "save/",
                "lang/pl/subtitles/movies/", "lang/pl/subtitles/dialogue/", "lang/pl/menu/",
                "lang/en/subtitles/movies/", "lang/en/subtitles/dialogue/", "lang/en/menu/",
            };
            for (const char* d : kDirs) MakeDirs(g_RootDir + d);
        }
#endif

        if (IsConsole())
        {
            MakeDirs(g_DataDir);
            g_pLogFile = fopen((g_DataDir + "reclaw.log").c_str(), "w");
            SDL_LogSetOutputFunction(&ConsoleLogOutput, NULL);
            SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
            SDL_Log("ReClaw " RECLAW_VERSION " " RECLAW_STAGE_EN " for %s - log started", Name());
#if defined(__3DS__)
            // Diagnostics for the sound start (crash in ndspInit): the DSP
            // firmware file, then ndsp alone, before SDL touches anything.
            // Skipped after a crash (audio_start.flag, see SDL2MixerSystem).
            {
                FILE* fw = fopen("sdmc:/3ds/dspfirm.cdc", "rb");
                if (fw)
                {
                    unsigned char hdr[0x110] = {};
                    size_t got = fread(hdr, 1, sizeof(hdr), fw);
                    fseek(fw, 0, SEEK_END);
                    long size = ftell(fw);
                    fclose(fw);
                    SDL_Log("dspfirm.cdc: %ld bytes, magic '%.4s' (should be DSP1), header size %u",
                            size, got >= 0x104 ? (const char*)hdr + 0x100 : "?",
                            got >= 0x108 ? (unsigned)(hdr[0x104] | hdr[0x105] << 8 | hdr[0x106] << 16 | hdr[0x107] << 24) : 0u);
                }
                else
                {
                    SDL_Log("dspfirm.cdc: not found");
                }
                FILE* flag = fopen((g_DataDir + "audio_start.flag").c_str(), "rb");
                if (flag)
                {
                    fclose(flag);
                    SDL_Log("ndsp probe skipped (the sound start crashed last time)");
                    // Silent start: SDL picks its audio driver in SDL_Init, so here.
                    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
                }
                else
                {
                    if (FILE* f = fopen((g_DataDir + "audio_start.flag").c_str(), "wb")) { fputs("ndsp probe\n", f); fclose(f); }
                    SDL_Log("ndsp probe: ndspInit");
                    Result rc = ndspInit();
                    SDL_Log("ndsp probe: ndspInit -> 0x%08lX", (unsigned long)rc);
                    if (R_SUCCEEDED(rc))
                    {
                        svcSleepThread(200 * 1000 * 1000LL);   // let the DSP thread run a moment
                        SDL_Log("ndsp probe: ndspExit");
                        ndspExit();
                    }
                    SDL_Log("ndsp probe: done");
                    remove((g_DataDir + "audio_start.flag").c_str());
                }
            }
#endif
        }
        else
        {
            MakeDirs(g_DataDir);
        }

        // First launch: seed the editable config from the bundled default.
        if (g_DataDir != g_BundleDir && !std::ifstream(g_DataDir + "config.xml").good())
        {
            CopyFile(g_BundleDir + "config.xml", g_DataDir + "config.xml");
        }
    }

    std::string DataDir() { return g_DataDir; }
    std::string RootDir() { return g_RootDir; }
    std::string GameDir() { return g_RootDir + "data/"; }
    std::string MoviesDir() { return g_RootDir + "movies/"; }
    std::string LangDir(const std::string& code) { return g_RootDir + "lang/" + code + "/"; }
    std::string BundleDir() { return g_BundleDir; }
    std::string BundlePath(const std::string& file) { return g_BundleDir + file; }

    std::string SplashFont()
    {
        const std::string candidates[] = {
            BundlePath("splash.ttf"),
            "splash.ttf",
            g_BinaryDir + "splash.ttf",
            "ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf",
            "../../ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf",
        };
        for (const std::string& c : candidates)
        {
            if (std::ifstream(c).good()) return c;
        }
        return candidates[0];
    }

    std::vector<std::string> GameDataFolders(int argc, char** argv)
    {
        std::vector<std::string> folders;
        if (!g_BinaryDir.empty()) folders.push_back(g_BinaryDir);
#if !defined(__vita__) && !defined(__3DS__) && !defined(__WIIU__)
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)))
        {
            std::string dir = WithSlash(cwd);
            if (dir != g_BinaryDir) folders.push_back(dir);
        }
#endif
        // data/ first, then the root folder itself.
        std::vector<std::string> bases = folders;
        folders.clear();
        folders.push_back(GameDir());
        folders.push_back(g_RootDir);
        for (const std::string& b : bases)
        {
            folders.push_back(b + "data/");
            folders.push_back(b);
        }
        (void)argc;
        (void)argv;
        // Without duplicates (the binary's folder is often the data folder).
        std::vector<std::string> unique;
        for (const std::string& f : folders)
        {
            if (std::find(unique.begin(), unique.end(), f) == unique.end()) unique.push_back(f);
        }
        return unique;
    }

    ButtonLabels Buttons()
    {
#if defined(__SWITCH__) || defined(__3DS__) || defined(__WIIU__)
        return ButtonLabels::Nintendo;
#elif defined(__vita__) || defined(__PSL1GHT__) || defined(__PS3__) || defined(__PS4__)
        return ButtonLabels::PlayStation;
#else
        return ButtonLabels::Xbox;
#endif
    }

    const char* AcceptButtonName()
    {
        return Buttons() == ButtonLabels::PlayStation ? "\xC3\x97" : "A";   // U+00D7
    }

    const char* BackButtonName()
    {
        return Buttons() == ButtonLabels::PlayStation ? "O" : "B";
    }

    const char* ControlsStyle()
    {
#if defined(__SWITCH__)
        return "switch";
#elif defined(__3DS__)
        return "3ds";
#elif defined(__WIIU__)
        return "wiiu";
#elif defined(__vita__) || defined(__PSL1GHT__) || defined(__PS3__) || defined(__PS4__)
        return "playstation";
#elif defined(OPENCLAW_STEAMDECK)
        return "steamdeck";
#elif defined(OPENCLAW_PORTMASTER)
        return "handheld";
#elif defined(__ANDROID__)
        return "xbox";
#else
        return "pc";
#endif
    }

    bool HasSecondScreen()
    {
#if defined(__3DS__) || defined(__WIIU__)
        return true;
#else
        return false;
#endif
    }

    bool SupportsRemasteredMovies()
    {
#if defined(__vita__) || defined(__3DS__)
        return false;
#else
        return true;
#endif
    }

    bool SupportsSaveStates()
    {
        // Alpha: save states and rewind are experimental (not tested enough);
        // on with experimental=1 in port_settings.txt / the cheats screen.
        // TODO(MK tests): always false on __vita__ / __3DS__ if too slow.
        return PortSettings::GetBool("experimental", false);
    }

    void DefaultRenderSize(int& width, int& height)
    {
#if defined(__3DS__)
        width = 400; height = 240;     // top screen, 1:1
#elif defined(__vita__)
        width = 854; height = 480;     // scaled to 960x544
#elif defined(__SWITCH__) || defined(__WIIU__) || defined(__PSL1GHT__) || defined(__PS3__) || defined(__PS4__)
        width = 854; height = 480;
#else
        width = 0; height = 0;         // desktop / Android: the window size
#endif
    }
}
