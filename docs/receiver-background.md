# Receiver background (archived README)

This preserves the earlier project description. For the current implementation
and run instructions, use the [main README](../README.md) and
[Simulink README](../simulink/README.md).

## C++ implementation status: Milestone 9

The C++20 decoder now validates 1024-byte CADUs, checks the `1A CF FC 1D` ASM,
recovers alignment, applies the CCSDS derandomizer, and reports uncorrected
VCID/version/spacecraft histograms. M5 applies CCSDS RS(255,223), interleave 4,
with dual-basis conversion before VCDU/M-PDU parsing and Space Packet reassembly.
It reports good/corrected/uncorrectable frames and corrections per lane; rejected
frames invalidate pending fragments. `--no-rs` preserves the uncorrected comparison
path. M6 inspects VCID 9 / APID 103–104 packets: counts, lengths, sequence/header
fields, bounded payload previews and optional complete-packet binary dumps.
M7 documents the verified payload layout; M8 checks VPC and reconstructs raw
10-bit counts in five active channels, with exactly 2048 Earth samples each.
Space/calibration/telemetry fields remain separate. M9 writes raw PGM P5 images
(Maxval 1023, big-endian samples), separated by semantic channel and source
identity, with exact row counts and per-row metadata. No calibration, projection
or contrast enhancement is applied.
See [decoder build, tests, and CLI](../decoder/README.md)
for the implemented behaviour and limitations.

MATLAB/Simulink supplies the existing physical-layer demodulation and Viterbi
processing separately; its aligned CADU output is the C++ input. Raw AVHRR scan
decoding and raw PGM image output now run in C++.

End-to-end experimental receiver for **MetOp AHRPT (Advanced High Resolution Picture Transmission)**.

The project is split into two main parts:

1. **Signal demodulation in MATLAB/Simulink**<br>
   Reads raw complex IQ samples (`.cs16`) and recovers the AHRPT CADU bitstream.

2. **Protocol and instrument decoding in C++**<br>
   Reads recovered CADUs, performs CCSDS processing, reconstructs AVHRR/3 packets, and produces raw satellite imagery.

The repository is intended as an educational and engineering implementation of the complete receive chain from recorded L-band IQ samples to instrument image data.

---

## Overview

The complete processing chain is:

```text
Recorded complex IQ (.cs16)
        │
        ▼
┌──────────────────────────────┐
│ MATLAB / Simulink            │
│                              │
│ IQ input                     │
│ DC removal                   │
│ sample-rate conversion       │
│ AGC                          │
│ matched RRC filtering        │
│ coarse frequency correction  │
│ symbol timing recovery       │
│ carrier recovery             │
│ QPSK hard demapping          │
│ convolutional FEC / Viterbi  │
│ CADU synchronization         │
└──────────────┬───────────────┘
               │
               ▼
         *.cadu / *.bin
               │
               ▼
┌──────────────────────────────┐
│ C++ decoder                  │
│                              │
│ CADU validation              │
│ CCSDS derandomization        │
│ Reed-Solomon decoding        │
│ VCDU parsing                 │
│ M-PDU parsing                │
│ Space Packet reassembly      │
│ AVHRR packet extraction      │
│ AVHRR scan reconstruction    │
│ image generation             │
└──────────────┬───────────────┘
               │
               ▼
          AVHRR images
```

---

# What is implemented where?

## MATLAB / Simulink: physical-layer receiver

The Simulink model is responsible for turning complex baseband samples into synchronized AHRPT CADUs.

It performs the **receiver-side demodulation**.

It does **not** decode the AVHRR image packets themselves.

Typical model:

```text
CS16FileSource
    ↓
DC Blocker
    ↓
Sample-Rate Converter
    ↓
AGC
    ↓
Raised Cosine Receive Filter
    ↓
Coarse Frequency Compensator
    ↓
Symbol Synchronizer
    ↓
Carrier Synchronizer
    ↓
QPSK Demapper
    ↓
Viterbi Decoder
    ↓
CADU Writer
```

### Input

Raw complex IQ in `cs16` format:

```text
I0 Q0 I1 Q1 I2 Q2 ...
```

where each component is:

```text
signed int16
little-endian
```

One complex sample therefore occupies:

```text
4 bytes
```

The current test recordings use:

```text
sample rate = 10 MHz
```

Do not assume that every recording has the same sample rate. The Simulink input sample rate must match the actual SDR recording.

---

## MetOp AHRPT modulation

The transmitted MetOp AHRPT waveform uses QPSK.

The important receiver parameters used by the current model are:

```text
Information bit rate         : 3.5 Mbit/s
Convolutional code rate      : 3/4
Coded bit rate               : 4.6666667 Mbit/s
QPSK symbol rate             : 2.3333333 Msymbol/s
Working samples/symbol       : 2
Working sample rate          : 4.6666667 MHz
RRC roll-off                 : 0.6
```

For a 10 MHz recording, the receiver resamples:

```text
10 MHz → 4.6666667 MHz
```

which corresponds to:

```text
interpolation / decimation = 7 / 15
```

### Important distinction

This repository currently implements the **demodulator / receiver**.

It does not currently implement a transmitter that generates the complete MetOp AHRPT waveform.

When this README refers to “modulation”, it describes the modulation used by the satellite:

```text
QPSK + convolutional FEC + puncturing
```

The actual implemented Simulink block chain performs the inverse operation: **demodulation and channel decoding**.

---

# Simulink receiver details

## 1. CS16 input

A custom MATLAB System object reads the raw IQ file.

Example:

```text
simulink/CS16FileSource.m
```

The reader outputs normalized complex samples:

```matlab
x = (I + 1j*Q) / 32768
```

---

## 2. DC removal

A DC blocker suppresses residual receiver DC offset.

Typical configuration:

```text
Algorithm              : IIR
Normalized bandwidth   : 0.001
Order                   : 6
```

---

## 3. Sample-rate conversion

The signal is converted to exactly two samples per QPSK symbol.

For the current 10 MHz recordings:

```text
Fs_in  = 10 MHz
Fs_out = 14e6 / 3
       = 4.6666667 MHz
```

The nominal QPSK symbol rate is:

```text
Rs = 2.3333333 Msymbol/s
```

therefore:

```text
Fs_out / Rs = 2 samples/symbol
```

---

## 4. AGC

Automatic gain control normalizes signal amplitude before synchronization.

Typical starting parameters:

```text
Target power       : 1
Averaging length   : 1024
Maximum gain       : 60 dB
```

---

## 5. Matched RRC filter

MetOp AHRPT uses root-raised-cosine pulse shaping.

Receiver parameters:

```text
Filter shape            : Square root
Rolloff                  : 0.6
Input samples/symbol     : 2
Decimation               : 1
```

The filter is used as the receive matched filter.

---

## 6. Coarse frequency correction

The receiver estimates and removes a large residual carrier-frequency offset before fine carrier recovery.

The exact offset depends on:

- SDR tuning accuracy,
- recording centre frequency,
- satellite Doppler,
- oscillator error.

The estimated offset can be monitored from the `FreqEst` output.

---

## 7. Symbol timing recovery

A Gardner timing-error detector is used.

Typical configuration:

```text
Timing error detector      : Gardner (non-data-aided)
Samples per symbol         : 2
Damping factor             : 1
```

The output is approximately one complex sample per QPSK symbol.

---

## 8. Carrier recovery

Fine carrier/phase synchronization is performed using the Simulink Carrier Synchronizer configured for QPSK.

The expected output constellation contains four clusters.

There is still a possible QPSK phase ambiguity of:

```text
0°
90°
180°
270°
```

The receiver therefore keeps a phase-correction stage before hard demapping.

---

## 9. QPSK demapping and MetOp bit ordering

Both I and Q components are used.

Hard decisions:

```text
Re > 0 → I bit 0
Re < 0 → I bit 1

Im > 0 → Q bit 0
Im < 0 → Q bit 1
```

The implementation also reorders punctured bits into the order expected by the Viterbi decoder.

---

## 10. Viterbi decoder

The MetOp convolutional code uses:

```text
constraint length K = 7
G1 = 171 octal
G2 = 133 octal
code rate after puncturing = 3/4
```

MATLAB trellis:

```matlab
poly2trellis(7,[171 133])
```

Puncture vector:

```matlab
[1; 1; 0; 1; 1; 0]
```

The current receiver uses hard-decision Viterbi decoding.

Soft-decision decoding may be added later.

---

## 11. CADU synchronization and output

After Viterbi decoding, the stream is searched for the CCSDS Attached Sync Marker:

```text
1A CF FC 1D
```

Each complete CADU has:

```text
8192 bits
1024 bytes
```

The Simulink receiver writes aligned CADUs directly to a binary file such as:

```text
metop_output.cadu
```

This file is the input to the C++ decoder.

A healthy file should look like:

```text
offset 0       : 1A CF FC 1D ...
offset 1024    : 1A CF FC 1D ...
offset 2048    : 1A CF FC 1D ...
...
```

---

# C++ decoder

The C++ part starts **after physical-layer demodulation**.

Its job is to turn the CADU stream into instrument data and images.

Processing:

```text
CADU
 ↓
CCSDS derandomization
 ↓
Reed-Solomon RS(255,223), interleave 4
 ↓
VCDU
 ↓
M-PDU
 ↓
CCSDS Space Packets
 ↓
AVHRR VCID / APID filtering
 ↓
AVHRR scan reconstruction
 ↓
10-bit detector samples
 ↓
images
```

---

## CADU structure

```text
CADU                                 1024 bytes
├── Attached Sync Marker                4
└── randomized CVCDU                 1020
```

ASM:

```text
1A CF FC 1D
```

---

## CCSDS derandomization

The CVCDU is randomized using the CCSDS pseudo-random sequence.

Polynomial:

```text
x^8 + x^7 + x^5 + x^3 + 1
```

Initial register state:

```text
all ones
```

The packed sequence begins with:

```text
FF 48 0E C0 9A ...
```

The randomizer resets at the start of each CADU.

---

## Reed-Solomon

MetOp AHRPT uses:

```text
RS(255,223)
interleaving depth = 4
```

Four interleaved codewords are carried in each CVCDU.

After decoding:

```text
1020-byte CVCDU
        ↓
4 × RS(255,223)
        ↓
892-byte VCDU
```

The implementation must use a CCSDS-compatible Reed-Solomon decoder.

---

## VCDU

Decoded VCDU size:

```text
892 bytes
```

Structure:

```text
6 bytes   VCDU Primary Header
2 bytes   VCDU Insert Zone
884 bytes M-PDU
```

The decoder tracks:

```text
spacecraft ID
VCID
24-bit VCDU counter
counter continuity
```

---

## M-PDU and Space Packet reassembly

M-PDU:

```text
2 bytes   M-PDU header
882 bytes packet zone
```

The 11-bit First Header Pointer tells where the first new CCSDS Space Packet begins.

Packets can span several VCDUs.

Correct packet reassembly is therefore required before instrument decoding.

---

# AVHRR/3 decoding

The AVHRR/3 High Rate stream is carried on:

```text
VCID = 9
```

Relevant packet APIDs:

```text
103
104
```

The program reconstructs complete CCSDS Space Packets before decoding the AVHRR instrument payload.

AVHRR/3 uses 10-bit detector samples.

One scan contains Earth-view samples with nominal image width:

```text
2048 pixels
```

The instrument has six named channels, while channels 3A and 3B are mutually exclusive during operation.

The first implementation should preserve the original raw detector counts.

---

# Output

Example decoder output:

```text
decoded/
├── stats.txt
├── stats.json
├── avhrr/
│   ├── ch1_raw.pgm
│   ├── ch2_raw.pgm
│   ├── ch3a_raw.pgm
│   ├── ch3b_raw.pgm
│   ├── ch4_raw.pgm
│   ├── ch5_raw.pgm
│   └── metadata.json
└── debug/
```

Raw images should preserve the 10-bit values:

```text
0 ... 1023
```

Preview images may use percentile stretching to convert them to 8-bit.

---

# Repository layout

Suggested layout:

```text
metop-ahrpt-receiver/
├── README.md
├── LICENSE
├── .gitignore
│
├── simulink/
│   ├── demodulator_metop.slx
│   ├── CS16FileSource.m
│   ├── MetopCADUWriter.m
│   ├── metop_ahrpt_init.m
│   └── README.md
│
├── decoder/
│   ├── CMakeLists.txt
│   ├── src/
│   ├── tests/
│   └── README.md
│
├── docs/
│   ├── architecture.md
│   └── references.md
│
└── examples/
    └── README.md
```

Do **not** commit large raw IQ recordings to Git.

Recommended:

```gitignore
*.cs16
*.cadu
*.bin
*.slxc
slprj/
*_ert_rtw/
*_grt_rtw/
build/
out/
decoded/
```

If small test vectors are needed, keep deliberately short files in:

```text
tests/data/
```

and document their origin/licensing.

---

# Building the C++ decoder

Requirements:

```text
CMake >= 3.20
C++20 compiler
```

Example:

```bash
cmake -S decoder -B build
cmake --build build --config Release
```

Then:

```bash
./build/metop_decoder metop_output.cadu --out decoded
```

On Windows with a multi-config generator:

```powershell
.\build\Release\metop_decoder.exe metop_output.cadu --out decoded
```

---

# Using the Simulink demodulator

Requirements:

- MATLAB
- Simulink
- Communications Toolbox
- DSP System Toolbox

Workflow:

1. Place or select a valid MetOp AHRPT `.cs16` recording.
2. Set the **actual recording sample rate**.
3. Configure the Sample-Rate Converter so that the working rate is:
   ```text
   4.6666667 MHz
   ```
4. Run the model.
5. Verify that the QPSK constellation contains four clusters.
6. Verify that the CADU counter increases.
7. Use the resulting `.cadu` file as input to the C++ decoder.

For the current recording:

```text
Fs_in = 10 MHz
```

Do not accidentally configure it as 5 MHz. A wrong input sample rate changes the effective samples-per-symbol seen by the synchronization loops and can completely destroy timing recovery.

---

# Quick validation

## Simulink

Expected after synchronization:

```text
four QPSK constellation clusters
```

Expected CADU marker:

```text
1A CF FC 1D
```

Expected CADU size:

```text
1024 bytes
```

## C++ decoder

Useful checkpoints:

```text
ASM every 1024 bytes
plausible VCID histogram
VCID 9 present
reasonable VCDU counter continuity
APID 103/104 present
AVHRR samples in 0..1023
2048 Earth pixels per reconstructed scan line
```

Do not jump directly to image rendering if any earlier checkpoint fails.

---

# Current status

The project is under active development.

### Working / being integrated

- [x] raw CS16 input
- [x] sample-rate conversion
- [x] RRC matched filtering
- [x] coarse frequency correction
- [x] QPSK symbol synchronization
- [x] QPSK carrier synchronization
- [x] hard QPSK demapping
- [x] punctured Viterbi decoding
- [x] CADU synchronization
- [x] binary CADU output

### Decoder

- [ ] CADU reader
- [ ] CCSDS derandomizer
- [ ] CCSDS Reed-Solomon
- [ ] VCDU parser
- [ ] M-PDU parser
- [ ] CCSDS Space Packet reassembly
- [ ] AVHRR APID extraction
- [ ] AVHRR scan reconstruction
- [ ] raw 10-bit image output
- [ ] image previews
- [ ] calibration
- [ ] geolocation

---

# Scope

This repository focuses first on:

```text
recorded MetOp AHRPT IQ
→ demodulation
→ channel decoding
→ packet decoding
→ raw AVHRR imagery
```

Possible future work:

- soft-decision Viterbi,
- live SDR input,
- better carrier/Doppler tracking,
- AVHRR radiometric calibration,
- brightness-temperature products,
- administrative-message decoding,
- orbit/attitude reconstruction,
- geolocation,
- map projection,
- RGB composites,
- additional MetOp instruments.

---

# References

Primary reference:

- EUMETSAT — **MetOp Direct Readout AHRPT Guide**<br>
  https://user.eumetsat.int/resources/user-guides/metop-direct-readout-ahrpt-guide

Additional relevant documents:

```text
EUM/OPS/TEN/08/1663
MetOp Direct Readout AHRPT Technical Description

EPS/SYS/SPE/95413
HRPT/LRPT Direct Broadcast Service Specification

MO-IC-MMT-AH-0001
AVHRR/3 Instrument ICD

MO-IF-MMT-SY-0001
MetOp Space-to-Ground Interface Specification
```

SatDump is also a useful known-working reference implementation for comparison:

https://github.com/SatDump/SatDump

---

# License

Choose the repository license deliberately.

If the implementation is written independently from specifications and only uses SatDump as a behavioural/reference implementation, a permissive license such as MIT can be appropriate.

If source code is copied or adapted from GPL-licensed SatDump components, the resulting licensing obligations must be respected. Do not copy GPL code into this repository while claiming the result is independently MIT-licensed.

---

# Disclaimer

This is an independent educational/engineering project and is not an official EUMETSAT product.
