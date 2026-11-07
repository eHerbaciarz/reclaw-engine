# Zremasterowane filmy

Filmy z `CLAW.FEC` można podmieniać pojedynczo plikami w folderze `movies/`
ReClaw (np. `sd:/switch/reclaw/movies/`, na Steam Decku `ReClaw/movies/`,
w PortMasterze `ports/reclaw/movies/`).

Kolejność szukania dla filmu nr XX (numeracja jak na stronie „Odtwórz film”,
`00`–`09`):

1. `movies/movie_XX.mp4` – H.264 + AAC (tylko wersje z FFmpeg, tabela niżej),
2. `movies/movie_XX.mpg` – MPEG-1 + MP2 (każda wersja poza Vitą i 3DS),
3. oryginał z `CLAW.FEC`.

Napisy jak dla oryginałów: `lang/<język>/subtitles/movies/movie_XX.srt`.
Obraz zachowuje proporcje filmu (czarne pasy, jeśli ekran ma inne).

## Format .mp4 (H.264 + AAC)

| | |
|---|---|
| Kontener | MP4 z faststart |
| Wideo | H.264 **Main**, level 3.1, 8 bit, yuv420p (inne formaty pikseli są odrzucane) |
| Rozdzielczość | 960×720 (4:3); remaster 16:9 najwyżej 1280×720 |
| Klatki | najwyżej 30 fps |
| Bitrate | CRF ok. 20, najwyżej 4 Mb/s |
| GOP | klatka kluczowa co 30, najwyżej 2 klatki B, najwyżej 3 referencyjne |
| Dźwięk | AAC-LC, stereo, 44,1 kHz, 160 kb/s |

```
ffmpeg -i wejscie.mov -vf "scale=960:720:flags=lanczos,fps=30,setsar=1" \
  -c:v libx264 -profile:v main -level 3.1 -pix_fmt yuv420p -preset slow -crf 20 \
  -maxrate 4M -bufsize 8M -g 30 -bf 2 -refs 3 \
  -c:a aac -b:a 160k -ar 44100 -ac 2 -movflags +faststart movie_01.mp4
```

## Format .mpg (MPEG-1 + MP2)

| | |
|---|---|
| Kontener | MPEG-PS (`-f mpeg`) |
| Wideo | MPEG-1, yuv420p |
| Rozdzielczość | 960×720 (4:3); 16:9 najwyżej 1280×720 |
| Klatki | najwyżej 30 fps |
| Bitrate | ok. 5–6 Mb/s, najwyżej 8 Mb/s |
| GOP | klatka kluczowa co 15, najwyżej 2 klatki B |
| Dźwięk | MP2, stereo, 44,1 kHz, 192–224 kb/s |

```
ffmpeg -i wejscie.mov -vf "scale=960:720:flags=lanczos,fps=30,setsar=1" \
  -c:v mpeg1video -b:v 6M -maxrate 8M -bufsize 2M -g 15 -bf 2 -pix_fmt yuv420p \
  -c:a mp2 -b:a 192k -ar 44100 -ac 2 -f mpeg movie_01.mpg
```

Długość filmu jak w oryginale (co do klatki), żeby pasowały napisy.

## Które wersje co odtwarzają

| Wersja | .mp4 | .mpg | uwagi |
|---|---|---|---|
| Switch | tak | tak | |
| PS4 | tak | tak | |
| Steam Deck | tak | tak | |
| PortMaster (aarch64) | tak | tak | słabsze układy (RK3326) – do sprawdzenia |
| PS3 | tak (eksperymentalnie) | tak | H.264 dekoduje jeden rdzeń PPU – .mpg bywa lżejsze; czasy w reclaw.log („Remastered movie: …”) |
| Wii U | – | tak | |
| Linux / Windows / Android | – (na razie) | tak | FFmpeg do dodania przy budowaniu |
| PS Vita, 3DS | – | – | tylko oryginały z CLAW.FEC |

Gdy konsola nie nadąża, gra opuszcza klatki obrazu (dźwięk gra dalej),
a w `save/reclaw.log` pojawia się ostrzeżenie „Remastered movie played late”.

## Budowanie z obsługą .mp4

`scripts/build_ffmpeg_min.sh <platforma> <prefix>` buduje minimalne FFmpeg
(tylko demuxer MP4 i dekodery H.264 / AAC; LGPL). Potem:
- CMake: `-DRECLAW_FFMPEG=<prefix>` (albo zmienna środowiskowa `RECLAW_FFMPEG`),
- Switch: `make RECLAW_FFMPEG=<prefix>`.
Bez tego wersja odtwarza tylko `.mpg`.
