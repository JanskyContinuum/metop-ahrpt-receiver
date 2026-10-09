# Simulink integration and validation

This report describes the earlier **instrumented** model. For the current
layout and removal of diagnostics, see the
[structural simplification report](simulink-simplification.md).

Integration date: 2026-09-29. Branch: `feat/simulink-demodulator`.
The acceptance run processes the supplied 10 MHz recording from zero through EOF,
then passes the resulting CADUs to the existing C++ decoder without changing its
decoding logic. See [running the receiver](../simulink/README.md).

## Repository audit and preservation

The initial Git tree had 57 tracked files, primarily the C++ decoder, tests,
documentation and CI. HEAD was `6f12247` on `feat/avhrr-image-output`.
The supplied Simulink directory was untracked. The local root README also had
pre-existing edits; its complete contents were preserved in
`docs/receiver-background.md` before rewriting the entry point.

The active supplied model was `simulink/Demodulator/demodulator_metop_r.slx`.
Its name was retained and it now lives directly in `simulink/`. The CS16 source
and CADU writer moved with it. `cs16_split.m` was preserved in `examples/`.
The separate Meteor model and older MetOp XML export were inspected and preserved
locally in `out/preserved-legacy-models/`; neither is a receiver dependency.
Original active sources/model are backed up under `out/original-simulink/`.
Imported generated caches, binaries and the redundant original directory contents
were moved into `out/imported-simulink-artifacts/`. No ambiguous source or raw
recording was deleted.

`simulink/` contains only the active model, MATLAB source, component checks and
its README. CADUs, diagnostics, cache, builds and decoded images are ignored.
New ignore rules cover MATLAB autosaves, generated CGXE kernels and model backups.
The existing rules cover CS16/CADU/BIN/PGM files, `slprj/`, generated code,
`build*/`, `out/` and `decoded/`. No nested repository was created.
The C++ sources and tests are unchanged. No license was supplied; choosing a
project license remains the owner's decision.

## Observed failure and repair

The supplied source class defaulted to 120 seconds while the saved model selected
90 seconds and an absolute local capture path. Its zero-offset diagnostic run
produced no CADUs: the timing output decayed from approximately 21504 symbols to
zero during the initial noise and remained frozen until the diagnostic run was
stopped at about 15 seconds. A debug start at 60 seconds was not a reliable fix:
AGC startup transients drove the timing output down to 114 symbols by 60.92 s.

The final source starts at zero, reads once through EOF and supplies a final-frame
`done` flag to the native Stop Simulation block. Input/output filenames are
runtime parameters set with `Simulink.SimulationInput`.

The synchronization implementation uses three standard MathWorks blocks inside
a reset-on-enable subsystem: Coarse Frequency Compensator, Symbol Synchronizer
and Carrier Synchronizer. The earlier custom synchronization wrapper was removed
from the active source tree. A short presence/retry supervisor gates these blocks
until the raw spectral contrast rises, allowing AGC startup to settle first.
No new CADU for 0.5 s triggers a fresh synchronization attempt; the source keeps
advancing. The feedback store explicitly reads the preceding frame's count before
the current frame writes it.

The original demapper discarded an unpaired last symbol at every odd-sized timing
frame, disrupting the continuous coded stream. The replacement carries that
symbol into the next frame. An automatic channel acquisition wrapper tries four
QPSK rotations and two symbol-pair alignments, accepting two exact ASMs spaced
8192 bits apart. It then runs one continuous standard `comm.ViterbiDecoder`.
Channel acquisition restarts after a physical reset or 0.25 s without an ASM.

The original Viterbi block **already used puncturing**, with pattern
`[1;1;0;1;1;0]`. The integration does not invent a new puncturing layer or implement
the Viterbi algorithm itself. The custom code selects the correct alignment and
preserves state. The trellis is `poly2trellis(7,[171 133])`, traceback 96,
continuous operation. Normal decoding uses unquantized soft values; hard decoding
remains a comparison option.

The CADU writer retains the ASM and randomized RS-coded payload required by the
C++ interface. It checks two-marker spacing at acquisition, packs MSB-first bytes,
emits exactly 1024 bytes per CADU and reacquires after a missing expected marker.
The available `ccsdsTMFrameSynchronizer` was inspected: it removes the ASM and does
not implement the same two-marker acquisition contract. Keeping the small writer
also avoids adding Satellite Communications Toolbox as a runtime requirement.

## Actual rates, dimensions and parameters

Compiled port inspection confirmed a block execution period of 0.009216 s.
Vector sample rates are distinct from this execution period. All IQ ports are
complex `single` columns; metadata and decoded bits are real.

| Stage | Input/output sample rate | Compiled output dimensions | Parameters/type |
| --- | --- | --- | --- |
| CS16 source | file / 10 MHz | 92160 × 1; done 1 × 1 | complex single; Boolean done; little-endian signed I,Q /32768 |
| DC Blocker | 10 / 10 MHz | 92160 × 1 | DSP block, IIR order 6, normalized bandwidth .001 |
| Sample-Rate Converter | 10 / 14/3 MHz | 43008 × 1 | DSP block, 7/15, 3.8 MHz bandwidth, 80 dB attenuation |
| AGC | 14/3 / 14/3 MHz | 43008 × 1 | rectifier, linear, averaging 100, step .1, power target 1 |
| RRC receive filter | 14/3 / 14/3 MHz | 43008 × 1 | square root, alpha .5, span 10, 2 samples/symbol, decimation 1 |
| Coarse frequency | 14/3 / 14/3 MHz | 43008 × 1; frequency 1 × 1 | QPSK FFT; requested resolution 100 Hz; frequency real single |
| Symbol Synchronizer | 14/3 MHz / approximately 7/3 Msymbol/s | variable, bound 23655 × 1 | Gardner, 2 sps, bandwidth .003, damping 1, gain 5.4 |
| Carrier Synchronizer | approximately 7/3 / 7/3 Msymbol/s | variable, bound 23655 × 1 | QPSK, 1 sps, bandwidth .002, damping .707 |
| Channel decoder | 14/3 M coded bit/s / 3.5 Mbit/s | variable logical, bound 70968 × 1; status 2 × 1 uint32 | typically 32256 decoded bits/frame while locked |
| CADU writer | decoded bits / file | 1 × 1 uint32 | cumulative count; 8192-bit records |

The timing bound is a maximum allocation, not the expected emitted count. During
lock the observed output is approximately 21504 symbols/frame. Disabled native
subsystems hold their previous outputs; downstream channel logic and diagnostics
explicitly reject those held values while the presence flag is false.

The RRC alpha was already .5 in the supplied model; it was not .6. Gardner is
rotation invariant, so retaining timing recovery before the carrier PLL is valid.
The before/after failures concerned initialization and recovery, not evidence
that the ordering must be replaced with SatDump's Costas/Mueller–Muller chain.
The coarse FFT estimator updates every active frame and the carrier PLL continues
tracking; the Doppler estimate is not frozen at acquisition.

## Simplification and measured optimization

The root diagram exposes the main data path, a native synchronization subsystem,
acquisition controls and an isolated Diagnostics subsystem. Goto/From taps keep
measurement wires out of the main path. Built-in Spectrum Analyzer and
Constellation Diagram remain available via `ShowScopes=true`; unattended runs
disable their rendering. Native simulation compiler optimization is enabled.

The resampler's former 4.5 MHz bandwidth forced a narrow transition near its
4.6667 MHz output rate. A 3.8 MHz passband still encloses the approximately 3.5 MHz
QPSK occupied bandwidth with alpha .5. MathWorks reports:

| Resampler cost | Supplied | Final |
| --- | ---: | ---: |
| Coefficients | 1969 | 387 |
| Multiplications per input sample | 131.2667 | 25.8 |

This is approximately 5.09× less resampler arithmetic, not a claimed 5.09× gain
for the whole model. A real 60–62 s comparison with the final native blocks and
narrower filter retained 822 CADUs, 11 accepted AVHRR scans and five 2048 × 11
images. The soft/hard comparison was:

| Mode, same 60–62 s input | CADUs | RS good | RS corrected | RS uncorrectable | AVHRR scans |
| --- | ---: | ---: | ---: | ---: | ---: |
| Soft, unquantized | 822 | 821 | 1 | 0 | 11 |
| Hard | 821 | 797 | 24 | 0 | 11 |

The lower RS correction burden supports keeping the soft path. These short
diagnostic runs do not replace the zero-offset full-recording acceptance run.

## Full recording acceptance

The instrumented baseline processed the re-downloaded
`Metop_20260731_1022.cs16` from zero to EOF: **16,276,389,888 input bytes**,
**406.9097472 s** of IQ and **131,737 complete CADUs** (134,898,688 bytes).
The Stop Simulation block ended the run at the expected final source-frame
time, **406.904832 s**, with `StopEvent = ModelStop`.

The original JSON export failed on a Simulink metadata object after simulation.
The completed CADU and execution metadata were preserved, checked and used to
recover the acceptance report. The run helper now serializes only the stop-event
name and description. See the simplification report for the before/after
comparison and downstream decoder results.

## Verification and reproducibility

- MATLAB R2024b Update 1, Simulink, Communications Toolbox and DSP System Toolbox.
  A supported native compiler is required by the standard System blocks.
- A clean MATLAB batch session initialized the Simulink Agentic Toolkit, opened
  the model, ran component checks, then started the real recording at zero.
- `metop_test_components` passed little-endian I/Q ordering, normalization,
  reset, partial/exact EOF and debug seeking; all 16 soft/hard × phase × alignment
  cases recovered byte-exact synthetic CADUs with odd-sized chunks. Framing tests
  covered an isolated false ASM, inserted bit, reacquisition and partial EOF.
- `model_check` reported healthy connectivity and no Stateflow lint errors.
  No unresolved library links were found. All six local runtime helper classes
  and functions resolved inside the repository.
- Compiled widths/types/complexity and sample times were inspected and recorded
  locally in `out/compiled-ports.json`.
- MATLAB Code Analyzer found only a benign local-variable/property-name advisory
  in the monitor's MAT-file serialization. Simulation reports single-precision
  RRC coefficient quantization (first absolute error 2.28e-12); it is recorded,
  not hidden. No precision warning was treated as a synchronization failure.
- The unchanged C++ suite passed **160/160** tests using its existing MinGW build
  with the matching compiler runtime on PATH.

Run instructions and output descriptions are in `simulink/README.md`.
Use separate `CacheFolder` and output paths for concurrent MATLAB sessions.
A legacy root CGXE MEX shadowing the configured cache was identified and preserved
outside the source path; initialization now detects that situation. Generated
MEX files must never be versioned as receiver source.

## Limits

This validates one supplied 10 MHz pass, not arbitrary gain levels, interference
or sample rates. Presence thresholds (4 dB enable, 2.5 dB disable) were selected
from this recording's center/outer-band contrast. The monitor's sampled times
have 92.16 ms resolution. The coarse frequency estimate is not a calibrated
measurement of satellite Doppler or the complete carrier PLL state.

Exact ASM matching sacrifices frames with damaged markers. EOF zero-pads only
the final source frame; filter and Viterbi tails are not separately flushed, and
incomplete terminal CADUs are discarded. AVHRR images preserve ten-bit raw counts;
missing scans are not interpolated. There is no geolocation, projection or
radiometric calibration. The simulation's measured wall time is an offline
processing result, not a real-time performance guarantee.

## References

- [MathWorks Symbol Synchronizer](https://www.mathworks.com/help/comm/ref/comm.symbolsynchronizer-system-object.html): Gardner timing recovery and variable-size output.
- [MathWorks Carrier Synchronizer](https://www.mathworks.com/help/comm/ref/comm.carriersynchronizer-system-object.html): carrier tracking and phase ambiguity.
- [MathWorks Viterbi Decoder](https://www.mathworks.com/help/comm/ref/comm.viterbidecoder-system-object.html): continuous decoding, puncturing and soft inputs.
- [SatDump MetOp decoder](https://github.com/SatDump/SatDump/blob/master/plugins/noaa_metop_support/metop/module_metop_ahrpt_decoder.cpp) and [rate-3/4 Viterbi](https://github.com/SatDump/SatDump/blob/master/src-core/common/codings/viterbi/viterbi_3_4.cpp): behavioral references for MetOp symbol ordering and acquisition alternatives.

SatDump source was used to inspect protocol behavior; its code was not copied
into this repository.
