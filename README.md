# ReClaw (reclaw-engine)

**ReClaw** is an unofficial, fan-made engine for the platform game **Claw**
(Monolith Productions, 1997), also known as Captain Claw / Kapitan Pazur.
It runs the original game from your own copy of its data on game
consoles, and adds a few things of its own.

> **ReClaw contains no game data.** You need `CLAW.REZ` (and, for the
> movies, `CLAW.FEC`) from a copy of Claw you own. ReClaw is not affiliated
> with or endorsed by Monolith Productions or Warner Bros.; "Claw" and
> "Captain Claw" are their trademarks.

## Platforms

| Platform | Package | ReClaw folder (CLAW.REZ goes to its `data/`) |
|---|---|---|
| Nintendo Switch | `reclaw.nro` | `sd:/switch/reclaw/` |
| PS Vita | `reclaw.vpk` | `ux0:data/reclaw/` |
| Nintendo 3DS | `reclaw.cia` / `reclaw.3dsx` | `sdmc:/3ds/reclaw/` |
| PS3 | `reclaw.pkg` / `reclaw_hen.pkg` | `/dev_hdd0/game/RCLW00001/USRDIR/` |

These four are the ones tested on real hardware. The source tree still holds
code for other platforms inherited from earlier work; it is untested and not
released.

Details (controls, second screens, building): [docs/PORTY.md](docs/PORTY.md) (Polish).

### The ReClaw folder

```
reclaw/
├── reclaw.nro          (Switch)
├── data/
│   ├── pl/             CLAW.REZ, CLAW.FEC of the Polish release (or an unpacked Assets)
│   └── en/             CLAW.REZ, CLAW.FEC of the English release
├── lang/
│   ├── pl/
│   │   └── menu/strings.txt      texts of the port's menus and screens
│   └── en/   (the same)
└── save/               config.xml, SAVES.XML, port_settings.txt, reclaw.log,
                        reclaw_previous.log (the last session's log)
```

The game creates these folders on its first start.

Each sub-folder of `data/` with CLAW.REZ is one language (the folder name is
the language code, `de/`, `fr/`... work too). With one folder the game uses
that language and Options → Language is greyed out. With several: English
(`en`) on the first start, then the language chosen in **Options →
Language** – saved in `save/config.xml`, applied when the game is started
again. CLAW.REZ put straight into `data/` also works (as the only language).


## What ReClaw adds

- Ports to Switch, PS Vita, 3DS and PS3
- GPU rendering with smoothing, 16:9 or classic 4:3, CRT effect, FPS counter
- Second screen on 3DS: the menu map with the player's status
- Polish and English texts of its own screens, controls page per platform

## Based on

ReClaw would not exist without these projects (all GPL-3.0):

- [OpenClaw](https://github.com/pjasicek/OpenClaw) by Petr Jašíček, Pavel Löbl and contributors – the engine
- [claw-web](https://github.com/arthurboss/claw-web) by Arthur Boss – this repository is a fork of it
  (its original README: [docs/README-claw-web.md](docs/README-claw-web.md))
- [ClawSwitch](https://github.com/djmuted/ClawSwitch) by djmuted – the first Switch port

ReClaw: Herbaciarz, made with the help of Claude (Anthropic).

## Building

Each console has its folder in `platform/` (`switch`, `vita`, `3ds`, `ps3`)
and is built with its homebrew SDK (devkitPro, VitaSDK, PSL1GHT) – see
[docs/PORTY.md](docs/PORTY.md).

## License

GPL-3.0, like the projects it is based on – see [LICENSE.txt](LICENSE.txt).
Third-party parts keep their own licenses (Box2D, TinyXML, TinySoundFont,
libsmacker, SDL2, fonts – see `ThirdParty/` and the font folders).
