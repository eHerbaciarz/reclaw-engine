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
W, H = 600, 290

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
        "move": "ruch (gałka lub krzyżak)", "move_pc": "ruch (strzałki)",
        "up": "patrz w górę, wspinaj się", "down": "kucnij, schodź w dół",
        "jump": "skok", "sword": "cios mieczem", "fire": "strzał", "lift": "podnieś / rzuć beczkę",
        "ammo": "zmiana broni", "save": "szybki zapis", "load": "szybkie wczytanie",
        "rewind": "trzymaj: cofanie czasu", "pause": "pauza i menu", "map": "mapa poziomu",
        "liftfar": "trzymaj: rzut dalej", "menu": "menu: wybór / powrót",
        "SPACE": "SPACJA",
    },
    "en": {
        "title": "Controls",
        "move": "move (stick or d-pad)", "move_pc": "move (arrow keys)",
        "up": "look up, climb", "down": "duck, climb down",
        "jump": "jump", "sword": "sword", "fire": "shoot", "lift": "lift / throw a keg",
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


def column(draw, x0, y0, rows, label_w):
    badge_font = font(13, BADGE_FONT)
    small_badge = font(10, BADGE_FONT)
    desc_font = font(14)
    y = y0
    step = 29 * S
    for labels, desc in rows:
        cy = y + step / 2
        x = x0
        for i, label in enumerate(labels):
            if i:
                x += 4 * S
            fnt = small_badge if len(label) > 3 and label not in PS_COLORS else badge_font
            x += badge(draw, x, cy, label, fnt)
        asc, d = desc_font.getmetrics()
        text_with_outline(draw, (x0 + label_w * S, cy - (asc + d) / 2), desc, desc_font, CREAM)
        y += step


def render(style, lang):
    t = TEXTS[lang]
    jump, fire, sword, lift, ammo, save, load, rewind, pause, mapb, ok, back = [
        t.get(b, b) if b == "SPACE" else b for b in BUTTONS[style]]
    move = t["move_pc"] if style == "pc" else t["move"]
    left = [
        (["<", ">"], move),
        (["^"], t["up"]),
        (["v"], t["down"]),
        ([jump], t["jump"]),
        ([sword], t["sword"]),
        ([fire], t["fire"]),
        ([lift], t["lift"]),
    ]
    right = [
        ([ammo], t["ammo"]),
        ([save], t["save"]),
        ([load], t["load"]),
        ([rewind], t["rewind"]),
        ([pause], t["pause"]),
        ([mapb], t["map"]),
        ([lift], t["liftfar"]),
        ([ok, back], t["menu"]),
    ]
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    title_font = font(30)
    tw = draw.textlength(t["title"], font=title_font)
    text_with_outline(draw, ((W * S - tw) / 2, 0), t["title"], title_font, GOLD, outline=3)

    wide = style in ("pc", "3ds", "playstation", "xbox")
    top = 58 * S
    column(draw, 0, top, left, 70 if wide else 56)
    column(draw, 296 * S, top, right, 88 if style == "pc" else (74 if wide else 56))

    img = img.resize((W, H), Image.LANCZOS)
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, f"controls_{style}_{lang}.png")
    img.save(path)
    return path


def main():
    for style in BUTTONS:
        for lang in TEXTS:
            print("wrote", render(style, lang))


if __name__ == "__main__":
    main()
