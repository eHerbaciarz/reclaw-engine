#include "Platform.h"

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
    std::string g_DataDir;
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
        // Creates every level of the path ("ux0:data/claw/" -> "ux0:data", ...).
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
#else
        return false;
#endif
    }

    void Init(int argc, char** argv)
    {
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
        g_DataDir = "/switch/claw/";
        g_BundleDir = "romfs:/";
#elif defined(__vita__)
        g_DataDir = "ux0:data/claw/";
        g_BundleDir = "app0:/";
#elif defined(__3DS__)
        romfsInit();
        g_DataDir = "sdmc:/3ds/claw/";
        g_BundleDir = "romfs:/";
#elif defined(__WIIU__)
        g_DataDir = "fs:/vol/external01/wiiu/apps/claw/";
        g_BundleDir = "fs:/vol/content/";   // the .wuhb's content folder
#elif defined(__PSL1GHT__) || defined(__PS3__)
        g_DataDir = "/dev_hdd0/game/CLAW00001/USRDIR/";
        g_BundleDir = g_DataDir;
#elif defined(__PS4__)
        // Homebrew convention: the app's files in /data/<name>/; the .pkg's
        // own files are mounted at /app0/.
        g_DataDir = "/data/claw/";
        g_BundleDir = "/app0/";
#elif defined(__ANDROID__)
        {
            // CLAW.REZ, config.xml and saves: Android/data/<app>/files/
            // (reachable over USB and from file managers).
            const char* external = SDL_AndroidGetExternalStoragePath();
            const char* internal = SDL_AndroidGetInternalStoragePath();
            g_DataDir = WithSlash(external ? external : (internal ? internal : "/sdcard/claw"));
            g_BundleDir = WithSlash(internal ? internal : g_DataDir.c_str()) + "bundle/";
            MakeDirs(g_BundleDir);
            ExtractAndroidBundle(g_BundleDir);
        }
#elif defined(_WIN32) || defined(__EMSCRIPTEN__)
        g_DataDir = g_BinaryDir;
        g_BundleDir = g_BinaryDir;
#else
        // Linux: a portable folder (config.xml next to the binary) or the
        // per-user folder.
        g_BundleDir = g_BinaryDir;
        if (std::ifstream(g_BinaryDir + "config.xml").good())
        {
            g_DataDir = g_BinaryDir;
        }
        else
        {
            const char* home = getenv("HOME");
            if (!home)
            {
                struct passwd* pw = getpwuid(getuid());
                home = pw ? pw->pw_dir : ".";
            }
            g_DataDir = std::string(home) + "/.config/openclaw/";
        }
#endif

        // Nintendo consoles: SDL reports the buttons by their printed labels
        // (A = right); the menus swap A/B for that (see SdlGamepadInput).
#ifdef SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS
        if (Buttons() == ButtonLabels::Nintendo)
        {
            SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "1");
        }
#endif

        if (IsConsole())
        {
            MakeDirs(g_DataDir);
            g_pLogFile = fopen((g_DataDir + "openclaw.log").c_str(), "w");
            SDL_LogSetOutputFunction(&ConsoleLogOutput, NULL);
            SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
            SDL_Log("Captain Claw (OpenClaw) for %s - log started", Name());
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
        folders.push_back(g_DataDir);
#if defined(__SWITCH__)
        // Ryujinx layout: sdcard/Games/claw/ next to openclaw.nro.
        folders.push_back("/Games/claw/");
#elif defined(__3DS__)
        folders.push_back("sdmc:/claw/");
#elif defined(__WIIU__)
        folders.push_back("fs:/vol/external01/claw/");
#elif defined(__vita__)
        folders.push_back("uma0:data/claw/");
#endif
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
