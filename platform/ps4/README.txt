Captain Claw - PS4 (homebrew, GoldHEN)

No game data is included. Copy CLAW.REZ (and CLAW.FEC for the movies) from
your own copy of Captain Claw (1997) to /data/claw/ on the PS4 (FTP or USB).

Building (Ubuntu 24.04):
  sudo apt install -y pacman-package-manager makepkg libarchive-tools cmake
  add to /etc/pacman.conf:
      [pacbrew]
      SigLevel = Optional TrustAll
      Server = https://pacman.mydedibox.fr/pacbrew/packages/
  sudo pacman -Sy ps4-openorbis ps4-openorbis-portlibs
  put libc.prx and libSceFios2.prx into platform/ps4/sce_module/
  source /opt/pacbrew/ps4/openorbis/ps4vars.sh
  openorbis-cmake -S platform/ps4 -B build-ps4 && cmake --build build-ps4 -j
