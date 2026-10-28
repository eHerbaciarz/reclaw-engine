#!/usr/bin/env python3
"""Empty SRT templates for the port's subtitles (stdlib only).

Run next to CLAW.REZ (and CLAW.FEC, if you have the movies):

    python subtitle_templates.py            [CLAW.REZ] [CLAW.FEC]

Creates the ReClaw folder layout next to the script (LANG = pl):

  lang/pl/subtitles/movies/movie_00.srt ... movie_09.srt
                                        one per movie in CLAW.FEC
  lang/pl/subtitles/dialogue/<path of the sound>.srt
                                        one per sound longer than MIN_SECONDS
                                        in CLAW.REZ (Claw and the enemies)
  lang/pl/subtitles/LISTA.txt           every sound with its length

Every template has one cue spanning the whole sound / movie and no text, so
an untouched template shows nothing. Write the text under the time line (and
split the cue if you like), delete the templates you do not need, and copy the
"lang" folder into the ReClaw folder (e.g. sdmc:/switch/reclaw/lang/).
English subtitles go to lang/en/... the same way (LANG = "en" below).

Only lengths and file names are read; nothing from the game is copied.
"""
import os
import struct
import sys

MIN_SECONDS = 0.6
LANG = "pl"


def rez_index(path):
    index = {}
    with open(path, "rb") as f:
        f.read(127)
        _version, root_off, root_size = struct.unpack("<III", f.read(12))

        def cstr():
            out = bytearray()
            while True:
                c = f.read(1)
                if not c or c == b"\0":
                    return out.decode("latin-1")
                out += c

        def walk(offset, size, prefix):
            cur, remaining = offset, size
            while remaining > 0:
                f.seek(cur)
                (is_dir,) = struct.unpack("<I", f.read(4))
                if is_dir:
                    d_off, d_size, _dt = struct.unpack("<III", f.read(12))
                    name = cstr()
                    used = 16 + len(name) + 1
                    walk(d_off, d_size, prefix + "/" + name.lower())
                else:
                    f_off, f_size, _dt, _fid = struct.unpack("<IIII", f.read(16))
                    ext = f.read(4).split(b"\0")[0][::-1].decode("latin-1").lower()
                    f.read(4)
                    name = cstr()
                    used = 28 + len(name) + 2
                    index[f"{prefix}/{name.lower()}.{ext}"] = (f_off, f_size)
                remaining -= used
                cur += used

        walk(root_off, root_size, "")
    return index


def wav_seconds(data):
    """Length of a RIFF WAVE in seconds, or None."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    pos, byte_rate, data_size = 12, 0, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt " and size >= 16:
            byte_rate = struct.unpack_from("<I", data, pos + 16)[0]
        elif cid == b"data":
            data_size = min(size, len(data) - pos - 8)
            break
        pos += 8 + size + (size & 1)
    if not byte_rate or data_size is None:
        return None
    return data_size / byte_rate


def smk_movies(path):
    """Smacker movies in file order (the game's movie_00, movie_01, ...)."""
    size = os.path.getsize(path)
    found = []
    with open(path, "rb") as f:
        data_pos = 0
        block = 1 << 22
        while data_pos < size:
            f.seek(data_pos)
            data = f.read(block + 8)
            if not data:
                break
            i, next_pos = 0, data_pos + block
            while True:
                i = data.find(b"SMK", i)
                if i < 0 or i >= block:
                    break
                info = smk_info(path, data_pos + i, size)
                if not info:
                    i += 1
                    continue
                found.append(info)
                end = data_pos + i + info[1]
                if end >= data_pos + block:
                    next_pos = end
                    break
                i = end - data_pos
            data_pos = next_pos
    return found


def smk_info(path, offset, file_size):
    with open(path, "rb") as f:
        f.seek(offset)
        hdr = f.read(104)
        if len(hdr) < 104 or hdr[:3] != b"SMK" or hdr[3:4] not in (b"2", b"4"):
            return None
        w, h, frames, rate, flags = struct.unpack_from("<IIIiI", hdr, 4)
        trees_size = struct.unpack_from("<I", hdr, 52)[0]
        if not (0 < w <= 4096 and 0 < h <= 4096 and 0 < frames < 200000):
            return None
        n = frames + (1 if flags & 1 else 0)
        sizes = struct.unpack("<%dI" % n, f.read(4 * n))
        total = 104 + 5 * n + trees_size + sum(s & ~3 for s in sizes)
        if offset + total > file_size:
            return None
        fps = 1000.0 / rate if rate > 0 else (100000.0 / -rate if rate < 0 else 10.0)
        return (frames / fps, total)


def srt_time(seconds):
    ms = int(round(seconds * 1000))
    return "%02d:%02d:%02d,%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def write_template(path, seconds):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        return False          # never overwrite your work
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("1\n%s --> %s\n\n" % (srt_time(0), srt_time(max(0.5, seconds))))
    return True


def main():
    here = os.path.dirname(os.path.abspath(sys.argv[0]))
    rez = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "CLAW.REZ")
    fec = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(rez), "CLAW.FEC")
    out = os.path.join(here, "lang", LANG, "subtitles")
    lines, made = [], 0

    if os.path.exists(fec):
        movies = smk_movies(fec)
        lines.append("Filmy (CLAW.FEC): %d" % len(movies))
        for i, (seconds, _size) in enumerate(movies):
            name = "movies/movie_%02d.srt" % i
            made += write_template(os.path.join(out, "movies", "movie_%02d.srt" % i), seconds)
            lines.append("  %-24s %7.1f s" % (name, seconds))
    else:
        lines.append("Brak CLAW.FEC - pominięto filmy")

    if os.path.exists(rez):
        index = rez_index(rez)
        sounds = []
        with open(rez, "rb") as f:
            for path in sorted(index):
                if not path.endswith(".wav"):
                    continue
                off, size = index[path]
                f.seek(off)
                seconds = wav_seconds(f.read(size))
                if seconds is None or seconds < MIN_SECONDS:
                    continue
                sounds.append((path, seconds))
        lines.append("")
        lines.append("Dźwięki dłuższe niż %.1f s: %d" % (MIN_SECONDS, len(sounds)))
        for path, seconds in sounds:
            rel = "dialogue" + path[:-4] + ".srt"
            made += write_template(os.path.join(out, *rel.split("/")), seconds)
            lines.append("  %-60s %6.2f s" % (rel, seconds))
    else:
        lines.append("Brak CLAW.REZ: " + rez)

    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "LISTA.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines[:12]))
    print("...\nNowych szablonów: %d, lista: %s" % (made, os.path.join(out, "LISTA.txt")))
    if len(sys.argv) < 2 and os.name == "nt":
        input("Enter...")


if __name__ == "__main__":
    main()
