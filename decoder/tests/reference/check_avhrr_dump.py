"""Optional local capture cross-check; Python standard library only.

Usage: python check_avhrr_dump.py apid_103_packets.bin capture-channel-checks.txt
Second argument is stdout from: avhrr_payload_tests capture <dump>.
Does not infer offsets: uses the documented M7 profile and a bit-string extraction
independent of the C++ shift accumulator. Requires complete, VPC-valid packets.
"""
import pathlib
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
actual = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8-sig").splitlines()
expected = []
offset = 0
while offset < len(data):
    remaining = data[offset:]
    if len(remaining) < 6:
        raise ValueError("Truncated primary header")
    size = int.from_bytes(remaining[4:6], "big") + 7
    if size != 12966 or len(remaining) < size:
        raise ValueError("Wrong/truncated packet size")
    packet = remaining[:size]
    if packet[0] != 8 or packet[1] not in (103, 104) or packet[2] >> 6 != 3:
        raise ValueError("Wrong packet profile")
    high = low = 0
    for a, b in zip(packet[::2], packet[1::2]):
        high ^= a
        low ^= b
    if high or low or packet[-3] & 3:
        raise ValueError("VPC/filler mismatch")
    bits = "".join(f"{byte:08b}" for byte in packet[20:-2])
    earth_bits = bits[550:102950]
    assert len(earth_bits) == 102400
    words = [int(earth_bits[i:i+10], 2) for i in range(0, len(earth_bits), 10)]
    row = [str(int.from_bytes(packet[2:4], "big") & 16383)]
    channels = ("1", "2", "3A" if packet[1] == 103 else "3B", "4", "5")
    for channel, name in enumerate(channels):
        values = words[channel::5]
        assert len(values) == 2048
        fingerprint = 14695981039346656037
        for value in values:
            for byte in value.to_bytes(2, "big"):
                fingerprint = ((fingerprint ^ byte) * 1099511628211) & ((1 << 64) - 1)
        row.append(f"{name}:{min(values)}:{max(values)}:{fingerprint:x}")
    expected.append(",".join(row))
    offset += size
assert expected, "Empty dump"
assert actual == expected + [f"scans={len(expected)}"], "C++ channel comparison failed"
print(f"Matched {len(expected)} scans, {len(expected)*5} channel fingerprints, "
      f"{len(expected)*5*2048} raw Earth samples represented.")
