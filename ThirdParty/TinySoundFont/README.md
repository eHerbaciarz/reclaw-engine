Vendored for the native (Linux / Nintendo Switch) music synthesizer:

- `tsf.h`, `tml.h` — TinySoundFont by Bernhard Schelling, MIT (see LICENSE-TinySoundFont.txt).
  Upstream: https://github.com/schellingb/TinySoundFont @ 853a0a1
- `stb_vorbis.c` — stb_vorbis by Sean Barrett, public domain / MIT (license at end of file).
  Upstream: https://github.com/nothings/stb @ 003501f
  Needed so TinySoundFont can read the Ogg-compressed SF3 soundfont shared with the web build.
