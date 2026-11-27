# Dokument projektowy Monolith (Claw_Design_Doc1.doc) a ReClaw

Dokument to projekt z czasu produkcji, nie opis wydanej gry: część rzeczy w CLAW.EXE wyszła inaczej
(np. Omar walczy pierścieniem kul, a nie „200 HP, 20 obrażeń za każdy atak”). Gdzie dokument i kod
CLAW.EXE się różnią, ReClaw idzie za kodem; dokument rozstrzyga tylko tam, gdzie kodu nie znamy.

## Zgodne z ReClaw (potwierdza to, co już zrobione)

| Temat | Dokument | ReClaw |
|---|---|---|
| Podnoszenie / rzut | Ctrl + strzałka w górę albo Z (pad: osobny przycisk „Throw”) | atak + góra, Z, pad X + góra / Y |
| Rzucony przeciwnik | krab pustelnik ginie m.in. „gdy zostanie podniesiony i rzucony” | rzucony przeciwnik ginie po uderzeniu |
| Bossowie nie do podniesienia | – (Le Rauxe ma w projekcie animację „podniesiony”, ale CLAW.EXE kasuje mu tę flagę) | bossów nie da się podnieść |
| Balansowanie na krawędzi | „Pre-fall/teeter forward … nie dzieje się na kołkach/windach” | jest; na windach wyłączone (27.11), na kołkach jeszcze nie |
| Przełącznik pierwszego planu | opcja „front layer on/off” | Opcje → Grafika → Pierwszy plan |
| Obszar gry +/− | klawisze + i −, opcja w menu | tylko Switch / Vita („Widok”) |
| Kocimiętka | biegnie szybciej, skacze wyżej, bije mocniej | tak (300 px/s, skok 195, obrażenia ×2) |
| Klejnoty | bossowie poz. 2, 4, 6, 8, 10, 12; Red Tail 2 klejnoty; Omar ostatni | lot klejnotu do celu |

## Liczby z dokumentu do porównania (projekt – sprawdzać z oryginałem)

Obrażenia zadawane przez kapitana (tryb normalny): pięść 5, kopniak 5, **rzut 4**, cios mieczem 5,
cios w kucki 2, cios w skoku 5, pazur na kocimiętce 10, pistolet 8, magiczny pazur 25, dynamit 15,
miecze lodu / ognia / błyskawic 20. W trybie łatwym ciosy wręcz po 20.

Czas kocimiętki: biała 15 s, czerwona 30 s. Amunicja: kula 5, worek 10, „worek śmierci” 25;
dynamit po 5; magia 5 / 10 / 25. Zdrowie: 5 / 10 / 15 / 25.

Bossowie w projekcie (HP / obrażenia): Le Rauxe 100 / 10, Katherine 100 / 10, Wolvington 150 / 10
(rękawica 20), Gabriel 100 / 10, Marrow 150 / 10 (papuga 10), Aquatis 200 / 10, Omar 200 / 20.
ReClaw: Le Rauxe 100, Katherine 150, Wolvington 150, Gabriel 100, Marrow 150, Aquatis 100,
Red Tail 150, Omar 100 (8 trafień po 1/4 – z kodu).

Opis Le Rauxe: blokuje pociski (dostaje część obrażeń), ma atak w skoku, „szarżuje i szybko
odskakuje przed atakami” – pasuje do skoku/odskoku, który w ReClaw jeszcze nie odpala.

Tygrysi strażnik: 20 HP, 10 obrażeń, „bardzo trudny i szybki”. ReClaw: 30 HP (z CLAW.EXE).

Kody: MPSUPERTHROW (daleki rzut przeciwnikami), MPFLUBBER (wysoki skok), MPBOTLESS / MPMIDLESS /
MPTOPLESS (warstwy), MPFPS, MPPOS i inne.
