"""Compare raw PGM rasters with the complete, VPC-valid M6 packet dump.
Usage: python check_avhrr_pgm.py apid_103_packets.bin output/avhrr/scid_11_vcid_9_replay_0
Standard library only. Intended for one source stream, in arrival order.
"""
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
directory = pathlib.Path(sys.argv[2])
expected = {name: [] for name in ("ch1", "ch2", "ch3a", "ch3b", "ch4", "ch5")}
offset = 0
while offset < len(data):
    if len(data) - offset < 6:
        raise ValueError("Truncated header")
    size = int.from_bytes(data[offset + 4:offset + 6], "big") + 7
    packet = data[offset:offset + size]
    if size != 12966 or len(packet) != size or packet[0] != 8 or packet[1] not in (103, 104):
        raise ValueError("Wrong packet profile")
    parity = 0
    for pair in struct.iter_unpack(">H", packet):
        parity ^= pair[0]
    if parity or packet[-3] & 3:
        raise ValueError("Invalid VPC/filler")
    bits = "".join(f"{byte:08b}" for byte in packet[20:-2])
    earth = [int(bits[i:i+10], 2) for i in range(550, 102950, 10)]
    channels = ("ch1", "ch2", "ch3a" if packet[1] == 103 else "ch3b", "ch4", "ch5")
    for slot, name in enumerate(channels):
        expected[name].extend(earth[slot::5])
    offset += size
compared = 0
for name, samples in expected.items():
    path = directory / (name + "_raw.pgm")
    if not samples:
        if path.exists():
            raise ValueError("Unexpected inactive-channel image: " + name)
        continue
    height = len(samples) // 2048
    header = f"P5\n2048 {height}\n1023\n".encode("ascii")
    pgm = path.read_bytes()
    if not pgm.startswith(header) or len(pgm) != len(header) + len(samples) * 2:
        raise ValueError("Wrong PGM geometry/header/size: " + name)
    actual = [pair[0] for pair in struct.iter_unpack(">H", pgm[len(header):])]
    if actual != samples:
        raise ValueError("Raw samples differ: " + name)
    compared += len(samples)
    print(f"{name}: 2048x{height}, {len(pgm)} bytes, range {min(actual)}..{max(actual)}, exact match")
if not compared:
    raise ValueError("No samples compared")
print(f"Compared all {compared} samples byte-for-byte as big-endian uint16.")
