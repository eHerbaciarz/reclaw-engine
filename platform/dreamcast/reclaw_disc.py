#!/usr/bin/env python3
"""ReClaw - Dreamcast disc maker (Python 3, standard library only).

Makes a GD-ROM image (.gdi + tracks) for Flycast, Redream or a GDEMU from the
ReClaw Dreamcast package and YOUR OWN copy of Claw (1997):

    python reclaw_disc.py --package reclaw-dreamcast.zip --rez CLAW.REZ [--fec CLAW.FEC] [--lang pl] [--out reclaw-gdi]

--package  the reclaw-dreamcast.zip from the build (or the folder unpacked from it)
--rez      CLAW.REZ of your copy of the game (CLAW.FEC too, for the movies)
--lang     pl or en: which folder of the disc the game files go to (data/pl, data/en)
--out      folder for disc.gdi, track01.iso, track02.raw, track03.iso

Nothing of the game is downloaded or sent anywhere: the files are only copied
into the image on your computer.

Pl: robi obraz płyty (.gdi) dla Flycasta/Redream/GDEMU z paczki ReClaw i Twojej
kopii gry. Przykład:
    python reclaw_disc.py --package reclaw-dreamcast.zip --rez D:\\Claw\\CLAW.REZ --fec D:\\Claw\\CLAW.FEC
"""

import argparse
import io
import os
import shutil
import struct
import sys
import tempfile
import zipfile

SECTOR = 2048
HIGH_DENSITY_LBA = 45000   # where the GD-ROM's game area (track 3) starts


# --------------------------------------------------------------- ISO 9660

def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def dstr(s, n):
    return s.encode("ascii").ljust(n, b" ")[:n]


DATE17 = b"2026010100000000\x00"           # volume dates (no time zone offset)
DATE7 = bytes([126, 1, 1, 0, 0, 0, 0])     # directory record date (year 2026)


def iso_name(name, is_dir):
    """ISO 9660 level 2 name: upper case, A-Z 0-9 _ (others become _)."""
    base = name.upper()
    out = []
    for ch in base:
        out.append(ch if (ch.isalnum() and ch.isascii()) or ch in "._" else "_")
    base = "".join(out)
    if is_dir:
        return base.replace(".", "_")[:31]
    if "." in base:
        stem, ext = base.rsplit(".", 1)
        stem = stem.replace(".", "_")
        base = stem[:30 - len(ext)] + "." + ext
    return base[:30] + ";1"


class Node:
    def __init__(self, name, is_dir, source=None, data=None):
        self.name = name                # name on the disc (ISO form)
        self.is_dir = is_dir
        self.source = source            # file on this computer
        self.data = data                # or the bytes themselves
        self.children = []
        self.parent = None
        self.lba = 0
        self.size = 0
        self.number = 0                 # path table number (directories)

    def file_size(self):
        if self.data is not None:
            return len(self.data)
        return os.path.getsize(self.source)


def dir_record(node_lba, size, flags, ident):
    ident_len = len(ident)
    length = 33 + ident_len + (1 if ident_len % 2 == 0 else 0)
    rec = bytearray(length)
    rec[0] = length
    rec[2:10] = both32(node_lba)
    rec[10:18] = both32(size)
    rec[18:25] = DATE7
    rec[25] = flags
    rec[28:32] = both16(1)
    rec[32] = ident_len
    rec[33:33 + ident_len] = ident
    return bytes(rec)


def dir_records(d):
    """The directory's records, none crossing a sector, as bytes."""
    recs = [
        dir_record(d.lba, d.size, 2, b"\x00"),
        dir_record(d.parent.lba if d.parent else d.lba, d.parent.size if d.parent else d.size, 2, b"\x01"),
    ]
    for c in sorted(d.children, key=lambda n: n.name):
        recs.append(dir_record(c.lba, c.size, 2 if c.is_dir else 0, c.name.encode("ascii")))
    out = bytearray()
    for r in recs:
        if len(out) // SECTOR != (len(out) + len(r) - 1) // SECTOR:
            out += b"\x00" * (SECTOR - len(out) % SECTOR)
        out += r
    return bytes(out)


def sectors(n):
    return (n + SECTOR - 1) // SECTOR


def build_iso(root, out_path, base_lba=0, system_area=b"", volume_id="RECLAW"):
    """Writes the ISO 9660 image of the tree `root`; its addresses count from base_lba."""
    # directories in path-table (breadth-first) order
    dirs, queue = [], [root]
    while queue:
        d = queue.pop(0)
        dirs.append(d)
        d.number = len(dirs)
        kids = sorted([c for c in d.children if c.is_dir], key=lambda n: n.name)
        for k in kids:
            k.parent = d
        queue.extend(kids)
    for d in dirs:
        for c in d.children:
            c.parent = d

    def path_table(big):
        t = bytearray()
        for d in dirs:
            ident = b"\x00" if d is root else d.name.encode("ascii")
            parent = d.parent.number if d.parent else 1
            t += bytes([len(ident), 0])
            t += struct.pack(">I" if big else "<I", d.lba)
            t += struct.pack(">H" if big else "<H", parent)
            t += ident + (b"\x00" if len(ident) % 2 else b"")
        return bytes(t)

    # sizes of the directories (records only depend on names, so lay out twice)
    for d in dirs:
        d.size = SECTOR
    for _ in range(2):
        pt_size = len(path_table(False))
        pt_sectors = sectors(pt_size)
        lba = base_lba + 18
        lba_pt_l = lba
        lba += pt_sectors
        lba_pt_m = lba
        lba += pt_sectors
        for d in dirs:
            d.lba = lba
            d.size = sectors(len(dir_records(d))) * SECTOR
            lba += d.size // SECTOR
        files = []
        for d in dirs:
            for c in sorted(d.children, key=lambda n: n.name):
                if not c.is_dir:
                    files.append(c)
        for f in files:
            f.size = f.file_size()
            f.lba = lba
            lba += max(1, sectors(f.size))
    total = lba  # first free sector (absolute)

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = dstr("SEGA SEGAKATANA", 32)
    pvd[40:72] = dstr(volume_id, 32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(pt_size)
    pvd[140:144] = struct.pack("<I", lba_pt_l)
    pvd[148:152] = struct.pack(">I", lba_pt_m)
    pvd[156:190] = dir_record(root.lba, root.size, 2, b"\x00")
    pvd[190:318] = dstr("", 128)
    pvd[318:446] = dstr("HERBACIARZ", 128)
    pvd[446:574] = dstr("", 128)
    pvd[574:702] = dstr("RECLAW", 128)
    pvd[702:813] = dstr("", 111)
    pvd[813:830] = DATE17
    pvd[830:847] = DATE17
    pvd[847:864] = b"0" * 16 + b"\x00"
    pvd[864:881] = b"0" * 16 + b"\x00"
    pvd[881] = 1
    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    with open(out_path, "wb") as out:
        out.write(system_area.ljust(16 * SECTOR, b"\x00")[:16 * SECTOR])
        out.write(pvd)
        out.write(term)
        for big in (False, True):
            t = path_table(big)
            out.write(t.ljust(pt_sectors * SECTOR, b"\x00"))
        for d in dirs:
            recs = dir_records(d)
            out.write(recs.ljust(d.size, b"\x00"))
        for f in files:
            assert out.tell() == (f.lba - base_lba) * SECTOR, f.name
            if f.data is not None:
                out.write(f.data)
            else:
                with open(f.source, "rb") as src:
                    shutil.copyfileobj(src, out, 1024 * 1024)
            pad = (-f.size) % SECTOR
            if pad or f.size == 0:
                out.write(b"\x00" * (pad if f.size else SECTOR))
    return total - base_lba


def tree_from_folder(folder, node=None):
    node = node or Node("", True)
    for entry in sorted(os.listdir(folder)):
        path = os.path.join(folder, entry)
        if os.path.isdir(path):
            child = Node(iso_name(entry, True), True)
            tree_from_folder(path, child)
        else:
            child = Node(iso_name(entry, False), False, source=path)
        child.parent = node
        node.children.append(child)
    return node


def find_dir(node, name):
    for c in node.children:
        if c.is_dir and c.name == name:
            return c
    d = Node(name, True)
    d.parent = node
    node.children.append(d)
    return d


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description="ReClaw Dreamcast disc maker (.gdi)")
    ap.add_argument("--package", required=True, help="reclaw-dreamcast.zip or its unpacked folder")
    ap.add_argument("--rez", required=True, help="CLAW.REZ of your copy of the game")
    ap.add_argument("--fec", help="CLAW.FEC (movies), optional")
    ap.add_argument("--lang", default="pl", choices=["pl", "en"])
    ap.add_argument("--out", default="reclaw-gdi")
    args = ap.parse_args()

    for f in [args.rez] + ([args.fec] if args.fec else []):
        if not os.path.isfile(f):
            sys.exit("Not found / nie ma pliku: " + f)

    work = tempfile.mkdtemp(prefix="reclaw-dc-")
    try:
        pkg = args.package
        if zipfile.is_zipfile(pkg):
            with zipfile.ZipFile(pkg) as z:
                z.extractall(work)
            pkg = work
        # the folder with disc/ and IP.BIN (the zip has reclaw-dreamcast/ inside)
        base = None
        for dirpath, dirnames, filenames in os.walk(pkg):
            if "disc" in dirnames and "IP.BIN" in filenames:
                base = dirpath
                break
        if not base:
            sys.exit("The package has no disc/ and IP.BIN / w paczce brak disc/ i IP.BIN")
        with open(os.path.join(base, "IP.BIN"), "rb") as f:
            ip = f.read()
        if len(ip) != 16 * SECTOR:
            sys.exit("IP.BIN is not 32 KB")

        root = tree_from_folder(os.path.join(base, "disc"))
        lang = find_dir(find_dir(root, "DATA"), args.lang.upper())
        for path, name in ((args.rez, "CLAW.REZ"), (args.fec, "CLAW.FEC")):
            if path:
                n = Node(iso_name(name, False), False, source=path)
                n.parent = lang
                lang.children = [c for c in lang.children if c.name != n.name] + [n]
        if not any(c.name == "1ST_READ.BIN;1" for c in root.children):
            sys.exit("disc/1ST_READ.BIN missing / brak disc/1ST_READ.BIN")

        os.makedirs(args.out, exist_ok=True)
        # track 1: a small data track (what a CD player / PC sees)
        t1 = Node("", True)
        readme = ("ReClaw for the Sega Dreamcast. This is a GD-ROM image: "
                  "use Flycast, Redream or a GDEMU.\r\n").encode("ascii")
        r = Node(iso_name("README.TXT", False), False, data=readme)
        r.parent = t1
        t1.children.append(r)
        build_iso(t1, os.path.join(args.out, "track01.iso"))
        with open(os.path.join(args.out, "track01.iso"), "r+b") as f:
            f.truncate(300 * SECTOR)   # (300 sectors, as on real GD-ROMs)
        # track 2: four seconds of silence (audio)
        with open(os.path.join(args.out, "track02.raw"), "wb") as f:
            f.write(b"\x00" * (300 * 2352))
        # track 3: the game, from sector 45000, IP.BIN in its first 16 sectors
        n3 = build_iso(root, os.path.join(args.out, "track03.iso"), HIGH_DENSITY_LBA, ip)
        with open(os.path.join(args.out, "disc.gdi"), "w", newline="\r\n") as f:
            f.write("3\n")
            f.write("1 0 4 2048 track01.iso 0\n")
            f.write("2 450 0 2352 track02.raw 0\n")
            f.write("3 %d 4 2048 track03.iso 0\n" % HIGH_DENSITY_LBA)
        print("OK: %s (track 3: %d sectors, %.1f MB)" % (os.path.join(args.out, "disc.gdi"), n3, n3 * SECTOR / 1048576.0))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
