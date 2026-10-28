#!/bin/bash
# Creates the ReClaw folder layout in <dir> for release packages (the game
# creates the same folders on its first start):
#   data/  movies/  custom/  lang/{pl,en}/{subtitles/movies,subtitles/dialogue,menu}/  save/
# Usage: scripts/make_reclaw_tree.sh <dir>
set -e
DIR="$1"
[ -n "$DIR" ] || { echo "usage: $0 <dir>"; exit 1; }
for d in data movies custom save; do mkdir -p "$DIR/$d"; done
for l in pl en; do
  mkdir -p "$DIR/lang/$l/subtitles/movies" "$DIR/lang/$l/subtitles/dialogue" "$DIR/lang/$l/menu"
done
cat > "$DIR/data/PUT_CLAW.REZ_HERE.txt" <<'EOF'
Copy CLAW.REZ (and CLAW.FEC with the movies, if you have it) from your own
copy of Claw (1997) into this folder. An unpacked "Assets" folder works too.
Skopiuj tu CLAW.REZ (i CLAW.FEC z filmami) z własnej kopii gry.
EOF
cat > "$DIR/movies/README.txt" <<'EOF'
Remastered movies (not supported yet). / Zremasterowane filmy (jeszcze nieobsługiwane).
EOF
cat > "$DIR/custom/README.txt" <<'EOF'
Custom levels (.WWD). / Własne poziomy (.WWD).
EOF
cat > "$DIR/lang/README.txt" <<'EOF'
lang/<code>/subtitles/movies/movie_01.srt        subtitles of the movies
lang/<code>/subtitles/dialogue/<sound path>.srt  what Claw and the enemies say
lang/<code>/menu/strings.txt                     texts of the port's menus
                                                 ("English text = translation")
Templates: scripts/subtitle_templates.py, scripts/make_lang_strings.py.
Missing files fall back to English (lang/en/).
EOF
cat > "$DIR/save/README.txt" <<'EOF'
config.xml, SAVES.XML, port_settings.txt and reclaw.log are written here.
Tu trafiają ustawienia, zapisy gry i dziennik błędów.
EOF
