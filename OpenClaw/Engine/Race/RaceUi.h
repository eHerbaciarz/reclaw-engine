#pragma once

// Menu-styled screens of the race mode (they run their own loop, like the
// custom level picker) and a small text cache for the in-game race overlay.

#include <functional>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

namespace RaceUi
{
    struct ListScreen
    {
        std::string title;
        std::vector<std::string> items;        // last item is usually "Back"
        std::vector<std::string> footer;       // lines under the list
        int selected = 0;
        bool backCancels = true;               // B returns -1
        // Called every frame; may change items/footer. Return a value >= 0
        // to close the screen with that result, or -2 to keep going.
        std::function<int(ListScreen&)> tick;
    };

    // Runs the screen. Returns the chosen item index, -1 on Back, or what
    // `tick` returned.
    int Run(SDL_Renderer* pRenderer, ListScreen& screen);

    // Text lines on the menu background until A / B / + is pressed.
    void ShowMessage(SDL_Renderer* pRenderer, const std::string& title, const std::vector<std::string>& lines);

    // Gold text with a dark outline, cached by (text, size). Drawn centered
    // on x (align 0), from x (align -1) or ending at x (align 1).
    void DrawText(SDL_Renderer* pRenderer, const std::string& text, int px, int x, int y, int align,
                  SDL_Color fill = SDL_Color{ 250, 230, 180, 255 });
    void FreeTextCache();
}
