#!/usr/bin/env python3
"""
png2pict -- write a QuickDraw PICT 2.0 from a PNG.

Why this exists (and grid2pict/ImageMagick do not suffice):

  * ImageMagick writes PICTs, but ALWAYS as an 8-bit PixMap -- `-depth 1`
    and `-monochrome` are ignored by its PICT coder.
  * pixelcraft's grid2pict writes 4-bit/8-bit indexed PixMaps.

Both are Color QuickDraw constructs. The Mac SE has a 68000 and ORIGINAL
QuickDraw: DrawPicture there understands opcode $0098 only when it carries
a BitMap (rowBytes high bit clear). A PixMap picture does not draw. So the
SE tier needs a genuine 1-bit BitMap PICT, which neither existing writer
emits -- hence `--mode 1bit` below.

Modes:
  1bit      opcode $0098 + BitMap, 1 bit/pixel, no colour table.
            Original QuickDraw safe (Mac SE, Plus, Classic).
  indexed   opcode $0098 + PixMap + colour table, 4-bit when the image has
            <= 16 colours else 8-bit. Colour QuickDraw (SE/30, LC, 6200,
            6400, G5). Half the size of ImageMagick's always-8-bit output.

PNG decoding is delegated to ImageMagick (raw RGBA out), so this file owns
no image-format code -- only PICT structure.

Usage:
  png2pict.py in.png out.pict --mode indexed [--no-header]
  png2pict.py in.png out.pict --mode 1bit --threshold 128

  --no-header   omit the 512-byte file header (resource-fork form; this is
                what scripts/embed-gfx.py would otherwise strip)
  --key RRGGBB  colour-tier transparency key (default FF00FF magenta).
                Applies only when the image HAS transparency: those pixels
                are flattened onto the key, the key takes palette slot 0,
                and pixel (0,0) is checked for it -- the colour renderer
                takes the top-left pixel's RGB as the background key (see
                CreateMaskFromGWorld in src/renderer.c). Fully opaque
                images keep all 256 slots for real colours.

Reference: Inside Macintosh: Imaging With QuickDraw, "Picture Opcodes".
All multi-byte values are big-endian.
"""
import argparse
import struct
import subprocess
import sys

MAGENTA = (0xFF, 0x00, 0xFF)


# ---- big-endian helpers -------------------------------------------------

def u8(v):
    return struct.pack('>B', v)


def u16(v):
    return struct.pack('>H', v & 0xFFFF)


def u32(v):
    return struct.pack('>I', v & 0xFFFFFFFF)


def rect(top, left, bottom, right):
    return struct.pack('>hhhh', top, left, bottom, right)


# ---- PackBits -----------------------------------------------------------

def packbits(data):
    """Apple PackBits RLE. Mirrors grid2pict.c's encoder."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        # Longest run of identical bytes at i (max 128).
        run = 1
        while i + run < n and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:
            out.append(257 - run)          # -(run-1) as a signed byte
            out.append(data[i])
            i += run
            continue
        # Otherwise a literal span, ending before the next 3-byte run.
        lit = 1
        while i + lit < n and lit < 128:
            if (i + lit + 2 < n
                    and data[i + lit] == data[i + lit + 1] == data[i + lit + 2]):
                break
            lit += 1
        out.append(lit - 1)
        out += data[i:i + lit]
        i += lit
    return bytes(out)


def pack_rows(rows, row_bytes):
    """Row data as PICT stores it: raw when rowBytes < 8, else PackBits
    with a length prefix (byte when rowBytes <= 250, else word)."""
    out = bytearray()
    for row in rows:
        if row_bytes < 8:
            out += row
            continue
        packed = packbits(row)
        out += u8(len(packed)) if row_bytes <= 250 else u16(len(packed))
        out += packed
    return bytes(out)


# ---- PNG in, via ImageMagick -------------------------------------------

def decode_png(path):
    """Returns (width, height, [ (r,g,b,a), ... ] in row-major order)."""
    try:
        dims = subprocess.run(['magick', 'identify', '-format', '%w %h', path],
                              capture_output=True, text=True, check=True).stdout
        w, h = (int(x) for x in dims.split())
        raw = subprocess.run(['magick', path, '-depth', '8', 'RGBA:-'],
                             capture_output=True, check=True).stdout
    except FileNotFoundError:
        sys.exit('error: ImageMagick (magick) not on PATH')
    except subprocess.CalledProcessError as exc:
        sys.exit(f'error: could not decode {path}: '
                 f'{exc.stderr.decode(errors="replace").strip()}')
    if len(raw) != w * h * 4:
        sys.exit(f'error: {path}: expected {w * h * 4} RGBA bytes, got {len(raw)}')
    return w, h, [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


# ---- picture assembly ---------------------------------------------------

def picture(width, height, bits_opcode_payload):
    """Wrap a $0098 payload in the standard v2 picture preamble/epilogue."""
    body = bytearray()
    body += rect(0, 0, height, width)              # picFrame
    body += u16(0x0011) + u16(0x02FF)              # VersionOp, Version 2
    body += u16(0x0C00)                            # HeaderOp
    body += u16(0xFFFE) + u16(0)                   # version -2, reserved
    body += u32(0x00480000) + u32(0x00480000)      # hRes, vRes = 72 dpi
    body += rect(0, 0, height, width) + u32(0)     # srcRect, reserved
    body += u16(0x0001) + u16(10)                  # ClipRgn, rgnSize
    body += rect(0, 0, height, width)              # rgnBBox
    body += bits_opcode_payload
    if len(body) % 2:                              # opcodes must be word-aligned
        body += b'\x00'
    body += u16(0x00FF)                            # EndOfPicture
    return u16(2 + len(body)) + bytes(body)        # picSize covers itself


def bits_1bit(width, height, pixels, threshold):
    """$0098 + BitMap. bit=1 is black; QuickDraw draws bit=0 as white,
    which is the SE sprite convention (white = transparent)."""
    row_bytes = ((width + 15) // 16) * 2           # word-aligned, high bit clear
    rows = []
    for y in range(height):
        row = bytearray(row_bytes)
        for x in range(width):
            r, g, b, a = pixels[y * width + x]
            # Transparent counts as white/background, not as ink.
            lum = 255 if a < 128 else (r * 299 + g * 587 + b * 114) // 1000
            if lum < threshold:
                row[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(row))

    payload = bytearray()
    payload += u16(0x0098) + u16(row_bytes)
    payload += rect(0, 0, height, width)           # bounds
    payload += rect(0, 0, height, width)           # srcRect
    payload += rect(0, 0, height, width)           # dstRect
    payload += u16(0)                              # mode = srcCopy
    payload += pack_rows(rows, row_bytes)
    return bytes(payload)


def bits_indexed(width, height, pixels, key):
    """$0098 + PixMap + colour table, 4-bit if it fits else 8-bit.

    When the image has transparency, it is flattened onto `key` and the key
    takes palette slot 0. A fully opaque image (splash art, backgrounds) does
    not reserve that slot -- it is drawn whole, never masked, and at 256
    colours the spare slot is the difference between fitting and not."""
    transparent = any(a < 128 for (_, _, _, a) in pixels)
    flat = [key if a < 128 else (r, g, b) for (r, g, b, a) in pixels]

    palette = [key] if transparent else []
    index_of = {key: 0} if transparent else {}
    for c in flat:
        if c not in index_of:
            index_of[c] = len(palette)
            palette.append(c)
    if len(palette) > 256:
        sys.exit(f'error: {len(palette)} colours; PICT indexed mode holds 256. '
                 'Quantize first (pixeltrue --palette).')

    if transparent and flat[0] != key:
        print(f'warning: top-left pixel is {flat[0]}, not the transparency key '
              f'{key}; the colour renderer keys transparency off it '
              '(CreateMaskFromGWorld)', file=sys.stderr)

    pixel_size = 4 if len(palette) <= 16 else 8
    if pixel_size == 4:
        row_bytes = ((width + 1) // 2 + 1) & ~1
    else:
        row_bytes = (width + 1) & ~1

    rows = []
    for y in range(height):
        row = bytearray(row_bytes)
        for x in range(width):
            idx = index_of[flat[y * width + x]]
            if pixel_size == 4:
                if x & 1:
                    row[x >> 1] |= idx & 0x0F
                else:
                    row[x >> 1] |= (idx & 0x0F) << 4
            else:
                row[x] = idx
        rows.append(bytes(row))

    payload = bytearray()
    payload += u16(0x0098) + u16(row_bytes | 0x8000)
    payload += rect(0, 0, height, width)           # bounds
    payload += u16(0) + u16(0) + u32(0)            # pmVersion, packType, packSize
    payload += u32(0x00480000) + u32(0x00480000)   # hRes, vRes
    payload += u16(0)                              # pixelType = chunky indexed
    payload += u16(pixel_size) + u16(1) + u16(pixel_size)   # pixelSize, cmpCount, cmpSize
    payload += u32(0) + u32(0) + u32(0)            # planeBytes, pmTable, pmReserved
    # Colour table. Entries are padded out to the full 2^n so QuickDraw's
    # index range always resolves.
    entries = 16 if pixel_size == 4 else 256
    payload += u32(0) + u16(0) + u16(entries - 1)  # ctSeed, ctFlags, ctSize
    for i in range(entries):
        r, g, b = palette[i] if i < len(palette) else (0, 0, 0)
        payload += u16(i) + u16(r * 257) + u16(g * 257) + u16(b * 257)
    payload += rect(0, 0, height, width)           # srcRect
    payload += rect(0, 0, height, width)           # dstRect
    payload += u16(0)                              # mode = srcCopy
    payload += pack_rows(rows, row_bytes)
    return bytes(payload)


def parse_key(text):
    text = text.lstrip('#')
    if len(text) != 6:
        raise argparse.ArgumentTypeError('key must be RRGGBB')
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def main():
    ap = argparse.ArgumentParser(description='Write a QuickDraw PICT 2.0 from a PNG.')
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('--mode', choices=('1bit', 'indexed'), required=True)
    ap.add_argument('--no-header', action='store_true',
                    help='omit the 512-byte file header (resource-fork form)')
    ap.add_argument('--threshold', type=int, default=128,
                    help='1bit mode: luminance below this becomes black (default 128)')
    ap.add_argument('--key', type=parse_key, default=MAGENTA,
                    help='indexed mode transparency key, RRGGBB (default FF00FF)')
    args = ap.parse_args()

    width, height, pixels = decode_png(args.input)
    if args.mode == '1bit':
        payload = bits_1bit(width, height, pixels, args.threshold)
    else:
        payload = bits_indexed(width, height, pixels, args.key)

    data = picture(width, height, payload)
    with open(args.output, 'wb') as f:
        if not args.no_header:
            f.write(b'\x00' * 512)
        f.write(data)


if __name__ == '__main__':
    main()
