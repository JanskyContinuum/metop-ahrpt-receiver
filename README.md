# metop-ahrpt-receiver

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
See [decoder build, tests, and CLI](decoder/README.md)
for the implemented behaviour and limitations.

MATLAB/Simulink supplies the existing physical-layer demodulation and Viterbi
processing separately; its aligned CADU output is the C++ input. Raw AVHRR scan
decoding and raw PGM image output now run in C++.

End-to-end MetOp AHRPT receiver: QPSK demodulation and channel decoding in MATLAB/Simulink, followed by CCSDS and AVHRR/3 decoding in C++.
