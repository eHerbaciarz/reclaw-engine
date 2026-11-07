#!/usr/bin/env python3
"""The game's spoken lines as plain sound files (for listening, writing
subtitles by hand, translating or re-recording).

Runs on your own PC, next to your own CLAW.REZ / CLAW.FEC; the files it
writes are the game's own sounds - keep them for yourself. Copy it together
with subtitle_templates.py and auto_subtitles.py (same folder). Standard
library only; the movies' sound needs ffmpeg (https://ffmpeg.org) in PATH.

    python extract_dialogue_audio.py                  # CLAW.REZ / CLAW.FEC here
    python extract_dialogue_audio.py --rez D:/claw/CLAW.REZ --out D:/dialogi
    python extract_dialogue_audio.py --all            # every sound, effects too
    python extract_dialogue_audio.py --only movies    # or --only dialogue

What it makes (in --out, default: next to the script):

  audio_pl/movies/movie_00.wav ...          the sound of each movie in CLAW.FEC
                                            (numbers as the game's movie_XX)
  audio_pl/dialogue/level1/sounds/....wav   sounds that are probably speech
                                            (path in the REZ kept: the matching
                                            subtitle is lang/pl/subtitles/
                                            dialogue/<same path>.srt)
  audio_pl/LISTA.tsv                        every file with its length and the
                                            subtitle file it belongs to

Speech or an effect? Without speech recognition only the length (--min) and
the path tell: STAGING/..., /CLAW/SOUNDS/, voice... are kept, GUNSHOT,
SPLASH, COIN... left out. For a sure split (and draft subtitle text) use
auto_subtitles.py.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
try:
    from subtitle_templates import rez_index, smk_movies, wav_seconds
except ImportError:
    sys.exit("subtitle_templates.py must be in the same folder as this script")
try:
    from auto_subtitles import DIALOGUE_HINTS, EFFECT_HINTS
except ImportError:
    sys.exit("auto_subtitles.py must be in the same folder as this script")


def probably_speech(path):
    p = path.lower()
    if DIALOGUE_HINTS.search(p):
        return True
    return not EFFECT_HINTS.search(os.path.basename(p))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rez", default=os.path.join(HERE, "CLAW.REZ"))
    ap.add_argument("--fec", default=None, help="default: CLAW.FEC next to the REZ")
    ap.add_argument("--lang", default="pl", help="only names the output folder (audio_<lang>)")
    ap.add_argument("--only", choices=["movies", "dialogue"])
    ap.add_argument("--min", type=float, default=0.6, help="shortest sound in seconds (0.6)")
    ap.add_argument("--all", action="store_true", help="every sound, effects too")
    ap.add_argument("--out", default=HERE)
    args = ap.parse_args()

    fec = args.fec or os.path.join(os.path.dirname(os.path.abspath(args.rez)), "CLAW.FEC")
    if args.fec is None and not os.path.exists(fec):
        alt = os.path.join(os.path.dirname(os.path.abspath(args.rez)), "claw.fec")
        if os.path.exists(alt):
            fec = alt
    root = os.path.join(args.out, "audio_" + args.lang)
    rows = []

    # ------------------------------------------------------------ movies
    if args.only in (None, "movies"):
        if not os.path.exists(fec):
            print("No CLAW.FEC (%s) - movies skipped" % fec)
        elif not shutil.which("ffmpeg"):
            print("ffmpeg not found in PATH - movies skipped (https://ffmpeg.org)")
        else:
            movies = smk_movies(fec, with_offsets=True)
            print("Movies in CLAW.FEC: %d" % len(movies))
            os.makedirs(os.path.join(root, "movies"), exist_ok=True)
            work = tempfile.mkdtemp(prefix="reclaw_audio_")
            try:
                with open(fec, "rb") as f:
                    for i, (seconds, size, offset) in enumerate(movies):
                        name = "movie_%02d" % i
                        smk = os.path.join(work, name + ".smk")
                        f.seek(offset)
                        with open(smk, "wb") as o:
                            o.write(f.read(size))
                        wav = os.path.join(root, "movies", name + ".wav")
                        r = subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", smk, "-vn",
                                            "-acodec", "pcm_s16le", wav], capture_output=True, text=True)
                        if r.returncode != 0 or not os.path.exists(wav):
                            print("  %s: no sound (%s)" % (name, (r.stderr or "").strip()[:120]))
                            continue
                        print("  %s.wav (%.0f s)" % (name, seconds))
                        rows.append(("movies/%s.wav" % name, seconds, "movies/%s.srt" % name))
            finally:
                shutil.rmtree(work, ignore_errors=True)

    # ---------------------------------------------------------- dialogue
    if args.only in (None, "dialogue"):
        if not os.path.exists(args.rez):
            sys.exit("CLAW.REZ not found: %s (use --rez)" % args.rez)
        index = rez_index(args.rez)
        kept = skipped = 0
        with open(args.rez, "rb") as f:
            for path in sorted(index):
                if not path.endswith(".wav"):
                    continue
                off, size = index[path]
                f.seek(off)
                data = f.read(size)
                seconds = wav_seconds(data)
                if seconds is None or seconds < args.min:
                    continue
                if not args.all and not probably_speech(path):
                    skipped += 1
                    continue
                rel = "dialogue" + path   # path starts with '/'
                out = os.path.join(root, *rel.split("/"))
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with open(out, "wb") as o:
                    o.write(data)
                kept += 1
                rows.append((rel, seconds, rel[:-4] + ".srt"))
        print("Sounds longer than %.1f s: %d written%s" % (
            args.min, kept, "" if args.all else ", %d effects left out (--all: them too)" % skipped))

    if rows:
        os.makedirs(root, exist_ok=True)
        with open(os.path.join(root, "LISTA.tsv"), "w", encoding="utf-8") as f:
            f.write("plik\tsekundy\tnapisy (w lang/%s/subtitles/)\n" % args.lang)
            for rel, seconds, srt in rows:
                f.write("%s\t%.2f\t%s\n" % (rel, seconds, srt))
        print("Done: %s" % root)


if __name__ == "__main__":
    main()
