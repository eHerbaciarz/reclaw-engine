#pragma once

// What differs between the platforms the port runs on: where the player's
// files live, where the files bundled with the port are, how the controller
// buttons are labelled and whether there is a second screen.
//
//   Platform     Player's folder                       Bundled files
//   Switch       sdmc:/switch/claw/                    romfs:/  (in the .nro)
//   PS Vita      ux0:data/claw/                        app0:/   (in the .vpk)
//   3DS          sdmc:/3ds/claw/                       romfs:/  (in the .3dsx)
//   Wii U        sd:/wiiu/apps/claw/                   romfs:/  (in the .wuhb)
//   PS3          /dev_hdd0/game/RCLW00001/USRDIR/      same folder
//   Android      <external files dir of the app>       same folder (copied from the APK)
//   Windows      folder of the .exe                    same folder
//   Linux        folder of the binary, ~/.config/...   folder of the binary

#include <string>
#include <vector>

namespace Platform
{
    enum class ButtonLabels
    {
        Xbox,           // A bottom, B right, X left, Y top (PC pads, Android)
        Nintendo,       // A right, B bottom, X top, Y left
        PlayStation,    // cross bottom, circle right, square left, triangle top
    };

    // "switch", "vita", "3ds", "wiiu", "ps3", "android", "windows", "linux", "web"
    const char* Name();

    // Consoles: no config next to the binary, data on the SD card / HDD,
    // errors shown on screen instead of a console.
    bool IsConsole();

    // Prepares the platform (romfs, folders, log file). Before anything else.
    void Init(int argc, char** argv);

    // The player's folder (config.xml, SAVES.XML, the log, CLAW.REZ). Ends with '/'.
    std::string DataDir();

    // Folder of the files that ship with the port (ASSETS.ZIP, fonts,
    // soundfont, default config). Ends with '/' (or ':/').
    std::string BundleDir();
    std::string BundlePath(const std::string& file);

    // The menu font (Cinzel Decorative) from the bundle, or from the source
    // tree when running a desktop build in place.
    std::string SplashFont();

    // Folders where CLAW.REZ / an Assets folder is looked for, in order.
    std::vector<std::string> GameDataFolders(int argc, char** argv);

    ButtonLabels Buttons();
    // Names of the menu buttons for on-screen hints: "A"/"B", or on
    // PlayStation "×" (cross) / "O" (circle).
    const char* AcceptButtonName();
    const char* BackButtonName();

    // Name used to pick the controls screen picture: "switch", "playstation",
    // "xbox", "3ds", "wiiu", "pc".
    const char* ControlsStyle();

    // 3DS bottom screen / Wii U GamePad.
    bool HasSecondScreen();

    // Logical size of the game view (the engine renders at this size and
    // the result is scaled to the screen).
    void DefaultRenderSize(int& width, int& height);
}
