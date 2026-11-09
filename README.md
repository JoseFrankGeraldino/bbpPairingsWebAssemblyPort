# bbpPairingsWebAssemblyPort

Build, test and packaging harness for the **BBP Pairings** Swiss-system chess
pairing engine (FIDE Dutch system), as used by TheChessSuperApp. The engine
itself is not developed here: it is our fork of bbpPairings, pinned as a git
submodule and built unmodified. This repository adds our own build entry
point, an engine-agnostic test suite with golden files, line coverage of the
engine sources with a 90% gate, a standalone FIDE tie-break tool checked
against FIDE's reference implementation, WebAssembly builds of both (checked
by the same suites) with a TypeScript bridge for the browser, and `dist/`
packaging with the licences.

State: E1 done (native Linux build, suite, coverage, dist). E3 done: the
tie-break tool for individual tournaments (milestone 1) and team tournaments
(milestone 2), every tie-break on FIDE's mandatory TRF-26 list, see
"Tie-break tool" below. E2 done: WebAssembly builds of the engine and the
tie-break tool, the same suites run against them, a browser bridge, see
"WebAssembly (E2)" below.

## Repository layout

| Path | What |
|---|---|
| `third_party/bbpPairings/` | git submodule: our fork of the engine (Apache 2.0). **Never edited from this repository.** |
| `Makefile` | our build entry point (`native`, `tiebreak`, `test`, `coverage`, `dist`, `golden-record`) |
| `tiebreak/` | our tie-break tool (C++20, MIT; rules ported from Gacrux, MIT, see `tiebreak/GACRUX-LICENSE.txt`) |
| `tests/run_tests.py` | black-box engine suite (Python 3 standard library only) |
| `tests/tiebreak_compare.py` | differential test of the tie-break tool against Gacrux, plus its error cases |
| `tests/tiebreak_inputs.py`, `tests/tiebreak_team_inputs.py` | synthetic individual tournaments and seeded team tournaments for that test |
| `tests/cases_inline.py`, `tests/trf_builder.py` | synthetic feature and error cases |
| `tests/roundtrip/trf_roundtrip.cpp` | the one white-box helper: TRF read -> write -> read |
| `tests/coverage_gate.py` | gcovr report + "no data" and minimum-coverage gates |
| `tests/fixtures/` | sample TRFs and generator configuration files |
| `tests/golden/` | golden outputs recorded from the pinned native build |
| `bridge/` | the browser bridge (TypeScript): `bbp-bridge.ts` runs a WebAssembly program, `bbp-worker.ts` runs it in a Web Worker |
| `tests/bridge/bridge_test.ts` | tests of the bridge on the browser builds (Node, no packages) |
| `tests/golden-wasm/` | golden files of the cases whose output depends on the build target (see "WebAssembly (E2)") |
| `.github/workflows/ci.yml` | CI (GitHub Actions, `ubuntu-latest`) |
| `docs/E2-notes.md` | notes from before the WebAssembly iteration (its findings are in "WebAssembly (E2)") |

## Licensing structure

- **Our code** — everything in this repository **outside `third_party/`** — is
  under the MIT License (root [`LICENSE`](LICENSE)).
- **The engine** in `third_party/bbpPairings` is BBP Pairings, Copyright
  2016-2026 Jeremy Bierema, under the **Apache License 2.0** (its own
  `LICENSE.txt` and `Apache-2.0.txt`).
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists every third-party
  component, test-data origin and tool. [`NOTICE`](NOTICE) is the notice for
  our distribution; it credits BBP Pairings.
- **The tie-break rules** in `tiebreak/` are a C++ port of Gacrux
  (TieBreakServer), Copyright (c) 2024 FIDE, MIT License
  ([`tiebreak/GACRUX-LICENSE.txt`](tiebreak/GACRUX-LICENSE.txt)); the port is
  MIT as well. As Gacrux's licence asks, products embedding it link to
  www.gacrux.no in their about section.
- Every built artifact ships with the fork's `LICENSE.txt` + `Apache-2.0.txt`,
  our `LICENSE` (as `LICENSE-tiebreak.txt`), `GACRUX-LICENSE.txt` and our
  `NOTICE` next to it (`make dist`).
- No GPL-sourced content is used anywhere, test data included. GCC and Make
  are used as tools only.

## Pinned engine

| | |
|---|---|
| Submodule | `third_party/bbpPairings` |
| Fork (the source we build) | https://github.com/JoseFrankGeraldino/bbpPairings |
| Branch | `master` (the first six fixes below were merged by fork PR #1, merge commit `b4db98e`; the last two were committed on `master`) |
| Pinned commit | **`1b3c79fac8ae82af1ab32dba8c5161bdd702372a`** (`1b3c79f`) = baseline + 8 fix commits (see "Engine fixes in the fork") |
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
- Python 3 (standard library only) for the suites and for Gacrux
- `gcovr` for the coverage report (`apt install gcovr`)
- for the WebAssembly builds: the Emscripten SDK **7.0.0** (emsdk; `make`
  takes `em++` from `PATH`, else sources `EMSDK_ENV`, default
  `~/emsdk/emsdk_env.sh`) and the Node.js it installs (24.x; runs TypeScript
  by type stripping)
- network access once, for `make gacrux` (clones Gacrux at the pinned commit
  into `build/gacrux`; `GACRUX_DIR=<clone>` uses an existing clone, which must
  be at that commit and clean)

## Build and test locally (WSL2 Ubuntu)

Work in a clone on the Linux file system (not under `/mnt/c`):

```bash
git clone --recurse-submodules https://github.com/JoseFrankGeraldino/bbpPairingsWebAssemblyPort.git ~/src/bbpPairingsWebAssemblyPort
cd ~/src/bbpPairingsWebAssemblyPort

make native                                 # -> build/native/bbpPairings
make tiebreak                               # -> build/native/tiebreak
make test ENGINE=build/native/bbpPairings   # engine suite (300 generated tournaments;
                                            # also builds build/native-limits, test
                                            # only) + tie-break comparison
make test-engine                            # engine suite only
make test-tiebreak                          # tie-break comparison only
BBP_GENERATED_COUNT=20 BBP_TIEBREAK_COUNT=3 make test   # quicker local run
make coverage                               # instrumented builds + suites + reports + gates
make dist                                   # -> dist/native-linux-x64/
make wasm                                   # -> build/wasm (Node CLIs), build/wasm/web (modules)
make test-wasm                              # the suites on the WebAssembly builds + bridge
make dist-wasm                              # -> dist/wasm/
git -C third_party/bbpPairings status       # must be clean
```

`make clean` removes `build/` and `dist/` (needed after switching compilers,
because objects are not rebuilt when only flags change).

## Test suite

`tests/run_tests.py --engine <executable>` runs every case against any
executable that behaves like the bbpPairings command line:
`build/native/bbpPairings`, or `build/wasm/bbpPairings` (the WebAssembly build
as a Node command line) with `--target wasm`. Each case copies its inputs into a fresh temporary directory, runs the
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
| `parity` | every tournament the native generator wrote into the `generated` and `generator` golden files, checked round by round (`-c`) with checklists: the pairing work behind the generator cases, the same on every build target | all |
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
limits group. The bump to `1b3c79f` changed the seven Burstein golden files
with a checklist (the bye player is now listed and marked, no player twice;
pairings unchanged) and added the `parity` group and the cases of the new
upstream-style test.

```bash
make golden-record CONFIRM=1b3c79f        # refuses unless CONFIRM equals the submodule's short SHA
make golden-record-wasm CONFIRM=1b3c79f   # tests/golden-wasm, from the WebAssembly build
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
- Result at `1b3c79f`: see the CI artifact; locally (GCC 15.2) 92.7% of
  lines, nothing excluded.
- **Tie-break tool:** all of `tiebreak/`, instrumented build in
  `build/obj/coverage-tiebreak`, run through `tests/tiebreak_compare.py`
  (`BBP_TIEBREAK_COVERAGE_COUNT` seeds per source), gate 85% lines (the
  workspace rule for new code), report `build/coverage/tiebreak-report/`.
  Locally 91.4% (milestone 2 changed lines: 90.1%).

## Engine fixes in the fork

Found by this suite (the first six at the baseline `8f9e3c5`, the last two by
the WebAssembly build), fixed in the fork (on branch `fix/e1-engine-defects`,
then on `master`; one commit each, each with an upstream-style
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
| `7462281` | Burstein kept an iterator past the bye player across `erase`; the re-insert was UB: with libstdc++ the checklist listed a player twice and left the bye player out, with libc++ (WebAssembly) the run aborted ("memory access out of bounds", as reported on upstream PR #31) | `samples/*/burstein-pair-file-checklist`, `upstream/burstein_bye_checklist-*` |
| `1b3c79f` | the limits configuration did not compile with clang (`-Wtautological-constant-out-of-range-compare` on a size guard) | `limits` group on WebAssembly (the build itself) |

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

## Tie-break tool (E3)

`build/native/tiebreak` computes FIDE tie-breaks (Handbook C.07) and the
resulting standings from a TRF16 or TRF-2026 file. It is a faithful C++20
port of the tie-break part of **Gacrux** (FIDE's reference implementation,
https://github.com/OttoMilvang/TieBreakServer, MIT, Copyright (c) 2024 FIDE),
pinned at commit `49b01e67ccb2f94945fa97b97ade66a35584b341` (2026-10-15); the
port keeps Gacrux's function names (`tiebreak/tiebreak.cpp` reads side by
side with `gacrux/tiebreak.py`) and Python's decimal arithmetic
(`tiebreak/decimal.h`), so values print exactly as Gacrux prints them.

```bash
build/native/tiebreak -i tournament.trf                 # rank order of record 202/212
build/native/tiebreak -i tournament.trf -t PTS DE BH/C1 BH SB
build/native/tiebreak -i tournament.trf -c -t PTS BH    # also check the file's ranks
```

Options (same meaning as Gacrux's `tiebreakchecker.py`): `-t <descriptor>...`
rank order descriptors (`BH/C1`, `SB/P`, `KS/L+1`, ...; default: the file's
record 202 or 212, else `PTS`), `-c` compare the computed ranks with the file,
`-p` / `-s` treat as pre-determined (round robin) / Swiss, `-u <rating>` rating
for unrated players, `-n <round>` standings after that round, `-r` rows in
rank order, `-d T|S|C|B` delimiter, `-o <file>` output. Output is the table
`tiebreakchecker.py -d T` writes (`StartNo`, `Rank`, one column per
descriptor; an empty value prints as `-0`).

Exit codes: 0 success, 1 the `-c` check found a rank that differs from the file
(**deliberate difference**: Gacrux exits 0 and reports this only in its JSON
status), 3 invalid file or request, 4 a number too large, 5 file not readable or
output not writable, 2 unexpected error.

**Individual tournaments (milestone 1):** every individual tie-break of FIDE's
mandatory list: DE, WIN, WON, BPG, BWG, REP, PS, BH, FB, SB, AOB, KS, ARO,
TPR, PTP, APRO, APPO with the C1/C2/M1/M2/P/F/Lx variants, plus the helpers
PTS, SNO/TPN, RANK, RTG, NUM, RIP, VUR, GE, COD, RTNG, ABH, AFB, and the team
criteria Gacrux also computes for players (EDE, EDET, EDEB, ESB, MPVGP after
PTS). Rules version by start date (record 042): before 2026-03-01 the 2024
rules, otherwise the 2026 rules (C.07 16.4).

**Team tournaments (milestone 2):** the team section of record 310 or the
older 013, with records 352 (board colours), 362 (match score system), 320
(pairing-allocated byes), 330 (forfeited matches), 300 (board orders), 240
team byes, 299 team assignments and penalties, and record 192's primary score.
Matches are built from the board games the way Gacrux's `games2matches` does.
Every team tie-break of the mandatory list: MPTS, GPTS, the shared ones with
`:MP` / `:GP` (WIN, WON, PS, BH, FB, AOB, KS with their variants), BC, TBR,
BBE, MPvGP, EMMSB/EMGSB/EGMSB/EGGSB (C1/C2/P), EDE, EDEBT, EDEBB, EDET, EDEB
(P), SSSC (F/P/Kx), plus the pair suffixes (`:MM`, `:MG`, `:GM`, `:GG`), ESB,
EDEC and the exchange modifier `/X`.

**Refused with exit 3:** national rating records (172/FID), a TRF16 file whose
points only fit a score system that has to be derived (add record 162),
Gacrux's non-tie-break helpers (ACC, FLT, RFP, TOP, RND, STD, CSQ, RSES), team
tie-breaks on an individual event, and the requests Gacrux itself cannot
answer (it stops with "Program error"): an SSSC whose normalising factor
rounds down to 0 (early rounds of an event with many boards; give `/K`), MPvGP
before any primary score, a player in a game who is in no team.

**Verification:** `tests/tiebreak_compare.py` runs every input through Gacrux
and through the tool with the same arguments and requires identical output:
all samples plus tournaments generated by the pinned engine with fixed seeds
(default generator and every `tests/fixtures/generator` configuration), the
synthetic tournaments of `tests/tiebreak_inputs.py` (records 042, 162,
202/212, 240, 299, XXZ; single, odd and double round robins; unrated players
and result codes; CRLF and a byte order mark), each in 9 variants (all
mandatory descriptors; the same for an intermediate round, as pre-determined,
as Swiss, with `-u 1400`; the file's own order; a checked and a rank-sorted
standing; helper values and non-mandatory modifiers), and the team
tournaments of `tests/tiebreak_team_inputs.py` (seeded: Swiss and round
robins, 2-6 boards, reserves, every team record above, board forfeits and
absences; plus one input per forced feature, including two teams that only
the EDE board criteria separate), each in 10 variants (the same on the team
list, the game points as primary score, the board criteria first on a fully
tied field, team helpers). The one reference defect met (SSSC dividing by
zero) is listed in the script and must be refused by our tool; it is counted
in the summary. 2026-11-09, locally: 711 individual inputs x 9 and 113 team
inputs x 10 (100 seeds per source), **0 differences**. CI runs 20 seeds per
source.

## WebAssembly (E2)

The engine and the tie-break tool are compiled with **Emscripten 7.0.0**
(pinned: `EMSDK_VERSION` in the Makefile, emsdk commit in CI). The engine is
built by its own Makefile, unmodified: `COMP=clang` (its clang warning set,
`-Werror`), `CXX="em++ -fwasm-exceptions"` (WebAssembly exception handling:
the engine reports every error by an exception, which Emscripten aborts on by
default) and `optional_ldflags` for the link (the default carries a bare `-s`,
GNU ld's strip, which emcc reads as a setting). The tie-break tool is built
with our warning set, as natively.

Two shapes of each program:

| Build | Files | Use |
|---|---|---|
| Node command line (`make wasm-node`, `wasm-limits`, `wasm-tools`, `wasm-tiebreak`) | `build/wasm/bbpPairings`, `build/wasm/tiebreak`, `build/wasm/trf-roundtrip`, `build/wasm-limits/bbpPairings` (each a loader with a `node` shebang + its `.wasm`) | the same command-line contract as the native programs (real files, exit code), so the suites run against them unchanged |
| browser module (`make wasm-web`) | `build/wasm/web/bbpPairings.mjs` + `.wasm`, `tiebreak.mjs` + `.wasm` (ES modules, `MODULARIZE`, in-memory file system, web/worker/node) | the main app, through `bridge/` |

**Bridge** (`bridge/bbp-bridge.ts`, `bridge/bbp-worker.ts`, TypeScript with
erasable syntax only): `runProgram(factory, { args, files, outputs })` runs
one program on a fresh module instance with the native command-line
arguments and returns `{ exitCode, stdout, stderr, files }` (bytes);
`pair`, `check`, `generate` and `tiebreak` wrap the usual calls. A compiled
`WebAssembly.Module` can be shared between runs. `bbp-worker.ts` is a module
Web Worker answering `{ id, program, request }` messages, and
`createWorkerClient(worker)` the page side, so pairing never blocks the UI.
The app's bundler compiles the `.ts` files; they are tested under Node.

**Tests** (`make test-wasm`):

- `test-wasm-engine`: `tests/run_tests.py --target wasm` against the Node
  command lines, every group including `limits`, `roundtrip` (the white-box
  helper compiled to WebAssembly) and `parity`. Every case must equal the
  native golden file, except the cases listed in `TARGET_DIFFERENCES`
  (run_tests.py), each with its reason, which compare with
  `tests/golden-wasm/` (recorded from the WebAssembly build only by `make
  golden-record-wasm`):
  - the generator (`generated/`, `generator/`, two generator error cases, and
    `generated/dutch-seed-0262`): its random distributions are libc++'s, not
    libstdc++'s, so the same seed gives another tournament (seed 262 gives one
    whose round 14 has no valid pairing; the native engine agrees on that
    tournament). The `parity` group still checks, on every target, the
    pairings of the tournaments the native generator wrote;
  - two limit messages that print a 32-bit maximum (wasm32 is ILP32);
  - a directory given as input file, which Emscripten's Node file system opens
    as an empty file (for these two the overlay golden, exit code included,
    replaces the exit-code check).
- `test-wasm-tiebreak`: the Gacrux comparison with the WebAssembly tie-break
  tool, on tournaments generated by the WebAssembly engine.
- `test-wasm-bridge`: `tests/bridge/bridge_test.ts`, every run through the
  bridge compared byte for byte with the native command line (pairing and
  checking every sample with both systems, usage and errors, tie-breaks of
  individual and team events) and with the WebAssembly command line for the
  generator; plus fresh instances, the shared compiled module, the host's
  exit code left alone, and the worker's message handling.

Result 2026-11-09, locally: engine suite 701/701 on WebAssembly (the same
cases as natively), tie-break comparison 0 differences, bridge 60/60.

Found on the way and fixed in the fork: the Burstein bye bug (`7462281`, the
"memory access out of bounds" of upstream PR #31; natively it had silently
corrupted the Burstein checklist) and a clang build error of the limits
configuration (`1b3c79f`).

## Packaging (`make dist`)

`dist/native-linux-x64/`:

| File | |
|---|---|
| `bbpPairings` | the native release binary |
| `tiebreak` | the tie-break tool |
| `LICENSE.txt`, `Apache-2.0.txt` | the engine's licence, copied from the fork |
| `LICENSE-tiebreak.txt`, `GACRUX-LICENSE.txt` | the tie-break tool's licence (ours, MIT) and the licence of the Gacrux code it ports (MIT) |
| `NOTICE` | our notice, crediting BBP Pairings and Gacrux |
| `VERSION` | fork URL, branch, commit SHA, fork tag at that commit (or `none`), the baseline tag, upstream base, this repository's commit (`-dirty` if uncommitted changes), the Gacrux commit the tool is checked against |

This is the shape the main app consumes for its server.

`make dist-wasm` -> `dist/wasm/`: `bbpPairings.mjs` + `bbpPairings.wasm`,
`tiebreak.mjs` + `tiebreak.wasm`, `bbp-bridge.ts`, `bbp-worker.ts`, the same
licence files and `NOTICE`, the licence texts of the Emscripten runtime and
of the libraries compiled into the `.wasm` files (`EMSCRIPTEN-LICENSE.txt`,
`LIBCXX-LICENSE.txt`, `LIBCXXABI-LICENSE.txt`, `MUSL-COPYRIGHT.txt`), and
`VERSION` (fork commit, this repository's commit, Gacrux commit, Emscripten
version). This is the shape the main app consumes for the browser.

## CI jobs and their local equivalents

`.github/workflows/ci.yml` runs on every push and pull request (actions
pinned by commit SHA):

| Job | Local equivalent (WSL2 Ubuntu, repo root) |
|---|---|
| `native` | `git submodule status && test -z "$(git -C third_party/bbpPairings status --porcelain)" && make native && make test ENGINE=build/native/bbpPairings && make dist` |
| `wasm` | `make wasm && make test-wasm && make dist-wasm` (CI installs emsdk 7.0.0 from its pinned commit into `build/emsdk`; 100 generated tournaments, 5 tie-break seeds per source) |
| `coverage` | `sudo apt-get install -y gcovr && make coverage` |

Every job checks out Gacrux at the pinned commit into `build/gacrux` first;
locally `make gacrux` clones it. Artifacts: `native-linux-x64` (the `dist/`
folder), `wasm` (`dist/wasm`) and `coverage-report` (engine and tie-break
tool reports).
