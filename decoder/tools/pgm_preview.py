"""Create display-only PNGs from the decoder's raw 10-bit PGM files.

Python 3.10+, standard library only. No calibration, rotation or gap filling.
Usage: python decoder/tools/pgm_preview.py PATH/TO/STREAM --out PATH/TO/PREVIEWS
"""
import argparse
from pathlib import Path
import struct
import zlib


def read_pgm(path):
    data = Path(path).read_bytes()
    pos = 0

    def token():
        nonlocal pos
        while pos < len(data):
            if data[pos] in b" \t\r\n\v\f":
                pos += 1
            elif data[pos] == 35:
                end = data.find(b"\n", pos)
                if end < 0:
                    raise ValueError("Unterminated PGM comment")
                pos = end + 1
            else:
                break
        start = pos
        while pos < len(data) and data[pos] not in b" \t\r\n\v\f":
            pos += 1
        if pos == start:
            raise ValueError("Truncated PGM header")
        return data[start:pos]

    if token() != b"P5":
        raise ValueError("Expected binary PGM P5")
    width, height, maximum = (int(token()) for _ in range(3))
    if width != 2048 or height <= 0 or maximum != 1023:
        raise ValueError("Expected positive-height 2048-wide PGM with Maxval 1023")
    if pos >= len(data) or data[pos] not in b" \t\r\n\v\f":
        raise ValueError("Missing raster separator")
    pos += 2 if data[pos:pos+2] == b"\r\n" else 1
    raster = data[pos:]
    if len(raster) != width * height * 2:
        raise ValueError("Raster size does not match header")
    gray = bytearray()
    for (value,) in struct.iter_unpack(">H", raster):
        if value > maximum:
            raise ValueError("Sample exceeds Maxval")
        gray.append((value * 255 + maximum // 2) // maximum)
    return width, height, gray


def chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))


def preview(source, destination):
    width, height, gray = read_pgm(source)
    rows = b"".join(b"\0" + gray[y*width:(y+1)*width] for y in range(height))
    encoded = (b"\x89PNG\r\n\x1a\n"
               + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
               + chunk(b"tEXt", b"Description\0Display preview: raw 0..1023 mapped linearly to 0..255; no calibration or gap filling")
               + chunk(b"IDAT", zlib.compress(rows))
               + chunk(b"IEND", b""))
    # Exclusive creation protects earlier previews and all source files.
    with Path(destination).open("xb") as output:
        output.write(encoded)
    return width, height


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="One raw PGM or a single stream directory")
    parser.add_argument("--out", type=Path, required=True, help="PNG output directory")
    args = parser.parse_args()
    sources = sorted(args.input.glob("ch*_raw.pgm")) if args.input.is_dir() else [args.input]
    if not sources or any(not p.is_file() for p in sources):
        parser.error("No raw PGM files found")
    destinations = [args.out / (p.stem + "_preview.png") for p in sources]
    if any(p.exists() for p in destinations):
        parser.error("Preview already exists; choose a fresh output directory")
    try:
        args.out.mkdir(parents=True, exist_ok=True)
        for src, dst in zip(sources, destinations):
            width, height = preview(src, dst)
            print(f"{dst}: {width}x{height}, grayscale 8-bit, fixed raw range 0..1023")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Preview failed: {error}\n")


if __name__ == "__main__":
    main()
