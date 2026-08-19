#!/usr/bin/env python3
"""
Host test for tools/png2pict.py.

png2pict writes PICT 2.0 pictures that only real QuickDraw can truly judge --
ImageMagick's PICT reader renders the BitMap form of opcode $0098 as solid
black no matter what the bits say, and macOS `sips` renders it solid white,
so neither is usable as an oracle. (Both DO read the PixMap form correctly,
which is why the indexed cases below also cross-check against ImageMagick.)

So this file parses the emitted picture back independently, walking the
opcode stream by the Inside Macintosh: Imaging With QuickDraw layout rather
than by png2pict's own writer code, and asserts the pixels survive the trip.
That catches structural, offset, and PackBits errors. It cannot catch a
misreading of the spec itself -- for that, see the hardware/emulator check in
docs/asset-pipeline.md.

    python3 tests/test_png2pict.py
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PNG2PICT = os.path.join(REPO, 'tools', 'png2pict.py')

failures = []


# ---- an independent PICT reader ----------------------------------------

class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from('>H', self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from('>I', self.d, self.p)[0]
        self.p += 4
        return v

    def rect(self):
        v = struct.unpack_from('>hhhh', self.d, self.p)
        self.p += 8
        return v

    def take(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


def unpackbits(data, expected):
    """Inverse of Apple PackBits; stops once `expected` bytes are produced."""
    out = bytearray()
    i = 0
    while len(out) < expected:
        flag = data[i]
        i += 1
        if flag < 128:
            n = flag + 1
            out += data[i:i + n]
            i += n
        else:
            n = 257 - flag
            out += bytes([data[i]]) * n
            i += 1
    return bytes(out[:expected])


def parse_pict(path):
    """Returns (width, height, pixel_size, palette_or_None, rows_of_bytes)."""
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) > 512 and data[:512] == b'\x00' * 512:
        data = data[512:]

    r = Reader(data)
    pic_size = r.u16()
    assert pic_size == len(data), f'picSize {pic_size} != file body {len(data)}'
    top, left, bottom, right = r.rect()
    width, height = right - left, bottom - top

    assert r.u16() == 0x0011, 'expected VersionOp'
    assert r.u16() == 0x02FF, 'expected version 2'
    assert r.u16() == 0x0C00, 'expected HeaderOp'
    r.take(24)
    assert r.u16() == 0x0001, 'expected ClipRgn'
    rgn_size = r.u16()
    r.take(rgn_size - 2)

    assert r.u16() == 0x0098, 'expected PackBitsRect'
    row_bytes_field = r.u16()
    is_pixmap = bool(row_bytes_field & 0x8000)
    row_bytes = row_bytes_field & 0x3FFF
    r.rect()                                     # bounds

    palette = None
    if is_pixmap:
        r.u16(); r.u16(); r.u32()                # pmVersion, packType, packSize
        r.u32(); r.u32()                         # hRes, vRes
        r.u16()                                  # pixelType
        pixel_size = r.u16()
        r.u16(); r.u16()                         # cmpCount, cmpSize
        r.u32(); r.u32(); r.u32()                # planeBytes, pmTable, pmReserved
        r.u32()                                  # ctSeed
        r.u16()                                  # ctFlags
        count = r.u16() + 1
        palette = []
        for _ in range(count):
            r.u16()                              # value
            palette.append(tuple(r.u16() >> 8 for _ in range(3)))
    else:
        pixel_size = 1

    r.rect(); r.rect(); r.u16()                  # srcRect, dstRect, mode

    rows = []
    for _ in range(height):
        if row_bytes < 8:
            rows.append(r.take(row_bytes))
        else:
            n = r.u8() if row_bytes <= 250 else r.u16()
            rows.append(unpackbits(r.take(n), row_bytes))

    if r.p % 2:
        r.u8()
    assert r.u16() == 0x00FF, 'expected EndOfPicture'
    assert r.p == len(data), f'{len(data) - r.p} trailing bytes'

    return width, height, pixel_size, palette, rows


def pict_pixels(path):
    """Decode a PICT to a flat list of (r, g, b)."""
    width, height, pixel_size, palette, rows = parse_pict(path)
    out = []
    for y in range(height):
        row = rows[y]
        for x in range(width):
            if pixel_size == 1:
                bit = (row[x >> 3] >> (7 - (x & 7))) & 1
                out.append((0, 0, 0) if bit else (255, 255, 255))
            elif pixel_size == 4:
                nib = (row[x >> 1] >> 4) if not (x & 1) else (row[x >> 1] & 0x0F)
                out.append(palette[nib])
            else:
                out.append(palette[row[x]])
    return width, height, out


def png_pixels(path):
    dims = subprocess.run(['magick', 'identify', '-format', '%w %h', path],
                          capture_output=True, text=True, check=True).stdout
    w, h = (int(v) for v in dims.split())
    raw = subprocess.run(['magick', path, '-depth', '8', 'RGBA:-'],
                         capture_output=True, check=True).stdout
    return w, h, [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


# ---- checks -------------------------------------------------------------

def run(name, fn):
    try:
        fn()
    except Exception as exc:                     # noqa: BLE001 - report, keep going
        failures.append(f'{name}: {exc}')
        print(f'FAIL {name}: {exc}')
    else:
        print(f'ok   {name}')


def make_png(tmp, name, args):
    path = os.path.join(tmp, name)
    subprocess.run(['magick'] + args + [path], check=True)
    return path


def convert(png, out, *extra):
    # stderr is dropped: png2pict warns when the top-left pixel is not the
    # transparency key, which most of these synthetic fixtures are not.
    subprocess.run([sys.executable, PNG2PICT, png, out] + list(extra),
                   check=True, stderr=subprocess.DEVNULL)


def main():
    with tempfile.TemporaryDirectory() as tmp:

        def one_bit_roundtrips():
            # A pattern with both long runs (PackBits) and busy areas.
            png = make_png(tmp, 'pat.png', [
                '-size', '64x64', 'pattern:checkerboard', '-monochrome'])
            pict = os.path.join(tmp, 'pat.pict')
            convert(png, pict, '--mode', '1bit')
            _, _, src = png_pixels(png)
            w, h, got = pict_pixels(pict)
            assert (w, h) == (64, 64), f'size {w}x{h}'
            for i, (s, g) in enumerate(zip(src, got)):
                dark = s[3] >= 128 and (s[0] * 299 + s[1] * 587 + s[2] * 114) // 1000 < 128
                want = (0, 0, 0) if dark else (255, 255, 255)
                assert g == want, f'pixel {i}: {g} != {want}'

        def one_bit_narrow_rows_are_unpacked():
            # rowBytes < 8 must be stored raw, not PackBits-compressed.
            png = make_png(tmp, 'narrow.png', ['-size', '16x16', 'xc:black'])
            pict = os.path.join(tmp, 'narrow.pict')
            convert(png, pict, '--mode', '1bit')
            _, _, _, _, rows = parse_pict(pict)
            assert len(rows) == 16 and all(r == b'\xff\xff' for r in rows), rows[:2]

        def one_bit_transparent_reads_as_white():
            png = make_png(tmp, 'alpha.png', ['-size', '8x8', 'xc:none'])
            pict = os.path.join(tmp, 'alpha.pict')
            convert(png, pict, '--mode', '1bit')
            _, _, got = pict_pixels(pict)
            assert all(p == (255, 255, 255) for p in got), 'alpha should be white'

        def indexed_4bit_roundtrips():
            png = make_png(tmp, 'few.png', [
                '-size', '32x32', 'gradient:red-blue', '-colors', '8', '-depth', '8'])
            pict = os.path.join(tmp, 'few.pict')
            convert(png, pict, '--mode', 'indexed')
            _, _, pixel_size, _, _ = parse_pict(pict)
            assert pixel_size == 4, f'8 colours + key should pack 4-bit, got {pixel_size}'
            _, _, src = png_pixels(png)
            _, _, got = pict_pixels(pict)
            for i, (s, g) in enumerate(zip(src, got)):
                assert g == s[:3], f'pixel {i}: {g} != {s[:3]}'

        def indexed_8bit_roundtrips():
            png = make_png(tmp, 'many.png', [
                '-size', '32x32', 'gradient:red-blue', '-colors', '40', '-depth', '8'])
            pict = os.path.join(tmp, 'many.pict')
            convert(png, pict, '--mode', 'indexed')
            _, _, pixel_size, _, _ = parse_pict(pict)
            assert pixel_size == 8, f'40 colours needs 8-bit, got {pixel_size}'
            _, _, src = png_pixels(png)
            _, _, got = pict_pixels(pict)
            for i, (s, g) in enumerate(zip(src, got)):
                assert g == s[:3], f'pixel {i}: {g} != {s[:3]}'

        def indexed_key_is_slot_zero():
            png = make_png(tmp, 'key.png', ['-size', '8x8', 'xc:none'])
            pict = os.path.join(tmp, 'key.pict')
            convert(png, pict, '--mode', 'indexed')
            _, _, _, palette, rows = parse_pict(pict)
            assert palette[0] == (255, 0, 255), f'slot 0 is {palette[0]}'
            assert all(b == 0 for row in rows for b in row), 'transparent should index 0'

        def indexed_opaque_keeps_every_slot():
            # A fully opaque image must not burn a palette slot on the
            # transparency key -- at 256 colours that is the difference
            # between fitting and being rejected.
            png = make_png(tmp, 'opaque.png', [
                '-size', '32x32', 'gradient:red-blue', '-colors', '16',
                '-depth', '8', '-alpha', 'off'])
            pict = os.path.join(tmp, 'opaque.pict')
            convert(png, pict, '--mode', 'indexed')
            _, _, pixel_size, palette, _ = parse_pict(pict)
            assert pixel_size == 4, f'16 opaque colours must still fit 4-bit, got {pixel_size}'
            assert palette[0] != (255, 0, 255), 'key should not be reserved when opaque'
            _, _, src = png_pixels(png)
            _, _, got = pict_pixels(pict)
            for i, (s_, g) in enumerate(zip(src, got)):
                assert g == s_[:3], f'pixel {i}: {g} != {s_[:3]}'

        def indexed_matches_imagemagick():
            # ImageMagick DOES read the PixMap form, so it is a real second
            # opinion on the indexed path (it is useless on the BitMap form).
            # -depth 8 matters: png2pict decodes at 8 bits, so a 16-bit
            # source would differ by rounding alone and prove nothing.
            png = make_png(tmp, 'im.png', [
                '-size', '24x24', 'plasma:', '-colors', '12', '-depth', '8'])
            pict = os.path.join(tmp, 'im.pict')
            convert(png, pict, '--mode', 'indexed')
            back = os.path.join(tmp, 'im_back.png')
            subprocess.run(['magick', pict, back], check=True)
            diff = subprocess.run(['magick', 'compare', '-metric', 'AE', png, back, 'null:'],
                                  capture_output=True, text=True)
            assert diff.stderr.strip().split()[0] == '0', \
                f'ImageMagick sees {diff.stderr.strip()} differing pixels'

        def no_header_omits_512_bytes():
            png = make_png(tmp, 'hdr.png', ['-size', '8x8', 'xc:black'])
            with_hdr = os.path.join(tmp, 'hdr_with.pict')
            without = os.path.join(tmp, 'hdr_without.pict')
            convert(png, with_hdr, '--mode', '1bit')
            convert(png, without, '--mode', '1bit', '--no-header')
            a, b = open(with_hdr, 'rb').read(), open(without, 'rb').read()
            assert len(a) - len(b) == 512, f'delta {len(a) - len(b)}'
            assert a[512:] == b, 'body differs between header forms'

        def odd_width_pads_rows():
            png = make_png(tmp, 'odd.png', [
                '-size', '15x7', 'gradient:red-blue', '-colors', '6', '-depth', '8'])
            pict = os.path.join(tmp, 'odd.pict')
            convert(png, pict, '--mode', 'indexed')
            _, _, _, _, rows = parse_pict(pict)
            assert len(rows[0]) % 2 == 0, f'rowBytes {len(rows[0])} must be even'
            _, _, src = png_pixels(png)
            _, _, got = pict_pixels(pict)
            for i, (s, g) in enumerate(zip(src, got)):
                assert g == s[:3], f'pixel {i}: {g} != {s[:3]}'

        run('1-bit round-trips through the opcode stream', one_bit_roundtrips)
        run('1-bit narrow rows stored unpacked', one_bit_narrow_rows_are_unpacked)
        run('1-bit treats alpha as white', one_bit_transparent_reads_as_white)
        run('indexed packs 4-bit when it fits', indexed_4bit_roundtrips)
        run('indexed falls back to 8-bit', indexed_8bit_roundtrips)
        run('indexed keys transparency to slot 0', indexed_key_is_slot_zero)
        run('indexed spends no slot when fully opaque', indexed_opaque_keeps_every_slot)
        run('indexed agrees with ImageMagick', indexed_matches_imagemagick)
        run('--no-header drops exactly the 512-byte header', no_header_omits_512_bytes)
        run('odd widths pad to even rowBytes', odd_width_pads_rows)

    if failures:
        print(f'\n{len(failures)} PNG2PICT TEST(S) FAILED')
        return 1
    print('\nPNG2PICT TESTS PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
