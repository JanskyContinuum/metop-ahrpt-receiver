# MetOp AHRPT Receiver

[![Decoder CI](https://github.com/JanskyContinuum/metop-ahrpt-receiver/actions/workflows/decoder-ci.yml/badge.svg?branch=main)](https://github.com/JanskyContinuum/metop-ahrpt-receiver/actions/workflows/decoder-ci.yml)

An offline MetOp AHRPT receiver for turning recorded IQ or aligned CADUs into
**raw AVHRR/3 images**. MATLAB/Simulink handles the radio signal; a standalone
C++20 decoder reconstructs CCSDS packets and writes the instrument's original
10-bit Earth-view samples.

**Simulink documentation:** the separate [simulink/README.md](simulink/README.md)
describes the complete receiver pipeline, every block and its parameters,
acquisition/reacquisition, running IQ recordings and validation.
For C++ options and protocol details, see [decoder/README.md](decoder/README.md).

```text
Recorded IQ (.cs16, 10 MHz)
    |
    v
MATLAB / Simulink
    Filtering, resampling, QPSK synchronization, Viterbi decoding, ASM alignment
    |
    v
Aligned CADUs (.cadu, 1024 bytes each)
    |
    v
C++ decoder
    Derandomization -> CCSDS Reed-Solomon -> VCDU / M-PDU -> Space Packets
    -> AVHRR payload validation -> 2048 Earth-view samples per active channel
    |
    v
Raw PGM images + scan metadata + decoding statistics
```

**Already have a CADU file?** Start with [Build and decode](#build-and-decode).
MATLAB is only needed for the [IQ-to-CADU stage](#decode-an-iq-recording).

[Simulink guide](simulink/README.md) · [Input formats](#input-formats) ·
[Build and decode](#build-and-decode) ·
[Find your images](#find-your-images) · [Expected results](#expected-results) ·
[Troubleshooting](#troubleshooting) · [Documentation](#tests-and-documentation)

## Input formats

| Starting point | Required input | Processing stage |
| --- | --- | --- |
| Recorded radio signal | Headerless `.cs16`: signed little-endian 16-bit `I0,Q0,I1,Q1,...`, **10 million complex samples/s** | MATLAB/Simulink, then C++ |
| Demodulated transmission | Binary `.cadu`: **1024-byte CADUs**, each beginning with `1A CF FC 1D` | C++ only |

The C++ input contract is **4 ASM bytes + 1020 randomized bytes, including RS
parity**. The decoder applies the CCSDS legacy derandomizer, then CCSDS
RS(255,223) with interleaving depth 4 and dual-basis conversion. It selects
AVHRR traffic on VCID 9, APID 103/104, and validates complete instrument packets
before writing scans.

> **Check the upstream processing.** CADUs already processed by a derandomizer
> must be adapted before using the current CLI: it always applies the XOR mask.
> An ASM alone does not identify the randomization state. `--no-rs` skips
> Reed-Solomon only; it does not skip derandomization. The included Simulink
> writer produces the randomized format expected by the decoder.

Keep the input file static during decoding. The decoder reads it without
modifying it. Raw IQ is not a valid input to the C++ executable.

## Build and decode

### 1. Get the source

```sh
git clone https://github.com/JanskyContinuum/metop-ahrpt-receiver.git
cd metop-ahrpt-receiver
```

### 2. Build and test the decoder

Requirements: **CMake 3.20+**, a **C++20 compiler**, and the compiler's build tools.
The decoder and its tests do not require MATLAB or downloaded test libraries.
Run these commands from the repository root in a configured compiler shell:

```sh
cmake -S decoder -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

On Windows, use a Visual Studio developer shell, or a configured MinGW-w64
shell with the matching runtime DLLs on `PATH`. For Ninja, add `-G Ninja` to the
configure command in a fresh build directory.

| Build configuration | Executable |
| --- | --- |
| Windows / Visual Studio | `build/Release/metop_decoder.exe` |
| Windows / Ninja or MinGW Makefiles | `build/metop_decoder.exe` |
| Linux / single-configuration generator | `build/metop_decoder` |

GitHub Actions builds and tests **Windows Release** on pushes to `main` and pull
requests targeting `main`. See [compiler setup and CI details](decoder/README.md#build-and-test).

### 3. Decode a CADU file

Windows / Visual Studio:

```powershell
.\build\Release\metop_decoder.exe "D:\captures\pass.cadu" --out "decoded\pass-01" --dump-stats
```

Windows / Ninja:

```powershell
.\build\metop_decoder.exe "D:\captures\pass.cadu" --out "decoded\pass-01" --dump-stats
```

Linux:

```sh
./build/metop_decoder /path/to/pass.cadu --out decoded/pass-01 --dump-stats
```

Replace the example input path with your recording. **Choose a new output
directory for each run**, such as `decoded/pass-02` for the next attempt.
The image writer refuses a nonempty `avhrr/` directory; other diagnostic files
can be replaced when reusing an output directory. Quote paths containing spaces.

### Useful options

| Option | Purpose |
| --- | --- |
| `--out DIR` | Required output directory for images, metadata and logs. |
| `--dump-stats` | Print full statistics; `stats.txt` and `stats.json` are saved regardless. |
| `--max-cadus N` | Inspect only the first N accepted CADUs. A short run may end before a complete scan is assembled. |
| `--dump-debug` | Also save complete APID 103/104 packets as binary dumps and print packet previews. |
| `--inspect-packets N` | Number of selected packets with payload previews; default 20, or 0 to disable previews. |
| `--verbose` | Log each accepted CADU and its original file offset. |
| `--no-rs` | Diagnostic comparison with RS correction bypassed; keep RS enabled for normal image decoding. |
| `--help` | Print command-line usage. |

For a packet-level investigation, use a separate output directory:

```powershell
.\build\metop_decoder.exe "D:\captures\pass.cadu" --out "decoded\pass-debug" --dump-debug --dump-stats
```

## Decode an IQ recording

The included model is [`simulink/demodulator_metop_r.slx`](simulink/demodulator_metop_r.slx).
Its configured input rate is **10 MHz**. IQ components are normalized by 32768;
source, resampler and acquisition settings are configured for that rate.

Use MATLAB/Simulink **R2024b** with Communications Toolbox, DSP System Toolbox,
and a supported C/C++ compiler. From the repository root in MATLAB:

```matlab
addpath('simulink')
result = metop_ahrpt_run("D:/captures/pass.cs16", "out/pass.cadu");
```

The default run starts at time zero and reads through EOF. It writes
`out/pass.cadu` plus compact MAT and JSON run reports. Reusing the
same Simulink output path replaces that run's products. Then run the C++ decoder:

```powershell
.\build\Release\metop_decoder.exe out\pass.cadu --out decoded\pass-01 --dump-stats
```

For Ninja builds, use `.\build\metop_decoder.exe`. Detailed model settings,
the constellation display, component checks and validation options are in the
[Simulink guide](simulink/README.md). Measured integration evidence is kept
separately in the [integration report](docs/simulink-integration.md) and
[structural simplification report](docs/simulink-simplification.md).

## Find your images

**Look inside the directory passed to `--out`, then open `avhrr/`.**
For `--out decoded/pass-01`, a stream from spacecraft ID 11 on VCID 9 produces:

```text
decoded/pass-01/
├── avhrr/
│   └── scid_11_vcid_9_replay_0/
│       ├── ch1_raw.pgm
│       ├── ch2_raw.pgm
│       ├── ch3a_raw.pgm          # Only when channel 3A is active
│       ├── ch3b_raw.pgm          # Only when channel 3B is active
│       ├── ch4_raw.pgm
│       ├── ch5_raw.pgm
│       ├── metadata.json        # Geometry, channels and validation mode
│       └── scan_rows.csv        # Row indices, packet counters and timestamps
├── stats.txt
├── stats.json
├── rs_log.csv
├── vcdu_log.csv
├── packet_log.csv
├── avhrr_scan_log.csv
└── debug/
    ├── packet_log.csv
    ├── apid_103_packets.bin     # With --dump-debug
    └── apid_104_packets.bin     # With --dump-debug
```

The stream directory name depends on the actual spacecraft ID, VCID and replay
flag. Streams are kept separate. In a recording containing only channel 3A,
`ch3b_raw.pgm` is not created. No image is created for a channel with zero valid
lines. Identical filenames in different run directories refer to different runs.

### Image format and geometry

- **Width:** exactly 2048 Earth-view samples per line.
- **Height:** the number of valid reconstructed scans for that channel.
- **Format:** binary PGM P5, `Maxval = 1023`, two bytes per sample, most significant byte first.
- **Values:** original 10-bit counts, held as `uint16_t` during decoding.
- **Order:** accepted scans in arrival order, without filling missing lines.

Channels 1, 2, 4 and 5 receive every accepted scan. Channel 3 switches between
3A and 3B; each file contains only the scans where that mode was active.
`scan_rows.csv` maps timestamps and packet sequence counters to each channel's
rows. Calibration and space-view samples are excluded from the Earth-view images.

Use a viewer that supports 16-bit PGM and respects its declared `Maxval`. A viewer
that assumes a full 0–65535 range can make these images appear very dark. The
counts are detector values, not calibrated temperatures or reflectances, and
image coordinates are scan/sample indices rather than map coordinates.

### PNG previews

Create ordinary 8-bit grayscale PNGs from a stream's raw images with Python 3.10+
(standard library only; no additional packages):

```powershell
python decoder/tools/pgm_preview.py decoded/pass-01/avhrr/scid_11_vcid_9_replay_0 --out decoded/pass-01/previews
```

Open `ch1_raw_preview.png`, `ch2_raw_preview.png`, etc. in the chosen preview
folder. Dimensions and row order stay unchanged. Counts 0–1023 map linearly to
0–255 for display; the original PGM files retain the raw ten-bit samples.
No calibration, contrast stretching, rotation or missing-line interpolation is
applied. Existing previews are protected: use a new output directory on reruns.

## Expected results

A longer, clean recording usually produces a taller image. File size alone does
not determine the number of usable scans: uncorrectable frames and interrupted
packets reduce the output. **2048 × 9 is not a decoder size limit.**

Two locally inspected CADU recordings produced the following results with RS enabled:

| Input bytes | CADUs | Uncorrectable RS frames | Valid scans | Images |
| ---: | ---: | ---: | ---: | --- |
| 13,862,912 | 13,538 | 6,756 | 9 | Five files, each 2048 × 9 |
| 117,027,840 | 114,285 | 6,158 | 1,346 | Five files, each 2048 × 1346 |

Both recordings contained active channels 1, 2, 3A, 4 and 5. The larger recording
was already derandomized: a separate copy was XOR-adapted to the CLI input
contract before decoding; the original file was unchanged. These are recorded
observations, not expected results for every pass or a full IQ-to-image benchmark.

All **13,783,040 samples** in the larger result matched an independent extraction
from the reconstructed packets. Each PGM was 5,513,234 bytes. Packet sequence
counters showed 51 missing scans in 20 gaps between the first and last accepted
scan. Adjacent output rows can therefore represent nonadjacent acquisition times;
consult `scan_rows.csv` when interpreting discontinuities.

A separate full **IQ-to-image** validation used a 16,276,389,888-byte,
10 MHz CS16 recording from t=0 through EOF. The Simulink receiver produced
131,737 CADUs; the C++ stage accepted 1,270 scans and wrote five **2048 × 1270**
images. The original and simplified models produced byte-identical CADUs and
raw images, with matching sampled acquisition controls. This run is separate
from the two CADU inputs above; see the
[validation report](docs/simulink-simplification.md) for hashes, losses and limits.

The captures and generated images are local data, not included in the repository.
The [independent comparison script](decoder/tests/reference/check_avhrr_pgm.py)
can verify a single-stream APID 103 packet dump against its PGM output.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No images or only a few rows | Read `images.scans`, `avhrr_scans` and `rs` in `stats.json`. A short limit or incomplete packets may yield no full scan. |
| All VCDUs rejected for invalid version | Verify input format, randomization state and alignment. Already-derandomized CADUs are incompatible with the CLI's unconditional XOR stage. |
| Many uncorrectable RS frames | Check reception and demodulation quality, as well as the input format. `--no-rs` is a diagnostic bypass, not a repair. |
| A previous image still has the old dimensions | Check the exact `--out` path and that run's `metadata.json`. Filenames repeat across run directories. |
| Output directory rejected | Use a new run directory; `avhrr/` must be absent or empty. |
| Image looks dark | Check the viewer's support for PGM `Maxval = 1023` before interpreting brightness. |
| `metop_decoder.exe` does not start | Check the executable location and matching compiler runtime DLLs on `PATH`, especially with MinGW. |

Exit status **0** means that valid CADU/M-PDU traffic was processed, not that an
AVHRR image necessarily exists. Check `images.scans` and the output files.
Status **1** reports input/output failure or no usable CADU/M-PDU traffic;
status **2** reports invalid command-line arguments. Recoverable data anomalies
are recorded in the logs and do not automatically fail the whole run.


## Tests and documentation

The local Release suite passed **161/161 CTest entries** with Python 3.10+
available: 160 C++ tests and one entry running four PNG-preview checks.
Python is optional for building and using the C++ decoder.

MATLAB component tests and a full before/after recording replay verified
framing, acquisition and EOF termination. GitHub Actions currently runs the
C++/CTest workflow on Windows Release; the MATLAB recording replay is a
separate local validation, not part of CI.

| Documentation | Contents |
| --- | --- |
| [Simulink README](simulink/README.md) | Complete pipeline, block parameters, IQ input, run options, outputs and MATLAB tests |
| [Decoder README](decoder/README.md) | CMake/CTest, CLI, CCSDS/AVHRR decoding and raw/preview image formats |
| [Simplification validation](docs/simulink-simplification.md) | Exact removed blocks, before/after results, hashes and known limitations |
| [Integration history](docs/simulink-integration.md) | Receiver integration decisions and earlier measurements |

## Repository layout

```text
simulink/          Simulink model, MATLAB helpers and component checks
decoder/           C++20 decoder, CMake project, tests and protocol documentation
docs/              Integration report and receiver background
examples/          CS16 recording-splitting utility
.github/workflows/ Windows Release CI
```

Recordings, generated CADUs, decoded images, build directories and Simulink
caches are ignored by Git. Keep run output outside the source directories.
