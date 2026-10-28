#!/usr/bin/env python3
"""ReClaw: where the letters are in the game's menu pictures (stdlib only).

Put it next to your CLAW.REZ (e.g. data/pl/) and double-click it, or:
    python menu_letters.py [CLAW.REZ]

For every menu picture (main menu .PCX and pause menu .PID) it writes
menu_letters.txt with numbers only: the picture size, the text lines (rows)
and, for each line, the letter columns (x from-to, top-bottom row, number of
pixels) plus the row/column profiles. No pixels or colours are written - the
game will cut the letters out of your own CLAW.REZ using these numbers.
"""
import os
import struct
import sys


# ---------------------------------------------------------------- REZ archive
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


# ---------------------------------------------------------------- pictures
def decode_pcx(data):
    """8-bit PCX -> (width, height, list of rows of palette indexes)."""
    xmin, ymin, xmax, ymax = struct.unpack_from("<HHHH", data, 4)
    planes = data[65]
    bpl = struct.unpack_from("<H", data, 66)[0]
    w, h = xmax - xmin + 1, ymax - ymin + 1
    if data[3] != 8 or planes != 1:
        raise ValueError("not an 8-bit PCX")
    pos, rows = 128, []
    for _ in range(h):
        row = bytearray()
        while len(row) < bpl and pos < len(data):
            b = data[pos]
            pos += 1
            if b >= 0xC0:
                row += bytes([data[pos]]) * (b & 0x3F)
                pos += 1
            else:
                row.append(b)
        rows.append(row[:w])
    return w, h, rows


def decode_pid(data):
    """Claw PID -> (width, height, rows of palette indexes; -1 = transparent)."""
    _desc, flags, w, h = struct.unpack_from("<IIII", data, 0)
    pos = 32
    px = [-1] * (w * h)
    i = 0
    n = w * h
    if flags & 0x20:   # compression
        while i < n and pos < len(data):
            b = data[pos]
            pos += 1
            if b > 128:
                i += b - 128
            else:
                for _ in range(b):
                    if i >= n or pos >= len(data):
                        break
                    px[i] = data[pos]
                    pos += 1
                    i += 1
    else:
        while i < n and pos < len(data):
            b = data[pos]
            pos += 1
            count = 1
            if b > 192:
                count = b - 192
                b = data[pos]
                pos += 1
            for _ in range(count):
                if i >= n:
                    break
                px[i] = b
                i += 1
    return w, h, [px[r * w:(r + 1) * w] for r in range(h)]


def mask_of(kind, w, h, rows):
    if kind == "pid":
        return [[v > 0 for v in r] for r in rows]          # -1 and 0: transparent
    bg = rows[0][0] if h and w else 0
    return [[(v != bg and v != 0) for v in r] for r in rows]


def runs(values, min_gap=1):
    """[(start, end)] of consecutive truthy values (inclusive)."""
    out, start, gap = [], None, 0
    for i, v in enumerate(values):
        if v:
            if start is None:
                start = i
            gap = 0
            end = i
        elif start is not None:
            gap += 1
            if gap >= min_gap:
                out.append((start, end))
                start = None
    if start is not None:
        out.append((start, end))
    return out


def describe(path, kind, data):
    w, h, rows = decode_pcx(data) if kind == "pcx" else decode_pid(data)
    m = mask_of(kind, w, h, rows)
    rowprof = [sum(r) for r in m]
    lines = []
    lines.append("== %s  %dx%d" % (path, w, h))
    lines.append("   rows: " + " ".join(str(c) for c in rowprof))
    for (y0, y1) in runs(rowprof, min_gap=2):
        colprof = [sum(m[y][x] for y in range(y0, y1 + 1)) for x in range(w)]
        lines.append("   line y=%d-%d" % (y0, y1))
        lines.append("     cols: " + " ".join(str(c) for c in colprof))
        for (x0, x1) in runs(colprof):
            ys = [y for y in range(y0, y1 + 1) if any(m[y][x0:x1 + 1])]
            cnt = sum(sum(m[y][x0:x1 + 1]) for y in range(y0, y1 + 1))
            lines.append("     blob x=%d-%d y=%d-%d px=%d" % (x0, x1, ys[0], ys[-1], cnt))
    return lines


def main():
    here = os.path.dirname(os.path.abspath(sys.argv[0]))
    rez = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "CLAW.REZ")
    if not os.path.exists(rez):
        sys.exit("CLAW.REZ not found: " + rez)
    index = rez_index(rez)
    wanted = sorted(p for p in index
                    if (p.startswith("/states/menu/images/") and p.endswith(".pcx"))
                    or (p.startswith("/game/images/ingamemenu/") and p.endswith(".pid")))
    out = ["Source: %s (%d pictures)" % (os.path.basename(rez), len(wanted)), ""]
    with open(rez, "rb") as f:
        for p in wanted:
            off, size = index[p]
            f.seek(off)
            data = f.read(size)
            try:
                out += describe(p, "pcx" if p.endswith(".pcx") else "pid", data)
            except Exception as e:
                out.append("== %s  error: %s" % (p, e))
            out.append("")
    path = os.path.join(here, "menu_letters.txt")
    with open(path, "w", encoding="utf-8") as fo:
        fo.write("\n".join(out) + "\n")
    print("Written: " + path)
    if len(sys.argv) <= 1:
        input("Enter...")


if __name__ == "__main__":
    main()
