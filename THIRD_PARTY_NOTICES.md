# Third-party notices

The root `LICENSE` (MIT) covers our code: everything in this repository
outside `third_party/`. The components below keep their own licences.

## BBP Pairings (pairing engine)

| | |
|---|---|
| Location | `third_party/bbpPairings` (git submodule, never edited from this repository) |
| Licence | Apache License 2.0 (`third_party/bbpPairings/LICENSE.txt`, `third_party/bbpPairings/Apache-2.0.txt`) |
| Copyright | Copyright 2016-2026 Jeremy Bierema |
| Upstream | https://github.com/BieremaBoyzProgramming/bbpPairings |
| Our fork (the source we build) | https://github.com/JoseFrankGeraldino/bbpPairings, branch `master` |
| Pinned commit | `1b3c79fac8ae82af1ab32dba8c5161bdd702372a`: fork baseline `8f9e3c5` (upstream master = tag v6.0.0 + 5 TRF-26 fix commits, fork tag `v6.0.0-lessader.1`) + 8 bugfix commits of ours (Apache 2.0, as the rest of the fork) |
| Shipped as | compiled binary in `dist/native-linux-x64/`, and as WebAssembly (`bbpPairings.mjs` + `bbpPairings.wasm`) in `dist/wasm/`, each with the fork's `LICENSE.txt`, `Apache-2.0.txt` and our `NOTICE` next to it |

## Gacrux (tie-break rules, ported)

| | |
|---|---|
| Location | `tiebreak/` (our C++ port; the Python original is not copied into this repository) |
| Licence | MIT (`tiebreak/GACRUX-LICENSE.txt`, verbatim from the original) |
| Copyright | Copyright (c) 2024 FIDE; source code developed by Otto Milvang and donated to FIDE |
| Original | https://github.com/OttoMilvang/TieBreakServer ("Gacrux tiebreak"), https://www.gacrux.no |
| Ported from | commit `49b01e67ccb2f94945fa97b97ade66a35584b341` (2026-10-15): `gacrux/tiebreak.py`, `rating.py`, `fidetables.py` (FIDE B.02 tables), and the TRF reading of `trf2json.py`, `scoresystem.py`, `chessjson.py` |
| Shipped as | compiled into `tiebreak` in `dist/native-linux-x64/` and into `tiebreak.mjs` + `tiebreak.wasm` in `dist/wasm/`, with `GACRUX-LICENSE.txt` next to it |
| Licence request | "If you embed this software in your product, please add a link to www.gacrux.no in the about section" |
| Also used as | the reference the tool is tested against (`tests/tiebreak_compare.py`), run from a clone at the same commit; never shipped |

## Emscripten runtime and libraries (WebAssembly builds only)

The WebAssembly builds in `dist/wasm/` are produced with Emscripten 7.0.0
(emsdk commit `1693ce7359fc9694f198694f902a2cc061239b86`). The `.mjs`
loaders contain Emscripten's JavaScript runtime, and the `.wasm` files
contain the C and C++ libraries Emscripten links in. Their licence texts are
copied from the SDK into `dist/wasm/` by `make dist-wasm`.

| Component | In | Licence (file in `dist/wasm/`) |
|---|---|---|
| Emscripten runtime (JavaScript glue) | `*.mjs` | MIT or University of Illinois/NCSA (`EMSCRIPTEN-LICENSE.txt`) |
| libc++ and libc++abi (LLVM) | `*.wasm` | Apache 2.0 with LLVM exception (`LIBCXX-LICENSE.txt`, `LIBCXXABI-LICENSE.txt`) |
| musl libc, as adapted by Emscripten | `*.wasm` | MIT (`MUSL-COPYRIGHT.txt`) |

## Test data

| Files | Origin | Licence |
|---|---|---|
| `tests/fixtures/samples/synthetic-*.trf` | Go module `github.com/gnutterts/chesspairing` v0.6.0, copied there from bbpPairings reference data | Apache 2.0 |
| `third_party/bbpPairings/test/tests/*` (read in place, not copied) | upstream regression tests | Apache 2.0 |
| everything else under `tests/` (generator configs, inline cases, golden files) | written for or produced by this repository | MIT |

No GPL-sourced content is used anywhere in this repository, test data included.

## Tools (not linked into anything we ship)

| Tool | Use | Licence |
|---|---|---|
| GCC / GNU Make | compiler and build tool | GPL (tools only; the GCC runtime library exception applies to the built binary) |
| Python 3 (standard library only) | test harness, running Gacrux | PSF |
| Gacrux at the pinned commit (stdlib-only tie-break path; its `networkx` dependency is for pairing and is not installed) | tie-break reference in tests | MIT |
| gcovr | coverage report | BSD-3-Clause |
| Emscripten SDK 7.0.0 (emsdk, clang/LLVM, Binaryen) | WebAssembly compiler (its runtime and libraries end up in `dist/wasm/`, see above) | MIT / NCSA; LLVM: Apache 2.0 with LLVM exception |
| Node.js (the one emsdk installs, 24.x) | runs the WebAssembly command lines and the bridge test (TypeScript by Node's type stripping; no npm packages) | MIT |

No test framework or other library is vendored.
