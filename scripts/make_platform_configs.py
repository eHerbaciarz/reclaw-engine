#!/usr/bin/env python3
"""Writes platform/<platform>/config.xml for every port from the Switch one.

Each platform differs in the screen size, where the bundled files are
(romfs:/, app0:/, the app's folder), where CLAW.REZ is expected and how much
memory the resource cache may use.

    python3 scripts/make_platform_configs.py
"""
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(REPO, "platform", "switch", "config.xml")

PLATFORMS = {
    #           bundle prefix  CLAW.REZ path                          window      scale  cache  mixer  freq
    "vita":    ("app0:/",     "ux0:data/reclaw/data/CLAW.REZ",       (960, 544),  "1",  "96",  "32", "44100",
                "PS Vita: 960x544 screen; the game renders 854x480 and is scaled up"),
    "3ds":     ("romfs:/",    "sdmc:/3ds/reclaw/data/CLAW.REZ",      (400, 240),  "1",  "24",  "16", "22050",
                "3DS: top screen 400x240 (the game view), bottom screen = status (SecondScreen)"),
    "wiiu":    ("fs:/vol/content/",    "fs:/vol/external01/wiiu/apps/reclaw/data/CLAW.REZ", (1280, 720), "1.5", "150", "64", "44100",
                "Wii U: 1280x720 on the TV, the GamePad shows the status screen"),
    "ps3":     ("",           "data/CLAW.REZ",                       (1280, 720), "1.5", "150", "64", "44100",
                "PS3: files in /dev_hdd0/game/RCLW00001/USRDIR/"),
    "ps4":     ("/app0/",     "/data/reclaw/data/CLAW.REZ",          (1920, 1080), "2.25", "200", "64", "48000",
                "PS4: files in /data/reclaw/ (FTP / USB); the game renders 854x480, scaled to the TV"),
    "android": ("",           "data/CLAW.REZ",                       (1280, 720), "1.5", "150", "64", "44100",
                "Android: files in the app's folder (Android/data/<app>/files/)"),
}


def main():
    base = open(BASE, encoding="utf-8").read().replace("\r\n", "\n")
    for name, (prefix, rez, (w, h), scale, cache, mixer, freq, comment) in PLATFORMS.items():
        s = base.replace("romfs:/", prefix)
        s = re.sub(r"<!-- Nintendo Switch:.*?-->", f"<!-- {comment} -->", s, flags=re.S)
        s = re.sub(r'<Size width="\d+" height="\d+" />', f'<Size width="{w}" height="{h}" />', s)
        s = re.sub(r"<Scale>[^<]*</Scale>", f"<Scale>{scale}</Scale>", s)
        s = re.sub(r"<RezArchive>[^<]*</RezArchive>", f"<RezArchive>{rez}</RezArchive>", s)
        s = re.sub(r"<ResourceCacheSize>[^<]*</ResourceCacheSize>", f"<ResourceCacheSize>{cache}</ResourceCacheSize>", s)
        s = re.sub(r"<MixingChannels>[^<]*</MixingChannels>", f"<MixingChannels>{mixer}</MixingChannels>", s)
        s = re.sub(r"<Frequency>[^<]*</Frequency>", f"<Frequency>{freq}</Frequency>", s)
        if name == "android":
            s = s.replace("<Enable>false</Enable>", "<Enable>true</Enable>")  # touch controls
        out = os.path.join(REPO, "platform", name)
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "config.xml"), "w", encoding="utf-8") as f:
            f.write(s)
        print("wrote", os.path.join(out, "config.xml"))


if __name__ == "__main__":
    main()
