# M6: observations from the local MetOp capture

This milestone inspects complete CCSDS Space Packets only. No instrument
application-data offsets, sample packing, time interpretation, calibration,
scan reconstruction or images are implemented.

## Reproduce

From the repository root, after the documented Release build:

```powershell
.\build\metop_decoder.exe metop_output.cadu --out decoded --dump-debug --inspect-packets 20 --dump-stats
```

The inspected local recording is 13,862,912 bytes / 13,538 aligned CADUs.
Its SHA-256 is
`721f11fb2c362068fe1ff44c1d1d98f053f7d8d7b6608e54129497ccbd4b116b`.
Recordings and binary dumps are ignored by Git. This report records small
observations, not a committed recording or an image-decoding claim.

For comparison, run again with `--no-rs` into a **different** output directory.
The following observations were made with RS enabled.

## Observed structure

| Field | Observed value |
| --- | --- |
| Reconstructed packets on all VCIDs | 141 |
| Selected VCID / spacecraft / replay | 9 / 11 / 0 |
| APID 103 count | 9 |
| APID 104 count | 0 |
| Total packet-size histogram | 12966 bytes: 9 |
| CCSDS Packet Data Length field | 12959 |
| Packet Data Field size | 12960 bytes = length field + 1 |
| Primary header | 6 bytes |
| Packet type | 0, telemetry, for all nine packets |
| Sequence flags | 3 (binary 11), for all nine packets |
| Secondary-header flag | 1, for all nine packets |
| APID 103 dump | 116694 bytes = 9 complete packets |
| APID 104 dump | 0 bytes (created empty with --dump-debug) |

The Packet Data Field is what the inspector calls **payload**. It starts
immediately after the six-byte primary header and includes any secondary
header. Its internal subdivision is deliberately not guessed.

| Global packet index | Sequence count | Start VCDU counter | End VCDU counter |
| ---: | ---: | ---: | ---: |
| 23 | 3715 | 16988 | 17002 |
| 36 | 3722 | 17091 | 17105 |
| 43 | 3731 | 17223 | 17238 |
| 91 | 3791 | 18105 | 18120 |
| 95 | 3792 | 18120 | 18134 |
| 101 | 3795 | 18164 | 18178 |
| 110 | 3798 | 18208 | 18223 |
| 115 | 3800 | 18237 | 18252 |
| 124 | 3802 | 18267 | 18281 |

These packets span 15 or 16 consecutive VCID-9 VCDU counters. The sequence
values are sparse; they are not converted into a count of missing scans.
In particular, flags 11 describe unsegmented **application data**, not
containment in one M-PDU. Multi-VCDU reconstruction is still necessary.

For the first selected packet (sequence 3715), the first 64 payload bytes are:

```text
25EB01E183D702CC0014130DE01A0902
6097E0F80270A025F83E00AC2909BE0F
802709C28F83E008825097E0F84280A0
24F83E00A82909BE1F802709C26F83E0
```

The final 16 payload bytes are:

```text
6C02F0AC267A5B00A0280A1E96C0CFEC
```

These are uninterpreted bytes. In particular, the final bytes are not labelled
a checksum or stripped from the dump.

## Comparison with references

[EUMETSAT TD18, issue 3A, table 1, page 11](https://user.eumetsat.int/s3/eup-strapi-media/TD_18_Metop_Direct_Readout_AHRPT_Technical_Description_v3_A_1cb789b653.pdf)
assigns AVHRR/3 High Rate to VCID 9 and APIDs 103/104. This agrees with the
selection and the observed APID 103 traffic. TD18 does not establish the
internal AVHRR sample offsets.

[CCSDS 133.0-B-2, sections 4.1.3–4.1.4](https://ccsds.org/Pubs/133x0b2e2.pdf)
defines the six-byte primary header, length-field-plus-one semantics,
sequence flags, and inclusion of a secondary header in the Packet Data Field.
The observed lengths and flags are consistent with those definitions.

A source-level behavioural comparison used SatDump revision
`f3d82adbfe04e57c596b93479d687f4b830ee26c`:
its [MetOp instrument dispatcher](https://github.com/SatDump/SatDump/blob/f3d82adbfe04e57c596b93479d687f4b830ee26c/plugins/noaa_metop_support/metop/module_metop_instruments.cpp)
selects VCID 9 and APIDs 103/104. Its
[AVHRR reader](https://github.com/SatDump/SatDump/blob/f3d82adbfe04e57c596b93479d687f4b830ee26c/plugins/noaa_metop_support/instruments/avhrr/avhrr_reader.cpp)
rejects MetOp payloads shorter than 12960 bytes. The observed 12960-byte data
fields meet that threshold; it is not an equality requirement imposed by our
inspector. The [AOS demultiplexer](https://github.com/SatDump/SatDump/blob/f3d82adbfe04e57c596b93479d687f4b830ee26c/src-core/common/ccsds/ccsds_aos/demuxer.cpp)
stores primary-header bytes separately from payload.

SatDump was **not executed on this capture** for M6. This is a comparison with
its source behaviour, not an independent end-to-end decode. No GPL source was
copied, linked, or used to choose implemented application-data offsets.

## Losses and remaining questions

RS classified 32 frames as good, corrected 6750 and rejected 6756.
The parser received 6782 frames, including 1402 version-1 VCID-9 frames.
The reassembler reported 465 discarded partial candidates across all streams.
A rejected frame clears all partials because its identity is untrusted.

The bypass run emitted 13 APID-103 candidates, also 12966 bytes each, and no
APID 104. It has different sequence values and unchecked payloads. A larger
bypass count is not evidence of better data. Neither run proves APID 104 was
absent from the transmitted pass; neither validates a channel-3 mode.

Before M7/M8, verify the following in the **AVHRR/3 Instrument ICD
MO-IC-MMT-AH-0001** and **MetOp Space-to-Ground Interface Specification
MO-IF-MMT-SY-0001**, with an applicable issue/revision:

- Meaning of APID 103 versus 104 and its relationship, if any, to channel 3A/3B.
- Secondary-header length, time fields, instrument/status fields and exact
  application-data boundary.
- Packet-to-scan mapping, scan identifiers and any application segmentation.
- Exact 10-bit sample order and positions of Earth, space-view, calibration
  and telemetry words; identify the 2048 Earth samples without padding/cropping.
- Any packet error-control field: location, coverage and validation algorithm.

Those two detailed interface documents were not available in the local
materials inspected for M6; the accessible TD18 and source comparison do not
replace their verified payload-layout requirements. M6 stops here.
