# Napisy (SRT)

Napisy pojawiają się na dole ekranu, jak w filmach: biały tekst z czarną
obwódką, długie wiersze są zawijane. Włącza się je w **Opcje → Wyświetlanie →
Napisy: wł./wył.** (w menu głównym i w pauzie). Ustawienie zapisuje się w
`port_settings.txt` w folderze `save/`. Tekst po wpisaniu kodu jest
pokazywany zawsze, niezależnie od tej opcji.

## Pliki

W folderze ReClaw, w `lang/<język>/subtitles/` (np.
`sd:/switch/reclaw/lang/pl/subtitles/`):

| Co | Plik |
|---|---|
| film z CLAW.FEC | `movies/movie_00.srt` … `movies/movie_09.srt` (kolejność w CLAW.FEC, jak na stronie „Odtwórz film”) |
| kwestia mówiona (Pazur, przeciwnicy) | `dialogue/<ścieżka dźwięku w REZ>.srt`, np. `/LEVEL2/SOUNDS/RAUX/LE000001.WAV` → `dialogue/level2/sounds/raux/le000001.srt` |

- `lang/pl/` – polskie napisy, `lang/en/` – angielskie. Gdy pliku nie ma
  w bieżącym języku, gra bierze wersję z `lang/en/`.
- Nazwy małymi literami.
- Czas w napisach do dźwięku liczy się od początku dźwięku.
- Kodowanie UTF-8 (z BOM lub bez), znaczniki `<i>` itp. są pomijane.

Szablony robi `scripts/subtitle_templates.py` (uruchomiony obok CLAW.REZ i
CLAW.FEC): tworzy `lang/pl/subtitles/` z pustymi plikami o właściwych nazwach
i długościach oraz `LISTA.txt`. Pusty szablon niczego nie wyświetla, więc
można skopiować cały folder `lang` do folderu ReClaw i uzupełniać po kolei.

## Teksty menu (`lang/<język>/menu/strings.txt`)

Napisy w menu i na ekranach dodanych przez port (kody, wyścig, tryby bossów,
zapis stanu…) można zmienić bez przebudowy gry. Wzory robi
`scripts/make_lang_strings.py` → `Build_Release/lang/{pl,en,xx}/menu/strings.txt`:

```
# angielski tekst = tłumaczenie (zmieniamy tylko prawą stronę)
Cheats = Kody
"Invincible: " = "Nieśmiertelność: "
```

- Cudzysłów zachowuje spacje na końcach, `\n` to nowa linia, `#` – komentarz.
- Nowy język, np. niemiecki: skopiować `xx/` do `lang/de/`, uzupełnić
  i ustawić `<Language>de</Language>` w `save/config.xml`. Brakujące
  linijki zostają po angielsku; grafiki z oryginalnej gry (z CLAW.REZ)
  się nie zmieniają.

Przykład:

```
1
00:00:00,000 --> 00:00:02,400
Ahoj, szczury lądowe!

2
00:00:02,600 --> 00:00:05,000
Nikt nie ukradnie skarbu Kapitana Pazura.
```
