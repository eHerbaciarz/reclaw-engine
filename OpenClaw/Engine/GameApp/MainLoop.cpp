#include "BaseGameApp.h"
#include "MainLoop.h"
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

// Everything the player provides or the game writes lives here on the SD card:
// CLAW.REZ (from the player's own copy of the game), config.xml, SAVES.XML and
// the log. Assets that ship with the port (ASSETS.ZIP, fonts, soundfont and the
// default config) are bundled in the NRO's romfs.
static const char* kSwitchDataDir = "/switch/claw/";

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
    freopen(logPath.c_str(), "w", stdout);
    freopen(logPath.c_str(), "a", stderr);
    setvbuf(stdout, NULL, _IOLBF, 0);

    // First launch: seed the editable config from the bundled default.
    std::string configPath = std::string(kSwitchDataDir) + "config.xml";
    if (!std::ifstream(configPath).good())
    {
        CopyFile("romfs:/config.xml", configPath);
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

    // Load options
    if (!g_pApp->LoadGameOptions(std::string(userDirectory + "config.xml").c_str()))
    {
        LOG_ERROR("Could not load game options. Exiting.");
        return -1;
    }

    g_pApp->GetGameConfig()->userDirectory = userDirectory;

    std::string savesFilePath = g_pApp->GetGameConfig()->userDirectory + g_pApp->GetGameConfig()->savesFile;
    LOG("Loaded with:\n\tConfig File: " + userDirectory + "config.xml" + "\n\tSaves File: " + savesFilePath);

    // Initialize game instance
    if (!g_pApp->Initialize(argc, argv))
    {
        LOG_ERROR("Failed to initialize. Exiting.");
        return -1;
    }

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
