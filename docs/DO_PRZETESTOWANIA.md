# Do przetestowania

Lista rzeczy zrobionych „na sucho”: skompilowane, ale jeszcze nie sprawdzone
przez MK w Ryujinx / na Switchu. Po teście przenosimy wpis do sekcji
„Sprawdzone” (z datą i krótkim wynikiem).

## Skrypty (uruchamiane obok CLAW.REZ, wynik .txt odsyłany)

| Skrypt | Do czego | Stan |
|---|---|---|
| `anim_report.py` | nazwy animacji kapitana (podnoszenie / noszenie / rzut beczki), beczki na poziomach | otrzymany 28.10 – nazwy pasują (lift, lift2, liftwalk, throw, ducklift) |
| `level_survey.py` | punkty startu i checkpointy poziomów 1–14, obiekty poziomu 14 (poprawki poziomu 10 i 14) | otrzymany 28.10 – poprawione checkpointy poziomu 10 |
| `rez_lang_probe.py` | odcisk polskiego i angielskiego REZ (rozpoznawanie języka napisów portu) | niepotrzebny – język z nazwy folderu data/<kod>/ |
| `boss_survey.py` | bossowie Red Tail, Aquatis, Le Rauxe, Katherine, Wolvington i kameleony: obiekty walki z ustawieniami, animacje, grafiki, dźwięki (poziomy 2, 4, 6, 12, 13, 14) | otrzymany 28.10 – do wdrożenia bossów |
| `ingame_menu_check.py` | czy w REZ są wszystkie grafiki menu pauzy (Wróć do gry, Zakończ życie, Opcje, Pomoc, Zakończ grę) | otrzymany 28.10 – wszystkie grafiki są |
| `subtitle_templates.py` | puste szablony SRT: filmy z CLAW.FEC (`movie_00_pl.srt`…) i dźwięki dłuższe niż 0,6 s (`sounds/…_pl.srt`) + `LISTA.txt` z długościami | nieprzetestowany na prawdziwym REZ (sprawdzony na sztucznym) |
| `pack_rez.py` | pakowanie rozpakowanego folderu Assets z powrotem do .REZ | nieprzetestowany (MK znalazł oryginalny polski REZ) |
| `claw_level_report.py` | raport poziomów | brak potwierdzenia |
| `rez_list.py` | lista plików w REZ | brak potwierdzenia |
| `wav_report.py` | dźwięki w REZ / Assets | brak potwierdzenia |

Skrypty w repozytorium (uruchamiam je ja, generują grafiki):
`scripts/make_controls_image.py`, `scripts/make_credits_image.py`.

## Funkcje w grze

| Funkcja | Co sprawdzić | Stan |
|---|---|---|
| Zapis stanu (ZL / ZR / trzymane R) | zapis, wczytanie, cofanie; znajdźki, skrzynie, wrogowie, winda, lina, śmierć, walka z bossem (Marrow), płynność cofania | nieprzetestowane |
| Plansza Opcje → Sterowanie | czy się otwiera, czy grafika dobrze leży, powrót B | nieprzetestowane |
| Ekran ładowania | polska plansza 4:3 z REZ na panoramicznym tle, przejście na brzegach | nieprzetestowane |
| Beczki z prochem (X / Z) | podniesienie, noszenie, rzut krótki i daleki, wybuch o ziemię / ścianę / wroga, odłożenie po trafieniu; animacje (po `anim_report.txt`) | nieprzetestowane |
| Twórcy (sam tekst) | wygląd planszy | nieprzetestowane |
| Wyścig – podzielony ekran | Multiplayer → Podzielony ekran: ekran wyboru kontrolerów, wybór poziomu, odliczanie, dwie połówki ekranu, gracz 2 na drugim padzie, odrodzenie na własnym punkcie kontrolnym, meta i ekran wyników, płynność | nieprzetestowane |
| Wyścig – przez sieć | Multiplayer → Przez sieć: Utwórz / Dołącz (dwie konsole lub dwa Ryujinxy), znalezienie wyścigu, odliczanie, duch rywala, meta, wyjście rywala / zerwanie połączenia | nieprzetestowane (sam moduł UDP sprawdzony na Linuksie) |
| Pauza (+ / Start / Esc) | otwiera menu: Wróć do gry, Zakończ życie, Opcje, Pomoc, Zakończ grę; w wyścigu przez sieć świat się nie zatrzymuje | nieprzetestowane |
| Mapa poziomu (− / Select / Tab) | półprzezroczysta mapa wokół gracza: podłoże, drabiny, kolce, skarby (złote), koniec poziomu (zielony), gracz (miga); w wyścigu drugi gracz / duch na niebiesko; czy skala 1:8 jest dobra | nieprzetestowane |
| Kod w menu pauzy | trzymając R (klawiatura: prawy Shift) wcisnąć dół, dół, lewo, prawo, góra → na dole napis „Nie dość, że pirat to jeszcze oszust” (także przy wyłączonych napisach); od tej chwili w pauzie pozycja „Kody” (zapamiętana); czy pozycja (y=320) mieści się na tle menu | nieprzetestowane |
| Ekran „Kody” | nieśmiertelność, nieskończona amunicja, skok bez ograniczeń (wł./wył.), pełne zdrowie, pełna amunicja, dodatkowe życie | nieprzetestowane |
| Napisy | Opcje → Wyświetlanie → „Napisy: wł./wył.” (menu główne i pauza); napisy SRT w filmach i przy dźwiękach (dialogi); wygląd i wielkość tekstu | nieprzetestowane (parser i rysowanie sprawdzone osobno na PC) |
| Języki danych gry | `data/pl/` i `data/en/` z CLAW.REZ: pierwszy start po angielsku, Opcje → Język (lista, komunikat o ponownym uruchomieniu), po restarcie wybrany język (także napisy i teksty z `lang/<kod>/`), `<Language>` w `save/config.xml`; z jednym folderem „Język” wyszarzony; CLAW.REZ prosto w `data/` dalej działa; nowe rozmieszczenie Opcji (8 pozycji co 32 px) mieści się na tle | nieprzetestowane |
| Tryby eksperymentalne (alfa) | „Tryb wieloosobowy”, „Wczytaj poziom” i „Skrzyżuj szpady ponownie” wyszarzone i nie do wybrania; włączenie: Kody → „Tryby eksperymentalne: wł.” (albo `experimental=1` w `save/port_settings.txt`), po powrocie do menu głównego znów działają | nieprzetestowane |
| Nowy układ folderów | `sd:/switch/reclaw/`: CLAW.REZ / CLAW.FEC w `data/`; po starcie powstają `movies/`, `custom/`, `lang/pl|en/…`, `save/` (config.xml, SAVES.XML, port_settings.txt, reclaw.log); napisy z `lang/pl/subtitles/movies|dialogue/`, brakujące z `lang/en/`; teksty menu z `lang/pl/menu/strings.txt`; stary folder `/switch/claw/` nie jest już czytany | nieprzetestowane |
| „Skrzyżuj szpady ponownie” (trening) | Jeden gracz → „Skrzyżuj szpady ponownie” (w miejscu „Wyślij wyniki”): lista bossów, do których się dotarło (nazwa + poziom + rekord); start od razu przy bossie (scenka wejścia), śmierć = nowa próba przy bossie, życia się nie kończą; po wygranej czas, rekord, liczba prób i wybór: walcz ponownie / inny boss / menu; czy start przy bossie nie wpada w ścianę lub przepaść (poziomy 2, 4, 6, 8, 10, 12, 13) | nieprzetestowane |
| „Paruj i kontruj” (Boss Rush) | ostatnia pozycja listy, otwarta po ukończeniu poziomu 14 (albo Kody → „Odblokuj wszystkich bossów”); bossowie po kolei, zdrowie przechodzi dalej (+20 i trochę amunicji między walkami), śmierć kończy próbę; stoper tylko w trakcie walki (nie liczy ładowania, scenki wejścia i pauzy); podsumowanie czasów i suma, rekord; Omara (14) jeszcze nie ma, więc teraz 7 bossów | nieprzetestowane |
| Gracz 2 po filmach / ekranach blokujących | czy drugi pad zostaje podłączony po filmie lub mapie tytułowej | nieprzetestowane |
| Poziom 14, etap 1 | start, przeciwnicy, elementy poziomu | brak potwierdzenia |
| Odsłanianie widoku na starcie poziomu | efekt od lewej do prawej | brak potwierdzenia |
| Własne poziomy (folder Custom) | lista, wczytanie poziomu | brak potwierdzenia |

## Porty na inne platformy

Opis instalacji i budowania: `docs/PORTY.md`. „Buduje się” = skompilowane
u mnie; „CI” = budowane tylko przez GitHub Actions (`.github/workflows/ports.yml`),
bo tutaj nie da się pobrać tych narzędzi.

| Platforma | Paczka | Stan | Co sprawdzić |
|---|---|---|---|
| Linux x86_64 | `reclaw-linux-x86_64.tar.gz` | buduje się, startuje (komunikat o braku CLAW.REZ) | pełna gra z CLAW.REZ, pad, dźwięk, muzyka |
| Windows x86_64 | `reclaw-windows-x86_64.zip` | buduje się (MinGW) | start, pad (Xbox), okno / pełny ekran, wyścig przez sieć (zapora!) |
| 3DS | `reclaw.3dsx` | buduje się | dolny ekran: mapa z menu + stan gracza; płynność (New 3DS), pamięć, dźwięk, czy widok 400×240 jest wygodny (`<Scale>` w config.xml) |
| Wii U | `reclaw.wuhb` | buduje się | GamePad: mapa z menu + stan gracza, TV: gra; przyciski Nintendo (A zatwierdza); wyścig przez sieć |
| PS Vita | `reclaw.vpk` | CI | ✕ zatwierdza, ○ wraca, skok i magia bez zamiany; pamięć (256 MB sterty) |
| PS3 | `reclaw.pkg` / `reclaw.self` | CI | ✕ / ○ jak na Vicie, mapowanie pada DualShock 3, RPCS3 |
| PS4 | `IV0001-RCLW00002_….pkg` | CI (kod sprawdzony clangiem z libc++ jak w OpenOrbis) | start z GoldHEN, pad DualShock 4 (✕ / ○, Options = pauza, touchpad = mapa), dźwięk, pliki w /data/reclaw/ (CLAW.REZ w data/); czy rusza bez / z libc.prx i libSceFios2.prx w sce_module (nie ma ich w repozytorium) |
| Android | `ReClaw.apk` | CI (kod skompilowany na PC) | sterowanie dotykiem i padem, pliki w `Android/data/org.reclaw.game/files/`, wyścig przez Wi‑Fi |

Wspólne poprawki do sprawdzenia przy okazji:
- Wii U i PS3 to procesory big‑endian: czytanie CLAW.REZ, SoundFont (muzyka)
  i zamiana XMI→MIDI zostały poprawione. REZ i SoundFont sprawdziłem na
  emulowanym PowerPC (qemu): wynik identyczny jak na PC. Poziomy (WWD), grafiki
  i animacje czytał już wcześniej kod odporny na kolejność bajtów.
- Mutex SDL zamiast `std::mutex` (PS3) – na Switchu bez zmian w działaniu.

## Sprawdzone

- Liny (poziom 9) – bez drgań („lina działa idealnie”).
- Folder Assets / dźwięki z Assets – działa.
- Filmy z CLAW.FEC, oba w 4:3, strona „Odtwórz film”.
- Rozwijana mapa tytułowa – „jest naprawdę nieźle”.
- Wyrównanie menu (menu główne; kolumny wyboru poziomu poprawione).
- Wyniki skryptów: `fec_report.py`, `states_tree.py`, `menu_sizes.py`.
