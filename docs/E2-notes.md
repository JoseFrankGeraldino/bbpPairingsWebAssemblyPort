# E2 notes: WebAssembly build of the pinned engine

**E2 is done (2026-11-09); README.md, "WebAssembly (E2)", describes what was
built and found.** Checked against these notes: the `-s` link flag, wasm32
limit messages and libc++ random distributions were as predicted; `-Werror`
with the newer clang needed one fork fix (`1b3c79f`, limits configuration
only); the PR #31 out-of-bounds report was a different defect than guessed
below (the Burstein bye iterator, fork `7462281`), not the `dutch.cpp` one.
Exceptions needed `-fwasm-exceptions`. The text below is kept as written.

Pointers collected during E1 for the next engine iteration (E2: Emscripten
build, Node runner, parity with the native build). Notes only; no WASM work was
done in E1.

## Goal of E2 (from the plan)

- Emscripten (MIT) build of `third_party/bbpPairings` at the pinned commit,
  from our own build script; the engine source stays unmodified.
- In-memory file system for TRF input/output; entry points callable from a
  Web Worker through a small TypeScript bridge.
- The **same suite** runs against it: `make test ENGINE=<node runner>`. The
  runner must accept exactly the native CLI's arguments, resolve file names
  relative to its working directory and return the engine's exit code.
- Parity: byte-identical output to the native build on every case.

## Upstream references (Apache 2.0; design input only, nothing copied)

- **PR #13** (2022): a Makefile-only `emcc` target producing a Node CLI. No
  source changes; useful for which Makefile variables an `emcc` build needs.
- **PR #31** (Dec 2025, "[WIP]", based on branch `dutch-2025`): adds
  `src/wasm.cpp`, an embind entry point `pairing(trf)` hard-coded to the Dutch
  system, a `wasm.sh` with the emcc flags, and a demo page. There is an
  unfixed "memory access out of bounds" report on it. It exposes **pairing
  only**, while FIDE TEC endorsement also needs the **checker** (`-c`) and the
  **random tournament generator** (`-g`): our build must expose all three
  modes (simplest: build `main.cpp` as is and drive it with `callMain`).
- Hypothesis worth checking: the out-of-bounds report may be the UB this suite
  found at `src/swisssystems/dutch.cpp:723-724` (`*sortedPlayers.front()` with
  nothing left to pair). The release native build survived it by chance; a
  WASM build may not. Fixed in the fork (`32155a7`, pinned since `0ee9df7`), so
  our WASM build starts without it.

## Makefile flags to revisit for emcc

- `-s` in `LDFLAGS` (strip, added when `debug=no`): under emcc `-s` introduces
  `-s SETTING=value` options; a bare `-s` will break or be misread. Build with
  `debug=yes` plus our own optimisation, or pass `optional_ldflags` explicitly.
- `-flto` (with `optimize=yes`): works with emcc/clang but changes the
  optimisation pipeline; decide deliberately, and test parity both ways.
- `-Werror` together with `COMP=clang`: the engine's clang warning list is
  long (based on clang 13) and emcc's clang is newer; expect new warnings that
  `-Werror` turns into errors. These are build fixes for the fork, not local
  patches.
- `-m64` / `bits`: do not set `bits` for wasm32. wasm32 is ILP32: `size_t`,
  `long` and pointers are 32-bit. That changes `tournament::round_index`
  (`size_t`), `std::minstd_rand::result_type` (`uint_fast32_t`) and every
  printed maximum. Expected golden differences, not engine bugs:
  `errors/generator-seed-out-of-range` (prints the seed maximum) and
  `errors/trf-xxr-out-of-range` (prints `maxRounds`). Decide whether E2 adds
  a per-target golden overlay for such limit messages or normalises them.
- Compile-time limits (`max_players`, `max_points`, `max_rating`,
  `max_rounds`): keep the defaults for the product build. The `limits` test
  group needs a WASM build with `LIMITS_VARS` too (passed as `ENGINE_LIMITS`);
  that configuration builds since fork commit `14efd0e`.
- `git describe` in the Makefile sets `VERSION_INFO`; it is normalised by the
  suite, but the WASM build should embed the same pinned commit information.

## Other parity risks found in E1

- **Random generator**: `tournament/generator.cpp` and `utility/random.h`
  use `std::uniform_int_distribution` / `std::uniform_real_distribution`.
  Their output is library-specific: libstdc++ (native) and libc++
  (Emscripten) produce different sequences from the same `minstd_rand`
  seed. The `generated/` and `generator/` goldens will therefore differ under
  WASM although pairing may be correct. For pairing parity, pair and check the
  **committed** generated TRFs (the `gen.trf` blocks in `tests/golden/generated/`)
  with both builds; treat generator output as a separate, library-dependent
  comparison.
- Build-dependent output from uninitialised state was found once (`bool
  roundsLine` in the model-preserving TRF writer, fixed in fork commit
  `472cbbc`). Parity failures that differ between optimisation levels point
  to this kind of UB first.
- Floating point: the generator uses `float` draws; check that emcc does not
  change rounding (it should not with default flags).
- Memory: large generated tournaments (150 players in
  `tests/fixtures/generator/large-150-players-9-rounds.cfg`) need
  `ALLOW_MEMORY_GROWTH`; the matching code allocates per round.

## External libraries to evaluate (licences unknown, check before any use)

- `marceljuenemann/tiebreak`
- `RufusK/swiss-tournament-calculator`

Both are listed for E3/E2 evaluation only. Licence must be on the allowed list
(MIT, BSD-2/3, ISC, zlib, Apache 2.0) before anything is used.
