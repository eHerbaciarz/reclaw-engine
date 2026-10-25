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
| Gracz 2 po filmach / ekranach blokujących | czy drugi pad zostaje podłączony po filmie lub mapie tytułowej | nieprzetestowane |
| Poziom 14, etap 1 | start, przeciwnicy, elementy poziomu | brak potwierdzenia |
| Odsłanianie widoku na starcie poziomu | efekt od lewej do prawej | brak potwierdzenia |
| Własne poziomy (folder Custom) | lista, wczytanie poziomu | brak potwierdzenia |

## Sprawdzone

- Liny (poziom 9) – bez drgań („lina działa idealnie”).
- Folder Assets / dźwięki z Assets – działa.
- Filmy z CLAW.FEC, oba w 4:3, strona „Odtwórz film”.
- Rozwijana mapa tytułowa – „jest naprawdę nieźle”.
- Wyrównanie menu (menu główne; kolumny wyboru poziomu poprawione).
- Wyniki skryptów: `fec_report.py`, `states_tree.py`, `menu_sizes.py`.
