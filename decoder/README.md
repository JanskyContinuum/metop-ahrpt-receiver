# C++ CADU decoder — Milestone 2

M1 provides CADU framing inspection; M2 adds the **CCSDS derandomizer and header
diagnostics**. The decoder validates the `1A CF FC 1D` ASM, recovers byte alignment,
and XORs all 1020 following bytes. RS correction, full VCDU/M-PDU parsing, packets,
and images are not yet implemented. Output explicitly reports `rs_applied: false`.

MATLAB/Simulink performs the existing physical-layer processing, including QPSK
demodulation, Viterbi decoding, and CADU alignment. Its binary output is the input
to this C++ program. No MATLAB installation is needed to build or run the decoder.

## Build and test

Requirements: CMake 3.20 or newer and a C++20 compiler. Tests use CTest, CMake,
and the C++ standard library; there are no downloaded test dependencies.
Run from the repository root:

```sh
cmake -S decoder -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For Ninja, add `-G Ninja` to the configure command. On Windows, run from a
configured compiler shell. If using the Code::Blocks MinGW bundle, its `MinGW/bin`
directory supplies GCC, CMake, Ninja, and the matching compiler runtime DLLs;
put that directory first on the shell's PATH. Avoid mixing MinGW runtime versions.
Very long Windows checkout paths can cause CMake object-path warnings; prefer a
short build directory if your toolchain cannot build at the existing path.

## Run

```sh
./build/metop_decoder metop_output.cadu --out decoded --dump-stats
```

Windows Ninja builds produce `build/metop_decoder.exe`. Visual Studio
multi-configuration builds normally produce `build/Release/metop_decoder.exe`.

```powershell
.\build\metop_decoder.exe metop_output.cadu --out decoded --dump-stats
.\build\metop_decoder.exe metop_output.cadu --out decoded --max-cadus 100 --verbose
```

| Option | M1 behaviour |
| --- | --- |
| `--out DIR` | Required; create DIR and write `stats.txt` and `stats.json`. Existing statistics files are replaced. |
| `--dump-stats` | Print the full framing statistics instead of the short summary. |
| `--max-cadus N` | Stop after N accepted CADUs; N must be a positive 64-bit integer. |
| `--verbose` | Write each accepted CADU's zero-based ordinal and original byte offset to stderr. |
| `--help` | Show usage without opening input/output files. |

Quote paths containing spaces. Windows uses its native wide command line so
non-ASCII paths work without changing the system code page. Arguments on other
platforms are interpreted as UTF-8. The input must be a regular, static binary file.
The input file is never modified; the reader retains the entire CADU including
the ASM. M2 derandomizes its in-memory copy; the ASM is preserved.

Exit codes:

- **0:** at least one complete CADU accepted (or help displayed). Recoverable
  framing anomalies are reported to stderr and in statistics, without stopping
  the entire run.
- **1:** no accepted CADUs, or an input/output failure. Empty, garbage-only, and
  truncated-only files still produce statistics when output is writable.
- **2:** invalid command-line arguments.

## Framing and recovery contract

A CADU is 1024 bytes: 4 ASM bytes followed by 1020 opaque bytes. In normal aligned
mode, a complete block with the exact ASM is accepted. This permits a single
aligned CADU at offset zero and the final aligned CADU without a following marker.

When the expected ASM is absent, the reader scans byte-by-byte. A recovery
candidate must have a complete 1024-byte CADU **and another ASM exactly 1024 bytes
later**. A candidate followed by a mismatching marker is rejected. A complete
candidate at EOF without enough following bytes for confirmation is not emitted;
it is reported as an unconfirmed candidate and included in the trailing suffix.
The confirming successor need not itself be complete: a truncated successor is
reported as trailing bytes on the next read.

This conservative recovery can omit an otherwise good last CADU after loss of
alignment. It avoids silently accepting an isolated payload marker as a recovered
boundary. Even two matching markers cannot establish payload integrity; these
milestones cannot detect or correct payload errors. RS validation is later work.

The reader uses a fixed 1028-byte circular lookahead buffer and returns one CADU
at a time. Memory use does not grow with capture size. Tests compare every emitted
byte and its original offset, including recovery across circular-buffer wraps.

## Statistics definitions

The JSON declares `schema_version: 2`, `stage: "derandomization_diagnostics"`, and
`rs_applied: false`. CADU counters retain their M1 meanings. Sparse histograms
`vcids_before`, `vcids_after`, `versions_after`, and `spacecraft_after` contain
observations from every accepted CADU; no unexpected values are filtered out.
They are diagnostic bit extractions, not RS-validated headers. APID and image
statistics remain absent.

| Field | Meaning |
| --- | --- |
| `input_size` | File size in bytes before decoding. |
| `cadu.cadus_read`, `cadu.valid_asm` | Complete CADUs emitted with valid boundary markers; equal in M1. |
| `cadu.asm_failures` | Episodes where an expected boundary is absent. This is not a lost-CADU estimate. |
| `cadu.resyncs` | Successful recoveries confirmed by the next ASM. |
| `cadu.skipped_bytes` | Bytes discarded during the byte-by-byte search. |
| `cadu.trailing_bytes` | Unaccepted EOF suffix: insufficient bytes for a complete CADU or recovery confirmation. It may include garbage. |
| `cadu.rejected_candidates` | Candidate ASMs whose next marker position does not match. |
| `cadu.unconfirmed_candidates` | Complete recovery candidates without enough next-marker bytes at EOF. |
| `cadu.bytes_consumed` | Emitted, skipped, and trailing bytes; excludes unread lookahead. |
| `cadu.reached_eof` | All input was classified, including the trailing suffix. |
| `stopped_by_limit` | Processing stopped after the requested number of accepted CADUs. |

On a complete run:

```text
input_size = bytes_consumed
           = 1024 * cadus_read + skipped_bytes + trailing_bytes
```

A limit counts accepted CADUs, not candidate markers or physical reads. Recovery
may require lookahead beyond the limit's last accepted CADU. The remaining input
is not classified as trailing data. Reaching the limit exactly at the physical end
still reports `stopped_by_limit: true`, `reached_eof: false`: no EOF read was made.

## Test coverage

The reader tests cover empty/single/multiple CADUs, binary payload preservation,
short input, trailing data, leading garbage, damaged markers, inserted/deleted
bytes, false and unconfirmed recovery markers, multiple recovery episodes,
truncated successors, and stream I/O failures. EOF calls must be idempotent and
complete runs must conserve bytes.

CLI integration tests launch the real executable on synthetic files. They check
exit codes, help, argument rejection, limits, recovery, JSON parsing and counters,
text/JSON consistency, verbose offsets, output failures, and input preservation.
Tests remain active in Release builds; they do not rely on `assert`.

All fixtures are synthetic and generated in memory or under the ignored build
directory. The local `metop_output.cadu` is not required by CTest and must not be
committed. Its initial independently inspected framing baseline is 13,862,912
bytes, 13,538 aligned CADUs, and zero trailing bytes.

### M1 validation result

On Windows, the Release build using GCC 14.2.0 (MinGW-w64), CMake 3.31.5, and
Ninja 1.12.1 passed all **37 CTest cases** (15 reader tests and 22 CLI integration
tests). The Unicode-path regression includes Polish characters in both input and
output names. Tests were run outside the agent sandbox after sandboxed CTest
subprocesses encountered Windows loader errors; direct execution and the final
unsandboxed suite succeeded.

Running the executable on the local capture reproduced the framing baseline:

```text
Input size:       13862912 bytes
CADUs read:       13538
Valid ASM:        13538
ASM failures:     0
Resyncs:          0
Skipped bytes:    0
Trailing bytes:   0
Bytes consumed:   13862912
Reached EOF:      yes
Stopped by limit: no
```

These results establish M1 framing behaviour, not the integrity or meaning of the
1020 bytes after each ASM. No M2 functionality was used for this validation.

## M2 randomizer and validation

The implementation generates a constant XOR mask from the legacy CCSDS
polynomial `x^8 + x^7 + x^5 + x^3 + 1`. State bit j holds sequence bit s[n+j];
the emitted bit is s[n], and the feedback is s[n+7] XOR s[n+5] XOR s[n+3] XOR s[n].
Output is packed MSB first. Every call applies the mask from its all-ones origin,
including all 128 parity bytes and excluding the ASM.

Reference: CCSDS 131.0-B-5, sections 10.4.2–10.4.4, which retain this 255-bit
sequence for legacy systems. We deliberately use the MetOp-specified legacy
sequence, not the newer 17-bit randomizer in the same standard.

All **42 tests passed** in the Windows Release build. Five new tests check the
standard `FF 48 0E C0 9A` prefix, XOR round-trip, independent calls, 255-bit period
through the full 1020 bytes, and protected ASM/end boundaries. CLI fixtures use a
fixed independently specified randomized header prefix and verify all diagnostic
counts. A full CVCDU is 32 PN periods long, so the reset test alone cannot detect
continuous-state code; the implementation avoids mutable PN state entirely.

Local capture observations after XOR (13,538 CADUs, framing unchanged):

| Diagnostic | Count |
| --- | ---: |
| Version 1 | 13,418 |
| Other versions | 120 |
| Spacecraft ID 11 | 13,096 |
| VCID 9 | 2,685 |
| VCID 10 | 6,259 |
| VCID 24 | 1,705 |
| VCID 63 | 1,921 |

The dominant version/spacecraft and expected VCIDs support the chosen randomizer
phase. Outliers remain recorded and uncorrected. XOR maps each VCID to another
VCID, so the VCID histogram's shape alone cannot prove correct randomization.
Version and spacecraft diagnostics provide additional evidence. If version 1
or VCID 9 is absent, stderr requests investigation before packet work; M2 still
saves diagnostics, without claiming instrument decoding succeeded.

## Requirements and subsequent work

The current engineering specification is `../CODEX_METOP_CADU_AVHRR_DECODER.md`
relative to the repository root (outside this Git repository), sections 2–5,
23, and milestone 1 in section 27. The CADU/ASM requirements used here come from
that specification. Protocol references for subsequent stages include
[EUMETSAT TD18](https://user.eumetsat.int/s3/eup-strapi-media/TD_18_Metop_Direct_Readout_AHRPT_Technical_Description_v3_A_1cb789b653.pdf)
and [CCSDS TM Synchronization and Channel Coding](https://ccsds.org/Pubs/131x0b5.pdf).

M3 will add full VCDU/M-PDU parsing in explicit `--no-rs` development mode.
Packet reassembly, RS correction, and AVHRR inspection follow separately.
AVHRR sample offsets and scan-to-packet mapping must be verified before instrument
decoding. The eventual raw image width is exactly 2048 Earth-view samples.
