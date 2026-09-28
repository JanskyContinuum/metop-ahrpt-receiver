# C++ CADU decoder — Milestones 7 and 8

M1 provides CADU framing inspection; M2 adds the **CCSDS derandomizer and header
diagnostics**. The decoder validates the `1A CF FC 1D` ASM, recovers byte alignment,
and XORs all 1020 following bytes. M3 adds VCDU/M-PDU inspection; M4 adds
Space Packet reassembly across VCDUs. **M5 applies CCSDS RS(255,223), interleave 4,
with dual-basis conversion before parsing packets by default.** Uncorrectable
frames are reported and rejected. `--no-rs` retains the uncorrected comparison
path. M6 inspects VCID 9 / APID 103–104 packets, reports lengths and header
fields, and can dump complete packets. M7 documents the verified AVHRR HR layout;
M8 validates VPC and reconstructs raw ten-bit scans with 2048 Earth samples per
active channel. Image rendering is not implemented.

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

## Continuous integration (CI)

[Decoder CI](https://github.com/JanskyContinuum/metop-ahrpt-receiver/actions/workflows/decoder-ci.yml)
(`.github/workflows/decoder-ci.yml`) runs on every pull request targeting `main`
(including subsequent commits) and every push to `main`. It checks out the
repository on Windows Server 2022, configures `decoder/`, builds all targets in
Release, and runs the complete CTest suite with failure output. It uses the
commands above with `-DBUILD_TESTING=ON` at configuration and `--no-tests=error`
at test time so an accidentally empty test suite fails. No recordings or MATLAB
installation are required.

Open or update a pull request into `main`, then open its **Checks** tab and the
**Windows Release** job. A green check means build and tests passed; for a red
check, open the failed step to read the compiler or CTest output, fix the issue,
and push another commit to the same branch. Runs are also listed in **Actions**.
CI does not merge pull requests or by itself make passing checks mandatory for
merging; requiring the check is a separate branch-protection setting.

## Run

```sh
./build/metop_decoder metop_output.cadu --out decoded --dump-stats
```

Windows Ninja builds produce `build/metop_decoder.exe`. Visual Studio
multi-configuration builds normally produce `build/Release/metop_decoder.exe`.

```powershell
.\build\metop_decoder.exe metop_output.cadu --out decoded --dump-stats
.\build\metop_decoder.exe metop_output.cadu --out decoded --max-cadus 100 --verbose
.\build\metop_decoder.exe metop_output.cadu --out decoded --no-rs --dump-stats
```

| Option | Implemented behaviour |
| --- | --- |
| `--out DIR` | Required; create DIR and write statistics plus RS, VCDU and packet CSV logs. Existing output files are replaced. |
| `--dump-stats` | Print full statistics for the selected mode instead of the short summary. |
| `--max-cadus N` | Stop after N accepted CADUs; N must be a positive 64-bit integer. |
| `--verbose` | Write each accepted CADU's zero-based ordinal and original byte offset to stderr. |
| `--no-rs` | Bypass RS and parse uncorrected VCDUs/packets. All logs explicitly label RS as `not_applied`. |
| `--dump-debug` | Write complete selected packets to `DIR/debug/apid_103_packets.bin` and `apid_104_packets.bin`; print the selected payload previews. |
| `--inspect-packets N` | Include first 64 / last 16 payload bytes for the first N selected packets across both APIDs in `DIR/debug/packet_log.csv`. Default 20; zero disables previews, not counts or dumps. |
| `--help` | Show usage without opening input/output files. |

Quote paths containing spaces. Windows uses its native wide command line so
non-ASCII paths work without changing the system code page. Arguments on other
platforms are interpreted as UTF-8. The input must be a regular, static binary file.
The input file is never modified; the reader retains the entire CADU including
the ASM. M2 derandomizes its in-memory copy; the ASM is preserved.

Exit codes:

- **0:** at least one complete CADU accepted and at least one
  M-PDU with a valid FHP (or help displayed). Recoverable anomalies are reported
  to stderr and in statistics, without stopping the entire run.
- **1:** no accepted CADUs, no M-PDUs with valid FHPs (including all-RS-rejected input), or an
  input/output failure. Empty, garbage-only, and
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
boundary. Matching markers alone do not establish payload integrity. M5 checks
RS parity after framing and derandomization; the bypass mode does not.

The reader uses a fixed 1028-byte circular lookahead buffer and returns one CADU
at a time. Memory use does not grow with capture size. Tests compare every emitted
byte and its original offset, including recovery across circular-buffer wraps.

## Statistics definitions

The JSON declares `schema_version: 8`, `stage: "space_packets_rs"` and
`rs_applied: true` by default; `--no-rs` uses `"space_packets_no_rs"` and false.
CADU counters retain their M1 meanings. Sparse histograms
`vcids_before`, `vcids_after`, `versions_after`, and `spacecraft_after` contain
observations from every accepted CADU; no unexpected values are filtered out.
They are diagnostic bit extractions, not RS-validated headers. `frames`,
`packets` and `avhrr` are present in both modes; `rs` is present only when
correction is enabled. `avhrr_scans` reports payload acceptance/rejection and
channel-3 mode counts; image statistics remain absent.

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

Fixtures are synthetic. Small independent RS reference vectors are checked in
with provenance; generated recordings stay under the ignored build directory. The local `metop_output.cadu` is not required by CTest and must not be
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

## M3 VCDU/M-PDU inspection

After derandomizing all 1020 CVCDU bytes, `--no-rs` takes exactly bytes 0–891 as
the uncorrected VCDU and bypasses parity bytes 892–1019. There is no dummy RS
decoder or claim that parity was checked. Separate modules implement `vcdu`,
`mpdu`, and `frame_inspector`; the main program orchestrates them.

The parsers require exact input lengths before reading offsets:

```text
VCDU: 892 bytes = primary header 6 + insert zone 2 + M-PDU 884
M-PDU: 884 bytes = header 2 + packet zone 882
```

The VCDU parser uses masks/shifts for the version, spacecraft ID, VCID, 24-bit
counter, replay flag, and remaining seven signaling bits. The latter are
preserved under the MetOp profile in the engineering specification. The newer
AOS count-cycle interpretation is not silently substituted for those bits.
Both insert-zone bytes are copied into the parsed object and logged.

The inspector rejects versions other than binary `01` before M-PDU inspection.
VCID 63 is an AOS Only Idle Data frame: it is counted and logged without M-PDU
or counter processing. Other VCIDs use the M-PDU profile in the specification.

The FHP is masked to **11 bits**; its offset origin is the packet zone, not the
VCDU or M-PDU header. The parser distinguishes:

- `0..881`: the first new packet starts at that packet-zone byte.
- `0x7FE`: idle-only zone, distinct from an idle VCID63 frame.
- `0x7FF`: no new packet starts; continuation only.
- `882..2045`: invalid FHP; report it without cropping or inventing a boundary.

The M3 frame inspector does not interpret packet headers. FHP 881 is valid even
though only one byte of the new packet's header fits; M4 retains split headers.

Spare bits are preserved and counted separately. The AOS reference convention is
zero for the five M-PDU spare bits, but the actual capture frequently carries
`FF FF`, meaning spare=31 and FHP=2047. The engineering specification defines the
low 11-bit pointer without a required spare value. We therefore **do not reject
an otherwise in-range FHP for nonzero spare bits**. A mission-specific spare-bit
conformance rule still needs verification; these counts are not RS-validated
conformance results. `valid_mpdus` means correct geometry and a valid FHP only.

### Counter continuity and CSV

Continuity state is independent per `(spacecraft ID, VCID, replay)`, so spacecraft
or replay changes cannot join unrelated VC streams. The 24-bit wrap from
`0xFFFFFF` to zero is contiguous. Equal counters are duplicates. Forward modular
steps 2 through `0x7FFFFF` are gaps; larger steps are `backward_or_reset`. After
an observation, its counter becomes the new baseline. This half-range convention
is a diagnostic choice; it cannot distinguish corruption, missing frames, and
resets. No missing-packet or lost-scan count is invented.

`vcdu_log.csv` is always written in `--no-rs` mode and replaced on reruns. It has
one row per accepted CADU, including invalid versions and idle frames. Columns:

```text
cadu_index,file_offset,rs_status,version,spacecraft_id,vcid,counter,replay,
signaling_spare,insert0,insert1,previous_counter,continuity,fhp,mpdu_spare,fhp_kind,status
```

All numeric fields are decimal. `rs_status` is always `not_applied`. Unchecked
FHP and previous-counter fields are empty, never fabricated zeros; the raw
header counter is retained even when its continuity is not checked. Insert bytes remain present even
on an invalid-version row. Successful structural parsing is labelled
`uncorrected`; invalid pointers are labelled `invalid_mpdu`.

The JSON `frames` counters count parsed headers, invalid versions, idle frames,
nonzero spare fields, valid/invalid FHPs, each zone kind, counter gaps, duplicates,
and backward/reset events. `frames.vcids` includes only version-1 headers;
`frames.discontinuities_by_vcid` aggregates counter events across spacecraft and
replay streams while keeping tracking state separate. Thus it differs from the
unfiltered M2 diagnostic histogram. Anomalies cause a stderr notice; detailed
evidence stays in CSV/JSON. Packet-state invalidation is implemented separately by the M4 reassembler.

### M3 validation result

The Windows Release build passed **61/61 CTest cases**: the existing 42 plus 14
parser/counter/inspector tests and five `--no-rs` CLI regressions. New tests cover
exact header fields, borrowed-view bounds, incorrect lengths, FHP 0/100/881/
882/2045/2046/2047, 24-bit wrap, gaps, duplicates, backwards/reset observations,
independent streams, FFFF continuation, CSV column consistency, insert bytes,
idle/invalid-version handling, explicit opt-in, and log/input collision protection.

The local capture produced 13,538 CSV rows:

| Uncorrected observation | Count |
| --- | ---: |
| Invalid versions | 120 |
| Version-1 idle frames (VCID63) | 1,916 |
| M-PDUs with valid FHP | 10,706 |
| Out-of-range FHP | 796 |
| Packet-start zones | 1,545 |
| Continuation zones | 9,141 |
| Idle zones | 20 |
| Nonzero M-PDU spare | 10,340 |
| Nonzero signaling spare, version-1 frames | 1,118 |
| Counter gaps | 2,822 |
| Duplicate counters | 11 |
| Backward/reset counter events | 668 |

The geometry accounting is `13538 = 120 + 1916 + 10706 + 796`. VCID9 appears in
2,673 version-1 headers (2,685 before version filtering). Example: CADUs 42–44
carry spacecraft 11, VCID9, counters 14455–14457, and `FFFF` continuation headers.
These observations support the parsing offsets but reveal substantial anomalies
that must remain visible until RS correction and packet reassembly are available.

## M4 Space Packet reassembly

M4 reassembly runs after RS by default; `--no-rs` retains the M4 bypass path.
`space_packet` parses primary headers; `packet_reassembler` owns partial bytes
and returns complete packets, including their six-byte headers, by value.
The CLI consumes these packets to write `packet_log.csv`. M6 additionally
inspects selected AVHRR packets as described below. It does not interpret
secondary headers or assemble application-level segments using sequence
flags. Those flags are preserved.

### Boundaries and recovery

- State and counters are independent per (spacecraft ID, VCID, replay).
- Ordinary FHP values delimit the continuation prefix and first new packet.
  A known partial must finish **exactly** at that boundary. A conflicting length
  is reported and the partial discarded; parsing resumes at the advertised FHP.
- FHP 0 starts a new packet immediately. FHP 881 can leave one header byte.
  Headers split after any of their first five bytes are retained.
- Following the first packet, lengths delimit every subsequent packet in the
  zone. All complete packets are delivered; the final incomplete packet is held.
- `0x7FF` supplies continuation only. Without a partial, its bytes are counted
  as orphan continuation. Completion before the zone end contradicts this FHP:
  the partial and remaining bytes are rejected, with a boundary mismatch.
- `0x7FE` contributes no packet bytes. A contiguous idle zone preserves a partial,
  including a split header; a counter gap on an idle zone still invalidates it.
  VCID 63 OID frames do not affect other virtual channels.
- Counter gaps and backwards/reset observations discard that stream's partial
  before processing the new FHP. Duplicate counters discard its partial and skip
  the duplicate frame, preventing repeated delivery. The 24-bit wrap is contiguous.
- Invalid FHP drops the affected partial. Invalid VCDU version/size or CADU
  alignment loss clears all partials because stream identity is not reliable.
  Clearing partials preserves per-stream counter history, so duplicates after
  recovery are still rejected. Use a new reassembler for an independent capture.
- Unsupported Space Packet versions stop parsing that zone. No search for a
  plausible replacement header is attempted. Recovery requires a later FHP.
- EOF and `--max-cadus` discard/report unfinished headers and packets. Nothing is
  padded, cropped to a desired length, or emitted as a complete partial.

Packet Data Length is data bytes minus one: total bytes = field + 7. All 16-bit
values are legal, including zero (7-byte packet) and FFFF (65,542-byte packet).
There is no invented mission-specific length limit. Storage grows only with
received bytes, bounded by that protocol maximum per active stream. A length
that disagrees with a subsequent FHP is impossible for that stream and rejected.
A plausible but corrupted length cannot always be detected without RS.

These rules follow specification sections 10–12 and
[CCSDS Space Packet Protocol 133.0-B-2](https://ccsds.org/Pubs/133x0b2e2.pdf),
section 4.1. In particular,
[CCSDS AOS 732.0-B-4](https://ccsds.org/Pubs/732x0b4.pdf), section 4.1.4.2.4.4 note 2,
permits idle M-PDUs in the middle of a split packet; idle must not erase a
contiguous partial. APID 2047 idle packets are length-delimited and counted
separately, without delivery to the caller.

### Output and limitations

`packet_log.csv` is replaced each run and has one row per delivered non-idle
packet. It records packet index, spacecraft ID, VCID, replay, start/end VCDU
counters, APID, sequence flags/count, secondary-header flag, total size, and
`rs_status=not_applied`. Input/output collisions are rejected before writing.
The counters refer to the first header byte and final data byte, respectively.

JSON schema 4 adds `packets` in `--no-rs` mode and uses stage
`space_packets_no_rs`. Text statistics expose the same counters:

| Counter | Meaning |
| --- | --- |
| `reconstructed`, `reconstructed_bytes`, `by_vcid` | Delivered non-idle packets, their byte total, and VCID counts. |
| `idle_packets`, `idle_zones` | Complete APID 2047 packets and FHP 0x7FE zones. |
| `invalid_headers` | Unsupported packet versions encountered at known boundaries. |
| `boundary_mismatches` | Packet lengths inconsistent with continuation/FHP boundaries. |
| `truncated_packets`, `discarded_partial_bytes` | Observed candidates discarded and buffered bytes discarded, including malformed headers and EOF/limit partials. These are not estimated missing-packet counts. |
| `orphan_continuation_bytes` | Prefix/continuation bytes with no saved partial. |
| `rejected_zone_bytes` | Unconsumed bytes after an error, or entire invalid-FHP/duplicate zones. |
| `invalid_frames`, `duplicate_frames` | Rejected VCDU sizes/versions/FHPs and skipped duplicate counters. |

Exit status retains the inspection contract: zero indicates successful processing
with at least one structurally valid M-PDU, not successful instrument decoding
or a guarantee of any reconstructed packets. Anomalies appear in stderr and
statistics. In bypass mode all data remain uncorrected. Even after RS, no
packet checksum, secondary-header format, AVHRR layout, or payload validity
is claimed.

### M4 validation

Windows Release (GCC 14.2.0 / MinGW-w64, Ninja, CMake 3.31.5) passes **96/96
CTest cases**. The 35 added cases cover primary-header masks, length extremes,
three-M-PDU byte-for-byte reconstruction, all header split positions, a mixed
100-packet stream, multiple packets per zone, idle insertion, independent streams,
gaps, duplicate/backward counters, rollover, impossible boundaries, invalid
headers/frames, EOF/limit partials, CLI recovery after gaps/alignment loss, output
errors, and input preservation. Five regressions cover duplicate rejection after
invalid VCDU version/size or alignment loss, including CLI CSV/statistics checks
and recovery at the next valid frame. All five fail before the counter-history
fix and pass after it.

The three-M-PDU fixture delivers a 2000-byte packet followed by a 646-byte
packet, exactly 2646 bytes total, without truncations or invalid headers.
Stopping after two CADUs emits no packet and reports one 1764-byte partial.
The maximum-length test reconstructs all 65,542 bytes across 75 M-PDUs.


## M5 CCSDS Reed-Solomon

The default pipeline is:

```text
1024-byte CADU -> validate ASM -> XOR all 1020 CVCDU bytes
-> deinterleave 4 x 255 dual-basis symbols -> correct each lane
-> reconstruct 892 dual-basis data bytes -> VCDU/M-PDU -> Space Packets
```

The parameters are fixed to the MetOp profile, not generic RS defaults:

| Parameter | Implemented value |
| --- | --- |
| Code | RS(255,223), 32 parity symbols, up to 16 unknown erroneous symbols per lane |
| Field | GF(256), polynomial x^8+x^7+x^2+x+1 (`0x187`), alpha represented by 02 |
| Generator | Product of (x - alpha^(11*j)), j = 112..143 |
| Representation | CCSDS dual basis on input/output; conventional polynomial basis internally |
| Dual basis definition | Dual of {1, beta, ..., beta^7}, beta = alpha^117; z0 transmitted MSB first |
| Interleaving | I=4; lane l, symbol i comes from CVCDU[4*i+l] |
| Shortening / erasures | None; full 255-symbol codewords, no supplied erasure positions |

These requirements and the conversion matrices come from
[CCSDS 131.0-B-5](https://ccsds.org/Pubs/131x0b5.pdf), sections 4.3.3–4.3.9 and
4.4.2. `reed_solomon` independently implements syndrome evaluation,
Berlekamp–Massey, Chien search and a GF linear solve for error magnitudes.
It checks all 32 syndromes again before accepting corrected bytes. Data and
parity errors both count as corrected symbols. Reconstruction takes the first
223 symbols of each corrected lane, interleaved in the original order.

If any lane fails, no VCDU from that frame reaches the parsers. All pending
packet fragments are invalidated because even the spacecraft/VCID header may
be damaged; counter history is retained. Recovery starts at a later trusted
FHP. This is conservative and can discard partial packets on other VCIDs.

RS checking is not a checksum or an absolute integrity guarantee. With more
than 16 erroneous symbols in one lane, an RS decoder may reject, miscorrect,
or accept another valid codeword. Tests of 17-error patterns assert rejection
of those particular patterns, not universal detection beyond the correction
radius. No malformed data is padded or cropped.

### Logs and counters

All runs replace `stats.txt`, `stats.json`, `rs_log.csv`, `vcdu_log.csv`,
and `packet_log.csv`. Use separate output directories for RS/bypass comparisons.

- `rs_log.csv`: one row per accepted CADU, with original index/offset, status
  `good`, `corrected`, `uncorrectable`, or `not_applied`, and four lane
  correction counts. A failed lane has -1; bypass lane fields are empty.
- `vcdu_log.csv`: only RS-accepted frames in normal mode, every frame in bypass
  mode. `rs_status` is good/corrected/not_applied. Structurally valid M-PDUs are
  labelled `valid` after RS, or `uncorrected` in bypass.
- `packet_log.csv`: `rs_status=rs_checked` means every contributing frame
  passed RS. It does not claim a packet was entirely error-free before
  correction. Bypass uses `not_applied`.
- `rs.good_frames`: all lanes had zero syndromes without changes.
- `rs.corrected_frames`: all lanes passed and at least one symbol changed.
- `rs.uncorrectable_frames`: at least one lane failed; whole frame rejected.
- `rs.corrected_symbols_per_lane`: four cumulative counts in lane order 0..3,
  including successful lane corrections in a frame rejected by another lane.
- `rs.uncorrectable_lanes`: four cumulative lane-failure counts.

The three frame counts sum to `cadu.cadus_read`. `frames.vcids` counts only
accepted version-1 headers after correction; M2 diagnostic histograms still
describe uncorrected bytes immediately after XOR. M3/M4 validation numbers
above are historical bypass results.

### M5 validation

Windows Release with GCC 14.2.0 / MinGW-w64 passes **116/116 CTest cases**.
The new RS unit cases verify all 256 basis conversions against an independent
field-trace calculation; external nonzero codewords; rejection of a conventional
basis word presented as dual; all 255 single-error positions on two words;
384 deterministic 1–16-error trials; exact deinterleaving and reconstruction;
and every position of a 64-byte burst in a CVCDU (16 errors per lane), including
parity. They also check rejected-input immutability, length bounds, and counters.

The CLI cases exercise derandomization plus RS plus three-frame packet
reassembly, header/FHP correction, whole-frame rejection, recovery, duplicate
suppression across an RS failure, partial packets at a limit, bypass,
output failures and input preservation. Older framing fixtures without parity
now explicitly select `--no-rs`.

The nonzero fixtures were generated with an independent libfec CCSDS encoder,
not an encoder from this implementation. Its source is neither vendored nor
linked into this project. See [vector provenance and regeneration](tests/reference/README.md)
for the pinned revision, parameters, generator and hashes.

The existing local 13,538-CADU capture produced:

| Observation | With RS | --no-rs |
| --- | ---: | ---: |
| Good / corrected / rejected frames | 32 / 6750 / 6756 | not checked |
| Headers presented to parsers | 6782 | 13538 |
| Invalid VCDU versions | 0 | 120 |
| Invalid FHP | 0 | 796 |
| Invalid Space Packet headers | 0 | 475 |
| Packet boundary mismatches | 0 | 60 |
| Reconstructed non-idle packets | 141 | 666 |

Lane correction totals were [53616, 54079, 53666, 53463], including successful
lanes in rejected frames. Packet counts are not directly comparable as a
quality metric: bypass can emit corrupted packets, while RS discards damaged
frames and conservatively clears fragments. The RS run reported 465 discarded
partial candidates; losses remain visible. These observations support the
selected field/basis/interleave conventions but do not validate AVHRR payloads.


## M6 AVHRR packet inspection

`avhrr` consumes complete reconstructed Space Packets, selects **VCID 9 AND
APID 103 or 104**, and revalidates primary headers and exact lengths before
writing outputs. Both APIDs are retained. No expected instrument packet size
is hard-coded: all valid CCSDS lengths remain visible.

```powershell
.\build\metop_decoder.exe metop_output.cadu --out decoded --dump-debug --inspect-packets 20 --dump-stats
```

Every run writes `--out/debug/packet_log.csv`, separate from the existing
all-APID `--out/packet_log.csv`. The debug CSV contains one row per selected
packet, with global/selected packet indices, spacecraft/VCID/replay, start/end
VCDU counters, APID, type, sequence flags/count, secondary-header flag, raw
Packet Data Length, data-field size, total size, RS status, optional binary
offset, preview flag and two hexadecimal preview fields.

Here **payload** means exactly the CCSDS Packet Data Field, starting at byte 6
of the full packet. It includes the secondary header when present. The first
64 and last 16 bytes are limited to available data for short packets; overlap
is allowed and nothing is padded. Previews stop after the first N selected
packets across both APIDs. All selected packets still contribute to metadata,
statistics and optional dumps. `--dump-debug` also prints these previews.

With `--dump-debug`, the two binary files contain concatenated **complete
Space Packets including their six-byte primary headers**, without extra
record markers or length prefixes. Recover each record's length as the
big-endian primary-header bytes 4–5 plus 7. CSV `binary_offset` is a zero-based
byte offset into that APID's dump, not into the input CADU file. Empty APIDs
produce empty files. Without this option, offsets are empty and binary files
are not written or removed; use a fresh directory to avoid stale dumps.

JSON schema 6 adds `avhrr.selected_packets`, `previewed_packets` and entries
`apids.103` / `apids.104`. Each has packet/byte totals, a total-size histogram,
sequence-count frequencies, and arrays for sequence flags (indices 0..3),
secondary-header flags (0..1), and packet types (0..1). Counts aggregate
identities per APID; the CSV retains source identity and arrival order. No
cross-stream sequence continuity or missing-scan count is inferred.
The existing `stage` values still distinguish RS and bypass processing;
`rs_status=not_applied` labels every bypass observation.

The M6 baseline had **134 tests**, including 18 M6 cases for
filtering, both APIDs, header flags, length extremes, malformed inputs,
bounded previews, exact full-packet dumps, optional-output behaviour, RS
rejection, CLI limits and output/input collision protection. No real capture
or instrument layout is required by CTest.

The local capture yields **9 APID-103 packets, 0 APID-104**, each 12966 bytes
including the primary header, with flags 3 and secondary-header flag 1.
See [M6 capture observations and reference comparison](docs/avhrr-packet-inspection.md)
for sequence counters, sample hex, loss context and the exact questions that
were open at M6. The M7/M8 layout document below records their resolution and
remaining calibration limitations.

## M7/M8 raw AVHRR scans

[Verified payload layout and source evidence](docs/avhrr-payload-layout.md)
documents the primary ICD revision, derived byte/bit offsets and pinned
behavioural cross-check. The pipeline now decodes each complete VCID-9 /
APID-103/104 packet into five channels of unscaled `uint16_t` counts (0..1023).
APID 103 selects 3A; 104 selects 3B. The other channel-3 mode is absent.

No new CLI flag is needed. Every run writes `DIR/avhrr_scan_log.csv`, with source
identity, start/end VCDU counters, packet sequence, RS provenance, rejection
reason or accepted mode/time fields, and Earth width. JSON schema 8 adds
`avhrr_scans`: candidates, accepted, rejected, channel_3a, channel_3b,
earth_samples_per_channel and error counters. Existing stage names still identify
the RS path. Each rejected candidate has one reason; earlier checks take precedence.

The library entry point `decode_avhrr_packet` returns an `AvhrrScan` or a
rejection. `AvhrrScanProcessor` streams scans through an optional callback.
Earth, space and back-scan arrays remain separate; ramp and temperature words
are preserved in wire order. The CLI currently exports scan metadata only;
raw sample arrays are available through the library for M9. No images,
calibrated radiances, missing-row fillers or inactive-channel arrays are produced.

The exact 12966-byte profile, header fields, VPC, time ranges, SBT reserved byte
and two filler bits are checked. Invalid packets stay visible in the M6 logs
and optional packet dumps. `--no-rs` still checks VPC and records the bypass.
Recoverable payload errors do not change the existing CLI exit-code contract.

The complete local Release suite passes **148/148 tests**. New coverage includes
all ten-bit values at multiple bit alignments, a literal packed vector,
overflow/bounds checks, all truncation lengths, both channel-3 modes,
every Earth sample and field boundary, VPC and metadata rejection,
multi-VCDU scans, callback routing, EOF partials and output failure/collision checks.
Tests require no capture or downloaded documents.

On the local capture: RS enabled yields **9 accepted 3A scans, 0 rejected
payloads**. Each has five times 2048 raw Earth samples. RS bypass yields
13 candidates, all rejected by VPC. No real APID-104 packet is available;
3B is covered synthetically.

## Requirements and subsequent work

The current engineering specification is `../CODEX_METOP_CADU_AVHRR_DECODER.md`
relative to the repository root (outside this Git repository), sections 2–5,
23, and milestone 1 in section 27. The CADU/ASM requirements used here come from
that specification. Protocol references for subsequent stages include
[EUMETSAT TD18](https://user.eumetsat.int/s3/eup-strapi-media/TD_18_Metop_Direct_Readout_AHRPT_Technical_Description_v3_A_1cb789b653.pdf)
and [CCSDS TM Synchronization and Channel Coding](https://ccsds.org/Pubs/131x0b5.pdf).

M3 field geometry follows specification sections 7–10. Version, idle-frame,
counter, and FHP behaviour also reference
[CCSDS AOS 732.0-B-4](https://ccsds.org/Pubs/732x0b4.pdf), sections 4.1.2 and 4.1.4.2.
M4 packet reassembly implements specification sections 11–12. M5 implements
CCSDS RS correction. M6 adds AVHRR packet inspection; M7 verifies and documents
the payload profile; M8 reconstructs raw scans. M9 image output and M10 previews
remain subsequent work. The raw scan width is exactly 2048 Earth-view samples.
