"""ETC1A4 textures in the 3DS (PICA200) layout: 8x8 tiles, each four 4x4
blocks in Z order; a block is 8 bytes of 4-bit alpha (column-major) then the
ETC1 block as a little-endian u64. Needs: pip install etcpak texture2ddecoder pillow."""
import struct
import etcpak
from PIL import Image

def _blocks(w, h):
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for bx, by in ((0, 0), (4, 0), (0, 4), (4, 4)):
                yield tx + bx, ty + by

def encode(im):
    im = im.convert("RGBA"); w, h = im.size
    px = im.load()
    rgba = im.tobytes()
    etc = etcpak.compress_etc1_rgb(rgba, w, h)   # 4x4 blocks in raster order
    bw = w // 4
    out = bytearray()
    for x, y in _blocks(w, h):
        a = 0
        for i in range(4):
            for j in range(4):
                a |= (px[x + i, y + j][3] >> 4) << ((i * 4 + j) * 4)
        out += struct.pack("<Q", a)
        k = ((y // 4) * bw + x // 4) * 8
        out += etc[k:k + 8][::-1]
    return bytes(out)

def decode(data, w, h):
    im = Image.new("RGBA", (w, h)); bw = w // 4
    std = bytearray(w * h // 2); alpha = {}
    o = 0
    for x, y in _blocks(w, h):
        alpha[(x, y)] = struct.unpack_from("<Q", data, o)[0]
        k = ((y // 4) * bw + x // 4) * 8
        std[k:k + 8] = data[o + 8:o + 16][::-1]
        o += 16
    import texture2ddecoder
    rgb = Image.frombytes("RGBA", (w, h), texture2ddecoder.decode_etc1(bytes(std), w, h), "raw", "BGRA")
    px = rgb.load()
    for (x, y), a in alpha.items():
        for i in range(4):
            for j in range(4):
                r, g, b, _ = px[x + i, y + j]
                v = (a >> ((i * 4 + j) * 4)) & 15
                px[x + i, y + j] = (r, g, b, v * 17)
    return rgb
