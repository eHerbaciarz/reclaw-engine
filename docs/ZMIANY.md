# Zmiany w claw-experimental (do przetestowania)

Lista tego, co jest w `claw-experimental`, a czego nie ma jeszcze w `claw-main-test`.
MK wybiera, co przechodzi dalej. Wybrane zmiany trafiają do `claw-main-test`
(cherry-pick po numerach commitów), a po sprawdzeniu, czy nic się nie zepsuło,
do `claw-main`.

Oznaczenia: **[wspólne]** – zmienia grę na wszystkich platformach;
**[PC]** – Windows / macOS; **[PS2/DC]** – tylko PS2 i Dreamcast (reszta platform
bez zmian); **[skrypty]** – tylko skrypty testowe, gra bez zmian.

Status: `[ ]` do testu · `[x]` przetestowane, do claw-main-test · `[-]` odrzucone

## Rozgrywka (zgodność z oryginałem) [wspólne]

| Status | Zmiana | Commity |
|---|---|---|
| [ ] | Ruch, drabiny, śmierć i otwarcie poziomu jak w oryginale; własne poziomy | 4c21e6f |
| [ ] | Kocimiętka i drabina jak w oryginale | d0b512f |
| [ ] | Noszenie i rzucanie beczek; beczka obraca się nad głową i kręci w locie | db08b6c, ce0b6b0 |
| [ ] | Claw podnosi i rzuca zwykłych wrogów; podnoszenie atak + góra, rzut atakiem | 28ddbe3, 33803c1 |
| [ ] | Zegar gry równo z czasem rzeczywistym (był 4% wolniejszy) | 54522f9 |
| [ ] | Tempo chodzenia i wspinania jak na nagraniach oryginału; animacja chodu przyspiesza z rozbiegiem i kocimiętką | 019d9c5, 483bfef |
| [ ] | Balansowanie na krawędzi (tylko na krawędziach kafli, nie na windach ani kołkach); limit 60 FPS; Esc w trybie lustra; skoki bossów | e7c889a, bd05e60, e246cfd |
| [ ] | Animowane dekoracje 100 ms na klatkę; domyślna głębokość (Z) dekoracji | f1c4983, 93f926e |
| [ ] | Plany tła wybierane po nazwie z pliku poziomu; plany działają mimo brakujących kafli | ce0b6b0, 9080dab |
| [ ] | Klejnot bossa leci łukiem w stałe miejsce | 1688704 |
| [ ] | Kameleony znikają i pojawiają się jak w oryginale | f51524f |
| [ ] | Tygrysy z poziomu 14 walczą jak w oryginale | ecd384e |
| [ ] | Bossowie z mieczem przyjmują tyle trafień co w oryginale | 785f56d |
| [ ] | Raux: blokuje 1 na 10 ciosów, przeskakuje stojącego Clawa, cofa się i podchodzi | 723fbd5, 26dedb0, b4b1c7e |
| [ ] | Katherine i Wolvington blokują; Katherine odskakuje przy zamachu | e106a67, c1fb99d |
| [ ] | Wolvington: decyzje w walce jak w CLAW.EXE | 4cbbbc3 |
| [ ] | Gabriel: walka jak w CLAW.EXE; dźwignia armaty | 14c1d66, 93f926e |
| [ ] | Marrow: papuga i zapadnie; nóż i pistolet | e916393, 841d177 |
| [ ] | Aquatis: walka jak w CLAW.EXE | b23ed51 |
| [ ] | Red Tail: walka jak w CLAW.EXE; szczątki niesione wiatrem | 68761ea, 657296b |
| [ ] | Omar: walka jak w CLAW.EXE (pierścień przy lawie, druga faza na górze); Omar w treningu bossów | 456f254, 5788fe3, b6439d1 |
| [ ] | Poziom 11: lasery | 6b387c1 |
| [ ] | Poziom 12: spadające stalaktyty, trzęsienie ekranu (strefy i trzęsienie u Aquatisa), znaki „Sign” nie są tworzone | c748d07, 8959255, ab45c33 |
| [ ] | Poziom 13: tło w drugiej połowie poziomu | 5788fe3 |

## Menu i interfejs [wspólne]

| Status | Zmiana | Commity |
|---|---|---|
| [ ] | Opcje > Grafika: przednia warstwa wł./wył.; wygładzanie na PC | 8ca3c9e |
| [ ] | Filmy, mapa tytułowa i ekran startowy bez podwójnego powiększenia | 20ff627 |
| [ ] | Wybór własnych poziomów: komunikat o pustym folderze w języku gracza, prawdziwa ścieżka | 67206af |
| [ ] | Komenda konsoli `konamicode` | 6241caa |
| [ ] | Błąd przy budowaniu pozycji menu: złapany i zapisany w logu z nazwą pozycji | 340446e, 9975e29 |

## Windows / macOS [PC]

| Status | Zmiana | Commity |
|---|---|---|
| [ ] | Lustrzane klawisze do nagrań obok oryginału; okno 4:3 z Enterem | acb881d, 0fb977b |
| [ ] | Spokojny komunikat o braku CLAW.REZ | 0fb977b |
| [ ] | Klawisze czytane po scancode; log w save\reclaw.log | 9591c2f |
| [ ] | Ostry obraz na ekranach ze skalowaniem (np. 150%) | 323d916 |
| [ ] | libwap: IO.h → WapIO.h – **bez tego claw-main nie buduje się na Windows** (main.bat / main-test.bat nie mają paczki) | 2741c1b |

## PS2 i Dreamcast [PS2/DC]

Zmiany za `#if` dla PS2/Dreamcast – na pozostałych platformach kod działa jak wcześniej.
Wyjątki oznaczone **(wspólny kod)**: zmieniają kod używany na wszystkich platformach,
więc warto je sprawdzić także na PC/Switchu.

| Status | Zmiana | Commity |
|---|---|---|
| [ ] | Bez soundfontu, prototypy aktorów czytane przy pierwszym użyciu, ścieżki host: | 4ce5732, 47228ee, 40043aa |
| [ ] | std::min/max na int (Omar, Gabriel) – kompilacja PS2 **(wspólny kod, bez zmiany działania)** | 219d79e, 3668144 |
| [ ] | Pliki menu wczytywane przy użyciu, nieobsłużone wyjątki w logu | 6859a9a |
| [ ] | Bez animacji przewijania tytułu i menu | c10fead |
| [ ] | Filmy wyłączone; za duże obrazki zmniejszane do tekstur GPU | 0d0888a, 0c8c2f2 |
| [ ] | Stos 1 MB | 71179c6 |
| [ ] | `shared_ptr` bez semafora jądra na obiekt (menu stawało na 48. obrazku) | e74df86 |
| [ ] | Strony menu budowane przy wejściu, usuwane przy wyjściu **(wspólny kod przełączania stron)** | 6ab052e |
| [ ] | Kolory jak w grze (łatka SDL dla PS2); pliki poziomu czytane przy użyciu | b9e0e84 |
| [ ] | Kafelki poziomu jako jeden tekst w XML | 31b5d5b |
| [ ] | Obiekty poziomu tworzone po 16; muzyka menu zwalniana; mniejszy bufor CLAW.REZ **(wspólny kod ładowania poziomu)** | 63131f6 |
| [ ] | Więcej pamięci na poziom: program kompilowany na rozmiar (-Os, bez nieużywanego kodu), stos 256 KB, bez drugiego obrazu ekranu ładowania | (ten commit) |
| [ ] | Ślady w logu do diagnozy (menu, obrazki, pamięć) | 5e8d100, 160b617, 8013935, 62c08f7 |

## Skrypty testowe [skrypty]

| Status | Zmiana | Commity |
|---|---|---|
| [ ] | Test Switcha w Ryujinxie (pobranie, uruchomienie bez praw administratora, logi) | 1912a03, 4829e8b, 7f3c235, 6ad1194, c830da8 |
| [ ] | Porównanie z oryginałem jednym kliknięciem | 40a1a3e, b8385d0, 2b2d66e, 71dea9f, 538658f |
| [ ] | Gałąź claw-ps2 i test w PCSX2 | 70005d5, 61b1972, 9f445bf, 5515b9d |
| [ ] | Skrypty: oryginał z builds\crazyhook-windows, ps-test → ps2-test | (ten commit) |
| [ ] | Nowy zestaw skryptów testowych (main / main-test / experimental, Switch / PS2 / Dreamcast) i gałęzie claw-main-test, claw-switch, claw-dreamcast | 3f5ab16, 328a568 |
