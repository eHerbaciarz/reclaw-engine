# ReClaw (reclaw-engine)

**ReClaw** is an unofficial, fan-made engine for the platform game **Claw**
(Monolith Productions, 1997), also known as Captain Claw / Kapitan Pazur.
It runs the original game from your own copy of its data on PC and on
consoles, and adds a few things of its own.

> **ReClaw contains no game data.** You need `CLAW.REZ` (and, for the
> movies, `CLAW.FEC`) from a copy of Claw you own. ReClaw is not affiliated
> with or endorsed by Monolith Productions or Warner Bros.; "Claw" and
> "Captain Claw" are their trademarks.

## Platforms

| Platform | Package | Where CLAW.REZ goes |
|---|---|---|
| Nintendo Switch | `reclaw.nro` | `sd:/switch/claw/` |
| Linux x86_64 | `reclaw-linux-x86_64.tar.gz` | next to `run.sh` |
| Windows x86_64 | `reclaw-windows-x86_64.zip` | next to `ReClaw.exe` |
| PS Vita | `reclaw.vpk` | `ux0:data/claw/` |
| Nintendo 3DS | `reclaw.3dsx` | `sdmc:/3ds/claw/` |
| Wii U | `reclaw.wuhb` | `sd:/wiiu/apps/claw/` |
| PS3 | `reclaw.pkg` / `reclaw.self` | `/dev_hdd0/game/RCLW00001/USRDIR/` |
| PS4 | `IV0001-RCLW00002_….pkg` | `/data/claw/` |
| Android | `ReClaw.apk` | picked in the app on first start |
| Web browser | see [claw-web](https://github.com/arthurboss/claw-web) | uploaded in the browser |

Details (controls, second screens, building): [docs/PORTY.md](docs/PORTY.md) (Polish).

## What ReClaw adds

- Ports to Switch, Linux, Windows, PS Vita, 3DS, Wii U, PS3, PS4 and Android
- Second screen on 3DS / Wii U: the menu map with the player's status
- Race mode: split screen or two consoles over the local network
- Save states (save / load / rewind), level map, carrying and throwing powder kegs
- Subtitles from SRT files for the movies and spoken lines ([docs/NAPISY.md](docs/NAPISY.md))
- Polish and English texts of its own screens, controls page per platform

## Based on

ReClaw would not exist without these projects (all GPL-3.0):

- [OpenClaw](https://github.com/pjasicek/OpenClaw) by Petr Jašíček, Pavel Löbl and contributors – the engine
- [claw-web](https://github.com/arthurboss/claw-web) by Arthur Boss – this repository is a fork of it
  (its original README: [docs/README-claw-web.md](docs/README-claw-web.md))
- [ClawSwitch](https://github.com/djmuted/ClawSwitch) by djmuted – the first Switch port

ReClaw: MK and Claude (Anthropic).

## Building

```
git clone <this repository> reclaw-engine && cd reclaw-engine
cmake -S platform/linux -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux -j
```

Every platform is built by GitHub Actions (`.github/workflows/ports.yml` and
`switch.yml`); the packages are under Actions → Artifacts.

## License

GPL-3.0, like the projects it is based on – see [LICENSE.txt](LICENSE.txt).
Third-party parts keep their own licenses (Box2D, TinyXML, TinySoundFont,
libsmacker, SDL2, fonts – see `ThirdParty/` and the font folders).
