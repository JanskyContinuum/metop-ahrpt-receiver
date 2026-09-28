# M7/M8: MetOp AVHRR HR payload layout

## Sources and verification boundary

The primary source retrieved for this milestone is **MetOp Space to Ground
Interface Specification, MO-IF-MMT-SY0001, issue 07 revision C, 29 March 2004**,
EADS Astrium. The identifier omits the final hyphen in its printed cover/header.
[Archived original PDF](https://web.archive.org/web/20160616220044id_/http://www.meteor.robonuka.ru/wp-content/uploads/2014/08/pdf_ten_eps-metop-sp2gr.pdf).
Its SHA-256 is
`391f7daef01e3f83357e3c9091db6ef97872c4977492fcef13ae9af8f5c03db3`.
Page numbers below are **printed** page numbers; add 15 for the PDF page number.
The APID and application-data tables were also checked visually.

- Sections 5.2.1.1 and 5.2.1.2, pp. 102-108: headers, CDS time, SBT and VPC.
- Table 5.2.1/2, p. 104: APID 103 selects channel 3A, 104 selects 3B.
- Section 5.2.2.2, pp. 110, 114: six ancillary bytes for NOAA instruments.
- Section 5.2.2.3.1, p. 117: one HR packet per scan, five active channels,
  field order and bit lengths; nominal rate six scans/second.
- Sections 5.2.2.4-5, pp. 130-131: AVHRR VPC and total packet size.
- [EUMETSAT TD18 v3A, table 1](https://user.eumetsat.int/s3/eup-strapi-media/TD_18_Metop_Direct_Readout_AHRPT_Technical_Description_v3_A_1cb789b653.pdf)
  confirms the HR VCID/APID assignment for the AHRPT stream.

The space-to-ground document gives the field boundaries but does not enumerate
each sample's channel index. For the contiguous MSB-first ten-bit word packing
and within-field channel interleave, the behavioural cross-check is
[SatDump's AVHRR reader](https://github.com/SatDump/SatDump/blob/f3d82adbfe04e57c596b93479d687f4b830ee26c/plugins/noaa_metop_support/instruments/avhrr/avhrr_reader.cpp),
pinned at `f3d82adbfe04e57c596b93479d687f4b830ee26c`, specifically
`work_metop` and `line2image`, together with
[`repackBytesTo10bits`](https://github.com/SatDump/SatDump/blob/f3d82adbfe04e57c596b93479d687f4b830ee26c/src-core/common/repack.cpp).
They corroborate application-data offset 14
relative to the Packet Data Field, Earth start word 55, five interleaved
channels, and 2048 pixels. Its calibration handling corroborates space and
back-scan stride five. This is source-behaviour comparison, not a run of the
full SatDump application. No GPL implementation is copied or linked.

The separate **MO-IC-MMT-AH-0001** was not retrieved. The verified
space-to-ground specification plus the explicitly identified behavioural
reference establish the raw-count profile implemented here. Detailed
radiometric meanings of the ramp, temperature and back-scan words are not
inferred. A future calibration implementation still needs the instrument ICD,
chapter 3.3 (referenced by the space-to-ground document as RD27), and applicable
calibration documentation. The downloaded PDFs and capture are not redistributed.

## Byte layout

Offsets are zero-based from the beginning of the **complete Space Packet**.
All multibyte header/time fields are most significant byte first.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 6 | CCSDS primary header; version 0, telemetry, secondary header present, unsegmented |
| 6 | 2 | UTC days since 2000-01-01 |
| 8 | 4 | UTC milliseconds in day |
| 12 | 2 | UTC microseconds within current millisecond |
| 14 | 1 | Reserved SBT byte, zero |
| 15 | 3 | SBT coarse seconds |
| 18 | 2 | SBT fractional seconds, units of 2^-16 seconds |
| 20 | 12944 | AVHRR HR application data |
| 12964 | 2 | VPC |

Thus total size is 12966, Packet Data Field size 12960, and CCSDS length field
12959. The application data begins 14 bytes into the Packet Data Field.
The VPC equals the XOR of all preceding two-byte pairs, including the primary
header. Equivalently, XOR over the whole packet including VPC must be zero.
It is not a CRC, and it is not a guarantee against every corruption pattern.

## Derived application-data bit layout

Bit zero below is the MSB of application-data byte zero (whole-packet byte 20).
The offsets follow by summing the documented field lengths.

| Start bit | Length bits | Ten-bit word indices | Raw content |
| ---: | ---: | --- | --- |
| 0 | 500 | 0-49 | Space view, ten samples in each of five channel slots |
| 500 | 50 | 50-54 | Ramp calibration words |
| 550 | 102400 | 55-10294 | Earth view |
| 102950 | 50 | 10295-10299 | IR target-temperature telemetry |
| 103000 | 50 | 10300-10304 | Patch-temperature telemetry |
| 103050 | 500 | 10305-10354 | Back scan, ten samples in each channel slot |
| 103550 | 2 | none | Zero filler |

The accounting is exact:
`500 + 50 + 102400 + 50 + 50 + 500 + 2 = 12944 * 8`.
Earth sample `x` of slot `c` is word `55 + 5*x + c`,
where `0 <= x < 2048` and `0 <= c < 5`.
Slots are `1, 2, 3A-or-3B, 4, 5`. The inactive channel 3 is absent.
The last Earth sample ends at bit 102949; temperature telemetry starts at the
next bit. No byte alignment, cropping, padding, rescaling or interpolation
is inserted at this boundary.

Space, Earth and back-scan counts are stored separately in `uint16_t`.
Ramp and temperature groups remain raw wire-order words. Calling the entire
back-scan field an internal-target radiometric measurement for every channel
would go beyond the verified raw layout; this milestone performs no calibration.

## Validation and output contract

`decode_avhrr_packet` returns a complete `AvhrrScan` or an explicit rejection.
It requires the exact documented packet size and header profile, correct VPC,
valid time-field ranges, zero SBT reserved byte and zero filler. The raw time
parser permits the positive leap second but does not verify historical leap
dates or convert the SBT epoch. Packet sequence counts remain packet metadata.

`AvhrrScanProcessor` consumes reconstructed VCID-9 packets and provides a
synchronous callback with the scan and source spacecraft/VCID/replay/counter
metadata. One accepted packet produces one scan. It never stitches across
spacecraft identities, fills missing scans, or creates an inactive channel.
There is no persistent scan accumulation. The CLI writes
`avhrr_scan_log.csv` and `stats.json.avhrr_scans`; raw arrays are available to
library callers for the later image-writer milestone. Images are not written.

Malformed packets are retained by the independent M6 inspector/dump and are
reported as rejected by the payload stage. RS bypass remains labelled
`not_applied`; VPC is still required. Existing CLI exit-code semantics remain:
recoverable payload rejections are logged rather than terminating the pass.

## Capture comparison

The M6 RS-enabled dump contains nine complete APID-103 packets and no APID 104.
All nine have the documented size, a zero VPC syndrome, and zero filler.
See [M6 observations](avhrr-packet-inspection.md) for capture hash and sequence
numbers. The time fields of the first packet decode to day 9707,
millisecond 31556567 and microsecond 716; its SBT is `0x14130d:e01a`.

## Executed M8 checks

Release CMake/Ninja/GCC 14.2 build and the complete CTest suite passed:
**148/148**. The synthetic tests compare every Earth and auxiliary sample,
not just array sizes. The reassembly test delivers both modes across 30 VCDUs;
the CLI partial test stops before the first full scan and emits no scan.

The RS-enabled CLI accepted all nine M6 packets: nine 3A scans, no payload
rejections. RS bypass produced 13 candidates, all rejected by VPC. Frame/packet
losses described in M6 still apply; nine recovered scans are not a continuous
image or evidence of a lossless recording.

An independent Python bit-string extraction matched the C++ per-channel
min/max and FNV-1a fingerprints for **45 channels / 92160 Earth samples**.
Fingerprints cover each count as an unscaled big-endian uint16. This checks the
bit unpacker/channel splitting independently, but is not an independent
instrument calibration or full SatDump run.

For the first scan (packet sequence 3715):

| Channel | Raw minimum | Raw maximum | FNV-1a 64-bit |
| --- | ---: | ---: | --- |
| 1 | 70 | 696 | 70e96ef8e9594224 |
| 2 | 53 | 651 | d2a823741faefcda |
| 3A | 42 | 620 | f2a753ad599ca851 |
| 4 | 306 | 871 | 040985acb2eec335 |
| 5 | 275 | 839 | ffd4dc75d6a425d4 |

Reproduce after building (use `.exe` on Windows; multi-configuration generators
place executables under `Release/`):

```sh
./build/metop_decoder metop_output.cadu --out decoded-m8 --dump-debug --dump-stats
./build/avhrr_payload_tests capture decoded-m8/debug/apid_103_packets.bin > channel-checks.txt
python decoder/tests/reference/check_avhrr_dump.py decoded-m8/debug/apid_103_packets.bin channel-checks.txt
```

The optional Python checker uses only the standard library and is not a CTest
dependency. The public suite uses synthetic packets because no recording is
committed. Real 3B traffic and detailed radiometric interpretation remain
unverified; neither is claimed from this APID-103-only capture.
