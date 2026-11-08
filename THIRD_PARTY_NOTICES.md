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
| Pinned commit | `8f9e3c5ffdc4d7a08a33d31c6fb49b4e7acef4d5` (upstream master = tag v6.0.0 + 5 TRF-26 fix commits; suggested fork tag `v6.0.0-lessader.1`) |
| Shipped as | compiled binary in `dist/native-linux-x64/`, with the fork's `LICENSE.txt`, `Apache-2.0.txt` and our `NOTICE` next to it |

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
| Python 3 (standard library only) | test harness | PSF |
| gcovr | coverage report | BSD-3-Clause |

No test framework or other library is vendored.
