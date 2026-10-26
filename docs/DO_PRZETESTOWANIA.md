# Do przetestowania

Lista rzeczy zrobionych „na sucho”: skompilowane, ale jeszcze nie sprawdzone
przez MK w Ryujinx / na Switchu. Po teście przenosimy wpis do sekcji
„Sprawdzone” (z datą i krótkim wynikiem).

## Skrypty (uruchamiane obok CLAW.REZ, wynik .txt odsyłany)

| Skrypt | Do czego | Stan |
|---|---|---|
| `anim_report.py` | nazwy animacji kapitana (podnoszenie / noszenie / rzut beczki), beczki na poziomach | czeka na `anim_report.txt` |
| `level_survey.py` | punkty startu i checkpointy poziomów 1–14, obiekty poziomu 14 (poprawki poziomu 10 i 14) | czeka na `level_survey.txt` |
| `rez_lang_probe.py` | odcisk polskiego i angielskiego REZ (rozpoznawanie języka napisów portu) | czeka na `rez_lang_*.txt` z obu wersji |
| `boss_survey.py` | bossowie Red Tail, Aquatis, Le Rauxe, Katherine, Wolvington i kameleony: obiekty walki z ustawieniami, animacje, grafiki, dźwięki (poziomy 2, 4, 6, 12, 13, 14) | czeka na `boss_survey.txt` |
| `ingame_menu_check.py` | czy w REZ są wszystkie grafiki menu pauzy (Wróć do gry, Zakończ życie, Opcje, Pomoc, Zakończ grę) | czeka na `ingame_menu_check.txt` |
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
| Linux x86_64 | `claw-linux-x86_64.tar.gz` | buduje się, startuje (komunikat o braku CLAW.REZ) | pełna gra z CLAW.REZ, pad, dźwięk, muzyka |
| Windows x86_64 | `claw-windows-x86_64.zip` | buduje się (MinGW) | start, pad (Xbox), okno / pełny ekran, wyścig przez sieć (zapora!) |
| 3DS | `claw.3dsx` | buduje się | dolny ekran: mapa z menu + stan gracza; płynność (New 3DS), pamięć, dźwięk, czy widok 400×240 jest wygodny (`<Scale>` w config.xml) |
| Wii U | `claw.wuhb` | buduje się | GamePad: mapa z menu + stan gracza, TV: gra; przyciski Nintendo (A zatwierdza); wyścig przez sieć |
| PS Vita | `claw.vpk` | CI | ✕ zatwierdza, ○ wraca, skok i magia bez zamiany; pamięć (256 MB sterty) |
| PS3 | `claw.pkg` / `claw.self` | CI | ✕ / ○ jak na Vicie, mapowanie pada DualShock 3, RPCS3 |
| Android | `CaptainClaw.apk` | CI (kod skompilowany na PC) | sterowanie dotykiem i padem, pliki w `Android/data/org.openclaw.claw/files/`, wyścig przez Wi‑Fi |

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
