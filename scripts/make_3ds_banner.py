#!/usr/bin/env python3
"""The 3DS HOME menu banner model (CGFX) for ReClaw: bannertool's flat banner
plane with our picture, plus our own skeletal animation "COMMON" that turns
the plane around its vertical axis (hold, one full turn, hold ...), with
face culling off so the back is visible while it turns.

    scripts/make_3ds_banner.py banner.png out.cgfx
    bannertool makebanner -ci out.cgfx -a banner.wav -o banner.bnr
"""
import math, struct, sys
from PIL import Image

BASE = __file__.rsplit("/", 2)[0] + "/platform/3ds/cia/banner_base.cgfx"
IMAG = 0x14F8                        # the IMAG block (vertex/index buffers, texture) follows DATA
PTRS_INTO_IMAG = (0xE14, 0xE4C, 0x1054)   # index buffer, vertex buffer, texture data
FACE_CULL_FIELD, FACE_CULL_PARAM = 0x9C4, 0x9C8
BONE_FLAGS = 0xF30                   # bone "banner": its flags (GfxBoneFlags)
BONE_IS_ROTATION_ZERO = 1 << 2       # set = the rotation is skipped, animated or not

HOLD, TURN = 150, 90                 # frames (60 per second): front, then one turn
FRAMES = HOLD + TURN

def tiles_rgba4444(png):
    im = Image.open(png).convert("RGBA").resize((256, 128), Image.LANCZOS)
    px = im.load(); out = bytearray(256 * 128 * 2)
    for y in range(128):
        for x in range(256):
            i = (((y >> 3) * 32 + (x >> 3)) << 6) + ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1)
                                                     | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))
            r, g, b, a = px[x, y]
            struct.pack_into("<H", out, i * 2, ((r & 0xF0) << 8) | ((g & 0xF0) << 4) | (b & 0xF0) | (a >> 4))
    return bytes(out)

def patricia_ref(name):
    """Bit index of a one-entry CGFX dictionary node (highest set bit of the last character)."""
    return (len(name) - 1) * 8 + (name[-1].bit_length() - 1)

class Blob:
    def __init__(self, base): self.b = bytearray(); self.base = base; self.fix = []
    def pos(self): return self.base + len(self.b)
    def u32(self, v): self.b += struct.pack("<I", v & 0xFFFFFFFF)
    def f32(self, v): self.b += struct.pack("<f", v)
    def ptr(self, label):            # self-relative pointer, filled in later
        self.fix.append((len(self.b), label)); self.u32(0)
    def resolve(self, labels):
        for at, label in self.fix:
            struct.pack_into("<I", self.b, at, labels[label] - (self.base + at))

def build(png, out):
    d = bytearray(open(BASE, "rb").read())
    assert d[:4] == b"CGFX" and d[IMAG:IMAG + 4] == b"IMAG"
    data = d[:IMAG]; imag = d[IMAG:] + tiles_rgba4444(png)

    # Both faces drawn (GfxFaceCulling.Never, PICA cull mode 0).
    struct.pack_into("<I", data, FACE_CULL_FIELD, 3)
    struct.pack_into("<I", data, FACE_CULL_PARAM, 0)
    # The bone turns: drop its "rotation is zero" flag (bannertool's plane
    # never moves, so its bone said the rotation could be skipped).
    flags = struct.unpack_from("<I", data, BONE_FLAGS)[0]
    assert flags == 0x1DC, hex(flags)
    struct.pack_into("<I", data, BONE_FLAGS, flags & ~BONE_IS_ROTATION_ZERO)

    B = Blob(IMAG); L = {}
    def dict1(name, target):
        B.b += b"DICT"; B.u32(0x2C); B.u32(1)
        B.u32(0xFFFFFFFF); B.b += struct.pack("<HH", 1, 0); B.u32(0); B.u32(0)
        B.u32(patricia_ref(name.encode())); B.b += struct.pack("<HH", 0, 1)
        B.ptr("s:" + name); B.ptr(target)
    L["animdict"] = B.pos(); dict1("COMMON", "canm")
    # CANM: skeletal animation "COMMON", looped
    L["canm"] = B.pos()
    B.b += b"CANM"; B.u32(0x05000000); B.ptr("s:COMMON"); B.ptr("s:SkeletalAnimation")
    B.u32(1); B.f32(FRAMES); B.u32(1); B.ptr("memberdict"); B.u32(0); B.u32(0)
    L["memberdict"] = B.pos(); dict1("banner", "elem")
    # Transform of bone "banner": only RotationY exists
    L["elem"] = B.pos()
    flags = (0x3FF << 16) & ~(1 << 20)
    B.u32(flags); B.ptr("s:banner"); B.ptr("s:banner"); B.u32(0); B.u32(5)   # 5 = Transform
    for i in range(10):
        if i == 4: B.ptr("curve")
        else: B.u32(0)
    L["curve"] = B.pos()
    B.f32(0); B.f32(FRAMES); B.u32(0); B.u32(4); B.u32(1); B.ptr("segment")
    L["segment"] = B.pos()
    keys = [(0, 0.0, 0.0), (HOLD, 0.0, 0.0), (FRAMES, 2 * math.pi, 0.0)]
    B.f32(0); B.f32(FRAMES); B.u32(0x68); B.u32(len(keys)); B.f32(1.0 / FRAMES)  # unified Hermite (frame, value, slope)
    for f, v, s in keys: B.f32(f); B.f32(v); B.f32(s)
    for s in ("COMMON", "SkeletalAnimation", "banner"):
        L["s:" + s] = B.pos(); B.b += s.encode() + b"\0"
    while len(B.b) % 0x80: B.b += b"\0"          # keeps the texture 128-byte aligned
    B.resolve(L); n = len(B.b)

    # Point DATA entry 9 (skeletal animations) at the new dictionary.
    e9 = 0x14 + 8 + 9 * 8
    struct.pack_into("<I", data, e9, 1)
    struct.pack_into("<I", data, e9 + 4, L["animdict"] - (e9 + 4))
    for p in PTRS_INTO_IMAG:
        struct.pack_into("<I", data, p, struct.unpack_from("<I", data, p)[0] + n)
    struct.pack_into("<I", data, 0x18, struct.unpack_from("<I", data, 0x18)[0] + n)   # DATA size
    res = data + B.b + imag
    struct.pack_into("<I", res, 0x0C, len(res))                                         # file size
    open(out, "wb").write(res)

if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2])
