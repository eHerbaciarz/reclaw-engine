# Do zrobienia (na później)

Pomysły i poprawki odłożone na później – do decyzji MK.

## Tryby bossów

- **Mignięcie menu między bossami.** W „Paruj i kontruj” (następny boss)
  i w treningu („Walcz ponownie”, „Wybierz innego bossa”) zmiana poziomu idzie
  przez menu główne (`EventData_Enter_Menu` → menu → `BossModes::OnMenu()` →
  ładowanie poziomu), bo bezpośrednie przejście poziom → poziom nie jest
  w silniku sprawdzone (`VResetLevel` przetwarza zdarzenia w środku obsługi
  zdarzenia, `UnloadLevel` ma `assert`, że nie wolno go wołać w trakcie).
  Skutek: na moment widać menu główne (i może ruszyć jego muzyka), zanim
  pojawi się ekran ładowania.
  Propozycja: zostawić tę samą drogę, ale gdy czeka następny boss
  (`g_PendingLevel` / `g_PendingList`), nie budować i nie rysować menu
  ani nie puszczać jego muzyki – na tę klatkę czarny ekran, potem od razu
  ekran ładowania. Kilka linijek w `BaseGameLogic::VChangeState(GameState_Menu)`
  i `HumanView::EnterMenuDelegate`.

## Słabsze konsole (PS Vita, 3DS)

- **Zremasterowane filmy** – na Vicie i 3DS niedostępne (zostają filmy
  z CLAW.FEC). Gotowe: `Platform::SupportsRemasteredMovies()` zwraca tam
  `false`; trzeba to uszanować, gdy powstanie odtwarzanie z `movies/`.
- **Zapis stanu i cofanie** – do sprawdzenia na Vicie i New 3DS; jeśli
  za wolne, wyłączyć w `Platform::SupportsSaveStates()` (w alfie wyłączone –
  działa tylko z `experimental=1`).
- Paczki `reclaw.vpk` / `reclaw.3dsx` do testów – zbudować dopiero na
  sygnał MK (po kolejnych poprawkach).

## Języki

- Zmiana języka działa po ponownym uruchomieniu gry. Na później:
  na Switchu automatyczny restart przez Homebrew Menu (`envSetNextLoad`),
  ewentualnie zmiana od razu w menu (przeładowanie CLAW.REZ – ryzykowne).

## Rozgrywka

- **Rzucanie beczką** działa, ale fizyka lotu nie jest poprawna (zgłoszone
  przez MK 30.10) – do porównania z oryginałem (tor, siła, odbicia).
  Animacje noszenia beczki są tymczasowe.

## Wcześniej odłożone

- Bossowie: Le Rauxe (2), Katherine (4), Wolvington (6) – własne zachowanie;
  Aquatis (12), Red Tail (13) – pełna walka; Omar (14) – brak; kameleony.
  `boss_survey.txt` jest (28.10); przydadzą się filmy z walk.
- Poziom 14: Omar, kameleony, tygrysy, lawa (dane z `level_survey.txt`). Poziom 10: checkpointy poprawione 28.10.
- Wersja przeglądarkowa (Emscripten) – zbudować i sprawdzić przed PR-ami do claw-web.
- Ikony portów: postać z ikony zastąpić własną grafiką (znak towarowy).
