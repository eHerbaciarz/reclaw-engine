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
| Ruch jak w oryginale (27.11, `docs/FIZYKA_ORYGINALU.md`) | chód wolniejszy (225 px/s), rozpęd po 1,5 s biegu (285, nogi szybciej: klatki ×0,9; z kocimiętką ×0,8), skok rośnie z trzymaniem (45–145 px, po rozpędzie 170), wolnieje ku górze, w powietrzu 225; czy wszystkie znane skoki na poziomach wychodzą | sprawdzone skryptem na PC, nie na konsoli |
| Krawędzie (27.11) | po lądowaniu na brzegu kapitan nie zsuwa się; chodzenie po łączeniach kafelków bez zahaczania | sprawdzone skryptem (poziom 1) |
| Drabina (27.11) | na szczycie staje, „w górę” nic nie robi, „w dół” schodzi; skok z drabiny | sprawdzone skryptem (poziom 1) |
| Śmierć (27.11) | od wroga: animacja upadku, spada z miejsca przez podłogę; kolce/woda: jak dawniej; odrodzenie | sprawdzone skryptem (obrażenia z konsoli) |
| Otwarcie poziomu (27.11) | wałek 100 px jedzie od lewej do prawej z zwiniętym obrazem poziomu, dźwięk flagi; płynność na 3DS/Vicie | sprawdzone zrzutami na PC |
| Kocimiętka (myszka) (27.11) | skok do 195 px, bieg 300 px/s, wspinanie 250 px/s, miecz zadaje podwójne obrażenia (już nie zabija jednym ciosem); bez myszki wspinanie 200 px/s | sprawdzone skryptem (poziom 1) |
| Beczki z prochem (27.11) | podniesiona beczka obraca się na bok nad głową, w locie się kręci (klatki LIFTED/LIFTED2/THROWN z oryginału); z beczką 30 px/s, bez skoku; trzymanie rzutu do 3 s = dalej (krótki ok. 150 px, pełny ok. 450 px); zejście z krawędzi z beczką – beczka leci dalej, kapitan spada | chód, skok i rzuty sprawdzone skryptem (poziom 1); zejście z krawędzi nie |
| Lustrzane klawisze (Windows, 27.11) | `ReClaw-mirror.bat`: okno 4:3 (960×720, widać 640×480 planszy jak w oryginale); ReClaw w tle powtarza strzałki, Spację, Enter, Esc, lewy Ctrl/Alt/Shift, Z, X, 1–5 wciśnięte w oknie oryginału | skompilowane (MinGW), nie uruchomione na Windows |
| Kameleony (poziomy 13–14, 27.11) | jak w oryginale znikają (ok. 0,13 s), po 0–1,5 s pojawiają się w innym miejscu swojej ścieżki, raz na sekundę najwyżej; niewidoczne nie dają się trafić | sprawdzone skryptem (poziom 14) |
| Tygrysy (poziom 14, 27.11) | co 1,25–2 s: skok-cios (przysiad 0,5 s, skok, 20 obrażeń), seria 2–4 cięć (po 15), salto w przód + cięcie, czasem salto w tył; blokują 40% ciosów z przodu (dźwięk BLOCK1); w trakcie skoku/salta się nie zatrzymują od ciosu, w salcie nie da się ich trafić; po trafieniu kontratakują od razu; zdrowie 30 (było 40) | sprawdzone skryptem (poziom 14): ataki, blok, trafienie, śmierć |
| Klejnot bossa (27.11) | po śmierci bossa klejnot leci powolnym łukiem (ok. 1–2 s) w stałe miejsce z pliku poziomu i tam wisi; w locie nie da się go złapać (wcześniej spadał i odbijał się po podłodze) | sprawdzone skryptem (poziom 2) |
| Bossowie – bloki (27.11) | Raux blokuje 1 na 10 ciosów miecza (było 1 na 2); Katherine 6 na 10 (wcześniej wcale) i odbija wszystkie pociski; Wolvington 1 na 2 ciosów (wcześniej wcale) | Katherine sprawdzona skryptem (poziom 4) |
| Bossowie – skoki (27.11) | Raux co jakiś czas przeskakuje nad stojącym kapitanem i ląduje 20 px za nim (jak w CLAW.EXE 0x481ba0, nie tylko gdy kapitan jest za plecami); Katherine robi salto w tył (ok. 200–400 px, dźwięk FLIP1), gdy kapitan jest blisko, raz na kilka sekund | Katherine i Raux sprawdzone skryptem (poziom 4 i 2) |
| Balansowanie na krawędzi (27.11) | kapitan stojący środkiem stóp nad przepaścią macha rękami (animacja TEETER z oryginału), zwrócony w stronę przepaści; czasem okrzyk (1021.WAV, 45%); ruch / skok przerywa | sprawdzone skryptem (poziom 1, krawędź ok. 13 px) |
| Limit klatek (27.11) | `<MaxFps>60</MaxFps>` w config.xml (PC; 0 = bez limitu): licznik FPS ok. 60 zamiast 1000+, gra płynna, mniej obciąża procesor | skompilowane, nie sprawdzone na Windows |
| Esc w trybie lustrzanym (27.11) | Esc wciśnięty w oknie oryginału trafia też do ReClaw (pauza / wyjście z menu) | skompilowane (MinGW), nie uruchomione na Windows |
| Tło i pierwszy plan (Windows, 27.11) | brakowało tła i pierwszego planu: SDL2 z MSYS2 to nakładka na SDL3, która odrzuca prostokąty szersze niż połowa INT_MAX (zapętlone warstwy) – naprawione; teraz jeden zły obrazek lub brakujący kafelek nie wyłącza całej warstwy, a `save/reclaw.log` podaje liczbę obrazków każdej warstwy („Plane Background: N tile pictures”) i naciśnięcia Esc | na PC (REZ i rozpakowane Assets) warstwy są; na Windows niesprawdzone |
| Pierwszy plan wł./wył. (27.11) | Opcje → Grafika (menu główne i pauza) → „Pierwszy plan: tak/nie” jak „Brak pierwszego planu” w oryginale; od razu znikają kolumny/most przed kapitanem; zapamiętane (`front_plane` w port_settings.txt) | sprawdzone skryptem (poziom 1, menu pauzy) |
| Wygładzanie na PC (27.11) | Opcje → Grafika → „Wygładzanie” także na Windows/Linux/macOS, domyślnie wyłączone (ostre piksele); widać przy oknie innym niż 1:1 (np. ReClaw-mirror.bat) | skompilowane, nie sprawdzone na Windows |
| Konsola: `konamicode` (27.11) | ` otwiera konsolę, `konamicode` (też `konami`) działa jak wpisanie kodu: napis „Nie dość, że pirat…”, w pauzie pozycja „Kody”, w menu głównym tryby bossów | sprawdzone skryptem (poziom 1, menu pauzy) |
| Omar jak w oryginale (27.11) | faza z pierścieniem (9 kul z przerwą, obraca się, po trafieniu w drugą stronę): tylko przeciwny miecz magiczny zabiera 1/4 paska, zły miecz mu ją oddaje; po trafieniu Omar przelatuje (2 s) na drugie miejsce przy lawie; po 4 trafieniach odlatuje na górną półkę (winda), pasek znów pełny, bez pierścienia – tam każde trafienie zabiera 1/4 i przelatuje między dwoma miejscami; kule energii 300 px/s | sprawdzone skryptem (trafienia mieczami, leczenie, przelot na górę, śmierć po 4 trafieniach na górze) |
| Podnoszenie i rzucanie przeciwników (27.11) | atak + góra (jak w oryginale; Z też działa; pad: X + góra) przy zwykłym przeciwniku albo beczce podnosi, atak z ciężarem nad głową rzuca (przytrzymany dalej). Z przy zwykłym przeciwniku (nie boss; do 64 px przed kapitanem, na tej samej wysokości, nie w trakcie zamachu/skoku) podnosi go jak beczkę (animacje lifted / lifted2), kapitan chodzi wolno; ponowne Z – rzut jak beczką (200+3·moc, 600+2·moc px/s, przytrzymanie do 3 s = dalej); rzucony leci (animacja thrown) i ginie po uderzeniu w ziemię, ścianę albo innego przeciwnika – ten też ginie; trafiony kapitan upuszcza przeciwnika, który walczy dalej | sprawdzone skryptem (żołnierz na poz. 1: podniesiony, rzucony ~150 px, zginął). Śmierć przy lądowaniu i zabicie trafionego to założenie – nie znalazłem tego w kodzie oryginału, do porównania |
| Animowane dekoracje (27.11) | świeczki, pochodnie itp. (AniCandy / AniCycle) zmieniają klatkę co 100 ms jak w CLAW.EXE (było 150 ms) – na nagraniu MK oryginał ~104 ms, ReClaw 150 ms | skompilowane |
| Szybszy chód i wspinaczka (27.11) | według nagrań MK, nie kodu: chód 261 px/s, po rozpędzie 321, z kocimiętką 338, wspinaczka 241 (z kocimiętką 301); skok bez zmian (w powietrzu 225) – porównać z oryginałem obok siebie | sprawdzone skryptem (261 → 321 po 1,5 s) |
| Zdrowie bossów (27.11) | w CLAW.EXE każdy boss ma 100 HP (×1.0, w trybie łatwym inaczej), a miecz zadaje 5 – czyli ok. 20 ciosów mieczem. ReClaw bije mieczem za 10, więc Raux, Katherine, Wolvington, Marrow i Red Tail mają teraz 200 (było 100/150) – ta sama liczba ciosów. Gabriel, Aquatis i Omar bez zmian (inne zasady walki) | sprawdzić, czy długość walki zgadza się z oryginałem |
| Tło w walce z Red Tailem (28.11) | tło poziomu 13 (dżungla, morze) rysuje się także dalej niż połowa poziomu – wcześniej od x≈30000 niebo było czarne, bo płaszczyzna tła przesuwająca się z prędkością 50% była ucinana na swojej szerokości zamiast na podwojonej | sprawdzone zrzutem (37100,2000); obejrzeć na PC/Switchu |
| Omar w treningu z bossami (28.11) | walka z Omarem (poziom 14) odblokowana w trybie treningu bossów i w „Paruj i kontruj” | sprawdzić start przy Omarze i koniec walki |
| Wolvington jak w oryginale (28.11) | decyzje z CLAW.EXE: Claw 56–200 px – w połowie przypadków cios (podbródkowy, a gdy Claw kuca – niski miecz), inaczej cofa się 75–125 px; Claw 200–500 px – 10% seria 1–4 pocisków na zmianę nisko/wysoko (300 px/s), 20% rozbieg (480 px/s) i podbródkowy, reszta – idzie (60 px/s); 0,5 s po trafieniu kontruje rozbiegiem. Obrażenia jak w oryginale: 5 za cios i pocisk (wcześniej 35 i 10); blokuje połowę ciosów mieczem jak dotąd | sprawdzone skryptem (wszystkie ruchy, kucanie → niski miecz); zagrać i porównać z oryginałem |
| Aquatis jak w oryginale (28.11) | według CLAW.EXE: po sekundzie bezczynności (z przytykiem) uderza górnym (strike1) albo dolnym (strike2) ciosem, 10 obrażeń; ciosy Pazura go nie ranią, tylko odrzucają (co 0,5 s), po ciosie zwykle oddaje, a po trzecim kuli się na 4 s; dynamit w szczelinę: trzęsienie 4 s, Aquatis kuli się, po nim spada następny sopel; sopel: −20 i 4 s szału (ciosy góra/dół na zmianę) – w szale szczelina nie działa; 5 sopli = koniec. Brak jeszcze trzęsienia ekranu | sprawdzone skryptem (odrzut, kulenie się, szał, 5 sopli); zagrać |
| Red Tail jak w oryginale (28.11) | cała walka według CLAW.EXE: skacze między kłodą (góra) a plażą (dół); z innego poziomu rzuca 3 noże (5 pkt) albo z kłody zsyła wiatr (5 s, spycha Pazura na plaży w lewo, 300 px/s, na kolce przy ścianie – 20 pkt); na tym samym poziomie strzela z pistoletu sygnałowego (15 pkt, 500 px/s), robi wślizg (tylko na plaży) albo kopie (10 pkt); przy co trzecim trafieniu odskakuje na drugi poziom; pociski/magię/wybuchy blokuje. Kolce przy ścianie są teraz widoczne. Brak jeszcze latających liści/piasku przy wietrze | sprawdzone skryptem (skoki, noże, pistolet, wślizg, kopniak, wiatr, kolce – Pazur ginie zepchnięty na kolce); zagrać i porównać |
| Gabriel jak w oryginale (28.11) | według CLAW.EXE: po każdej animacji spoczynku jeden z trzech ruchów po równo – 3 z 4 bomb (lądują 475/350/225/100 px przed nim, rzut pod 45°), przywołanie pirata (najwyżej 2 naraz, pirat po animacji) albo rozkaz dla armaty; ciosy go nie ranią – blokuje z docinkiem; armata podniesiona przyciskiem trafia go za 20 (5 trafień zamiast 4), a on odpowiada bombami | sprawdzone skryptem (wszystkie ruchy, 4 trafienia armatą 100→20); zagrać do końca |
| Marrow i papuga jak w oryginale (28.11) | dotąd papuga i zapadnie nic nie robiły. Teraz według CLAW.EXE: papuga od początku walki lata pod sufitem (300 px/s) i z dalszego końca nurkuje na Pazura (800 px/s, 10 pkt); trafiona wraca na grzędę. Co ćwierć zdrowia Marrow gwiżdże (nietykalny, do 5 s) – jeśli papuga siedzi na grzędzie, przylatuje, przenosi go na drugą półkę (z dala od Pazura), a most nad kolcami rozsuwa się; zamyka się, gdy papuga, trafiona, wróci na grzędę | sprawdzone skryptem (lot, nurkowanie, gwizd, przeniesienie, rozsunięty most – Pazur spada na kolce); zagrać i porównać, zwłaszcza kiedy most się zamyka |
| Lasery na poziomie 11 (28.11) | 6 laserów (wcześniej w ogóle niewczytywane): według CLAW.EXE czekają 1,5 s (kryształy widać), potem jeden błysk (25 klatek po 50 ms), który rani w pasie 40×200 px | sprawdzone zrzutem przy 5795,1311 (błysk i przerwa); sprawdzić obrażenia i czy da się przebiec |
| Trzęsienie ekranu (28.11) | jak w CLAW.EXE: obraz drga do 6 px w każdą stronę, nowe przesunięcie co 0–35 ms. Poziom 12: 30 stref Shake (wcześniej pomijane) – wejście Pazura trzęsie 5 s z dźwiękiem trzęsienia, potem 7 s przerwy. Aquatis: dynamit w szczelinę trzęsie 4 s | sprawdzone logiem (strefa przy 5986,3091, przesunięcia kamery); obejrzeć, czy nie za mocno na dużym ekranie |
| Dekoracje bez Z, glut na poziomie 2 (28.11) | jak w CLAW.EXE: przednie dekoracje (FrontCandy/FrontAniCandy) bez podanego Z dostają 5100 (wcześniej 0, czyli za Pazurem), tylne (BehindCandy/BehindAniCandy) 990, GooCoverup 8010 – krawędź glutu na poziomie 2 (np. przy 2208,5210) zakrywa nogi Pazura; bez ostrzeżeń „Unknown logic” dla GooCoverup i AniCycle | zrzut z poziomu 2 (Pazur w glucie); obejrzeć przednie dekoracje na innych poziomach |
| Dźwignia przy armacie Gabriela (28.11) | CannonSwitch (wcześniej nieruchomy obrazek): na rozkaz strzału Gabriela dźwignia przechyla się raz (switch, 400 ms) z kliknięciem SWITCH1 | sprawdzone logiem (dźwignia znaleziona przy strzale); obejrzeć i posłuchać |
| JumpSwitch, CursePowerup i Sign (28.11) | pomijane – zgodnie z CLAW.EXE: JumpSwitch nic nie robi (same puste metody), CursePowerup istnieje tylko w grze wieloosobowej z klątwami, a logiki Sign w ogóle nie ma (3 strzałki w dół na poziomie 12 – w oryginale niewidoczne; wcześniej ReClaw je rysował z ostrzeżeniem) | nic do testowania |
| Wiatr Red Taila – lecące śmieci (28.11) | jak w CLAW.EXE (WindDebris): póki wieje, co 100 ms z prawej strony plaży (x 37800–37900) wylatuje źdźbło trawy, liść albo piasek, w lewo z prędkością 300–600 px/s i lekko w górę lub w dół, część przed Pazurem, część za nim; znika za x 36500 albo po 10 s | zrzut z poziomu 13 (trawa i liście w powietrzu, 50 sztuk na jeden podmuch); obejrzeć w ruchu |
| Spadające sople na poziomie 12 (28.11) | 170 sopli (logika Stalactite z CLAW.EXE): spada, gdy Pazur wejdzie w jego obszar (UserRect1 z mapy), 2000 px/s², rani za 5 i kruszy się; po śmierci Pazura wraca na miejsce. Wcześniej były tylko nieruchome obrazki | sprawdzone skryptem (spadł na Pazura); przejść poziom 12 |
| Raux – odskok i podejście (27.11) | jak w CLAW.EXE (0x481a60): kapitan bliżej niż 100 px – w połowie przypadków Raux cofa się 75–125 px (dalej twarzą do kapitana), 100–150 px – co szósty raz podchodzi 40–80 px, 150–300 px – przeskok nad kapitanem; kroki 150 px/s, tylko po podłodze w jego zasięgu | sprawdzone skryptem (odskok przy kapitanie, przeskok) |
| Zegar gry (27.11) | gra szła 4% wolniej niż czas rzeczywisty (krok 16 ms zamiast 16,67 ms, ułamek ucinany) – teraz równo; dotyczy wszystkich platform | sprawdzone na PC (10 s gry = 10 s zegara) |
| Własne poziomy (27.11) | wszystkie 16 od MK się wczytują (wcześniej 6 nie startowało lub wysypywało grę); czy da się je przejść; windy w TRC3 stoją (brak ścieżki w pliku) | wczytanie sprawdzone skryptem |
| Zapis stanu (ZL / ZR / trzymane R) | zapis, wczytanie, cofanie; znajdźki, skrzynie, wrogowie, winda, lina, śmierć, walka z bossem (Marrow), płynność cofania | nieprzetestowane |
| Plansza Opcje → Sterowanie | czy się otwiera, czy grafika dobrze leży, powrót B | nieprzetestowane |
| Ekran ładowania | polska plansza 4:3 z REZ na panoramicznym tle, przejście na brzegach | nieprzetestowane |
| Beczki z prochem (X / Z) | podniesienie, noszenie, rzut krótki i daleki, wybuch o ziemię / ścianę / wroga, odłożenie po trafieniu; animacje (po `anim_report.txt`) | nieprzetestowane |
| Twórcy (sam tekst) | wygląd planszy | nieprzetestowane |
| Wyścig – podzielony ekran | Multiplayer → Podzielony ekran: ekran wyboru kontrolerów, wybór poziomu, odliczanie, dwie połówki ekranu, gracz 2 na drugim padzie, odrodzenie na własnym punkcie kontrolnym, meta i ekran wyników, płynność | nieprzetestowane |
| Wyścig – przez sieć | Multiplayer → Przez sieć: Utwórz / Dołącz (dwie konsole lub dwa Ryujinxy), znalezienie wyścigu, odliczanie, duch rywala, meta, wyjście rywala / zerwanie połączenia | nieprzetestowane (sam moduł UDP sprawdzony na Linuksie) |
| Pauza (+ / Start / Esc) | otwiera menu: Wróć do gry, Zakończ życie, Opcje, Pomoc, Zakończ grę; w wyścigu przez sieć świat się nie zatrzymuje | nieprzetestowane |
| Mapa poziomu (− / Select / Tab) | półprzezroczysta mapa wokół gracza: podłoże, drabiny, kolce, skarby (złote), koniec poziomu (zielony), gracz (miga); w wyścigu drugi gracz / duch na niebiesko; czy skala 1:8 jest dobra | nieprzetestowane |
| Kod Konami (w trakcie gry) | na planszy: góra, góra, dół, dół, lewo, prawo, lewo, prawo, B, A (PlayStation: ✕, ○; klawiatura: strzałki, Spacja, Alt) → napis „Nie dość, że pirat to jeszcze oszust”; od tej chwili w pauzie pozycja „Kody”, zapamiętana aż do usunięcia zapisów (Wyzeruj postępy / brak SAVES.XML) | nieprzetestowane |
| Ekran „Kody” | nieśmiertelność, nieskończona amunicja, skok bez ograniczeń (wł./wył.), pełne zdrowie, pełna amunicja, dodatkowe życie | nieprzetestowane |
| Napisy | Opcje → Wyświetlanie → „Napisy: wł./wył.” (menu główne i pauza); napisy SRT w filmach i przy dźwiękach (dialogi); wygląd i wielkość tekstu | nieprzetestowane (parser i rysowanie sprawdzone osobno na PC) |
| Języki danych gry | `data/pl/` i `data/en/` z CLAW.REZ: pierwszy start po angielsku, Opcje → Język (lista, komunikat o ponownym uruchomieniu), po restarcie wybrany język (także napisy i teksty z `lang/<kod>/`), `<Language>` w `save/config.xml`; z jednym folderem „Język” wyszarzony; CLAW.REZ prosto w `data/` dalej działa; nowe rozmieszczenie Opcji (8 pozycji co 32 px) mieści się na tle | nieprzetestowane |
| Tryby eksperymentalne (alfa) | „Tryb wieloosobowy”, „Wczytaj poziom” i „Skrzyżuj szpady ponownie” ukryte, menu główne i Jeden gracz bez dziur między pozycjami; ZL/ZR i trzymane R nic nie robią; po włączeniu (Kody → „Tryby eksperymentalne: wł.” albo `experimental=1` w `save/port_settings.txt`) i powrocie do menu wszystko wraca | nieprzetestowane |
| Nowy układ folderów | `sd:/switch/reclaw/`: CLAW.REZ / CLAW.FEC w `data/`; po starcie powstają `movies/`, `custom/`, `lang/pl|en/…`, `save/` (config.xml, SAVES.XML, port_settings.txt, reclaw.log); napisy z `lang/pl/subtitles/movies|dialogue/`, brakujące z `lang/en/`; teksty menu z `lang/pl/menu/strings.txt`; stary folder `/switch/claw/` nie jest już czytany | nieprzetestowane |
| „Skrzyżuj szpady ponownie” (trening) | Jeden gracz → „Skrzyżuj szpady ponownie” (w miejscu „Wyślij wyniki”): lista bossów, do których się dotarło (nazwa + poziom + rekord); start od razu przy bossie (scenka wejścia), śmierć = nowa próba przy bossie, życia się nie kończą; po wygranej czas, rekord, liczba prób i wybór: walcz ponownie / inny boss / menu; czy start przy bossie nie wpada w ścianę lub przepaść (poziomy 2, 4, 6, 8, 10, 12, 13) | nieprzetestowane |
| „Paruj i kontruj” (Boss Rush) | ostatnia pozycja listy, otwarta po ukończeniu poziomu 14 (albo Kody → „Odblokuj wszystkich bossów”); bossowie po kolei, zdrowie przechodzi dalej (+20 i trochę amunicji między walkami), śmierć kończy próbę; stoper tylko w trakcie walki (nie liczy ładowania, scenki wejścia i pauzy); podsumowanie czasów i suma, rekord; od 28.11 z Omarem (14), czyli 8 bossów | nieprzetestowane |
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
