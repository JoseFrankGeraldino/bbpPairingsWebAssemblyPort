# bbpPairingsWebAssemblyPort

Build, test and packaging harness for the **BBP Pairings** Swiss-system chess
pairing engine (FIDE Dutch system), as used by TheChessSuperApp. The engine
itself is not developed here: it is our fork of bbpPairings, pinned as a git
submodule and built unmodified. This repository adds our own build entry
point, an engine-agnostic test suite with golden files, line coverage of the
engine sources with a 90% gate, and `dist/` packaging with the licences.

Iteration E1 (this state): native Linux build only. WebAssembly (E2) and the
standalone FIDE tie-break tool (E3) come later and will be judged by this same
suite; see [docs/E2-notes.md](docs/E2-notes.md).

## Repository layout

| Path | What |
|---|---|
| `third_party/bbpPairings/` | git submodule: our fork of the engine (Apache 2.0). **Never edited from this repository.** |
| `Makefile` | our build entry point (`native`, `test`, `coverage`, `dist`, `golden-record`) |
| `tests/run_tests.py` | black-box test suite (Python 3 standard library only) |
| `tests/cases_inline.py`, `tests/trf_builder.py` | synthetic feature and error cases |
| `tests/roundtrip/trf_roundtrip.cpp` | the one white-box helper: TRF read -> write -> read |
| `tests/coverage_gate.py` | gcovr report + "no data" and 90% gates |
| `tests/fixtures/` | sample TRFs and generator configuration files |
| `tests/golden/` | golden outputs recorded from the pinned native build |
| `.github/workflows/ci.yml` | CI (GitHub Actions, `ubuntu-latest`) |
| `docs/E2-notes.md` | pointers for the WebAssembly iteration |

## Licensing structure

- **Our code** — everything in this repository **outside `third_party/`** — is
  under the MIT License (root [`LICENSE`](LICENSE)).
- **The engine** in `third_party/bbpPairings` is BBP Pairings, Copyright
  2016-2026 Jeremy Bierema, under the **Apache License 2.0** (its own
  `LICENSE.txt` and `Apache-2.0.txt`).
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists every third-party
  component, test-data origin and tool. [`NOTICE`](NOTICE) is the notice for
  our distribution; it credits BBP Pairings.
- Every built artifact ships with the fork's `LICENSE.txt` + `Apache-2.0.txt`
  and our `NOTICE` next to it (`make dist`).
- No GPL-sourced content is used anywhere, test data included. GCC and Make
  are used as tools only.

## Pinned engine

| | |
|---|---|
| Submodule | `third_party/bbpPairings` |
| Fork (the source we build) | https://github.com/JoseFrankGeraldino/bbpPairings |
| Branch | `master` (the fixes below are on `fix/e1-engine-defects` until merged into `master` by fast-forward) |
| Pinned commit | **`0ee9df74304a8ba7febb84811c2771ab1c8def6e`** (`0ee9df7`) = baseline + 6 fix commits (see "Engine fixes in the fork") |
| Baseline | `8f9e3c5` = upstream `master`, tagged **`v6.0.0-lessader.1`** in the fork; golden files were first recorded there |
| Upstream | https://github.com/BieremaBoyzProgramming/bbpPairings |
| Upstream base | tag **v6.0.0** + 5 commits (TRF-26 fixes: line 162 absence points, 2026 tournament type codes) |

Rules:

- Files under `third_party/` are never edited from this repository; the
  engine is built with its own Makefile, out of tree (objects go to
  `build/obj/`), so `git -C third_party/bbpPairings status` stays clean.
- Any engine change (bugfix, feature, build fix) is a commit in the fork,
  reviewed there, followed by a **pin bump** here.
- A pin bump re-runs the suite; golden differences are reviewed and, if
  accepted, re-recorded with `make golden-record CONFIRM=<new short sha>` in
  the same change.

## Toolchain

Linux only (WSL2 Ubuntu locally, `ubuntu-latest` in CI); no MSVC build.

- `g++` with C++20 (GCC 11 or newer; tested locally with GCC 15.2, CI uses
  the runner's default GCC), GNU Make, git
- Python 3 (standard library only) for the suite
- `gcovr` for the coverage report (`apt install gcovr`)

## Build and test locally (WSL2 Ubuntu)

Work in a clone on the Linux file system (not under `/mnt/c`):

```bash
git clone --recurse-submodules https://github.com/JoseFrankGeraldino/bbpPairingsWebAssemblyPort.git ~/src/bbpPairingsWebAssemblyPort
cd ~/src/bbpPairingsWebAssemblyPort

make native                                 # -> build/native/bbpPairings
make test ENGINE=build/native/bbpPairings   # full suite (300 generated tournaments);
                                            # also builds build/native-limits (test only)
BBP_GENERATED_COUNT=20 make test            # quicker local run
make coverage                               # instrumented build + suite + report + gate
make dist                                   # -> dist/native-linux-x64/
git -C third_party/bbpPairings status       # must be clean
```

`make clean` removes `build/` and `dist/` (needed after switching compilers,
because objects are not rebuilt when only flags change).

## Test suite

`tests/run_tests.py --engine <executable>` runs every case against any
executable that behaves like the bbpPairings command line: today
`build/native/bbpPairings`, in E2 a Node runner script for the WebAssembly
build. Each case copies its inputs into a fresh temporary directory, runs the
engine there with relative file names, and renders exit code, stdout, stderr
and output files into one block that is compared **byte for byte** with
`tests/golden/<case>.txt`. Build-dependent text (version string, build date,
the engine's own path in usage messages) is normalised first.

| Group | What | Golden |
|---|---|---|
| `samples` | every TRF in `tests/fixtures/samples`, Dutch and Burstein: pair to stdout, pair to a file with checklist, check (`-c`) with checklist | all |
| `upstream` | the engine's own regression inputs (`third_party/bbpPairings/test/tests`, read in place): where a test compares with `.output.expected`, the same command must reproduce it, plus the other mode; tests with their own checks (e.g. error output) are run in both modes | all |
| `generated` | `-g` with fixed seeds 1..N (N = `BBP_GENERATED_COUNT`, default 300) for Dutch, N/10 for Burstein; every generated tournament is re-checked with `-c` and must show no discrepancy | seeds 1-20 (Dutch), 1-5 (Burstein) |
| `generator` | `-g` with the configuration files in `tests/fixtures/generator` and with model files; each result checked with `-c` | all |
| `features` | synthetic TRFs for each input feature: 192 type codes incl. Baku acceleration, XXC/152 colours, XXC rank, 162 and BB* point systems, 250/XXA accelerations, 260/XXP forbidden pairs, 240 byes, result codes, CR/CRLF, UTF-8 names, no valid pairing | all |
| `errors` | bad arguments, missing/unwritable files, malformed TRF (one defect per case), generator configuration errors, values beyond the build's type limits; exit codes and messages | all |
| `roundtrip` | TRF read -> write -> read for every sample and upstream input (see below) | all |
| `limits` | against `ENGINE_LIMITS`, the engine built with small compile-time limits (`LIMITS_VARS` in the Makefile: `max_players=40 max_points=99 max_rating=255 max_rounds=9`, no checker/generator): player IDs, ratings, scores, score sums and rounds beyond the build's types; modes left out of that build | all |

The Burstein system is exercised only because its code is part of the
coverage scope; it is not FIDE-endorsed and not in our product scope.

Seeds are fixed everywhere; nothing depends on time. The runner fails on a
missing golden file and on a golden file that no longer has a case.

### Golden files

Recorded from the **native release build of the pinned commit** and changed
only by a reviewed decision (a pin bump). First recorded at the baseline
`8f9e3c5`; the bump to `0ee9df7` changed exactly two of them
(`errors/trf-invalid-utf8`, `generator/model-issue_15-dutch`), both as the
fixes intend, and added cases for the new upstream-style tests and the
limits group.

```bash
make golden-record CONFIRM=0ee9df7   # refuses unless CONFIRM equals the submodule's short SHA
```

`tests/fixtures/**` and `tests/golden/**` are `-text` in `.gitattributes`
(the engine writes CR line endings; git must not convert them).

### Round-trip (read -> write -> read)

The CLI has no "read a TRF and write it back" mode, so
`tests/roundtrip/trf_roundtrip.cpp` links the engine's objects (built by the
engine Makefile) and calls `fileformats::trf::readFile` / `writeFile`
directly. For each file it checks that the canonical writer is lossless for
what the engine models (write -> read -> write is identical) and that the
model-preserving writer's output reads back to the same tournament, and it
lists every input line the writer does not reproduce verbatim. Documented,
not skipped — what the engine's TRF writers do **not** preserve:

- **Canonical writer** (used for generated tournaments): only the tournament
  model survives. Names become `TestNNNN PlayerNNNN`; sex, title, federation,
  FIDE ID and birth date are dropped; all header tags except `142` (rounds),
  `092` and `162` (points) are dropped. The pairing system is written as
  `092 FIDE_DUTCH_2025` but read only from `192`, so it is not round-tripped
  (the CLI flag supplies it). Accelerations are not written at all ("Outputting
  accelerations is not currently supported"); no sample uses them.
- **Model-preserving writer** (used with `model-file -g`): `012` lines are
  prefixed with `### `; `XXC white1`/`black1` lines are dropped (`XXC rank` is
  kept); opponent numbers are re-padded (`0005` -> `   5`); points, rank and
  round columns of `001` lines are rewritten from the model; rounds a player
  did not play become explicit `0000 - Z` blocks; a missing `142`/`XXR` line is
  appended as `142 <rounds>`.
- **Team tournaments** (`synthetic-trf2026-team-swiss.trf`, tag `192
  FIDE_TEAM_BAKU`, lines `013`/`310`) cannot be read at all: the engine pairs
  individual Swiss events only. Expected; the sample stays as an error case.

## Coverage

- **Scope (decided):** all upstream sources under `third_party/bbpPairings/src`
  (`.cpp` and `.h`), nothing excluded; our `tests/` are not in the scope.
- **Build:** the engine's own Makefile with `optimize=no` (drops `-O3` and
  `-flto`) and `CXX="g++ --coverage -D_GLIBCXX_NO_ASSERTIONS"`, objects in
  `build/obj/coverage`; the same again with `LIMITS_VARS` in
  `build/obj/coverage-limits`. Every other flag is the engine's release
  configuration. No out-of-tree compile with our own flags.
  `-D_GLIBCXX_NO_ASSERTIONS` keeps the unoptimised build's library behaviour
  equal to the release build (GCC 15 enables libstdc++ assertions by default
  only when not optimising).
- **Run:** the full suite against the instrumented binaries (and the round-trip
  helper linked to the instrumented objects); gcovr merges both builds. With
  `--instrumented`, cases listed in `KNOWN_ENGINE_UB` (run_tests.py) would be
  excluded there **and named in the output**; the list is empty since the fork
  fixes.
- **Report:** `build/coverage/report/` — `index.html` (HTML details),
  `summary.json`, `summary.txt`, `cobertura.xml`, `per-file.txt`,
  `uncovered-lines.txt`; uploaded as a CI artifact.
- **Gates** (`tests/coverage_gate.py`): fails when no `.gcda` data exists or
  gcovr measures zero lines, and when line coverage is below 90%.
- Result at `0ee9df7`: see the CI artifact; locally (GCC 15.2) 92.7% of
  lines, nothing excluded.

## Engine fixes in the fork

Found by this suite at the baseline `8f9e3c5`, fixed in the fork on branch
`fix/e1-engine-defects` (one commit each, each with an upstream-style
regression test in the fork's `test/tests/` where a default build can reach
it), then pinned here:

| Fork commit | Defect | Seen as |
|---|---|---|
| `32155a7` | `dutch.cpp` dereferenced `sortedPlayers.front()` when no player was left to pair (UB; unoptimised builds crashed) | `features/history10-bye-column-dutch-check`, `upstream/dutch_2025_C5-other-mode` |
| `472cbbc` | model-based TRF writer read an uninitialised `bool roundsLine`; release builds dropped the `142` line | `generator/model-issue_15-dutch` |
| `bcffb41` | invalid UTF-8 ended as "Unexpected error (please report)" (exit 2) instead of a format error (exit 3) | `errors/trf-invalid-utf8` |
| `d3bf93f` | a blank `XXA` entry was only accepted as the last entry (`substr` length vs end index) | `upstream/xxa_blank_entry-*` |
| `14efd0e` | builds with custom limits or `burstein=no` / `dutch=no` / `engine_comparison=no` did not compile under `-Werror` | `limits` group (the build itself) |
| `0ee9df7` | limit messages for types narrower than `int` (`toString(~T{ })` = `toString(-1)`) looped forever | `limits/rating-beyond-type` |

## Known engine limitations (not fixed)

- **Small `max_players` / `max_rounds` need `engine_comparison=no`**: the
  generator asserts at compile time that its random ranges fit (ratings up to
  2800, up to 15 rounds, its player counts). Intended, not a defect.
- **`engine_comparison=no` with `optimize=yes`** fails at the LTO link with a
  GCC 15 `-Wstrict-overflow` diagnostic inside libstdc++ (`stl_deque.h`), a
  compiler false positive turned into an error by `-Werror`. The limits build
  therefore uses `optimize=no`. Changing the engine's warning set is a fork
  decision.
- **`max_players` sizes a type, it is not a hard cap**: with `max_players=40`
  the build pairs 41 players (`limits/player-count-above-max-players`) and only
  rejects IDs beyond the 8-bit type, while the message says "up to 40".

## Packaging (`make dist`)

`dist/native-linux-x64/`:

| File | |
|---|---|
| `bbpPairings` | the native release binary |
| `LICENSE.txt`, `Apache-2.0.txt` | the engine's licence, copied from the fork |
| `NOTICE` | our notice, crediting BBP Pairings |
| `VERSION` | fork URL, branch, commit SHA, fork tag at that commit (or `none`), the baseline tag, upstream base, this repository's commit (`-dirty` if uncommitted changes) |

This is the shape the main app consumes for its server.

## CI jobs and their local equivalents

`.github/workflows/ci.yml` runs on every push and pull request (actions
pinned by commit SHA):

| Job | Local equivalent (WSL2 Ubuntu, repo root) |
|---|---|
| `native` | `git submodule status && test -z "$(git -C third_party/bbpPairings status --porcelain)" && make native && make test ENGINE=build/native/bbpPairings && make dist` |
| `coverage` | `sudo apt-get install -y gcovr && make coverage` |

Artifacts: `native-linux-x64` (the `dist/` folder) and `coverage-report`.
