# Napisy (SRT)

Napisy pojawiają się na dole ekranu, jak w filmach: biały tekst z czarną
obwódką, długie wiersze są zawijane. Włącza się je w **Opcje → Wyświetlanie →
Napisy: wł./wył.** (w menu głównym i w pauzie). Ustawienie zapisuje się w
`port_settings.txt` w folderze z zapisami. Tekst po wpisaniu kodu jest
pokazywany zawsze, niezależnie od tej opcji.

## Pliki

Folder `subtitles` obok CLAW.REZ albo w folderze z zapisami
(np. `sd:/switch/claw/subtitles/`). Można je też dołożyć do ASSETS.ZIP jako
`/SUBTITLES/...`.

| Co | Nazwa pliku |
|---|---|
| film z CLAW.FEC | `movie_00_pl.srt` … `movie_09_pl.srt` (kolejność w CLAW.FEC, jak na stronie „Odtwórz film”) |
| dźwięk / kwestia mówiona | `sounds/<ścieżka dźwięku w REZ>_pl.srt`, np. `/LEVEL2/SOUNDS/RAUX/LE000001.WAV` → `sounds/level2/sounds/raux/le000001_pl.srt` |

- Nazwy małymi literami. `_pl` dla polskich tekstów portu, `_en` dla
  angielskich; plik bez końcówki pasuje do obu.
- Czas w napisach do dźwięku liczy się od początku dźwięku.
- Kodowanie UTF-8 (z BOM lub bez), znaczniki `<i>` itp. są pomijane.

Szablony robi `scripts/subtitle_templates.py` (uruchomiony obok CLAW.REZ i
CLAW.FEC): tworzy folder `subtitles` z pustymi plikami o właściwych nazwach i
długościach oraz `LISTA.txt`. Pusty szablon niczego nie wyświetla, więc można
skopiować cały folder i uzupełniać po kolei.

Przykład:

```
1
00:00:00,000 --> 00:00:02,400
Ahoj, szczury lądowe!

2
00:00:02,600 --> 00:00:05,000
Nikt nie ukradnie skarbu Kapitana Pazura.
```
