#include "BaseGameApp.h"
#include "MainLoop.h"
#include "../Util/StartupTrace.h"
#include "../Resource/ResourceDirectory.h"
#include <fstream>

#if !(defined(__ANDROID__) || defined(__WINDOWS__))
#include <pwd.h>
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#include <cstdio>
#include <vector>
#include <SDL2/SDL_ttf.h>
#include <climits>

// Everything the player provides or the game writes lives here on the SD card:
// CLAW.REZ (from the player's own copy of the game), config.xml, SAVES.XML and
// the log. Assets that ship with the port (ASSETS.ZIP, fonts, soundfont and the
// default config) are bundled in the NRO's romfs.
static const char* kSwitchDataDir = "/switch/claw/";

static FILE* g_pLogFile = nullptr;

// Single sink for every SDL_Log* message (engine errors/warnings, startup
// trace): appended to /switch/claw/openclaw.log and mirrored to the debug
// output (shown as "Guest" lines in emulator logs such as Ryujinx's).
// Opening the log twice for writing (e.g. freopen of stdout and stderr) fails
// on the Switch filesystem, so there is exactly one handle.
static void SwitchLogOutput(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message)
{
    static const char* kPrefix[] = {"", "VERBOSE: ", "DEBUG: ", "INFO: ", "WARN: ", "ERROR: ", "CRITICAL: "};
    const char* prefix = (priority > 0 && priority < SDL_NUM_LOG_PRIORITIES) ? kPrefix[priority] : "";
    if (g_pLogFile)
    {
        fprintf(g_pLogFile, "%s%s\n", prefix, message);
        fflush(g_pLogFile);
    }
    std::string line = std::string(prefix) + message;
    svcOutputDebugString(line.c_str(), line.size());
}

static bool CopyFile(const std::string& from, const std::string& to)
{
    std::ifstream in(from, std::ios::binary);
    if (!in.good())
    {
        return false;
    }
    std::ofstream out(to, std::ios::binary);
    out << in.rdbuf();
    return out.good();
}

static void SwitchPlatformInit()
{
    romfsInit();
    mkdir("/switch", 0777);
    mkdir(kSwitchDataDir, 0777);

    // No console on the Switch: keep warnings/errors in a log next to the saves.
    std::string logPath = std::string(kSwitchDataDir) + "openclaw.log";
    g_pLogFile = fopen(logPath.c_str(), "w");
    SDL_LogSetOutputFunction(&SwitchLogOutput, NULL);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
    SDL_Log("Captain Claw (OpenClaw) for Nintendo Switch - log started");

    // First launch: seed the editable config from the bundled default.
    std::string configPath = std::string(kSwitchDataDir) + "config.xml";
    if (!std::ifstream(configPath).good())
    {
        CopyFile("romfs:/config.xml", configPath);
    }
}

static bool FileExists(const std::string& path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string DirectoryOf(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Players often copy CLAW.REZ next to the NRO instead of into /switch/claw/.
// Accept it there too, and accept an "Assets" folder with the unpacked
// contents of CLAW.REZ (Polish "Kapitan Pazur" release) in the same places.
// Returns the paths that were checked, for error reporting.
static std::vector<std::string> ResolveRezArchive(GameOptions* pOptions, int argc, char** argv)
{
    std::vector<std::string> folders;
    if (argc > 0 && argv && argv[0])
    {
        std::string nroDir = DirectoryOf(argv[0]);
        if (!nroDir.empty()) folders.push_back(nroDir);
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)))
    {
        std::string dir(cwd);
        if (!dir.empty() && dir.back() != '/') dir += '/';
        folders.push_back(dir);
    }
    folders.push_back(kSwitchDataDir);
    // Common layout for emulators (e.g. Ryujinx), which do not pass the NRO's
    // path in argv: sdcard/Games/claw/ next to openclaw.nro.
    folders.push_back("/Games/claw/");

    // CLAW.REZ first; an unpacked Assets folder is used only when there is no REZ.
    std::vector<std::string> candidates;
    candidates.push_back(pOptions->assetsFolder + pOptions->rezArchive);
    for (const std::string& folder : folders) candidates.push_back(folder + "CLAW.REZ");
    for (const std::string& folder : folders)
    {
        candidates.push_back(folder + "Assets");
        candidates.push_back(folder + "ASSETS");
    }

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        bool isFolder = ResourceDirectory::LooksLikeAssetsFolder(candidates[i]);
        if (FileExists(candidates[i]) || isFolder)
        {
            if (candidates[i] != pOptions->assetsFolder + pOptions->rezArchive)
            {
                LOG_WARNING("Game data not at the configured path, using: " + candidates[i]);
                pOptions->assetsFolder = "";
                pOptions->rezArchive = candidates[i];
            }
            STARTUP_TRACE(("Game data found: " + candidates[i]).c_str());
            return std::vector<std::string>();
        }
    }
    return candidates;
}

// Shows a readable error instead of silently returning to the HOME menu.
// Works whether or not the engine managed to create its window.
static void ShowFatalErrorScreen(const std::vector<std::string>& lines)
{
    for (const std::string& line : lines)
    {
        if (!line.empty()) SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", line.c_str());
    }

    SDL_Renderer* pRenderer = g_pApp ? g_pApp->GetRenderer() : nullptr;
    SDL_Window* pOwnWindow = nullptr;
    if (!pRenderer)
    {
        if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        {
            return;
        }
        pOwnWindow = SDL_CreateWindow("Captain Claw", 0, 0, 1280, 720, SDL_WINDOW_SHOWN);
        pRenderer = pOwnWindow ? SDL_CreateRenderer(pOwnWindow, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
        if (!pRenderer)
        {
            return;
        }
    }
    SDL_SetRenderTarget(pRenderer, NULL); // draw straight to the screen
    SDL_RenderSetScale(pRenderer, 1.0f, 1.0f);

    if (!TTF_WasInit())
    {
        TTF_Init();
    }
    TTF_Font* pFont = TTF_OpenFont("romfs:/clacon.ttf", 26);

    PadState pad;
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);   // 8: keep player 2 (race) connected
    padInitializeDefault(&pad);

    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & (HidNpadButton_Plus | HidNpadButton_A | HidNpadButton_B))
        {
            break;
        }
        SDL_Event evt;
        while (SDL_PollEvent(&evt))
        {
        }

        SDL_SetRenderDrawColor(pRenderer, 20, 24, 48, 255);
        SDL_RenderClear(pRenderer);
        if (pFont)
        {
            int y = 60;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                SDL_Color color = (i == 0) ? SDL_Color{240, 196, 80, 255} : SDL_Color{230, 230, 235, 255};
                if (lines[i].empty())
                {
                    y += 20;
                    continue;
                }
                SDL_Surface* pSurface = TTF_RenderUTF8_Blended(pFont, lines[i].c_str(), color);
                if (!pSurface) continue;
                SDL_Texture* pTexture = SDL_CreateTextureFromSurface(pRenderer, pSurface);
                SDL_Rect dst = {60, y, pSurface->w, pSurface->h};
                SDL_RenderCopy(pRenderer, pTexture, NULL, &dst);
                y += pSurface->h + 8;
                SDL_DestroyTexture(pTexture);
                SDL_FreeSurface(pSurface);
            }
        }
        SDL_RenderPresent(pRenderer);
    }

    if (pFont) TTF_CloseFont(pFont);
    if (pOwnWindow)
    {
        SDL_DestroyRenderer(pRenderer);
        SDL_DestroyWindow(pOwnWindow);
    }
}
#endif

int RunGameEngine(int argc, char** argv)
{
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) != 0)
    {
        LOG_WARNING("Failed to set high priority class to this process");
    }

    std::string userDirectory = "";

#if defined(__ANDROID__)
    userDirectory = "/sdcard/claw/";
#elif defined(__SWITCH__)
    SwitchPlatformInit();
    STARTUP_TRACE("SwitchPlatformInit done");
    userDirectory = kSwitchDataDir;
#elif defined(__WINDOWS__)
    userDirectory = "";
#else
    const char* homedir;

    if ((homedir = getenv("HOME")) == NULL) 
    {
        homedir = getpwuid(getuid())->pw_dir;
    }
    assert(homedir != NULL);

    userDirectory = std::string(homedir) + "/.config/openclaw/";

#endif

    LOG("Looking for: " + userDirectory + "config.xml");

#ifndef __SWITCH__
    // Temporary hack - always prefer config in the same folder as binary to default config
    // (Not on Switch: the working directory there depends on how the NRO was launched.)
    if (TryToFindFile("config.xml"))
    {
        // File was found in a working directory
        userDirectory = "";
    }
#endif

    LOG("Expecting config.xml in path: " + userDirectory + "config.xml");

    STARTUP_TRACE("LoadGameOptions");
    // Load options
    if (!g_pApp->LoadGameOptions(std::string(userDirectory + "config.xml").c_str()))
    {
        LOG_ERROR("Could not load game options. Exiting.");
        return -1;
    }

    g_pApp->GetGameConfig()->userDirectory = userDirectory;

#ifdef __SWITCH__
    std::vector<std::string> rezSearched = ResolveRezArchive(g_pApp->GetGameConfig(), argc, argv);
    if (!rezSearched.empty())
    {
        std::vector<std::string> lines = {
            "CLAW.REZ not found",
            "",
            "This port needs the data file from your own copy of",
            "Captain Claw (1997). Copy CLAW.REZ to the SD card:",
            "    /switch/claw/CLAW.REZ",
            "(or next to openclaw.nro, or /Games/claw/CLAW.REZ).",
            "An unpacked \"Assets\" folder works too.",
            "",
            "Searched:"};
        for (size_t i = 0; i < rezSearched.size() && i < 7; ++i)
        {
            lines.push_back("    " + rezSearched[i]);
        }
        lines.push_back("");
        lines.push_back("Press + to exit.");
        ShowFatalErrorScreen(lines);
        return -1;
    }
#endif

    std::string savesFilePath = g_pApp->GetGameConfig()->userDirectory + g_pApp->GetGameConfig()->savesFile;
    LOG("Loaded with:\n\tConfig File: " + userDirectory + "config.xml" + "\n\tSaves File: " + savesFilePath);

    // Initialize game instance
    if (!g_pApp->Initialize(argc, argv))
    {
        LOG_ERROR("Failed to initialize. Exiting.");
#ifdef __SWITCH__
        ShowFatalErrorScreen({
            "Captain Claw failed to start",
            "",
            "Details were written to /switch/claw/openclaw.log",
            "Please include that file when reporting the problem.",
            "",
            "Press + to exit."});
#endif
        return -1;
    }

    STARTUP_TRACE("entering main loop");
    // Run the game
    return g_pApp->Run();
}

bool TryToFindFile(const char *path) {
    {
        std::ifstream f(path);
        if (f.good()) {
            return true;
        }
    }
#ifdef __EMSCRIPTEN__
    // File does not exist. Try to download
    emscripten_wget(path, path);
    {
        // Check is file downloaded
        std::ifstream f(path);
        if (f.good()) {
            return true;
        }
    }
#endif
    return false;
}
