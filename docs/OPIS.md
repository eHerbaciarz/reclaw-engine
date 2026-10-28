# ReClaw – opis / description

## 🇵🇱 Polski

### Czym jest ReClaw

**ReClaw** to nieoficjalny, fanowski silnik do platformówki **Claw** (Monolith
Productions, 1997), znanej też jako *Captain Claw* / *Kapitan Pazur*. Uruchamia
oryginalną grę z danych z Twojej własnej kopii (plik `CLAW.REZ`) na Nintendo
Switch, a także na PC (Linux, Windows) i innych konsolach (PS Vita, 3DS,
Wii U, PS3, PS4, Android – w przygotowaniu).

ReClaw powstał z połączenia dwóch projektów opartych na
[OpenClaw](https://github.com/pjasicek/OpenClaw) (Petr Jašíček i inni):
- [claw-web](https://github.com/arthurboss/claw-web) (Arthur Boss) – nowsze
  poprawki gry i silnika; to repozytorium jest jego forkiem,
- [ClawSwitch](https://github.com/djmuted/ClawSwitch) (djmuted) – pierwszy
  port na Switcha.

Do tego dochodzą nowe funkcje: zapis stanu z cofaniem, podnoszenie i rzucanie
beczek z prochem, mapa poziomu, wyścig dla dwóch graczy (podzielony ekran albo
dwie konsole w sieci), filmy z płyty, napisy SRT, polskie teksty nowych ekranów.

**ReClaw nie zawiera żadnych danych gry.** Potrzebny jest `CLAW.REZ`
(i opcjonalnie `CLAW.FEC` z filmami) z kupionej kopii gry. Projekt nie jest
związany z Monolith Productions ani Warner Bros.; „Claw” i „Captain Claw” to
ich znaki towarowe. Licencja: GPL-3.0.

### Dlaczego nowa nazwa

Projekt nie nazywa się „OpenClaw”, bo pod tą nazwą działa dziś popularna
platforma agentów AI i wyszukiwanie „OpenClaw” prowadzi głównie do niej. Dawny
silnik gry OpenClaw jest też osobnym projektem, a ReClaw poszedł dalej
(konsole, nowe funkcje). Nowa, krótka nazwa ułatwia znalezienie projektu
i odróżnia go od obu. „Re-” oznacza Claw od nowa, a dopisek „engine” w nazwie
repozytorium podkreśla, że to silnik, a nie sama gra.

### Instalacja na Switchu

Potrzebna jest przerobiona konsola z Homebrew Menu albo emulator (Ryujinx).

1. Skopiuj `reclaw.nro` do folderu **`sd:/switch/claw/`**.
2. Skopiuj **`CLAW.REZ`** ze swojej kopii gry do tego samego folderu.
   Zamiast `CLAW.REZ` może być rozpakowany folder `Assets`.
3. Opcjonalnie: **`CLAW.FEC`** (filmy z płyty) do tego samego folderu.
4. Uruchom „ReClaw” z Homebrew Menu. Najlepiej przez gierkę z trzymanym R
   (title override) – więcej pamięci niż z albumu.

Na Ryujinx działa też folder `sdcard/Games/claw/` obok pliku `.nro`.
Przy pierwszym uruchomieniu powstają tam `config.xml`, zapisy (`SAVES.XML`)
i dziennik błędów `openclaw.log`.

**Sterowanie (Switch):** B – skok, Y – szabla, A – strzał/magia/dynamit,
X – podnieś/rzuć beczkę, L – zmiana broni, + – pauza, − – mapa poziomu,
ZL/ZR – zapisz/wczytaj stan, trzymane R – cofanie czasu.
Pełna plansza jest w Opcje → Sterowanie.

### Co działa, a co jeszcze nie

**Sprawdzone na Switchu / w Ryujinx:**
- uruchamianie gry i przechodzenie poziomów,
- liny (poziom 9) bez drgań,
- dane z folderu `Assets` i dźwięki z niego,
- filmy z `CLAW.FEC` (4:3) i strona „Odtwórz film”,
- rozwijana mapa tytułowa, wyrównanie menu.

**Zrobione, czeka na testy:**
- zapis stanu (ZL/ZR) i cofanie (R),
- beczki z prochem, mapa poziomu, menu pauzy,
- wyścig: podzielony ekran i dwie konsole w sieci,
- polski ekran ładowania z REZ, plansze Sterowanie i Twórcy,
- napisy SRT w filmach i dialogach (opcja w Wyświetlanie), sekretny kod
  w pauzie z ekranem kodów,
- własne poziomy (folder Custom),
- tryby bossów: „Skrzyżuj szpady ponownie” (trening z wybranym bossem)
  i „Paruj i kontruj” (wszyscy bossowie pod rząd na czas, po ukończeniu gry).

**Jeszcze nie działa / niekompletne:**
- bossowie: Le Rauxe (2), Katherine (4) i Wolvington (6) mają tylko ogólne
  zachowanie, Aquatis (12) i Red Tail (13) są uproszczeni, Omara (14) jeszcze
  nie ma; Gabriel (8) i Marrow (10) działają,
- poziom 14: działa tylko pierwszy etap,
- poziom 10: zły punkt startu,
- animacje noszenia beczki są tymczasowe,
- porty poza Switchem: Linux, Windows, 3DS i Wii U się budują, ale nie były
  testowane z grą; Vita, PS3, PS4 i Android powstają w GitHub Actions.

Błędy najlepiej zgłaszać z plikiem `openclaw.log` z folderu gry.

---

## 🇬🇧 English

### What ReClaw is

**ReClaw** is an unofficial, fan-made engine for the platform game **Claw**
(Monolith Productions, 1997), also known as *Captain Claw*. It runs the
original game from the data of your own copy (`CLAW.REZ`) on the Nintendo
Switch, as well as on PC (Linux, Windows) and other consoles (PS Vita, 3DS,
Wii U, PS3, PS4 and Android are in progress).

ReClaw merges two projects based on
[OpenClaw](https://github.com/pjasicek/OpenClaw) (Petr Jašíček and contributors):
- [claw-web](https://github.com/arthurboss/claw-web) (Arthur Boss) – newer game
  and engine fixes; this repository is a fork of it,
- [ClawSwitch](https://github.com/djmuted/ClawSwitch) (djmuted) – the first
  Switch port.

On top of that it adds: save states with rewind, picking up and throwing
powder kegs, a level map, a two-player race (split screen or two consoles over
the network), the original CD movies, SRT subtitles, and Polish texts for its
own screens.

**ReClaw contains no game data.** You need `CLAW.REZ` (and optionally
`CLAW.FEC` with the movies) from a copy of the game you own. The project is
not affiliated with Monolith Productions or Warner Bros.; "Claw" and
"Captain Claw" are their trademarks. License: GPL-3.0.

### Why a new name

It is not called "OpenClaw" because that name now belongs to a popular AI
agent platform, and searching for "OpenClaw" mostly finds it. The original
OpenClaw game engine is also a separate project, and ReClaw has gone further
(consoles, new features). A new, short name makes the project easier to find
and tells it apart from both. "Re-" stands for Claw made anew, and "engine" in
the repository name makes clear that this is an engine, not the game itself.

### Installing on the Switch

You need a modded console with the Homebrew Menu, or an emulator (Ryujinx).

1. Copy `reclaw.nro` to **`sd:/switch/claw/`**.
2. Copy **`CLAW.REZ`** from your copy of the game into the same folder.
   An unpacked `Assets` folder works instead of `CLAW.REZ`.
3. Optional: **`CLAW.FEC`** (the CD movies) into the same folder.
4. Start "ReClaw" from the Homebrew Menu, preferably through a game with R held
   (title override) – more memory than from the album.

On Ryujinx, `sdcard/Games/claw/` next to the `.nro` works too. The first start
creates `config.xml`, the saves (`SAVES.XML`) and the error log `openclaw.log`
there.

**Controls (Switch):** B – jump, Y – sword, A – pistol/magic/dynamite,
X – lift/throw a keg, L – change weapon, + – pause, − – level map,
ZL/ZR – save/load state, hold R – rewind. The full chart is in
Options → Controls.

### What works and what does not yet

**Tested on the Switch / in Ryujinx:**
- starting the game and playing through the levels,
- ropes (level 9) without jitter,
- data from an `Assets` folder, including its sounds,
- movies from `CLAW.FEC` (4:3) and the "Replay movies" page,
- the unrolling title map, menu alignment.

**Done, waiting for testing:**
- save states (ZL/ZR) and rewind (R),
- powder kegs, level map, pause menu,
- race: split screen and two consoles over the network,
- Polish loading screen from the REZ, Controls and Credits pages,
- SRT subtitles for movies and dialogue (option under Display), a secret code
  in the pause menu with a cheats screen,
- custom levels (Custom folder),
- boss modes: "Cross Swords Again" (practice against a chosen boss) and
  "Parry & Thrust" (every boss in a row against the clock, after finishing
  the game).

**Not working yet / incomplete:**
- bosses: Le Rauxe (2), Katherine (4) and Wolvington (6) only have generic
  behaviour, Aquatis (12) and Red Tail (13) are simplified, Omar (14) is not
  there yet; Gabriel (8) and Marrow (10) work,
- level 14: only the first stage works,
- level 10: wrong start position,
- the keg-carrying animations are placeholders,
- ports other than the Switch: Linux, Windows, 3DS and Wii U build but have
  not been tested with the game; Vita, PS3, PS4 and Android are built by
  GitHub Actions.

Please report bugs with the `openclaw.log` file from the game folder.
