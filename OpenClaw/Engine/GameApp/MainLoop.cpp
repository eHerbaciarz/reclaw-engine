#include "BaseGameApp.h"
#include "MainLoop.h"
#include "../Util/StartupTrace.h"
#include "../Resource/ResourceDirectory.h"
#include "../Platform/Platform.h"
#include "../Platform/Input/BlockingInput.h"
#include "../UserInterface/SecondScreen.h"
#include <fstream>
#include "GameLanguages.h"
#include <sys/stat.h>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifndef __EMSCRIPTEN__
#include <SDL2/SDL_ttf.h>

static bool FileExists(const std::string& path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// CLAW.REZ (from the player's own copy of the game) or an "Assets" folder
// with its unpacked contents (Polish "Kapitan Pazur" release), in the
// configured place or any of the platform's usual folders.
// Returns the paths that were checked when nothing was found.
static std::vector<std::string> ResolveRezArchive(GameOptions* pOptions, int argc, char** argv)
{
    std::vector<std::string> folders = Platform::GameDataFolders(argc, argv);

    // CLAW.REZ first; an unpacked Assets folder is used only when there is no REZ.
    std::vector<std::string> candidates;
    candidates.push_back(pOptions->assetsFolder + pOptions->rezArchive);
    for (const std::string& folder : folders) candidates.push_back(folder + "CLAW.REZ");
    for (const std::string& folder : folders) candidates.push_back(folder + "claw.rez");
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

// Shows a readable error instead of silently quitting (consoles have no
// console window). Works whether or not the engine managed to create its window.
static void ShowFatalErrorScreen(const std::vector<std::string>& lines)
{
    for (const std::string& line : lines)
    {
        if (!line.empty()) SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", line.c_str());
    }
    if (!Platform::IsConsole())
    {
        std::string text;
        for (const std::string& line : lines) text += line + "\n";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ReClaw", text.c_str(), NULL);
        return;
    }

    SDL_Renderer* pRenderer = g_pApp ? g_pApp->GetRenderer() : nullptr;
    SDL_Window* pOwnWindow = nullptr;
    if (!pRenderer)
    {
        if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
        {
            return;
        }
        int w = 1280, h = 720;
#if defined(__3DS__)
        w = 400; h = 240;
#elif defined(__vita__)
        w = 960; h = 544;
#endif
        pOwnWindow = SDL_CreateWindow("ReClaw", 0, 0, w, h, SDL_WINDOW_SHOWN);
        pRenderer = pOwnWindow ? SDL_CreateRenderer(pOwnWindow, -1, 0) : nullptr;
        if (!pRenderer)
        {
            return;
        }
    }
    SDL_SetRenderTarget(pRenderer, NULL); // draw straight to the screen
    SDL_RenderSetScale(pRenderer, 1.0f, 1.0f);
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(pRenderer, &outW, &outH);

    if (!TTF_WasInit())
    {
        TTF_Init();
    }
    int fontSize = outH >= 700 ? 26 : (outH >= 500 ? 18 : 11);
    TTF_Font* pFont = TTF_OpenFont(Platform::BundlePath("clacon.ttf").c_str(), fontSize);

    BlockingInput::Reset();
    while (BlockingInput::AppAlive())
    {
        int in = BlockingInput::Poll();
        if (in & (BlockingInput::Accept | BlockingInput::Back | BlockingInput::Start | BlockingInput::Quit))
        {
            break;
        }

        SDL_SetRenderDrawColor(pRenderer, 20, 24, 48, 255);
        SDL_RenderClear(pRenderer);
        if (pFont)
        {
            int y = outH / 12;
            int x = outW / 20;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                SDL_Color color = (i == 0) ? SDL_Color{240, 196, 80, 255} : SDL_Color{230, 230, 235, 255};
                if (lines[i].empty())
                {
                    y += fontSize * 3 / 4;
                    continue;
                }
                SDL_Surface* pSurface = TTF_RenderUTF8_Blended(pFont, lines[i].c_str(), color);
                if (!pSurface) continue;
                SDL_Texture* pTexture = SDL_CreateTextureFromSurface(pRenderer, pSurface);
                SDL_Rect dst = {x, y, pSurface->w, pSurface->h};
                SDL_RenderCopy(pRenderer, pTexture, NULL, &dst);
                y += pSurface->h + fontSize / 3;
                SDL_DestroyTexture(pTexture);
                SDL_FreeSurface(pSurface);
            }
        }
        SDL_RenderPresent(pRenderer);
        SDL_Delay(16);
    }

    if (pFont) TTF_CloseFont(pFont);
    if (pOwnWindow)
    {
        SDL_DestroyRenderer(pRenderer);
        SDL_DestroyWindow(pOwnWindow);
    }
}
#endif

#if defined(__PS3__) || defined(__PSL1GHT__)
extern "C" void ReclawBootMark(const char* step);
#endif

int RunGameEngine(int argc, char** argv)
{
#if defined(__PS3__) || defined(__PSL1GHT__)
    ReclawBootMark("3 RunGameEngine");
#endif
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) != 0)
    {
        LOG_WARNING("Failed to set high priority class to this process");
    }

#ifdef __EMSCRIPTEN__
    std::string userDirectory = "";
#else
    Platform::Init(argc, argv);
    STARTUP_TRACE("Platform::Init done");
    std::string userDirectory = Platform::DataDir();

    // Desktop: a config.xml in the working directory wins (running in place
    // from the source tree). Release packages keep the default config.xml
    // next to the binary and the player's copy in save/.
    if (!Platform::IsConsole() && TryToFindFile("config.xml") &&
        !std::ifstream(Platform::RootDir() + "config.xml").good())
    {
        userDirectory = "";
    }
#endif

    LOG("Expecting config.xml in path: " + userDirectory + "config.xml");

    STARTUP_TRACE("LoadGameOptions");
    // Load options
    if (!g_pApp->LoadGameOptions(std::string(userDirectory + "config.xml").c_str()))
    {
        LOG_ERROR("Could not load game options. Exiting.");
#ifndef __EMSCRIPTEN__
        ShowFatalErrorScreen({
            "config.xml not found or damaged",
            "",
            "Expected: " + userDirectory + "config.xml",
            "",
            "Press any button to exit."});
#endif
        return -1;
    }

    g_pApp->GetGameConfig()->userDirectory = userDirectory;

#ifndef __EMSCRIPTEN__
    // data/pl/, data/en/...: the language picked in Options (saved in config.xml).
    GameLanguages::Setup(g_pApp->GetGameConfig(), userDirectory + "config.xml");
    std::vector<std::string> rezSearched = ResolveRezArchive(g_pApp->GetGameConfig(), argc, argv);
    if (!rezSearched.empty())
    {
        std::vector<std::string> lines = {
            "CLAW.REZ not found",
            "",
            "This port needs the data file from your own copy of",
            "Claw (1997). The folders are ready - copy CLAW.REZ",
            "(and CLAW.FEC for the movies) into one of them:",
            "    " + Platform::GameDir() + "pl/   (Polish release)",
            "    " + Platform::GameDir() + "en/   (English release)",
            "An unpacked \"Assets\" folder works too.",
            "",
            "Searched:"};
        for (size_t i = 0; i < rezSearched.size() && i < 7; ++i)
        {
            lines.push_back("    " + rezSearched[i]);
        }
        lines.push_back("");
        lines.push_back("Press any button to exit.");
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
#ifndef __EMSCRIPTEN__
        extern std::string g_StartupFailureHint;
        std::vector<std::string> lines = { "ReClaw failed to start", "" };
        if (!g_StartupFailureHint.empty())
        {
            std::string h = g_StartupFailureHint;
            for (size_t pos; (pos = h.find('|')) != std::string::npos; h = h.substr(pos + 1)) lines.push_back(h.substr(0, pos));
            lines.push_back(h);
            lines.push_back("");
        }
        lines.push_back("Details were written to " + Platform::DataDir() + "reclaw.log");
        lines.push_back("Please include that file when reporting the problem.");
        lines.push_back("");
        lines.push_back("Press any button to exit.");
        ShowFatalErrorScreen(lines);
#endif
        return -1;
    }

    STARTUP_TRACE("entering main loop");
    // Run the game
#if defined(__3DS__)
    // 3DS: shut SDL down (sound, both screens) before the program ends; left
    // running, its threads crashed the console on the way back to HOME.
    int result = g_pApp->Run();
    STARTUP_TRACE("main loop left, shutting SDL down");
    SecondScreen::Shutdown();
    SDL_Quit();
    STARTUP_TRACE("SDL shut down");
    return result;
#else
    return g_pApp->Run();
#endif
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
