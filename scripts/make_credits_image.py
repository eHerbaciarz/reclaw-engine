#!/usr/bin/env python3
"""Renders the text-only credits screen (main menu > Credits).

Output: Build_Release/ASSETS/Credits/credits_text.png (menu coordinates,
640x480 screen). Same look as the controls screen.

    python3 scripts/make_credits_image.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_controls_image import font, text_with_outline, GOLD, CREAM, S, REPO  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

OUT = os.path.join(REPO, "Build_Release/ASSETS/Credits/credits_text_{lang}.png")
W, H = 600, 300

TEXTS = {
    "pl": ("ReClaw – twórcy", [
        ("Gra oryginalna", "Claw © 1997 Monolith Productions"),
        ("OpenClaw", "Petr Jašíček · Pavel Löbl"),
        ("Pomoc przy OpenClaw", "RicoRodriges · Denis Papec · Bakasura · Greg V · Piotr Krzeminski"),
        ("Claw Web", "Arthur Boss"),
        ("ClawSwitch", "djmuted"),
        ("ReClaw", "MK  i  Claude (Anthropic)"),
    ]),
    "en": ("ReClaw – credits", [
        ("Original game", "Claw © 1997 Monolith Productions"),
        ("OpenClaw", "Petr Jašíček · Pavel Löbl"),
        ("OpenClaw contributors", "RicoRodriges · Denis Papec · Bakasura · Greg V · Piotr Krzeminski"),
        ("Claw Web", "Arthur Boss"),
        ("ClawSwitch", "djmuted"),
        ("ReClaw", "MK  and  Claude (Anthropic)"),
    ]),
}


def centered(draw, y, text, fnt, fill, outline=2):
    tw = draw.textlength(text, font=fnt)
    text_with_outline(draw, ((W * S - tw) / 2, y), text, fnt, fill, outline)


def render(lang):
    TITLE, BLOCKS = TEXTS[lang]
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    centered(draw, 0, TITLE, font(30), GOLD, outline=3)

    label_font, name_font = font(12), font(15)
    y = 54 * S
    for label, names in BLOCKS:
        centered(draw, y, label, label_font, GOLD)
        nf = name_font
        if draw.textlength(names, font=nf) > (W - 10) * S:
            nf = font(12)
        centered(draw, y + 16 * S, names, nf, CREAM)
        y += 41 * S

    img = img.resize((W, H), Image.LANCZOS)
    path = OUT.format(lang=lang)
    img.save(path)
    print("wrote", path)


def main():
    for lang in TEXTS:
        render(lang)


if __name__ == "__main__":
    main()
