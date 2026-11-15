# ReClaw (reclaw-engine)

*English below · [Opis po polsku](#po-polsku)*

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

All four versions were tested on real hardware (Switch, PS Vita, New
Nintendo 3DS, PS3) and run at a full 60 fps. On the New 3DS the stereoscopic
3D mode and the second screen work too. The game has not been tested on the
original (Old) 3DS / 2DS, so how well it runs there is unknown.

The source tree still holds code for other platforms inherited from earlier
work; it is untested and not released.

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
- New 3DS: stereoscopic 3D and a second screen with the menu map and the player's status
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

---

## Po polsku

**ReClaw** to nieoficjalny, fanowski silnik platformówki **Claw**
(Monolith Productions, 1997), znanej też jako Captain Claw / Kapitan Pazur.
Uruchamia oryginalną grę z Twojej własnej kopii jej danych na konsolach
i dodaje kilka rzeczy od siebie.

> **ReClaw nie zawiera danych gry.** Potrzebujesz pliku `CLAW.REZ` (a do
> filmów `CLAW.FEC`) z posiadanej kopii Clawa. ReClaw nie jest powiązany
> z Monolith Productions ani Warner Bros. ani przez nie wspierany; „Claw”
> i „Captain Claw” to ich znaki towarowe.

### Platformy

| Platforma | Paczka | Folder ReClaw (CLAW.REZ trafia do jego `data/`) |
|---|---|---|
| Nintendo Switch | `reclaw.nro` | `sd:/switch/reclaw/` |
| PS Vita | `reclaw.vpk` | `ux0:data/reclaw/` |
| Nintendo 3DS | `reclaw.cia` / `reclaw.3dsx` | `sdmc:/3ds/reclaw/` |
| PS3 | `reclaw.pkg` / `reclaw_hen.pkg` | `/dev_hdd0/game/RCLW00001/USRDIR/` |

Wszystkie cztery wersje przetestowałem na prawdziwym sprzęcie (Switch,
PS Vita, New Nintendo 3DS, PS3) – działają w pełnych 60 klatkach na sekundę.
Na New 3DS działa też tryb 3D i drugi ekran. Na starym 3DS / 2DS (Old 3DS)
gra nie była testowana, więc nie wiadomo, jak wydajnie będzie tam działać.

W kodzie zostały jeszcze inne platformy z wcześniejszych prac – nie są
przetestowane ani wydawane.

Szczegóły (sterowanie, drugie ekrany, budowanie): [docs/PORTY.md](docs/PORTY.md).

### Folder ReClaw

```
reclaw/
├── reclaw.nro          (Switch)
├── data/
│   ├── pl/             CLAW.REZ, CLAW.FEC z polskiego wydania (albo rozpakowane Assets)
│   └── en/             CLAW.REZ, CLAW.FEC z angielskiego wydania
├── lang/
│   ├── pl/
│   │   └── menu/strings.txt      teksty menu i ekranów portu
│   └── en/   (to samo)
└── save/               config.xml, SAVES.XML, port_settings.txt, reclaw.log,
                        reclaw_previous.log (log poprzedniej sesji)
```

Gra sama tworzy te foldery przy pierwszym uruchomieniu.

Każdy podfolder `data/` z plikiem CLAW.REZ to jeden język (nazwa folderu to
kod języka, działają też `de/`, `fr/`...). Przy jednym folderze gra używa
tego języka, a Opcje → Język są wyszarzone. Przy kilku: przy pierwszym
uruchomieniu angielski (`en`), potem język wybrany w **Opcje → Język** –
zapisany w `save/config.xml`, działa od następnego uruchomienia gry.
CLAW.REZ wrzucony prosto do `data/` też działa (jako jedyny język).

### Co dodaje ReClaw

- Porty na Switch, PS Vita, 3DS i PS3
- Rysowanie przez GPU z wygładzaniem, tryb 16:9 lub klasyczny 4:3, efekt CRT, licznik FPS
- New 3DS: tryb 3D i drugi ekran z mapą z menu i stanem gracza
- Polskie i angielskie teksty własnych ekranów, strona sterowania dla każdej konsoli

### Na podstawie

ReClaw nie powstałby bez tych projektów (wszystkie na GPL-3.0):

- [OpenClaw](https://github.com/pjasicek/OpenClaw) – Petr Jašíček, Pavel Löbl i inni – silnik
- [claw-web](https://github.com/arthurboss/claw-web) – Arthur Boss – to repozytorium jest jego forkiem
- [ClawSwitch](https://github.com/djmuted/ClawSwitch) – djmuted – pierwszy port na Switcha

ReClaw: Herbaciarz, z pomocą Claude (Anthropic).

### Budowanie

Każda konsola ma swój folder w `platform/` (`switch`, `vita`, `3ds`, `ps3`)
i buduje się jej homebrewowym SDK (devkitPro, VitaSDK, PSL1GHT) – zobacz
[docs/PORTY.md](docs/PORTY.md).

### Licencja

GPL-3.0, tak jak projekty, na których się opiera – zobacz [LICENSE.txt](LICENSE.txt).
Części zewnętrzne mają własne licencje (Box2D, TinyXML, TinySoundFont,
libsmacker, SDL2, czcionki – zobacz `ThirdParty/` i foldery czcionek).
