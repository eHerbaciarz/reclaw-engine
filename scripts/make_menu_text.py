#!/usr/bin/env python3
"""Menu buttons drawn as text in the style of the game's menu lettering
(gold letters, dark outline, light glow when selected), for menu items the
original game does not have. Polish and English versions.

    python3 scripts/make_menu_text.py            # writes the port's buttons
    python3 scripts/make_menu_text.py --preview  # also /tmp/menu_text_preview.png

Font: TeX Gyre Bonum Bold (GUST Font License), only the rendered pictures are
stored in the repository. Needs Pillow and the font (Debian: fonts-texgyre).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "Build_Release", "ASSETS")
FONT_CANDIDATES = [
    "/usr/share/texmf/fonts/opentype/public/tex-gyre/texgyrebonum-bold.otf",
    "/usr/share/fonts/opentype/tex-gyre/texgyrebonum-bold.otf",
]

S = 4  # supersampling

# (file stem without extension, Polish text, English text)
BUTTONS = [
    ("STATES/MENU/IMAGES/MAIN/OPTIONS/DISPLAY/SUBTITLES_ON", "NAPISY: WŁ.", "SUBTITLES: ON"),
    ("STATES/MENU/IMAGES/MAIN/OPTIONS/DISPLAY/SUBTITLES_OFF", "NAPISY: WYŁ.", "SUBTITLES: OFF"),
    ("GAME/IMAGES/INGAMEMENU/MAIN/CHEATS", "KODY", "CHEATS"),
    ("STATES/MENU/IMAGES/MAIN/SINGLEPLAYER/BOSS_MODES", "SKRZYŻUJ SZPADY PONOWNIE", "CROSS SWORDS AGAIN"),
]


def font_path():
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    sys.exit("TeX Gyre Bonum Bold not found (apt install fonts-texgyre)")


def render(text, active, height=35):
    size = int(height * 0.86) * S
    font = ImageFont.truetype(font_path(), size)
    pad = 6 * S
    bbox = font.getbbox(text)
    w = bbox[2] - bbox[0] + pad * 2
    h = height * S
    y0 = (h - (bbox[3] - bbox[1])) // 2 - bbox[1]
    x0 = pad - bbox[0]

    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).text((x0, y0), text, font=font, fill=255)

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    # drop shadow down-right, then a dark outline
    shadow = mask.filter(ImageFilter.MaxFilter(3 * S // 2 * 2 + 1))
    sh = Image.new("RGBA", (w, h), (24, 10, 2, 255))
    out.paste(sh, (2 * S, 2 * S), shadow)
    outline = mask.filter(ImageFilter.MaxFilter(2 * S // 2 * 2 + 1))
    if active:
        glow = mask.filter(ImageFilter.MaxFilter(5 * S // 2 * 2 + 1)).filter(ImageFilter.GaussianBlur(S))
        out.paste(Image.new("RGBA", (w, h), (255, 236, 200, 255)), (0, 0), glow)
        out.paste(Image.new("RGBA", (w, h), (120, 70, 30, 255)), (0, 0), outline)
    else:
        out.paste(Image.new("RGBA", (w, h), (40, 20, 4, 255)), (0, 0), outline)

    # gold vertical gradient inside the letters
    grad = Image.new("RGBA", (w, h))
    gd = ImageDraw.Draw(grad)
    top, mid, bottom = ((255, 246, 170), (232, 200, 80), (150, 110, 30)) if not active else \
                       ((255, 255, 225), (250, 226, 120), (190, 150, 50))
    for y in range(h):
        t = y / max(1, h - 1)
        if t < 0.5:
            c = tuple(int(top[i] + (mid[i] - top[i]) * t * 2) for i in range(3))
        else:
            c = tuple(int(mid[i] + (bottom[i] - mid[i]) * (t - 0.5) * 2) for i in range(3))
        gd.line([(0, y), (w, y)], fill=c + (255,))
    out.paste(grad, (0, 0), mask)
    return out.resize((w // S, h // S), Image.LANCZOS)


def main():
    written = []
    for stem, pl, en in BUTTONS:
        for lang, text in (("pl", pl), ("en", en)):
            for active in (False, True):
                path = os.path.join(ASSETS, f"{stem}_{lang}_{'ACTIVE' if active else 'INACTIVE'}.png")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                render(text, active).save(path)
                written.append(path)
                print("wrote", os.path.relpath(path, REPO))
    if "--preview" in sys.argv:
        imgs = [Image.open(p) for p in written]
        imgs.append(render("WIDESCREEN: ON", False))
        W = max(i.width for i in imgs) + 20
        H = sum(i.height + 6 for i in imgs) + 10
        sheet = Image.new("RGBA", (W, H), (70, 36, 12, 255))
        y = 5
        for i in imgs:
            sheet.alpha_composite(i, (10, y))
            y += i.height + 6
        sheet.save("/tmp/menu_text_preview.png")


if __name__ == "__main__":
    main()
