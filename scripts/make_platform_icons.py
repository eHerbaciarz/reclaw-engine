#!/usr/bin/env python3
"""Icons and banners for the packages, from the ReClaw icon by Herbaciarz
(platform/icon/reclaw-icon.png: the claw on the map). No game data is used.

    python3 scripts/make_platform_icons.py

Writes:
  platform/switch/icon.jpg                             256x256
  platform/windows/reclaw.ico                          16..256
  platform/3ds/cia/banner.png                          256x128 (HOME menu banner)
  platform/vita/sce_sys/icon0.png                      128x128 (8-bit palette)
  platform/vita/sce_sys/livearea/contents/bg.png       840x500
  platform/vita/sce_sys/livearea/contents/startup.png  280x158
  platform/3ds/icon.png                                48x48
  platform/wiiu/icon.png                               128x128
  platform/wiiu/tv-splash.png, drc-splash.png          1280x720, 854x480
  platform/ps3/ICON0.PNG                               320x176
  platform/ps3/PIC1.PNG                                1920x1080
  platform/ps4/sce_sys/icon0.png, pic1.png            512x512, 1920x1080
  platform/android/app/src/main/res/mipmap-*/ic_launcher.png
"""
import os

from PIL import Image, ImageDraw, ImageFont, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON = os.path.join(REPO, "platform", "icon", "reclaw-icon.png")
FONT = os.path.join(REPO, "ThirdParty", "Fonts", "CinzelDecorative", "CinzelDecorative-Bold.ttf")

TOP = (92, 52, 22)
BOTTOM = (28, 14, 4)
GOLD = (246, 205, 86)
EDGE = (30, 15, 2)


def background(w, h):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(TOP[i] * (1 - t) + BOTTOM[i] * t) for i in range(3))
        d.line([(0, y), (w, y)], fill=c)
    # soft vignette
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).ellipse([-w // 4, -h // 4, w + w // 4, h + h // 4], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(min(w, h) // 6))
    dark = Image.new("RGB", (w, h), BOTTOM)
    return Image.composite(img, dark, mask)


def art(size):
    return Image.open(ICON).convert("RGBA").resize((size, size), Image.LANCZOS)


def framed(size):
    """The icon with a dark rim and rounded corners, for banners."""
    img = art(size)
    mask = Image.new("L", (size, size), 0)
    r = max(2, size // 10)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], r, fill=255)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    ImageDraw.Draw(out).rounded_rectangle([0, 0, size - 1, size - 1], r, outline=EDGE + (255,),
                                          width=max(1, size // 64))
    return out


def title(img, text, cx, cy, size, max_width=None):
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, size)
    while max_width and size > 6 and d.textlength(text, font=font) > max_width:
        size -= 1
        font = ImageFont.truetype(FONT, size)
    stroke = max(1, size // 14)
    d.text((cx, cy), text, font=font, fill=GOLD, anchor="mm", stroke_width=stroke, stroke_fill=EDGE)


def icon(size):
    return art(size).convert("RGB")


def banner(w, h, with_title=True):
    """Generated background, the framed icon and the title (no game art)."""
    img = background(w, h).convert("RGBA")
    if with_title:
        s = int(min(h * 0.78, w * 0.4))
        x0 = int(w * 0.06)
        img.alpha_composite(framed(s), (x0, (h - s) // 2))
        left = x0 + s + int(w * 0.03)
        right = w - int(w * 0.04)
        title(img, "ReClaw", (left + right) // 2, h // 2, int(h * 0.2), right - left)
    else:
        s = int(h * 0.6)
        img.alpha_composite(framed(s), ((w - s) // 2, (h - s) // 2))
    return img.convert("RGB")


def save(img, *parts, palette=False):
    path = os.path.join(REPO, *parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if palette:
        img = img.convert("P", palette=Image.ADAPTIVE, colors=256)
    img.save(path)
    print("wrote", os.path.relpath(path, REPO))


def main():
    # Switch (NRO icon)
    save(icon(256), "platform/switch/icon.jpg")
    # Windows (ReClaw.exe)
    ico = os.path.join(REPO, "platform/windows/reclaw.ico")
    art(256).save(ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("wrote", os.path.relpath(ico, REPO))
    # PS Vita: icon0 and the LiveArea pictures must be 8-bit indexed PNGs.
    save(icon(128), "platform/vita/sce_sys/icon0.png", palette=True)
    save(banner(840, 500), "platform/vita/sce_sys/livearea/contents/bg.png", palette=True)
    save(banner(280, 158), "platform/vita/sce_sys/livearea/contents/startup.png", palette=True)
    # 3DS: SMDH icon and the HOME menu banner
    save(icon(48), "platform/3ds/icon.png")
    save(banner(256, 128), "platform/3ds/cia/banner.png")
    # Wii U (HOMEBREW launcher / WUHB)
    save(icon(128), "platform/wiiu/icon.png")
    save(banner(1280, 720), "platform/wiiu/tv-splash.png")
    save(banner(854, 480), "platform/wiiu/drc-splash.png")
    # PS3 XMB
    save(banner(320, 176), "platform/ps3/ICON0.PNG")
    save(banner(1920, 1080, with_title=False), "platform/ps3/PIC1.PNG")
    # PS4 (OpenOrbis .pkg); the Steam Deck package uses icon0 too
    save(icon(512), "platform/ps4/sce_sys/icon0.png")
    save(banner(1920, 1080, with_title=False), "platform/ps4/sce_sys/pic1.png")
    # Android launcher icons
    for name, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        save(icon(size), f"platform/android/app/src/main/res/mipmap-{name}/ic_launcher.png")


if __name__ == "__main__":
    main()
