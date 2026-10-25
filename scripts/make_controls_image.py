#!/usr/bin/env python3
"""Renders the controls screen shown in Options > Controls (Switch layout).

Output: Build_Release/ASSETS/CONTROLS/switch_controls.png (menu coordinates,
640x480 screen). Re-run after changing the button mapping in ActorController.

    python3 scripts/make_controls_image.py
"""
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(REPO, "ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf")
BADGE_FONT = os.path.join(REPO, "Box2D/Data/DroidSans.ttf")
OUT = os.path.join(REPO, "Build_Release/ASSETS/CONTROLS/switch_controls.png")

S = 4                      # supersampling
W, H = 600, 290

GOLD = (246, 205, 86, 255)
CREAM = (250, 236, 200, 255)
DARK = (20, 10, 0, 255)

TITLE = "Sterowanie"

# (button labels, description). A label in brackets is drawn as a badge.
ROWS_LEFT = [
    (["<", ">"], "ruch (gałka lub krzyżak)"),
    (["^"], "patrz w górę, wspinaj się"),
    (["v"], "kucnij, schodź w dół"),
    (["B"], "skok"),
    (["Y"], "cios mieczem"),
    (["A"], "strzał"),
    (["X"], "podnieś / rzuć beczkę"),
]
ROWS_RIGHT = [
    (["L"], "zmiana broni"),
    (["ZL"], "szybki zapis"),
    (["ZR"], "szybkie wczytanie"),
    (["R"], "trzymaj: cofanie czasu"),
    (["+"], "pauza i menu"),
    (["X"], "trzymaj: rzut dalej"),
    (["A", "B"], "menu: wybór / powrót"),
]


def font(size, path=FONT):
    return ImageFont.truetype(path, size * S)


def text_with_outline(draw, xy, text, fnt, fill, outline=2):
    x, y = xy
    o = outline * S
    draw.text((x, y), text, font=fnt, fill=DARK, stroke_width=o, stroke_fill=DARK)
    draw.text((x, y), text, font=fnt, fill=fill)


def badge(draw, x, cy, label, fnt):
    """Draws a button badge with its left edge at x, centred on cy. Returns its width."""
    arrow = label in ("<", ">", "^", "v")
    tw = 0 if arrow else draw.textlength(label, font=fnt)
    h = 22 * S
    w = max(h, tw + 12 * S)
    box = (x, cy - h / 2, x + w, cy + h / 2)
    draw.rounded_rectangle(box, radius=h / 2, fill=(40, 24, 8, 235), outline=GOLD, width=2 * S)
    if arrow:
        # The font has no arrow glyphs: draw a triangle.
        cx, r = x + w / 2, 5.5 * S
        pts = {
            "^": [(cx, cy - r), (cx + r, cy + r * 0.7), (cx - r, cy + r * 0.7)],
            "v": [(cx, cy + r), (cx + r, cy - r * 0.7), (cx - r, cy - r * 0.7)],
            "<": [(cx - r, cy), (cx + r * 0.7, cy - r), (cx + r * 0.7, cy + r)],
            ">": [(cx + r, cy), (cx - r * 0.7, cy - r), (cx - r * 0.7, cy + r)],
        }[label]
        draw.polygon(pts, fill=CREAM)
    else:
        asc, desc = fnt.getmetrics()
        draw.text((x + w / 2, cy), label, font=fnt, fill=CREAM, anchor="mm")
    return w


def plain_label(draw, x, cy, label, fnt):
    asc, desc = fnt.getmetrics()
    text_with_outline(draw, (x, cy - (asc + desc) / 2), label, fnt, GOLD)
    return draw.textlength(label, font=fnt)


def column(draw, x0, y0, rows, label_w):
    badge_font = font(13, BADGE_FONT)
    word_font = font(14)
    desc_font = font(14)
    y = y0
    step = 32 * S
    for labels, desc in rows:
        cy = y + step / 2
        x = x0
        for i, label in enumerate(labels):
            if i:
                x += 4 * S
            if len(label) <= 2:
                x += badge(draw, x, cy, label, badge_font)
            else:
                x += plain_label(draw, x, cy, label, word_font)
        asc, d = desc_font.getmetrics()
        text_with_outline(draw, (x0 + label_w * S, cy - (asc + d) / 2), desc, desc_font, CREAM)
        y += step


def main():
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    title_font = font(30)
    tw = draw.textlength(TITLE, font=title_font)
    text_with_outline(draw, ((W * S - tw) / 2, 0), TITLE, title_font, GOLD, outline=3)

    top = 58 * S
    column(draw, 0, top, ROWS_LEFT, 56)
    column(draw, 300 * S, top, ROWS_RIGHT, 56)

    img = img.resize((W, H), Image.LANCZOS)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
