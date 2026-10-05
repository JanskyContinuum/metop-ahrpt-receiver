# Structural simplification and regression evidence

Validation completed on 2026-10-05, on the existing
`feat/simulink-demodulator` branch. No new branch was created.

The receiver's main diagram was reduced from **31 to 15 top-level blocks**
(counting commented blocks in the original model). The signal-processing and
channel-decoding algorithms were preserved. The full before/after runs produced
**byte-identical CADU files**, with matching sampled acquisition/control traces.

![Current receiver pipeline](../simulink/docs/receiver-pipeline.png)

## Full recording comparison

Both runs used the same static `Metop_20260731_1022.cs16`, starting at file
offset zero, with soft Viterbi decisions and scopes disabled. The short visible
scope test below was separate. The full validation stop-time guard was set
beyond the expected EOF; it did not terminate the run.

| Measurement | Instrumented baseline | Simplified model |
| --- | ---: | ---: |
| Input bytes | 16,276,389,888 | 16,276,389,888 |
| Input duration at 10 MHz | 406.9097472 s | 406.9097472 s |
| Complete CADUs | 131,737 | 131,737 |
| Output bytes | 134,898,688 | 134,898,688 |
| Final source-frame time | 406.904832 s | 406.904832 s |
| Stop event | ModelStop | ModelStop |
| Physical acquisition attempts | 6 | 6 |
| Channel acquisitions | 22 | 22 |

Input SHA-256:

```text
62F0359F2D0E633E1FE639C375DB70D98A55E6C36FBBB4877006690A34088F49
```

SHA-256 of **both** complete CADU files:

```text
FCC3690840F94F0AB3B6FC5BBA8B0641CA4FACADE731B07195E257918F919A58
```

The comparison also read both files in 1 MiB chunks and checked every byte.
The final EOF flag was asserted in the simplified model's native signal log.
The final source-frame time equals
`(ceil(inputBytes / 4 / 92160) - 1) * 0.009216`; it is the timestamp of the
last frame, not the timestamp immediately after its last IQ sample.

The baseline monitor recorded controls every ten source frames (92.16 ms).
All **4,416 available records** matched the corresponding native signal logs
after simplification: enable flags, attempt counts, channel hypothesis, channel
acquisition count and cumulative CADUs. Spectral contrast matched within
0.000051 dB, accounting for the original CSV's four-decimal rounding.
This verifies the sampled control trajectory; it does not claim to observe
every transition between baseline CSV samples.

The simplified full run took 3,408.6 wall-clock seconds on the test machine.
The baseline's recorded wall time includes an extended interruption between
sessions, so these runs do not support a numerical speedup claim.

## Exactly what was removed

SIDs below refer to `demodulator_metop_r`. Names are the original block names.
Each logging pair was inspected before editing, including its destination.

| Removed block | SID | Why removal preserves receiver behavior |
| --- | ---: | --- |
| CADUCount | 53 | Display-only sink. The writer count still drives UpdateCADUCount. |
| CarrierOffsetHz | 55 | Display-only frequency sink; no control output. |
| EndOfFile | 57 | Display-only EOF sink. The separate direct connection to Stop Simulation remains. |
| ChannelAcquisition | 79 | Display-only channel-status sink; no control output. |
| Spectrum Analyzer | 73 | Commented monitoring scope; no receiver output. |
| ConstellationSamples | 61 | Adapter selecting samples solely for the display. The retained scope now receives synchronized symbols directly. |
| Diagnostics | 96 | Monitoring-only container; its consumer had no outputs. |
| Diagnostics/FrameDiagnostics | 107 | MetopMonitor had ten inputs and zero outputs; it wrote diagnostics only. |
| QPSKSynchronization/Out1 | 83 | Exported coarse samples only for monitoring. Internal coarse-to-timing connection remains. |
| QPSKSynchronization/Timing | 84 | Exported timing samples only for monitoring. Internal timing-to-carrier connection remains. |
| QPSKSynchronization/Frequency | 86 | Exported the coarse estimate only for monitoring. The estimation algorithm/output setting remains unchanged. |

These ten **Goto/From pairs** were also removed:

| Root Goto (SID) | From inside Diagnostics (SID) | Tag | Destination / safety |
| --- | --- | --- | --- |
| LogIQ (97) | IQ (108) | metop_IQ | FrameDiagnostics only; raw-IQ acquisition branch retained |
| LogAGC (98) | AGC (109) | metop_AGC | FrameDiagnostics only; AGC-to-RRC path retained |
| LogRRC (99) | RRC (110) | metop_RRC | FrameDiagnostics only; synchronization input retained |
| LogCoarse (100) | Coarse (111) | metop_Coarse | FrameDiagnostics only; internal timing input retained |
| LogTiming (101) | Timing (112) | metop_Timing | FrameDiagnostics only; internal carrier input retained |
| LogSymbols (102) | Symbols (113) | metop_Symbols | FrameDiagnostics only; decoder and direct constellation branches retained |
| LogFrequency (103) | Frequency (114) | metop_Frequency | FrameDiagnostics only; frequency correction unchanged |
| LogAcquisition (104) | Acquisition (115) | metop_Acquisition | FrameDiagnostics only; enable and decoder-state connections retained |
| LogChannel (105) | Channel (116) | metop_Channel | FrameDiagnostics only; decoded-bit output retained |
| LogCADUs (106) | CADUs (117) | metop_CADUs | FrameDiagnostics only; functional count feedback retained |

No functional Goto/From pair was removed. There are no Goto/From blocks in the
simplified receiver. The unused legacy `MetopMonitor.m` source is retained for
historical reference; it is no longer instantiated by the model.

## What remains and what was added

The main path is:

```text
CS16FileSource -> DC Blocker -> Sample-Rate Converter -> AGC -> RRC
  -> QPSKSynchronization -> AutomaticChannelDecoder -> CADUWriter
```

The QPSK subsystem still contains the original coarse frequency compensator,
Gardner Symbol Synchronizer and Carrier Synchronizer. Its remaining Symbols
outport (SID 85) is now port 1. Both the channel decoder and the one retained
Constellation Diagram (SID 54) consume this output directly.

The functional controls remain:

- Source EOF directly drives Stop Simulation (SID 74).
- AcquisitionSupervisor (90) receives raw IQ and the preceding CADU count.
- CADUFeedback (93), PreviousCADUCount (94) and UpdateCADUCount (95) preserve
  the read-before-write ordering, including priorities 1 and 2.
- The supervisor enables/reset-controls synchronization and supplies state to
  the channel decoder, retaining acquisition and reacquisition from t=0.

Two Terminators were added: UnusedChannelStatus (118) and
QPSKSynchronization/UnusedFrequencyEstimate (119). They consume unused outputs
without changing the algorithms or enabling unconnected-port warnings.

Cosmetic block renames were CS16Input to CS16FileSource, CADUFile to CADUWriter,
Raised Cosine Receive Filter to RRC, and removal of the line break in
Sample-Rate Converter. Diagram positions and wire routes were adjusted.

All dialog parameters and execution priorities for the source, filters, AGC,
synchronization blocks, channel decoder, writer and acquisition/feedback blocks
were compared before and after the structural edits and matched. SHA-256 checks
also confirmed unchanged contents of `CS16FileSource.m`, `MetopAcquisition.m`,
`MetopChannelDecoder.m`, `MetopCADUWriter.m` and `metop_demapper.m`.

## Helpers and reporting

`metop_ahrpt_init` no longer configures the deleted monitor or spectrum scope.
`ShowScopes=true` enables the retained constellation display; its default is
false for unattended runs.

`metop_ahrpt_run` now stores the stop-event name and description as primitive
values. The earlier JSON export attempted to serialize a Simulink BlockPath
object and failed **after** a successful simulation. This reporting fix was
tested with an EOF run. No release-dependent StopEventTime property is assumed.

`metop_validate_run` temporarily logs acquisition state, channel status, CADU
count and EOF through native Simulink logging. It restores the prior signal
marks and does not save diagnostic settings into the model file.
`metop_compare_simplification` compares those logs to the legacy CSV and
checks the complete CADU bytes.

The first baseline attempt to override logging without marking signals did
not retain native logs. The completed simulation and its legacy CSV were
preserved; the baseline EOF report was recovered from execution metadata.
The later native-logging smoke test and full simplified run passed.
These initial reporting/logging failures were not treated as successful tests.

## Tests and output inspection

| Check | Result |
| --- | --- |
| Full input from zero before/after | PASS: equal CADUs, sampled controls and EOF |
| Model connectivity / Stateflow lint | Healthy; no unconnected ports or lines |
| MATLAB source and channel component tests | PASS: source format, reset, seeking, exact/partial EOF, all 16 soft/hard x rotation x pairing cases |
| CADU framing regression | PASS: false marker, inserted bit, reacquisition, odd chunks and partial terminal frame |
| Tiny EOF run and JSON report | PASS: 12-byte source, zero CADUs, ModelStop, readable report |
| Native validation logging smoke | PASS: all four signals present; expected NoCADUs assertion for tiny source |
| Visible direct constellation, real 60-62 s window | PASS: 822 CADUs, 841,728 bytes |
| CMake Release build and complete CTest suite | PASS: 161/161 entries |
| Python preview checks included in CTest | PASS: all four unittest cases |

Simulink Test was not installed, so MATLAB assertions and simulation replay
provided the model tests. The same single-precision RRC coefficient
quantization warning occurred before and after (first absolute error
approximately 2.28e-12); it was retained in the logs.

Both complete CADU files were decoded with the existing C++ executable. Their RS statistics, accepted scans and all five raw PGM files matched exactly:

| Decoder statistic | Result |
| --- | ---: |
| RS good frames | 78,194 |
| RS corrected frames | 32,370 |
| RS uncorrectable frames | 21,173 |
| Accepted AVHRR scans / APID 103 packets | 1,270 |
| APID 104 packets | 0 |
| Raw images | Five, each 2048 x 1270, channels 1/2/3A/4/5 |

The uncorrectable frames and resulting packet/scan gaps are present in the
baseline; simplification neither repairs nor hides them. Counts must not be
confused with the earlier separately supplied CADU capture, whose previews
have 1,346 rows. Different input/receiver runs are kept in different directories.

Current full-recording products are under `out/metop-20260731-1022-simplified/`: raw PGM files in `avhrr/scid_11_vcid_9_replay_0/` and five 2048 x 1270 PNG files in `previews/`. The earlier 2048 x 1346 PNGs remain separately under `out/png-previews-large/`.

PNG previews use a fixed linear 0-1023 to 0-255 mapping. Raw PGM samples remain
unchanged. Visual inspection shows clouds and land/coast detail, with a horizontal discontinuity near the lower edge; missing scans are not repaired. There is no calibration, contrast enhancement, rotation, geolocation,
interpolation or padding.

## Reproduce and locate evidence

Run instructions, exact block parameters and output conventions are in the
[Simulink README](../simulink/README.md). For a new recording:

```matlab
addpath('simulink')
metop_test_components
report = metop_validate_run("D:/captures/pass.cs16", ...
    "out/validation.cadu", CacheFolder="out/validation-cache");
```

Local evidence for this comparison is under ignored `out/simplify-check/`:
`before.slx`, `baseline.json`, both CADU files, the baseline physical CSV,
`after.cadu.validation.mat/json`, `comparison.json`,
`protected-parameters.mat`, `algorithm-hashes-before.json`,
`tags-before.json`, model-check output and test logs. The input recording and
generated products are not part of the public source repository.

The saved before/after results can be compared again without rerunning IQ:

```matlab
metop_compare_simplification("out/simplify-check/baseline.cadu", ...
    "out/simplify-check/baseline.cadu.physical.csv", ...
    "out/simplify-check/after.cadu", ...
    "out/simplify-check/after.cadu.validation.mat");
```

Validation covers this recording and the listed synthetic cases. It is evidence
for preservation of behavior on these inputs, not a proof for every possible
recording or a real-time performance guarantee.
