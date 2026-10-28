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
  za wolne, wyłączyć w `Platform::SupportsSaveStates()` (teraz wszędzie wł.).
- Paczki `reclaw.vpk` / `reclaw.3dsx` do testów – zbudować dopiero na
  sygnał MK (po kolejnych poprawkach).

## Wcześniej odłożone

- Bossowie: Le Rauxe (2), Katherine (4), Wolvington (6) – własne zachowanie;
  Aquatis (12), Red Tail (13) – pełna walka; Omar (14) – brak; kameleony.
  Czeka na `boss_survey.txt` i filmy z walk.
- Poziom 14, etapy 2–4; poziom 10 – zły punkt startu (czeka na `level_survey.txt`).
- Animacje noszenia beczki (czeka na `anim_report.txt`).
- Wersja przeglądarkowa (Emscripten) – zbudować i sprawdzić przed PR-ami do claw-web.
- Ikony portów: postać z ikony zastąpić własną grafiką (znak towarowy).
