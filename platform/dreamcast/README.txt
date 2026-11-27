ReClaw - Sega Dreamcast (EXPERIMENTAL)

EN
An experiment: the Dreamcast has 16 MB of RAM and the engine was designed for
much more, so the game is not expected to start a level (maybe not even the
menu). Use an emulator (Flycast, Redream) first.
The disc/ folder holds the files that go on the disc. Add data/en/ or data/pl/
with CLAW.REZ (and CLAW.FEC) from YOUR copy of Claw (1997), then make a
disc image with 1ST_READ.BIN as the boot file, e.g.
  mkdcdisc -e reclaw.elf -d disc -o reclaw.cdi
Saves and the log are kept in the RAM disk only (lost when switched off).

PL
Eksperyment: Dreamcast ma 16 MB RAM, a silnik był pisany pod dużo więcej
pamięci, więc gra najpewniej nie dojdzie do planszy (może nawet do menu).
Najpierw testuj w emulatorze (Flycast, Redream).
Folder disc/ zawiera pliki na płytę. Dodaj data/pl/ (albo data/en/) z CLAW.REZ
(i CLAW.FEC) z TWOJEJ kopii Claw (1997) i zrób obraz płyty, np.
  mkdcdisc -e reclaw.elf -d disc -o reclaw.cdi
Zapisy i log są tylko w RAM-dysku (znikają po wyłączeniu).
