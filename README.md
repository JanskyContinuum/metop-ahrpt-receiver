# metop-ahrpt-receiver

## C++ implementation status: Milestone 5

The C++20 decoder now validates 1024-byte CADUs, checks the `1A CF FC 1D` ASM,
recovers alignment, applies the CCSDS derandomizer, and reports uncorrected
VCID/version/spacecraft histograms. M5 applies CCSDS RS(255,223), interleave 4,
with dual-basis conversion before VCDU/M-PDU parsing and Space Packet reassembly.
It reports good/corrected/uncorrectable frames and corrections per lane; rejected
frames invalidate pending fragments. `--no-rs` preserves the uncorrected comparison
path. Frame/packet CSV logs and loss diagnostics are available; AVHRR payload
decoding and images are not implemented.
See [decoder build, tests, and CLI](decoder/README.md)
for the implemented behaviour and limitations.

MATLAB/Simulink supplies the existing physical-layer demodulation and Viterbi
processing separately; its aligned CADU output is the C++ input. AVHRR instrument
decoding remains planned.

End-to-end MetOp AHRPT receiver: QPSK demodulation and channel decoding in MATLAB/Simulink, followed by CCSDS and AVHRR/3 decoding in C++.
