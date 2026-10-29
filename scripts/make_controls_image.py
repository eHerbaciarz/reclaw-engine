#!/usr/bin/env python3
"""Renders the controls screens shown in Options > Controls.

One picture per controller style and language:
    Build_Release/ASSETS/CONTROLS/controls_<style>_<lang>.png
styles: switch, 3ds, wiiu, playstation, xbox, pc   languages: pl, en
(menu coordinates, 640x480 screen). The game picks the picture for its
platform and language (MENU.xml: /CONTROLS/controls_{platform}_{lang}.png).
Re-run after changing the button mapping in ActorController.

    python3 scripts/make_controls_image.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(REPO, "ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf")
BADGE_FONT = os.path.join(REPO, "Box2D/Data/DroidSans.ttf")
OUT_DIR = os.path.join(REPO, "Build_Release/ASSETS/CONTROLS")

S = 4                      # supersampling
W, H = 600, 240

GOLD = (246, 205, 86, 255)
CREAM = (250, 236, 200, 255)
DARK = (20, 10, 0, 255)
PS_COLORS = {"PS_X": (120, 160, 255), "PS_O": (255, 110, 110), "PS_SQ": (240, 140, 220), "PS_TRI": (90, 220, 170)}

# Buttons of each action per controller style.
BUTTONS = {
    #             jump      fire     sword     lift      ammo     save   load   rewind  pause     map       ok       back
    "switch":      ["B",     "A",     "Y",      "X",      "L",     "ZL",  "ZR",  "R",    "+",      "-",      "A",     "B"],
    "3ds":         ["B",     "A",     "Y",      "X",      "L",     "ZL",  "ZR",  "R",    "START",  "SELECT", "A",     "B"],
    "wiiu":        ["B",     "A",     "Y",      "X",      "L",     "ZL",  "ZR",  "R",    "+",      "-",      "A",     "B"],
    "playstation": ["PS_X",  "PS_O",  "PS_SQ",  "PS_TRI", "L1",    "L2",  "R2",  "R1",   "START",  "SELECT", "PS_X",  "PS_O"],
    "xbox":        ["A",     "B",     "X",      "Y",      "LB",    "LT",  "RT",  "RB",   "START",  "BACK",   "A",     "B"],
    "pc":          ["SPACE", "ALT",   "CTRL",   "Z",      "SHIFT", "F5",  "F8",  "F6",   "ESC",    "TAB",    "ENTER", "ESC"],
}

TEXTS = {
    "pl": {
        "title": "Sterowanie",
        "left": "w lewo", "right": "w prawo",
        "up": "wspinanie", "down": "kucanie i zejście",
        "jump": "skok", "sword": "atak wręcz", "fire": "strzelanie", "lift": "podnoszenie i rzut",
        "ammo": "zmień broń", "save": "szybki zapis", "load": "szybkie wczytanie",
        "rewind": "trzymaj: cofanie czasu", "pause": "pauza i menu", "map": "mapa poziomu",
        "liftfar": "trzymaj: daleki rzut", "menu": "menu: wybór i powrót",
        "SPACE": "SPACJA",
    },
    "en": {
        "title": "Controls",
        "left": "left", "right": "right",
        "up": "climb", "down": "duck, climb down",
        "jump": "jump", "sword": "melee attack", "fire": "shoot", "lift": "lift, throw",
        "ammo": "change weapon", "save": "quick save", "load": "quick load",
        "rewind": "hold: rewind time", "pause": "pause and menu", "map": "level map",
        "liftfar": "hold: throw farther", "menu": "menu: select / back",
        "SPACE": "SPACE",
    },
}


def font(size, path=FONT):
    return ImageFont.truetype(path, size * S)


def text_with_outline(draw, xy, text, fnt, fill, outline=2):
    x, y = xy
    o = outline * S
    draw.text((x, y), text, font=fnt, fill=DARK, stroke_width=o, stroke_fill=DARK)
    draw.text((x, y), text, font=fnt, fill=fill)


def badge(draw, x, cy, label, fnt):
    """Draws a button badge with its left edge at x, centred on cy. Returns its width."""
    shape = label in ("<", ">", "^", "v") or label in PS_COLORS
    tw = 0 if shape else draw.textlength(label, font=fnt)
    h = 22 * S
    w = max(h, tw + 12 * S)
    box = (x, cy - h / 2, x + w, cy + h / 2)
    draw.rounded_rectangle(box, radius=h / 2, fill=(40, 24, 8, 235), outline=GOLD, width=2 * S)
    cx, r = x + w / 2, 5.5 * S
    if label in ("<", ">", "^", "v"):
        # The font has no arrow glyphs: draw a triangle.
        pts = {
            "^": [(cx, cy - r), (cx + r, cy + r * 0.7), (cx - r, cy + r * 0.7)],
            "v": [(cx, cy + r), (cx + r, cy - r * 0.7), (cx - r, cy - r * 0.7)],
            "<": [(cx - r, cy), (cx + r * 0.7, cy - r), (cx + r * 0.7, cy + r)],
            ">": [(cx + r, cy), (cx - r * 0.7, cy - r), (cx - r * 0.7, cy + r)],
        }[label]
        draw.polygon(pts, fill=CREAM)
    elif label in PS_COLORS:
        # PlayStation face button symbols
        col = PS_COLORS[label] + (255,)
        lw = int(1.8 * S)
        if label == "PS_X":
            draw.line([(cx - r, cy - r), (cx + r, cy + r)], fill=col, width=lw)
            draw.line([(cx - r, cy + r), (cx + r, cy - r)], fill=col, width=lw)
        elif label == "PS_O":
            draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=col, width=lw)
        elif label == "PS_SQ":
            draw.rectangle([cx - r * 0.85, cy - r * 0.85, cx + r * 0.85, cy + r * 0.85], outline=col, width=lw)
        else:
            draw.polygon([(cx, cy - r), (cx + r, cy + r * 0.75), (cx - r, cy + r * 0.75)], outline=col, width=lw)
    else:
        draw.text((x + w / 2, cy), label, font=fnt, fill=CREAM, anchor="mm")
    return w


# One row per action, like the original controls screen: buttons right-aligned
# in the key column, the action on the right. Experimental actions (save
# states, rewind) are left out of the pictures.
ROW_STEP = 20
KEY_RIGHT = 252          # right edge of the key column (picture coordinates)
GAP = 14


def rows_for(style, t):
    jump, fire, sword, lift, ammo, save, load, rewind, pause, mapb, ok, back = [
        t.get(b, b) if b == "SPACE" else b for b in BUTTONS[style]]
    left = [
        (["<"], t["left"]),
        ([">"], t["right"]),
        (["^"], t["up"]),
        (["v"], t["down"]),
        ([jump], t["jump"]),
        ([sword], t["sword"]),
        ([fire], t["fire"]),
    ]
    right = [
        ([lift], t["lift"]),
        ([lift], t["liftfar"]),
        ([ammo], t["ammo"]),
        ([pause], t["pause"]),
        ([mapb], t["map"]),
        ([ok, back], t["menu"]),
    ]
    return left, right


def badges_width(draw, labels, fnt_big, fnt_small):
    w = 0
    for i, label in enumerate(labels):
        if i:
            w += 4 * S
        shape = label in ("<", ">", "^", "v") or label in PS_COLORS
        fnt = fnt_small if len(label) > 3 and label not in PS_COLORS else fnt_big
        tw = 0 if shape else draw.textlength(label, font=fnt)
        w += max(22 * S, tw + 12 * S)      # as badge() draws it
    return w


def render(style, lang):
    t = TEXTS[lang]
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    title_font = font(26)
    tw = draw.textlength(t["title"].upper(), font=title_font)
    text_with_outline(draw, ((W * S - tw) / 2, 0), t["title"].upper(), title_font, GOLD, outline=3)
    draw.rectangle(((W * S - tw) / 2, 36 * S, (W * S + tw) / 2, 38 * S), fill=GOLD)

    badge_font = font(11, BADGE_FONT)
    small_badge = font(9, BADGE_FONT)
    desc_font = font(13)
    step = 25 * S
    wide = style in ("pc", "3ds", "playstation", "xbox")
    left, right = rows_for(style, t)
    for rows, key_right in ((left, 70 if wide else 48), (right, 352 if not wide else 372)):
        y = 54 * S
        for labels, desc in rows:
            cy = y + step / 2
            x = key_right * S - badges_width(draw, labels, badge_font, small_badge)
            for i, label in enumerate(labels):
                if i:
                    x += 4 * S
                fnt = small_badge if len(label) > 3 and label not in PS_COLORS else badge_font
                x += badge(draw, x, cy, label, fnt)
            asc, d = desc_font.getmetrics()
            text_with_outline(draw, ((key_right + 10) * S, cy - (asc + d) / 2), desc.upper(), desc_font, GOLD)
            y += step

    img = img.resize((W, H), Image.LANCZOS)
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, f"controls_{style}_{lang}.png")
    img.save(path)
    return path


def badge_file(label):
    return {"<": "left", ">": "right", "^": "up", "v": "down", "+": "plus", "-": "minus"}.get(label, label).lower()


def render_badges():
    """Single button badges for the in-game controls screen made of the
    game's menu letters (GameFont): CONTROLS/BADGES/<name>.png, 18 px high."""
    out = os.path.join(OUT_DIR, "BADGES")
    os.makedirs(out, exist_ok=True)
    labels = set(["<", ">", "^", "v"])
    for style in BUTTONS:
        for b in BUTTONS[style]:
            labels.add(TEXTS["pl"].get(b, b) if b == "SPACE" else b)
            labels.add(TEXTS["en"].get(b, b) if b == "SPACE" else b)
    for label in sorted(labels):
        probe = ImageDraw.Draw(Image.new("RGBA", (10, 10)))
        big, small = font(13, BADGE_FONT), font(10, BADGE_FONT)
        w = int(badges_width(probe, [label], big, small) // S) + 4
        img = Image.new("RGBA", (w * S, 24 * S), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        fnt = small if len(label) > 3 and label not in PS_COLORS else big
        badge(draw, 2 * S, 12 * S, label, fnt)
        img = img.resize((w * 18 // 24 + 1, 18), Image.LANCZOS)
        img.save(os.path.join(out, badge_file(label) + ".png"))


def main():
    for style in BUTTONS:
        for lang in TEXTS:
            print("wrote", render(style, lang))
    render_badges()
    print("wrote CONTROLS/BADGES/*.png")


if __name__ == "__main__":
    main()
