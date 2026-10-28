#!/usr/bin/env python3
"""Draft subtitles for ReClaw made by speech recognition (faster-whisper).

Runs on your own PC, next to your own CLAW.REZ / CLAW.FEC. Nothing is sent
anywhere: the speech model works offline (it is downloaded once on the first
run). Copy this file together with subtitle_templates.py (same folder).

    pip install faster-whisper
    python auto_subtitles.py                       # Polish, both parts
    python auto_subtitles.py --lang en --rez CLAW_EN.REZ
    python auto_subtitles.py --only movies         # or --only dialogue
    python auto_subtitles.py --model medium        # smaller / faster model

What it makes (next to the script, ready to copy into the ReClaw folder):

  lang/pl/subtitles/movies/movie_00.srt ...     every movie in CLAW.FEC
  lang/pl/subtitles/dialogue/<sound>.srt        sounds recognised as speech
  _do_sprawdzenia/lang/pl/subtitles/dialogue/   uncertain ones (listen, move the
                                                good ones into lang/ by hand)
  raport_pl/raport.html                         every sound with its class,
                                                speech share, confidence, text
                                                and a player - open in a browser
  raport_pl/raport.tsv                          the same as a table

Speech or a sound effect? Each sound longer than --min seconds gets:
  1. speech share - Silero VAD (built into faster-whisper): how much of the
     sound is voice; a cannon, a splash or a hit give ~0;
  2. the transcription with Whisper's own "no speech" probability and
     confidence; known phrases Whisper invents on noise are thrown away;
  3. a hint from the path in the REZ (STAGING/... is dialogue, GUNSHOT,
     CANNON, SPLASH... are effects).
  -> "dialog" (written to lang/), "niepewne" (to _do_sprawdzenia/) or
     "odglos" (skipped).

Files you already edited are never overwritten (only empty templates are
replaced). The report folder contains sounds from your game - keep it for
yourself; send back only the .tsv / .srt text if you want help.
"""
import argparse
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
try:
    from subtitle_templates import rez_index, smk_movies, wav_seconds, srt_time
except ImportError:
    sys.exit("subtitle_templates.py must be in the same folder as this script")

# Whisper writes these on music / noise.
HALLUCINATIONS = [
    "dziękuję za obejrzenie", "dzięki za obejrzenie", "napisy wykonane", "napisy stworzone",
    "subskrybuj", "zasubskrybuj", "transkrypcja", "tłumaczenie", "amara.org", "do zobaczenia",
    "thanks for watching", "thank you for watching", "subscribe", "subtitles by", "you you",
    "muzyka", "music", "♪", "[", "(",
]
EFFECT_HINTS = re.compile(
    r"gun|shot|cannon|canon|explo|boom|splash|splsh|hit|punch|kick|slash|sword|swish|swoosh|"
    r"step|land|jump|coin|treasure|gem|door|elevat|spring|crate|break|fire|flame|ambient|amb|"
    r"loop|bell|wave|wind|water|drip|bird|squeak|crash|thud|clank|click|pickup|powerup|magic|"
    r"dynamit|bomb|whoosh|bounce|creak|tick|alarm|rope|ladder|lava|tar|spike|saw")
DIALOGUE_HINTS = re.compile(r"staging|/claw/sounds/|voice|talk|speech|taunt|dialog")


def clean(text):
    text = re.sub(r"\s+", " ", text).strip()
    return text


def is_hallucination(text):
    t = text.lower()
    return any(h in t for h in HALLUCINATIONS)


def letters(text):
    return sum(c.isalpha() for c in text)


class Recognizer:
    def __init__(self, model, device, lang):
        try:
            from faster_whisper import WhisperModel
            from faster_whisper.audio import decode_audio
            from faster_whisper.vad import VadOptions, get_speech_timestamps
        except ImportError:
            sys.exit("Install faster-whisper first:  pip install faster-whisper")
        self.decode_audio = decode_audio
        self.get_speech_timestamps = get_speech_timestamps
        self.vad_options = VadOptions(min_silence_duration_ms=300, speech_pad_ms=60,
                                      min_speech_duration_ms=150)
        print("Loading the speech model %s (first run downloads it)..." % model)
        self.model = WhisperModel(model, device=device, compute_type="default")
        self.lang = lang

    @staticmethod
    def load_wav(path):
        """PCM WAV (the game's sounds) -> mono float32 at 16 kHz, stdlib + numpy."""
        import wave
        import numpy as np
        with wave.open(path, "rb") as w:
            ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
            raw = w.readframes(w.getnframes())
        if width == 1:
            a = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
        elif width == 2:
            a = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
        else:
            raise ValueError("unsupported WAV sample size %d" % width)
        if ch > 1:
            a = a[: len(a) // ch * ch].reshape(-1, ch).mean(axis=1)
        if rate != 16000 and len(a):
            n = int(round(len(a) * 16000.0 / rate))
            a = np.interp(np.linspace(0, len(a) - 1, n), np.arange(len(a)), a).astype(np.float32)
        return a

    def load(self, path):
        if path.lower().endswith(".wav"):
            try:
                return self.load_wav(path)
            except Exception:
                pass
        try:
            return self.decode_audio(path, sampling_rate=16000)
        except Exception:
            # e.g. a Smacker movie the bundled decoder does not open: ffmpeg
            if not shutil.which("ffmpeg"):
                raise
            wav = path + ".16k.wav"
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-vn",
                            "-ac", "1", "-ar", "16000", wav], check=True)
            return self.decode_audio(wav, sampling_rate=16000)

    def speech_share(self, audio):
        if len(audio) == 0:
            return 0.0
        spans = self.get_speech_timestamps(audio, self.vad_options, sampling_rate=16000)
        return sum(s["end"] - s["start"] for s in spans) / float(len(audio))

    def transcribe(self, audio):
        segments, _info = self.model.transcribe(
            audio, language=self.lang, beam_size=5, condition_on_previous_text=False,
            vad_filter=False, temperature=0.0)
        out = []
        for s in segments:
            text = clean(s.text)
            if not text:
                continue
            out.append({"start": s.start, "end": s.end, "text": text,
                        "logprob": s.avg_logprob, "nospeech": s.no_speech_prob})
        return out


def write_srt(path, cues, total_seconds):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        for i, c in enumerate(cues, 1):
            start = max(0.0, c["start"])
            end = min(max(c["end"], start + 0.8), total_seconds + 0.5) if total_seconds else c["end"]
            f.write("%d\n%s --> %s\n%s\n\n" % (i, srt_time(start), srt_time(end), c["text"]))


def may_write(path):
    """True for a missing file or an untouched template (no text in it)."""
    if not os.path.exists(path):
        return True
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        body = [l.strip() for l in f if l.strip()]
    text = [l for l in body if not l.isdigit() and "-->" not in l]
    return not text


def classify(path, seconds, share, cues):
    text = " ".join(c["text"] for c in cues)
    good = [c for c in cues if not is_hallucination(c["text"])]
    if not good:
        return ("odglos" if share < 0.15 else "niepewne"), text
    nospeech = sum(c["nospeech"] for c in good) / len(good)
    logprob = sum(c["logprob"] for c in good) / len(good)
    words = len(" ".join(c["text"] for c in good).split())
    score = 0
    score += 2 if share >= 0.35 else (1 if share >= 0.15 else -2)
    score += 1 if nospeech < 0.4 else (-1 if nospeech > 0.7 else 0)
    score += 1 if logprob > -0.8 else (-1 if logprob < -1.3 else 0)
    score += 1 if words >= 2 else 0
    if letters(text) < 3:
        score -= 2
    p = path.lower()
    if DIALOGUE_HINTS.search(p):
        score += 1
    elif EFFECT_HINTS.search(os.path.basename(p)):
        score -= 1
    if score >= 3:
        return "dialog", text
    if score >= 1:
        return "niepewne", text
    return "odglos", text


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rez", default=os.path.join(HERE, "CLAW.REZ"))
    ap.add_argument("--fec", default=None, help="default: CLAW.FEC next to the REZ")
    ap.add_argument("--lang", default="pl")
    ap.add_argument("--model", default="large-v3")
    ap.add_argument("--device", default="auto", help="auto, cuda or cpu")
    ap.add_argument("--only", choices=["movies", "dialogue"])
    ap.add_argument("--min", type=float, default=0.6, help="shortest sound in seconds (0.6)")
    ap.add_argument("--out", default=HERE)
    args = ap.parse_args()
    fec = args.fec or os.path.join(os.path.dirname(os.path.abspath(args.rez)), "CLAW.FEC")

    rec = Recognizer(args.model, args.device, args.lang)
    subs = os.path.join(args.out, "lang", args.lang, "subtitles")
    review = os.path.join(args.out, "_do_sprawdzenia", "lang", args.lang, "subtitles")
    report_dir = os.path.join(args.out, "raport_" + args.lang)
    os.makedirs(os.path.join(report_dir, "audio"), exist_ok=True)
    rows = []
    work = tempfile.mkdtemp(prefix="reclaw_subs_")

    try:
        # ------------------------------------------------------------ movies
        if args.only in (None, "movies"):
            if os.path.exists(fec):
                movies = smk_movies(fec, with_offsets=True)
                print("Movies in CLAW.FEC: %d" % len(movies))
                with open(fec, "rb") as f:
                    for i, (seconds, size, offset) in enumerate(movies):
                        name = "movie_%02d" % i
                        target = os.path.join(subs, "movies", name + ".srt")
                        if not may_write(target):
                            print("  %s: already edited, skipped" % name)
                            continue
                        f.seek(offset)
                        smk = os.path.join(work, name + ".smk")
                        with open(smk, "wb") as o:
                            o.write(f.read(size))
                        print("  %s (%.0f s)..." % (name, seconds), flush=True)
                        try:
                            audio = rec.load(smk)
                        except Exception as e:
                            print("    cannot read the sound (%s); install ffmpeg" % e)
                            continue
                        cues = [c for c in rec.transcribe(audio) if not is_hallucination(c["text"])]
                        if cues:
                            write_srt(target, cues, seconds)
                        rows.append(("film", "movies/" + name, seconds, rec.speech_share(audio),
                                     " / ".join(c["text"] for c in cues), None))
            else:
                print("No CLAW.FEC (%s) - movies skipped" % fec)

        # ---------------------------------------------------------- dialogue
        if args.only in (None, "dialogue"):
            if not os.path.exists(args.rez):
                sys.exit("CLAW.REZ not found: %s (use --rez)" % args.rez)
            index = rez_index(args.rez)
            sounds = []
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
                    sounds.append((path, seconds, data))
            print("Sounds longer than %.1f s: %d" % (args.min, len(sounds)))
            for n, (path, seconds, data) in enumerate(sounds, 1):
                rel = "dialogue" + path[:-4] + ".srt"
                target = os.path.join(subs, *rel.split("/"))
                if not may_write(target):
                    rows.append(("edytowany", path, seconds, None, "", None))
                    continue
                wav = os.path.join(report_dir, "audio", *path.lstrip("/").split("/"))
                os.makedirs(os.path.dirname(wav), exist_ok=True)
                with open(wav, "wb") as o:
                    o.write(data)
                try:
                    audio = rec.load(wav)
                except Exception as e:
                    rows.append(("blad", path, seconds, None, str(e), wav))
                    continue
                share = rec.speech_share(audio)
                cues = rec.transcribe(audio) if share >= 0.05 else []
                kind, text = classify(path, seconds, share, cues)
                good = [c for c in cues if not is_hallucination(c["text"])]
                if kind == "dialog":
                    write_srt(target, good, seconds)
                elif kind == "niepewne" and good:
                    write_srt(os.path.join(review, *rel.split("/")), good, seconds)
                rows.append((kind, path, seconds, share, text, wav))
                if n % 25 == 0 or n == len(sounds):
                    print("  %d / %d" % (n, len(sounds)), flush=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    # ------------------------------------------------------------ report
    order = {"dialog": 0, "niepewne": 1, "film": 2, "odglos": 3, "edytowany": 4, "blad": 5}
    rows.sort(key=lambda r: (order.get(r[0], 9), r[1]))
    with open(os.path.join(report_dir, "raport.tsv"), "w", encoding="utf-8") as f:
        f.write("klasa\tplik\tsekundy\tmowa\ttekst\n")
        for kind, path, seconds, share, text, _wav in rows:
            f.write("%s\t%s\t%.2f\t%s\t%s\n" % (kind, path, seconds,
                                                "" if share is None else "%.2f" % share, text))
    colors = {"dialog": "#d8f5d0", "niepewne": "#fff1c2", "odglos": "#eee", "film": "#d6e8ff"}
    with open(os.path.join(report_dir, "raport.html"), "w", encoding="utf-8") as f:
        f.write("<!doctype html><meta charset=utf-8><title>ReClaw - napisy (%s)</title>"
                "<style>body{font:14px sans-serif;margin:16px}table{border-collapse:collapse}"
                "td,th{border:1px solid #ccc;padding:4px 6px;vertical-align:middle}"
                "audio{height:28px;width:220px}</style>"
                "<h1>Napisy - %s</h1><p>dialog: zapisane w lang/; niepewne: w _do_sprawdzenia/ "
                "(dobre przenieś do lang/); odglos: pominięte.</p><table>"
                "<tr><th>klasa</th><th>plik</th><th>s</th><th>mowa</th><th>tekst</th><th></th></tr>"
                % (args.lang, args.lang))
        for kind, path, seconds, share, text, wav in rows:
            player = ""
            if wav:
                src = os.path.relpath(wav, report_dir).replace(os.sep, "/")
                player = "<audio controls preload=none src='%s'></audio>" % html.escape(src)
            f.write("<tr style='background:%s'><td>%s</td><td>%s</td><td>%.1f</td><td>%s</td>"
                    "<td>%s</td><td>%s</td></tr>\n" % (
                        colors.get(kind, "#fff"), kind, html.escape(path), seconds,
                        "" if share is None else "%.0f%%" % (share * 100), html.escape(text), player))
        f.write("</table>")

    counts = {}
    for r in rows:
        counts[r[0]] = counts.get(r[0], 0) + 1
    print("\nGotowe: " + ", ".join("%s %d" % kv for kv in sorted(counts.items())))
    print("Napisy:  " + subs)
    print("Raport:  " + os.path.join(report_dir, "raport.html"))
    if len(sys.argv) == 1 and os.name == "nt":
        input("Enter...")


if __name__ == "__main__":
    main()
