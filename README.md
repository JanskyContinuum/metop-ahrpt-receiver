# metop-ahrpt-receiver

## C++ implementation status: Milestone 4

The C++20 decoder now validates 1024-byte CADUs, checks the `1A CF FC 1D` ASM,
recovers alignment, applies the CCSDS derandomizer, and reports uncorrected
VCID/version/spacecraft histograms. Explicit `--no-rs` mode adds VCDU/M-PDU
parsing, counter continuity, FHP validation, and insert-zone CSV logging.
M4 reconstructs CCSDS Space Packets across VCDUs, with packet CSV logging and
explicit partial/loss diagnostics. RS correction and images are not implemented.
See [decoder build, tests, and CLI](decoder/README.md)
for the implemented behaviour and limitations.

MATLAB/Simulink supplies the existing physical-layer demodulation and Viterbi
processing separately; its aligned CADU output is the C++ input. Full CCSDS and
AVHRR decoding remain planned.

End-to-end MetOp AHRPT receiver: QPSK demodulation and channel decoding in MATLAB/Simulink, followed by CCSDS and AVHRR/3 decoding in C++.
