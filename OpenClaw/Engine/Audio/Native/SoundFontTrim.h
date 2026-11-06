#ifndef __SOUNDFONT_TRIM_H__
#define __SOUNDFONT_TRIM_H__

// A smaller soundfont with only what the game's music plays.
//
// TinySoundFont keeps every sample of a soundfont in memory, decoded. The
// bundled soundfont (General MIDI, Ogg-compressed .sf3) is far too big for
// that on the PS3 and the 3DS. At the first start the music of the game
// (every .XMI in the game data) is read once: which instruments it uses and
// which notes of them. A .sf2 with only those instruments, zones and samples
// is written next to the port's other files (music/soundfont_small.sf2);
// the samples are decoded one at a time, so writing it needs little memory.
// Later starts load that file directly.

#include <functional>
#include <string>
#include <vector>

namespace SoundFontTrim
{
    // Where the small soundfont of the given full one is (or will be).
    std::string SmallPath(const std::string& fullPath);

    // True when the small soundfont exists and was made from this full one.
    bool SmallIsValid(const std::string& fullPath);

    // Audio start: where the full soundfont may be.
    void SetFullPaths(const std::vector<std::string>& fullPaths);

    // Main thread, after the game data is loaded: reads the music of the game
    // when the small soundfont still has to be made. Does nothing when
    // SetFullPaths was not called.
    void CollectUsage();

    // Loader thread: waits (at most timeoutMs) for CollectUsage, then writes the
    // small soundfont. True when it exists afterwards.
    bool MakeSmall(const std::string& fullPath, int timeoutMs);

    // Main thread, right after CollectUsage: makes the small soundfont now
    // (first start, with a progress screen), if it has to be made.
    // progress(done, total) is called before each sample.
    bool MakeSmallNow(const std::function<void(int, int)>& progress);
}

#endif
