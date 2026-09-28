# CCSDS RS reference vectors

These small hexadecimal fixtures are **encoder-generated reference vectors**,
not CCSDS-published official test vectors. They were generated with Phil Karn's
independent libfec CCSDS encoder at
[quiet/libfec commit 9750ca0a6d0a786b506e44692776b541f90daa91](https://github.com/quiet/libfec/tree/9750ca0a6d0a786b506e44692776b541f90daa91),
using `encode_rs_ccsds(data, parity, 0)`.

The encoder's fixed parameters are `GF(256), 0x187, fcr=112, prim=11,
nroots=32, pad=0`. Its wrapper converts dual input to conventional basis and
parity back to dual basis. Production code here is an independent mathematical
implementation; it does not link libfec or contain its encoder/decoder source.
The optional oracle source files state LGPL licensing in their headers. They
are downloaded only into an ignored build directory. Normal builds/CTest are
offline and use the checked-in numeric vectors without third-party libraries.
No SatDump/GPL implementation was copied.

Authoritative parameter and basis reference:
[CCSDS 131.0-B-5](https://ccsds.org/Pubs/131x0b5.pdf), sections 4.3.3–4.3.9,
4.4.2 and annex F. The two 8-by-8 basis matrices in production code are
mathematical constants from section 4.3.9. The tests independently verify every
conversion through field traces with beta = alpha^117.

## Contents

Each line contains one complete codeword or CVCDU, in transmission byte order,
without ASM or randomization. Data and parity are both in CCSDS dual basis.

- `codewords.hex`: two 255-byte words, with data bytes 0..222 and 223 copies of
  FF respectively, followed by 32 reference parity symbols.
- `cvcdus.hex`: four 1020-byte CVCDUs with SCID 12, VCID 9, counters 0..3.
  Their FHPs are 0, 2047, 236, 0. A synthetic 2000-byte packet (APID 103) spans
  the first three frames; a 646-byte packet (104) completes the third zone.
  The fourth frame contains one 882-byte packet (105). These APIDs are test
  identifiers only; no instrument payload format is inferred.
- `generate_vectors.c`: original, optional fixture generator calling the
  external encoder. Its formulas completely specify all packet/header/data
  bytes. No decoder implementation is used to generate parity.

SHA-256 of concatenated **decoded bytes**, independent of hex-file line endings:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| codewords.hex | 510 | `34bf549ee1358fe4f9225c8d7c727d52bf85b841ba583a0fb381450cd6f973b0` |
| cvcdus.hex | 4080 | `0ec801d8ca80ece5c864e0b0b10d9b36900571844fd8582e6d35cdb1a562b781` |

## Optional regeneration

Run from the repository root in PowerShell with GCC on PATH. This is an audit
procedure, **not** a normal build prerequisite. Keep the external checkout and
executables under the ignored build directory.

```powershell
git clone https://github.com/quiet/libfec.git build-m5/libfec-reference
Set-Location build-m5/libfec-reference
git checkout 9750ca0a6d0a786b506e44692776b541f90daa91
gcc gen_ccsds.c init_rs_char.c -o gen_ccsds.exe
./gen_ccsds.exe | Set-Content -Encoding ascii ccsds_tab.c
gcc gen_ccsds_tal.c -o gen_ccsds_tal.exe
./gen_ccsds_tal.exe | Set-Content -Encoding ascii ccsds_tal.c
gcc -O2 -I. ../../decoder/tests/reference/generate_vectors.c encode_rs_ccsds.c encode_rs_8.c ccsds_tab.c ccsds_tal.c -o generate_vectors.exe
./generate_vectors.exe ../../decoder/tests/reference/codewords.hex ../../decoder/tests/reference/cvcdus.hex
Set-Location ../..
```

Stop if any compiler command fails. The decoder tests never regenerate expected
parity themselves: changing the decoder cannot silently change these expectations.
