# Porty: Linux, Windows, PS Vita, 3DS, Wii U, PS3, PS4, Android

Każda paczka zawiera tylko silnik i pliki portu (menu, czcionki, SoundFont,
grafiki sterowania). **Danych gry nie ma w żadnej paczce**: gracz kopiuje
`CLAW.REZ` (i opcjonalnie `CLAW.FEC` z filmami) z własnej kopii Captain Claw.
Zamiast `CLAW.REZ` działa też rozpakowany folder `Assets`.

## Gdzie jest folder ReClaw

CLAW.REZ (i CLAW.FEC) idzie do podfolderu `data/`.

| Platforma | Plik gry | Folder ReClaw |
|---|---|---|
| Switch | `reclaw.nro` | `sd:/switch/reclaw/` |
| Linux | `reclaw-linux-x86_64.tar.gz` → `run.sh` | folder z `run.sh` |
| Windows | `reclaw-windows-x86_64.zip` → `ReClaw.exe` | folder z `ReClaw.exe` |
| PS Vita | `reclaw.vpk` (VitaShell) | `ux0:data/reclaw/` |
| 3DS | `reclaw.3dsx` (Homebrew Launcher) | `sdmc:/3ds/reclaw/` |
| Wii U | `reclaw.wuhb` (Aroma, `sd:/wiiu/apps/`) | `sd:/wiiu/apps/reclaw/` |
| PS3 | `reclaw.pkg` (HEN/CFW) lub `reclaw.self` (RPCS3) | `/dev_hdd0/game/RCLW00001/USRDIR/` |
| PS4 | `IV0001-RCLW00002_…pkg` (GoldHEN) | `/data/reclaw/` (FTP / USB) |
| Android | `ReClaw.apk` | `Android/data/org.reclaw.game/files/` (USB / menedżer plików) |

## Układ folderu ReClaw

```
reclaw/
├── reclaw.nro          (Switch; na PC ReClaw.exe / run.sh)
├── data/
│   ├── pl/             CLAW.REZ, CLAW.FEC polskiego wydania (albo rozpakowany Assets)
│   └── en/             CLAW.REZ, CLAW.FEC angielskiego wydania
├── movies/             zremasterowane filmy (jeszcze nieobsługiwane)
├── custom/             własne poziomy (.WWD)
├── lang/
│   ├── pl/
│   │   ├── subtitles/movies/     napisy do filmów (movie_01.srt …)
│   │   ├── subtitles/dialogue/   napisy do kwestii Pazura i przeciwników
│   │   └── menu/strings.txt      teksty menu i ekranów portu
│   └── en/   (to samo)
└── save/               config.xml, SAVES.XML, port_settings.txt, reclaw.log
```

Gra tworzy te foldery przy pierwszym uruchomieniu.

Każdy podfolder `data/` z CLAW.REZ to jeden język (nazwa folderu = kod
języka, także `de/`, `fr/`…). Z jednym folderem gra uruchamia ten język,
a „Język” w Opcjach jest wyłączony. Z kilkoma: pierwszy start po angielsku
(`en`), potem język wybrany w **Opcje → Język** – zapisany w
`save/config.xml`, zmienia się po ponownym uruchomieniu gry. CLAW.REZ
wrzucony prosto do `data/` też działa (jako jedyny język).

## Przyciski

W grze przyciski są **pozycyjne** na każdym padzie: dolny skacze, prawy
strzela (pistolet / magia / dynamit), lewy atakuje szablą, górny podnosi
beczki. Różni się tylko menu:

- **PlayStation (Vita, PS3, PS4)**: ✕ zatwierdza, ○ wraca. Skok (✕) i strzał /
  magia (○) nie są zamieniane.
- **Nintendo (Switch, 3DS, Wii U)**: A zatwierdza, B wraca (A jest po prawej,
  więc w menu port zamienia przyciski, w grze nie).
- **Xbox / PC / Android z padem**: A zatwierdza, B wraca.
- **Android bez pada**: lewa połowa ekranu – ruch, prawa – skok, przesunięcie
  w prawej połowie – strzał, dotknięcie – atak, lewy górny róg – pauza,
  prawy górny róg – zmiana broni (sterowanie dotykowe z oryginalnego OpenClaw).

Plansza „Sterowanie” w Opcjach pokazuje układ właściwy dla platformy.

## Drugi ekran (3DS, Wii U)

Dolny ekran 3DS i ekran GamePada pokazują tło mapy z menu głównego (z CLAW.REZ)
i interfejs: poziom, skarby, zdrowie, życia, amunicję z zaznaczoną bronią,
czas mocy i znacznik wyścigu. Gra jest na górnym ekranie / telewizorze.
Ekran odświeża się tylko po zmianie stanu (3DS rysuje programowo).

## Rozdzielczość

| Platforma | Obraz gry |
|---|---|
| Switch, Vita, Wii U, PS3, PS4 | 854×480 skalowane do ekranu |
| 3DS | 400×240 (górny ekran 1:1; `<Scale>` w config.xml zmienia przybliżenie) |
| Android | 480 linii, szerokość wg proporcji ekranu |
| Linux, Windows | rozmiar okna z config.xml |

## Budowanie

Wszystkie platformy buduje `.github/workflows/ports.yml` (Switch:
`switch.yml`) – po pushu paczki są w zakładce Actions → Artifacts.
Lokalnie (przykłady, ścieżki względem repozytorium):

```
# Linux
cmake -S platform/linux -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux -j
platform/linux/package.sh build-linux dist

# Windows (MSYS2 MinGW64 albo krzyżowo z Linuksa: platform/windows/build.sh)
cmake -S platform/linux -B build-win -G Ninja -DMINGW_PREFIX=/mingw64 && cmake --build build-win

# 3DS (devkitARM, 3ds-dev, 3ds-zlib)
platform/deps/fetch.sh
cmake -S platform/3ds -B build-3ds -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake && cmake --build build-3ds -j

# Wii U (devkitPPC, wiiu-dev, ppc-zlib)
platform/deps/fetch.sh wiiu
cmake -S platform/wiiu -B build-wiiu -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake && cmake --build build-wiiu -j

# PS Vita (VitaSDK + vdpm zlib)
platform/deps/fetch.sh
cmake -S platform/vita -B build-vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake && cmake --build build-vita -j

# PS3 (ps3toolchain + PSL1GHT + ps3libraries 1 2 4 14 16 + platform/ps3/build-sdl-addons.sh)
cmake -S platform/ps3 -B build-ps3 -DCMAKE_TOOLCHAIN_FILE=platform/ps3/ps3.toolchain.cmake && cmake --build build-ps3 -j

# PS4 (PacBrew: ps4-openorbis ps4-openorbis-portlibs; libc.prx i libSceFios2.prx w platform/ps4/sce_module/)
source /opt/pacbrew/ps4/openorbis/ps4vars.sh
openorbis-cmake -S platform/ps4 -B build-ps4 && cmake --build build-ps4 -j

# Android (SDK + NDK 27)
platform/android/fetch-deps.sh
cd platform/android && ./gradlew assembleRelease
```

Wspólne pliki: `platform/cmake/OpenClawGame.cmake` (źródła gry, flagi),
`platform/cmake/BundledSDL.cmake` (SDL2 + image/mixer/ttf kompilowane razem
z grą: Android, 3DS, Wii U, Vita), `scripts/stage_bundle.sh` (pliki portu),
`scripts/make_platform_configs.py` (config.xml dla platform),
`scripts/make_platform_icons.py` (ikony i banery z ikony portu).

## Na wzór czego

- **jazz2-native**: osobne zadania CI w oficjalnych obrazach Dockera
  (vitasdk/vitasdk, devkitpro/devkitarm, devkitpro/devkitppc), PS3 z gotowego
  wydania ps3toolchain, paczki .vpk / .3dsx / .wuhb / .pkg.
- **ScummVM**: PS3 na SDL2 z ps3libraries (sfo.xml, make_self_npdrm, pkg.py),
  Vita przez vita-mksfoex / LiveArea.
- **Wacki**: jedna baza kodu SDL2 z cienką warstwą platformy – tu
  `Engine/Platform/Platform.{h,cpp}` (ścieżki, przyciski, drugi ekran,
  rozdzielczość).
