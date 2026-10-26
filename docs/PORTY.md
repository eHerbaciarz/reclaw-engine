# Porty: Linux, Windows, PS Vita, 3DS, Wii U, PS3, Android

Każda paczka zawiera tylko silnik i pliki portu (menu, czcionki, SoundFont,
grafiki sterowania). **Danych gry nie ma w żadnej paczce**: gracz kopiuje
`CLAW.REZ` (i opcjonalnie `CLAW.FEC` z filmami) z własnej kopii Captain Claw.
Zamiast `CLAW.REZ` działa też rozpakowany folder `Assets`.

## Gdzie wrzucić CLAW.REZ

| Platforma | Plik gry | Folder z CLAW.REZ (tam też config.xml, zapisy i openclaw.log) |
|---|---|---|
| Switch | `openclaw.nro` | `sd:/switch/claw/` |
| Linux | `claw-linux-x86_64.tar.gz` → `run.sh` | obok `run.sh` |
| Windows | `claw-windows-x86_64.zip` → `Claw.exe` | obok `Claw.exe` |
| PS Vita | `claw.vpk` (VitaShell) | `ux0:data/claw/` |
| 3DS | `claw.3dsx` (Homebrew Launcher) | `sdmc:/3ds/claw/` |
| Wii U | `claw.wuhb` (Aroma, `sd:/wiiu/apps/`) | `sd:/wiiu/apps/claw/` |
| PS3 | `claw.pkg` (HEN/CFW) lub `claw.self` (RPCS3) | `/dev_hdd0/game/CLAW00001/USRDIR/` |
| Android | `CaptainClaw.apk` | `Android/data/org.openclaw.claw/files/` (USB / menedżer plików) |

## Przyciski

W grze przyciski są **pozycyjne** na każdym padzie: dolny skacze, prawy
strzela (pistolet / magia / dynamit), lewy atakuje szablą, górny podnosi
beczki. Różni się tylko menu:

- **PlayStation (Vita, PS3)**: ✕ zatwierdza, ○ wraca. Skok (✕) i strzał /
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
| Switch, Vita, Wii U, PS3 | 854×480 skalowane do ekranu |
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
