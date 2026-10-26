# ReClaw — Nintendo Switch port

A homebrew (`.nro`) build of this repository's engine for the Nintendo Switch.

It combines the two existing forks of [pjasicek/OpenClaw](https://github.com/pjasicek/OpenClaw):

- **Game code from claw-web** (this repository): the 2020–2022 upstream fixes
  that ClawSwitch never received (level 12 and 13 loading, level 13 path
  elevators, level 13 finishable with a simplified Red Tail boss, enemy line of
  sight, floor spikes…) plus claw-web's own work: smooth position
  interpolation, in-engine XMI→MIDI music conversion, menu and save fixes.
- **Platform layer from [ClawSwitch](https://github.com/djmuted/ClawSwitch)**
  (2019): libnx build, `appletMainLoop`, SD-card layout, controller support.

No game data is included. You need `CLAW.REZ` from your own copy of
Captain Claw (1997).

## Installing

1. Copy `reclaw.nro` to `/switch/claw/` on the SD card.
2. Copy `CLAW.REZ` from your copy of the game to `/switch/claw/CLAW.REZ`.
3. Launch "ReClaw" from the Homebrew Menu.
   Use title-override (hold R while starting a retail game) for full memory;
   applet mode (album) works too but has less RAM.

The first launch creates `/switch/claw/config.xml`; `SAVES.XML` and
`openclaw.log` (warnings and errors) are written to the same folder.

## Controls

Face buttons are mapped by **position**, so the layout matches the web and
PC versions on any controller (including the Joy-Cons held sideways):

| Action                  | Button                       |
|-------------------------|------------------------------|
| Move / climb / duck     | Left stick or D-pad          |
| Jump                    | B (bottom)                   |
| Sword attack            | Y (left)                     |
| Fire pistol/magic/dynamite | A (right)                 |
| Previous / next ammo    | L / R                        |
| Pause menu              | +                            |
| Menus                   | A confirm, B back, D-pad/stick to navigate |

Rumble is supported (landing, attacks, damage, explosions, death).

## What differs from the web build

- Rendering uses SDL_Renderer (as upstream OpenClaw) instead of WebGL.
- Level music is synthesized on the console with TinySoundFont and the same
  GeneralUser GS soundfont the web build uses; it is decoded in the background
  at startup, so music starts a few seconds after launch.
- Browser-only menu entries (export/import saves to a file, the Display page
  with fullscreen/aspect toggles) are hidden. Saves already live in a file on
  the SD card and can be backed up by copying `SAVES.XML`.
- Cutscene videos are not played (the web build uses the browser's video
  player).

## Building

Locally, with devkitPro:

```sh
export DEVKITPRO=/opt/devkitpro
sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_mixer switch-sdl2_image \
                   switch-sdl2_ttf switch-sdl2_gfx switch-zlib
make -C platform/switch -j"$(nproc)"
```

Or push the repository to GitHub: `.github/workflows/switch.yml` builds the
`.nro` in the `devkitpro/devkita64` container and uploads it as the
`openclaw-switch` artifact (ready-to-copy `switch/claw/` folder).

The same sources build natively on Linux, which is the quickest way to test
changes before putting them on the console:

```sh
cmake -S platform/linux -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"
cd Build_Release && ../build-linux/openclaw   # needs CLAW.REZ and ASSETS.ZIP here
```

## Layout of the port

| Path | Purpose |
|------|---------|
| `platform/switch/Makefile` | libnx build; bundles ASSETS.ZIP, fonts, soundfont and default config into romfs |
| `platform/switch/config.xml` | default config (1280×720, 1.5× scale = the web build's 480-line view) |
| `platform/linux/CMakeLists.txt` | native desktop build of the same sources |
| `OpenClaw/Engine/Platform/Input/SdlGamepadInput.*` | SDL controllers → the engine's AppEvent gamepad pipeline |
| `OpenClaw/Engine/Audio/Native/SDL2MixerSystem.*` | SDL_mixer effects + TinySoundFont MIDI music |
| `OpenClaw/Engine/Graphics/Native/GraphicsAdapterNative.cpp` | native no-op for the WebGL layer |

## Licenses

- Engine: GPL-3.0 (see `LICENSE.txt`), as upstream OpenClaw.
- TinySoundFont: MIT. stb_vorbis: public domain / MIT
  (`ThirdParty/TinySoundFont/`).
- GeneralUser GS soundfont © S. Christian Collins, free to use in software
  (license text embedded in `Build_Release/music/soundfont.sf3`).
- Captain Claw is © Monolith Productions. This project contains none of its
  data; `CLAW.REZ` must come from a copy of the game you own.
