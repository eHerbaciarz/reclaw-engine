#include "CreditsScreen.h"

#include <algorithm>
#include <string>
#include <vector>

#include "GameFont.h"
#include "../Graphics2D/Image.h"

namespace
{
    const int kWidth = 600;
    // The picture sits where the generated credits picture does (MENU.xml);
    // the title starts lower, under the CLAW logo, like on the other pages.
    const int kTopPad = 35;
    const int kLabelPercent = 40;   // "OPENCLAW", "RECLAW"... against the menu buttons' size
    const int kNamePercent = 55;    // the names
    const int kFirstLabel = kTopPad + 58;     // baseline of the first label
    const int kLabelToName = 17;    // baseline to baseline
    const int kNameToName = 16;     // second line of names
    const int kNameToLabel = 19;

    // Same texts as scripts/make_credits_image.py (no digits in the letters,
    // so the year of the original game is left out here).
    struct Block { const char* label; std::vector<const char*> names; };
    const Block kBlocks[] = {
        { "GRA ORYGINALNA", { "CLAW · MONOLITH PRODUCTIONS" } },
        { "OPENCLAW", { "PETR JAŠÍČEK · PAVEL LÖBL" } },
        { "POMOC PRZY OPENCLAW", { "RICORODRIGES · DENIS PAPEC · BAKASURA", "GREG V · PIOTR KRZEMINSKI" } },
        { "CLAW WEB", { "ARTHUR BOSS" } },
        { "CLAWSWITCH", { "DJMUTED" } },
        { "RECLAW", { "HERBACIARZ" } },
    };
}

namespace CreditsScreen
{
    std::shared_ptr<Image> Build()
    {
        if (!GameFont::Available()) return nullptr;
        typedef GameFont::Pixels Pixels;

        int height = kFirstLabel;
        for (const Block& b : kBlocks)
            height += kLabelToName + kNameToName * ((int)b.names.size() - 1) + kNameToLabel;
        Pixels canvas;
        canvas.w = kWidth;
        canvas.h = height + 8;
        canvas.px.assign((size_t)canvas.w * canvas.h, 0);

        // Letters' baseline inside a GameFont picture.
        const int baseline = GameFont::ExtraTop() + 27;
        auto line = [&](const char* text, int percent, int y) {
            Pixels p;
            if (!GameFont::RenderPixels(text, GameFont::Look::Normal, -1, p)) return;
            if (p.w * percent / 100 > kWidth - 10) percent = (kWidth - 10) * 100 / p.w;
            Pixels small = GameFont::Scale(p, percent);
            int x = (kWidth - small.w) / 2;
            GameFont::Paste(canvas, GameFont::WithShadow(small, 2, 2, 200), x, y - baseline * percent / 100);
        };

        // Title, full size, underlined like the original page titles.
        Pixels title;
        if (GameFont::RenderPixels("RECLAW – TWÓRCY", GameFont::Look::Normal, -2, title))
            GameFont::Paste(canvas, GameFont::WithShadow(title, 3, 3, 170), (kWidth - title.w) / 2, kTopPad - GameFont::ExtraTop());

        int y = kFirstLabel;
        for (const Block& b : kBlocks)
        {
            line(b.label, kLabelPercent, y);
            y += kLabelToName;
            for (size_t i = 0; i < b.names.size(); ++i)
            {
                if (i) y += kNameToName;
                line(b.names[i], kNamePercent, y);
            }
            y += kNameToLabel;
        }
        return GameFont::ToImage(canvas);
    }
}
