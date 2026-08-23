#!/usr/bin/env python3
"""Turn the PPMs render_frames writes into the PNGs the docs commit.

    python3 scripts/frames-to-png.py build-tests/frames docs/frames reflex judge night

With no names it converts every .ppm it finds. Palette PNGs when the image has 256
colours or fewer, which is every Pip frame: flat RGB565 fills, no gradients, so a
640x480 screen lands in a few kilobytes instead of a megabyte. Standard library only,
no Pillow, so CI and a bare Mac both run it.
"""
import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path):
    data = path.read_bytes()
    fields, pos = [], 0
    while len(fields) < 4:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while pos < len(data) and data[pos] != 0x0A:
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    magic, w, h, maxval = fields[0], int(fields[1]), int(fields[2]), int(fields[3])
    if magic != b"P6" or maxval != 255:
        raise SystemExit(f"{path}: only binary P6 with maxval 255 is handled")
    return w, h, data[pos + 1 : pos + 1 + w * h * 3]


def chunk(tag, payload):
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def write_png(path, w, h, rgb):
    colours = sorted({rgb[i : i + 3] for i in range(0, len(rgb), 3)})
    if len(colours) <= 256:
        index = {c: i for i, c in enumerate(colours)}
        rows = bytearray()
        for y in range(h):
            rows.append(0)  # filter 0: these images have hard edges, so None beats Sub
            base = y * w * 3
            for x in range(w):
                rows.append(index[rgb[base + x * 3 : base + x * 3 + 3]])
        head = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
        head += chunk(b"PLTE", b"".join(colours))
    else:
        rows = bytearray()
        for y in range(h):
            rows.append(0)
            rows += rgb[y * w * 3 : (y + 1) * w * 3]
        head = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    body = zlib.compress(bytes(rows), 9)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + head + chunk(b"IDAT", body) + chunk(b"IEND", b""))
    return len(colours)


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    src, dst = Path(argv[1]), Path(argv[2])
    names = argv[3:]
    dst.mkdir(parents=True, exist_ok=True)
    ppms = [src / f"{n}.ppm" for n in names] if names else sorted(src.glob("*.ppm"))
    if not ppms:
        raise SystemExit(f"no PPMs in {src}")
    for ppm in ppms:
        w, h, rgb = read_ppm(ppm)
        out = dst / (ppm.stem + ".png")
        colours = write_png(out, w, h, rgb)
        print(f"{out}  {w}x{h}  {colours} colours  {out.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main(sys.argv)
