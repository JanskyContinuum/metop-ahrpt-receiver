# M9: raw AVHRR image output

The image writer consumes only the accepted-scan callback from M8. It does not
change payload offsets or packet acceptance. The [M7/M8 layout](avhrr-payload-layout.md)
and [M6 capture report](avhrr-packet-inspection.md) provide protocol and recording
provenance.

## Local capture result

The RS-enabled run on the same 13,538-CADU recording recovered nine valid scans
on SCID 11 / VCID 9 / replay 0. All use APID 103 (3A active).
Each PGM has header `P5\n2048 9\n1023\n`: 15 header bytes plus
`2048 * 9 * 2 = 36864` raster bytes. Every sample is stored as a big-endian
uint16 with its original ten-bit value.

| Output | Dimensions | Bytes | Raw range |
| --- | --- | ---: | --- |
| ch1_raw.pgm | 2048 x 9 | 36879 | 67..927 |
| ch2_raw.pgm | 2048 x 9 | 36879 | 51..846 |
| ch3a_raw.pgm | 2048 x 9 | 36879 | 40..690 |
| ch4_raw.pgm | 2048 x 9 | 36879 | 298..880 |
| ch5_raw.pgm | 2048 x 9 | 36879 | 262..849 |

No ch3b_raw.pgm was created. Mixed 3A/3B traffic is covered by synthetic tests:
shared channels receive every accepted scan; each channel-3 image receives
only its own active rows, with no blank substitute rows.

Independent Python extraction from the complete M6 packet dump compared
**every one of the 92,160 saved Earth counts**, including row order and channel
separation. The comparison also checks the exact header, byte order and file
length. Reproduce from the repository root after the normal Release build:

```powershell
.\build\metop_decoder.exe metop_output.cadu --out decoded-m9 --dump-debug --dump-stats
.\build\avhrr_payload_tests.exe capture decoded-m9/debug/apid_103_packets.bin
python decoder/tests/reference/check_avhrr_pgm.py decoded-m9/debug/apid_103_packets.bin decoded-m9/avhrr/scid_11_vcid_9_replay_0
```

Use an unused output directory. For a Visual Studio generator, executables are
under `build/Release/`. The optional comparison script uses only Python's standard
library; CTest does not need Python or a real recording.

## Visible limitations

A diagnostic display using a fixed 0..1023 grayscale range and expanded row
height was inspected; the raw PGM files were not transformed. The across-track
patterns differ between visible/near-IR and thermal channels. A pronounced
horizontal discontinuity occurs between output rows 2 and 3 (zero-based),
corresponding to the jump from packet sequence 3731 to 3791. Finer vertical
variation is also visible. These observations alone do not establish whether
individual structures are instrument noise or scene features.

The nine rows have packet sequences 3715, 3722, 3731, 3791, 3792, 3795, 3798,
3800 and 3802. They are sparse recovered scans, not a continuous swath. Rows
are adjacent in the file because height counts reconstructed scans, not elapsed
scan periods. The writer does not infer or insert missing lines.
`scan_rows.csv` retains the sequence/time/counter metadata and channel row indices.

The original capture had 6756 uncorrectable RS frames, as recorded in M6;
PGM output does not repair that upstream loss. Nine rows are insufficient to
claim a geographically recognizable or correctly oriented full image. No
geolocation, axis flip, calibration, histogram equalization, contrast stretch
or preview-generation feature is included.

## Validation

Release CMake/Ninja/GCC 14.2 build and the full **160/160 CTest suite** passed.
The twelve new M9 tests exercise PGM byte structure, all raw count values,
row order, invalid widths/counts, zero-height avoidance, 3A/3B switching,
source/replay isolation, output failures and CLI integration. A bad-VPC fixture
proves rejected M8 payloads never reach the image writer. Output/input collision
tests verify existing files remain intact.

Images are written into a new/empty `avhrr` directory with one subdirectory per
stream identity. Raster spools keep memory independent of pass length; final
PGM height is written after all scans arrive. Metadata is completed after its
stream's image files. An I/O failure returns a nonzero status; a failed pass's
directory should be discarded rather than treated as a complete result.
