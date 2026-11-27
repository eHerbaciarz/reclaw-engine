# Fizyka i zachowanie oryginału (CLAW.EXE)

Wartości odczytane z kodu oryginalnego CLAW.EXE (MK dostarczył plik do analizy;
plik nie jest w repozytorium). Adresy dotyczą tej wersji pliku. Jednostki:
piksele, milisekundy.

## Ruch kapitana (wdrożone, `GlobalOptions::originalMovement`, domyślnie włączone)

| Co | Oryginał | Stary OpenClaw | Gdzie w CLAW.EXE |
|---|---|---|---|
| Chód | 225 px/s | 337 px/s (4,5 m/s) | 0x417d.. (stan 0x18), `0xe1` |
| Rozpęd | po 1,5 s biegu w jedną stronę po ziemi: 285 px/s (animacja ×0,9) | brak | licznik `[0x45] = 0x5dc` |
| Kocimiętka | 300 px/s (animacja ×0,8) | 5,5 m/s | `DAT_00532d34 == 0x26bc` |
| W powietrzu | zawsze 225 px/s | jak na ziemi | 0x41cb8f |
| Grawitacja | 2250 px/s² | 9,8 × 4 × 75 = 2940 px/s² | `0x40a19400` (double 2250) |
| Najszybszy spadek | 1000 px/s | 1050 px/s | 0x416ee0, `FUN_00421de0(1000.0)` |
| Skok | najmniej 45 px; trzymany do 250 ms rośnie o 0,58 px/ms (145 px) | stałe 8,8 m/s do 155 px | 0x41ca10 |
| Skok po rozpędzie | 0,68 px/ms (170 px) | – | 0x41cb09 |
| Skok z kocimiętką (biała/czerwona myszka) | 0,78 px/ms (195 px), jak kod „Super Jump” | 205 px | 0x41cad3 |
| Kod „Super Jump” | 0,78 px/ms (195 px) | – | `DAT_00535938` |
| Kształt skoku | prędkość liczona co klatkę tak, by przy grawitacji 2250 dojść dokładnie do wierzchołka (`sqrt(2·g·h)`): wolnieje ku górze | stała prędkość, potem ucięcie | 0x422070 |

## Drabiny (wdrożone)

Wspinanie 200 px/s, z kocimiętką 250 px/s (0x41d0..; stary OpenClaw: 375 px/s).

Na szczycie drabiny kapitan staje na niej; „w górę” nic już nie robi (patrzy w górę).
W ReClaw puszczał się dalej w górę, skakał, spadał na drabinę i tak w kółko.

## Kocimiętka a miecz (wdrożone)

Miecz zadaje 5 (w kucki 2), z kocimiętką 10 (w kucki 4) – podwójnie, a nie zabija
jednym ciosem jak w starym OpenClaw (100). W ReClaw: obrażenia ×2.

## Śmierć (wdrożone)

- `CLAW_SPIKEDEATH` – tylko w kafelku śmierci (kolce, woda, smoła, lawa); po animacji traci się życie.
- `CLAW_FALLDEATH` – każda inna śmierć: animacja w pętli, spadek z miejsca, bez podskoku,
  od prędkości 0, grawitacja 2250, życie traci się po 800 px spadku (0x41d670).

## Otwarcie poziomu (wdrożone)

`CFaderMgr`, efekt 0, tryb 1 (0x4936a0, wołany przy starcie poziomu z 0x47ca40):
wałek o średnicy 100 px jedzie od lewej krawędzi do prawej przez 1,5 s, z dźwiękiem
`GAME_FLAGWAVE`. Na wałku zwinięty obraz samego poziomu (ciemniejszy ku brzegom),
za wałkiem czarno. Menu używa trybu 3 (dwa wałki od środka, papier `STATES/ROLLER`).

## Krawędzie (wdrożone)

Kapitan miał okrągłe stopy (koło Box2D) i ześlizgiwał się z każdej krawędzi, gdy
środek minął jej brzeg. Teraz płaska podeszwa ze ściętymi rogami – stoi, dopóki
stopy są na ziemi.

## Do zrobienia (zbadane częściowo)

- **Noszenie i rzut** – noszenie: `CLAW_LIFTWALK`, prędkość ustawiana na ±30 (0x41a160);
  rzut woła metodę rzucanego obiektu (vtable +0xbc, 0x41c5b0). Siła zależna od
  czasu trzymania przycisku – jeszcze nie odczytana.
- **Bossowie, poziom 13–14 (tygrysy, kameleony)** – jeszcze nie analizowane.

## Narzędzie testowe

`RECLAW_AUTOPLAY=skrypt.txt ./openclaw` (tylko komputer) – skrypt wejścia
i zapis ruchu kapitana; opis komend w `OpenClaw/Engine/Util/Autoplay.cpp`.
