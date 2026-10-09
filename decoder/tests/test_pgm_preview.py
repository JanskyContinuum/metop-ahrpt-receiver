"""Run: python -m unittest discover -s decoder/tests -p test_pgm_preview.py"""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("pgm_preview", Path(__file__).parents[1] / "tools" / "pgm_preview.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "raw.pgm"
        self.dest = Path(self.temp.name) / "preview.png"

    def write(self, values=None, header=b"P5\n2048 1\n1023\n"):
        if values is None:
            values = list(range(1024)) * 2
        data = header + b"".join(struct.pack(">H", x) for x in values)
        self.source.write_bytes(data)
        return data

    def test_png_geometry_crc_and_all_levels(self):
        original = self.write()
        self.assertEqual(module.preview(self.source, self.dest), (2048, 1))
        data = self.dest.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        pos, compressed, types = 8, b"", []
        while pos < len(data):
            n = struct.unpack(">I", data[pos:pos+4])[0]
            kind, payload = data[pos+4:pos+8], data[pos+8:pos+8+n]
            crc = struct.unpack(">I", data[pos+8+n:pos+12+n])[0]
            self.assertEqual(crc, zlib.crc32(kind+payload) & 0xffffffff)
            if kind == b"IHDR":
                self.assertEqual(struct.unpack(">IIBBBBB", payload), (2048, 1, 8, 0, 0, 0, 0))
            if kind == b"IDAT":
                compressed += payload
            types.append(kind)
            pos += n + 12
        pixels = zlib.decompress(compressed)
        expected = bytes(round(x * 255 / 1023) for x in list(range(1024))*2)
        self.assertEqual(pixels, b"\0" + expected)
        self.assertEqual(types[-1], b"IEND")
        self.assertEqual(self.source.read_bytes(), original)

    def test_two_rows_and_comments(self):
        self.write([0]*2048 + [1023]*2048, b"P5\n# raw counts\n2048 2\n1023\n")
        w, h, gray = module.read_pgm(self.source)
        self.assertEqual((w, h), (2048, 2))
        self.assertEqual(gray, bytes(2048) + bytes([255])*2048)

    def test_rejects_malformed(self):
        good = self.write()
        for data in (good[:-1], good+b"\0", good.replace(b"2048", b"2047", 1),
                     good.replace(b"1023", b"65535", 1),
                     b"P5\n2048 0\n1023\n", b"P5\n# unterminated",
                     good[:17] + b"\xff\xff" + good[19:]):
            with self.subTest(data=data[:24]):
                self.source.write_bytes(data)
                with self.assertRaises(ValueError):
                    module.preview(self.source, self.dest)
                self.assertFalse(self.dest.exists())

    def test_never_overwrites(self):
        self.write()
        self.dest.write_bytes(b"keep")
        with self.assertRaises(FileExistsError):
            module.preview(self.source, self.dest)
        self.assertEqual(self.dest.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()
