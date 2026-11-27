ReClaw - Sega Dreamcast (EXPERIMENTAL)

EN
An experiment: the Dreamcast has 16 MB of RAM and the engine was designed for
much more, so the game may not get far. Test in an emulator first (Flycast,
Redream).
Make the disc image on your computer (Python 3, nothing to install):
  python reclaw_disc.py --package reclaw-dreamcast.zip --rez CLAW.REZ --fec CLAW.FEC --lang en
CLAW.REZ / CLAW.FEC come from YOUR copy of Claw (1997); they are only copied
into the image. Open reclaw-gdi/disc.gdi in Flycast or Redream (or put the
folder on a GDEMU). Saves and the log are kept in the RAM disk only (lost when
switched off). reclaw.elf is for dcload / a CD-R made with mkdcdisc.

PL
Eksperyment: Dreamcast ma 16 MB RAM, a silnik był pisany pod dużo więcej
pamięci, więc gra może nie zajść daleko. Najpierw testuj w emulatorze
(Flycast, Redream).
Obraz płyty robisz na swoim komputerze (Python 3, nic nie trzeba instalować):
  python reclaw_disc.py --package reclaw-dreamcast.zip --rez CLAW.REZ --fec CLAW.FEC --lang pl
CLAW.REZ / CLAW.FEC z TWOJEJ kopii Claw (1997) są tylko kopiowane do obrazu.
Otwórz reclaw-gdi/disc.gdi we Flycaście albo Redream (albo wgraj folder na
GDEMU). Zapisy i log są tylko w RAM-dysku (znikają po wyłączeniu). reclaw.elf
jest dla dcload / płyty CD-R zrobionej narzędziem mkdcdisc.
