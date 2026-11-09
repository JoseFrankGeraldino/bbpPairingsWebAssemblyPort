#!/usr/bin/env python3
"""Engine-agnostic black-box test suite for bbpPairings builds.

Part of bbpPairingsWebAssemblyPort (MIT). Python 3 standard library only.

The engine under test is any executable that behaves like the bbpPairings
command line (``--engine``): the native build, or the WebAssembly build as a
Node command line (``--target wasm``, which compares the cases listed in
TARGET_DIFFERENCES with ``tests/golden-wasm/``). Every case copies its input files into a fresh
working directory, runs the engine there with relative file names, and renders
exit code, stdout, stderr and any output files into one text block. That block
is compared byte for byte with a golden file under ``tests/golden/``. Golden
files are recorded once from the native build of the pinned engine commit
(``make golden-record``) and change only by a reviewed decision.

Some cases also carry semantic checks (for example: a generated tournament
must pass the engine's own checker with no discrepancies).

Case groups:
  samples     every TRF in tests/fixtures/samples (pair, pair+checklist, check)
  upstream    the regression inputs shipped in third_party/bbpPairings/test
  generated   -g with fixed seeds (count: BBP_GENERATED_COUNT), each checked
              with -c; a fixed subset is golden
  generator   -g with configuration files and model files (golden)
  features    synthetic TRFs for each input feature (tests/cases_inline.py)
  errors      malformed input, missing files, limits, bad arguments (golden)
  roundtrip   TRF read -> write -> read through tests/roundtrip (golden)
  limits      build-limit errors, against --engine-limits (golden)
  parity      the tournaments of the generated and generator golden files,
              checked round by round with checklists (golden)
"""

from __future__ import annotations

import argparse
import concurrent.futures
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Optional

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
FIXTURES = TESTS / "fixtures"
GOLDEN = TESTS / "golden"
UPSTREAM_TEST_DATA = ROOT / "third_party" / "bbpPairings" / "test" / "tests"

sys.path.insert(0, str(TESTS))
import cases_inline  # noqa: E402  (tests/cases_inline.py)

STEP_TIMEOUT_SECONDS = 300

# Seeds whose generated tournaments are golden (the "fixed subset").
GOLDEN_GENERATED_SEEDS = range(1, 21)
GOLDEN_GENERATED_BURSTEIN_SEEDS = range(1, 6)

# Cases whose input hits known undefined behaviour in the pinned engine
# (case name -> description). They always run, golden-compared, against
# release builds; --instrumented (unoptimized coverage build, where UB tends to
# crash) excludes them and names them in the summary. Add an entry only
# together with a fork fix in progress, and remove it with the pin bump.
# Empty since the fixes in fix/e1-engine-defects (README.md, "Engine fixes in
# the fork").
KNOWN_ENGINE_UB: dict[str, str] = {}

# Cases whose output depends on the build target and not on the engine logic
# (name or "group/" prefix -> reason). With --target wasm they are compared
# with tests/golden-wasm/<name>.txt, recorded from the WASM build only through
# --record --target wasm; every other case must equal the native golden file.
# The parity group checks the pairing work behind the generator cases on the
# tournaments the native build generated, so a generator difference cannot
# hide a pairing difference.
GENERATOR_DRAWS = (
    "random distributions: libc++ (Emscripten) and libstdc++ draw different"
    " numbers from the same minstd_rand seed, so the generator writes another"
    " tournament"
)
TARGET_DIFFERENCES: dict[str, dict[str, str]] = {
    "native": {},
    "wasm": {
        "generated/": GENERATOR_DRAWS,
        "generator/": GENERATOR_DRAWS,
        "errors/generator-config-cr-line-endings": GENERATOR_DRAWS,
        "errors/generator-config-score-overflow": GENERATOR_DRAWS,
        "errors/generator-seed-out-of-range":
            "wasm32 is ILP32: the integer type of the seed is 32 bits wide",
        "errors/trf-xxr-out-of-range": "wasm32: size_t (round index) is 32 bits wide",
        "errors/file-directory-as-input":
            "Emscripten's Node file system (NODERAWFS) opens a directory as an"
            " empty file",
        "errors/file-directory-as-config":
            "Emscripten's Node file system (NODERAWFS) opens a directory as an"
            " empty file",
        "generated/dutch-seed-0262":
            GENERATOR_DRAWS + "; with 17 players its round 14 has no valid pairing"
            " (the native engine agrees on the first 13 rounds and on round 14),"
            " so the generator stops with exit 1",
    },
}
GOLDEN_BY_TARGET = {"native": GOLDEN, "wasm": TESTS / "golden-wasm"}
# Listed cases whose exit code itself differs on the target: their overlay
# golden file (output and exit code) is the whole check.
TARGET_EXIT_DIFFERENCES: dict[str, set[str]] = {
    "native": set(),
    "wasm": {
        "errors/file-directory-as-input",
        "errors/file-directory-as-config",
        "generated/dutch-seed-0262",
    },
}

# Inputs the engine rejects on purpose; they cannot serve as generator models.
UNREADABLE_INPUTS = {
    "synthetic-trf2026-team-swiss",  # team tournaments are not supported
    "invalid_utf8",  # upstream-style regression test for invalid UTF-8
}

BUILD_INFO_RE = re.compile(
    rb"BBP Pairings \(https://github\.com/BieremaBoyzProgramming/bbpPairings\)"
    rb" - [^\n]*?\(Built [^)\n]*\)"
)


@dataclass
class Case:
    """One test case: one or more engine invocations in one working dir."""

    name: str  # golden path relative to tests/golden (without .txt)
    steps: list[list[str]]  # engine arguments per step
    inputs: dict[str, bytes] = field(default_factory=dict)
    outputs: list[str] = field(default_factory=list)  # files to capture
    golden: bool = True
    tool: str = "engine"  # "engine" or "roundtrip"
    # Semantic check: receives [(rc, stdout, stderr)] and the captured files,
    # returns an error message or None.
    check: Optional[Callable[[list, dict], Optional[str]]] = None


@dataclass
class Outcome:
    case: Case
    error: Optional[str]
    recorded: bool = False


# --- helpers -------------------------------------------------------------------


def read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def normalize(data: bytes, engine_names: list[bytes]) -> bytes:
    """Remove build-dependent text (version, build date, engine path)."""
    data = BUILD_INFO_RE.sub(b"BBP Pairings <build info>", data)
    for name in engine_names:
        if name:
            data = data.replace(name, b"<engine>")
    return data


def render(results: list, files: dict[str, Optional[bytes]]) -> bytes:
    out = []
    for index, (args, rc, stdout, stderr) in enumerate(results, start=1):
        out.append(b"=== step %d: %s\n" % (index, " ".join(args).encode()))
        out.append(b"exit: %d\n" % rc)
        out.append(b"--- stdout\n" + stdout + b"\n")
        out.append(b"--- stderr\n" + stderr + b"\n")
    for name in sorted(files):
        content = files[name]
        out.append(b"--- file " + name.encode() + b"\n")
        out.append((b"<missing>" if content is None else content) + b"\n")
    return b"".join(out)


def show_diff(expected: bytes, actual: bytes, limit: int = 40) -> str:
    def lines(data: bytes) -> list[str]:
        text = data.decode("utf-8", errors="replace").replace("\r", "\\r\r")
        return text.split("\n")

    diff = list(
        difflib.unified_diff(
            lines(expected), lines(actual), "golden", "actual", lineterm=""
        )
    )
    if len(diff) > limit:
        diff = diff[:limit] + ["... (%d more diff lines)" % (len(diff) - limit)]
    return "\n".join(diff)


# --- semantic checks -----------------------------------------------------------


def expect_exit(*codes: int):
    def check(results, _files):
        for index, (_args, rc, _out, _err) in enumerate(results, start=1):
            if rc not in codes:
                return "step %d exited %d, expected one of %s" % (index, rc, codes)
        return None

    return check


def generated_ok(results, files):
    """Generator succeeded and the checker found no discrepancy."""
    (_, gen_rc, _, gen_err), (_, chk_rc, chk_out, chk_err) = results
    if gen_rc != 0:
        return "generator exited %d: %s" % (gen_rc, gen_err[:300])
    if chk_rc != 0:
        return "checker exited %d: %s" % (chk_rc, chk_err[:300])
    if b"Checker pairings" in chk_out or b"No valid pairing" in chk_out:
        return "checker reported discrepancies:\n" + chk_out.decode(errors="replace")[:1500]
    if not files.get("gen.trf"):
        return "generator wrote no tournament file"
    return None


def same_as_upstream_expected(expected: bytes, to_stdout: bool):
    def check(results, files):
        _args, rc, stdout, _stderr = results[0]
        if rc != 0:
            return "engine exited %d" % rc
        actual = stdout if to_stdout else files.get("out.txt")
        if actual != expected:
            return "output differs from upstream's .output.expected"
        return None

    return check


def upstream_command(test_source: str, input_name: str) -> Optional[tuple[list[str], bool]]:
    """Mirror the command in an upstream test/tests/*.cpp: system flag and mode.

    Returns (arguments, output goes to stdout), or None when the test is not a
    plain "pair or check, compare with .output.expected" test."""
    system = "--burstein" if '" --burstein "' in test_source else "--dutch"
    if '" -c > "' in test_source:
        return [system, input_name, "-c"], True
    if '" -p "' in test_source:
        return [system, input_name, "-p", "out.txt"], False
    return None


def roundtrip_ok(results, _files):
    rc = results[0][1]
    return None if rc == 0 else "round-trip helper reported a failed check (exit %d)" % rc


# --- case definitions ----------------------------------------------------------


def sample_cases() -> list[Case]:
    cases = []
    for path in sorted((FIXTURES / "samples").glob("*.trf")):
        data = read_bytes(path)
        stem = path.stem
        for system in ("dutch", "burstein"):
            flag = "--" + system
            base = "samples/%s/%s" % (stem, system)
            cases.append(
                Case(base + "-pair", [[flag, path.name, "-p"]], {path.name: data})
            )
            cases.append(
                Case(
                    base + "-pair-file-checklist",
                    [[flag, path.name, "-p", "pairs.txt", "-l"]],
                    {path.name: data},
                    outputs=["pairs.txt", stem + ".list"],
                )
            )
            cases.append(
                Case(
                    base + "-check",
                    [[flag, path.name, "-c", "-l", "check.list"]],
                    {path.name: data},
                    outputs=["check.list"],
                )
            )
    return cases


def upstream_cases() -> list[Case]:
    cases = []
    for path in sorted(UPSTREAM_TEST_DATA.glob("*.input")):
        name = path.stem
        expected_path = path.with_suffix(".output.expected")
        source = path.with_suffix(".cpp").read_text()
        command = upstream_command(source, path.name) if expected_path.exists() else None
        if command is None:
            # A test with its own checks (error output, generator, ...): the
            # fork's test suite runs it; here both modes are golden-compared.
            for mode, label in (("-p", "pair"), ("-c", "check")):
                cases.append(
                    Case(
                        "upstream/%s-%s" % (name, label),
                        [["--dutch", path.name, mode]],
                        {path.name: read_bytes(path)},
                        check=expect_exit(0, 1, 3),
                    )
                )
            continue
        expected = read_bytes(expected_path)
        args, to_stdout = command
        cases.append(
            Case(
                "upstream/%s-as-upstream" % name,
                [args],
                {path.name: read_bytes(path)},
                outputs=[] if to_stdout else ["out.txt"],
                check=same_as_upstream_expected(expected, to_stdout),
            )
        )
        # The other mode on the same input (golden only).
        other = [args[0], path.name, "-p"] if to_stdout else [args[0], path.name, "-c"]
        cases.append(
            Case(
                "upstream/%s-other-mode" % name,
                [other],
                {path.name: read_bytes(path)},
                check=expect_exit(0, 3) if to_stdout else expect_exit(0),
            )
        )
    return cases


def generated_cases(count: int) -> list[Case]:
    cases = []
    burstein_count = max(len(GOLDEN_GENERATED_BURSTEIN_SEEDS), count // 10)
    for system, seeds, golden_seeds in (
        ("dutch", range(1, count + 1), GOLDEN_GENERATED_SEEDS),
        ("burstein", range(1, burstein_count + 1), GOLDEN_GENERATED_BURSTEIN_SEEDS),
    ):
        flag = "--" + system
        for seed in seeds:
            cases.append(
                Case(
                    "generated/%s-seed-%04d" % (system, seed),
                    [
                        [flag, "-g", "-o", "gen.trf", "-s", str(seed)],
                        [flag, "gen.trf", "-c"],
                    ],
                    outputs=["gen.trf"],
                    golden=seed in golden_seeds,
                    check=generated_ok,
                )
            )
    return cases


def generator_cases() -> list[Case]:
    """Configuration-file and model-file generation, all golden."""
    cases = []
    for path in sorted((FIXTURES / "generator").glob("*.cfg")):
        for system in ("dutch", "burstein"):
            flag = "--" + system
            cases.append(
                Case(
                    "generator/%s-%s" % (path.stem, system),
                    [
                        [flag, "-g", path.name, "-o", "gen.trf", "-s", "20261108", "-l"],
                        [flag, "gen.trf", "-c"],
                    ],
                    {path.name: read_bytes(path)},
                    outputs=["gen.trf", "gen.list"],
                    check=generated_ok,
                )
            )
    models = sorted((FIXTURES / "samples").glob("*.trf")) + sorted(
        UPSTREAM_TEST_DATA.glob("*.input")
    )
    for path in models:
        if path.stem in UNREADABLE_INPUTS:
            continue  # cannot serve as a model; covered by other groups
        cases.append(
            Case(
                "generator/model-%s-dutch" % path.stem,
                [
                    ["--dutch", path.name, "-g", "-o", "gen.trf", "-s", "7"],
                    ["--dutch", "gen.trf", "-c"],
                ],
                {path.name: read_bytes(path)},
                outputs=["gen.trf"],
                check=generated_ok,
            )
        )
    return cases


def golden_file(path: Path, name: str) -> Optional[bytes]:
    """A file section of a rendered golden file, or None if it is missing."""
    data = path.read_bytes()
    marker = b"\n--- file " + name.encode() + b"\n"
    start = data.find(marker)
    if start < 0:
        return None
    start += len(marker)
    end = data.find(b"\n--- file ", start)
    content = data[start:] if end < 0 else data[start:end]
    content = content[:-1] if content.endswith(b"\n") else content
    return None if content == b"<missing>" else content


def parity_cases() -> list[Case]:
    """Check every tournament the native generator wrote into the golden
    files of the generated and generator groups, round by round and with
    checklists, so that the pairing work is compared on every build even
    where the builds' random distributions make the generators differ."""
    cases = []
    for group in ("generated", "generator"):
        for path in sorted((GOLDEN / group).glob("*.txt")):
            trf = golden_file(path, "gen.trf")
            if trf is None:
                continue
            flag = "--burstein" if "burstein" in path.stem else "--dutch"
            cases.append(
                Case(
                    "parity/%s-%s" % (group, path.stem),
                    [[flag, "gen.trf", "-c", "-l", "check.list"]],
                    {"gen.trf": trf},
                    outputs=["check.list"],
                    check=expect_exit(0),
                )
            )
    return cases


def target_difference(target: str, name: str) -> Optional[str]:
    """The reason a case's output differs on this build target, if it does."""
    for key, reason in TARGET_DIFFERENCES[target].items():
        if name == key or (key.endswith("/") and name.startswith(key)):
            return reason
    return None


def inline_cases(group: str, specs: list[dict], tool: str = "engine") -> list[Case]:
    return [
        Case(
            "%s/%s" % (group, spec["name"]),
            spec["steps"],
            spec["inputs"],
            outputs=spec["outputs"],
            tool=tool,
            check=expect_exit(*spec["exit"]),
        )
        for spec in specs
    ]


def roundtrip_cases() -> list[Case]:
    cases = []
    paths = sorted((FIXTURES / "samples").glob("*.trf")) + sorted(
        UPSTREAM_TEST_DATA.glob("*.input")
    )
    for path in paths:
        cases.append(
            Case(
                "roundtrip/" + path.stem,
                [[path.name]],
                {path.name: read_bytes(path)},
                tool="roundtrip",
                check=roundtrip_ok,
            )
        )
    return cases


# --- runner --------------------------------------------------------------------


def run_case(
    case: Case, tools: dict[str, Path], record: bool, build_target: str
) -> Outcome:
    executable = tools[case.tool]
    engine_names = [str(executable).encode(), str(tools["engine"]).encode()]
    with tempfile.TemporaryDirectory(prefix="bbp-test-") as work:
        workdir = Path(work)
        for name, content in case.inputs.items():
            target = workdir / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        results = []
        for args in case.steps:
            try:
                proc = subprocess.run(
                    [str(executable)] + args,
                    cwd=workdir,
                    stdin=subprocess.DEVNULL,
                    capture_output=True,
                    timeout=STEP_TIMEOUT_SECONDS,
                )
            except subprocess.TimeoutExpired:
                return Outcome(case, "timeout after %ds: %s" % (STEP_TIMEOUT_SECONDS, args))
            results.append(
                (
                    args,
                    proc.returncode,
                    normalize(proc.stdout, engine_names),
                    normalize(proc.stderr, engine_names),
                )
            )
        files = {}
        for name in case.outputs:
            path = workdir / name
            files[name] = normalize(path.read_bytes(), engine_names) if path.exists() else None

    errors = []
    if case.check and case.name not in TARGET_EXIT_DIFFERENCES[build_target]:
        semantic = case.check([r for r in results], files)
        if semantic:
            errors.append(semantic)

    differs = target_difference(build_target, case.name) is not None
    # A case listed by its full name is pinned by an overlay file even when it
    # is not golden on the native build.
    named = case.name in TARGET_DIFFERENCES[build_target]
    if case.golden or named:
        actual = render(results, files)
        root = GOLDEN_BY_TARGET[build_target] if differs else GOLDEN
        golden_path = root / (case.name + ".txt")
        # A non-native target records only its listed differences.
        if record and (build_target == "native" or differs):
            golden_path.parent.mkdir(parents=True, exist_ok=True)
            golden_path.write_bytes(actual)
            return Outcome(case, "; ".join(errors) or None, recorded=True)
        if not golden_path.exists():
            errors.append("golden file missing: %s" % golden_path.relative_to(ROOT))
        else:
            expected = golden_path.read_bytes()
            if expected != actual:
                errors.append(
                    "output differs from golden %s\n%s"
                    % (golden_path.relative_to(ROOT), show_diff(expected, actual))
                )
    return Outcome(case, "\n".join(errors) or None)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--engine", required=True, help="engine executable under test")
    parser.add_argument(
        "--engine-limits",
        default="",
        help="the engine built with small compile-time limits (Makefile LIMITS_VARS);"
        " empty = limits group not run",
    )
    parser.add_argument(
        "--roundtrip",
        default="",
        help="trf-roundtrip helper (white-box); empty = round-trip group not run",
    )
    parser.add_argument("--record", action="store_true", help="re-record golden files")
    parser.add_argument(
        "--generated-count",
        type=int,
        default=int(os.environ.get("BBP_GENERATED_COUNT", "300")),
    )
    parser.add_argument(
        "--instrumented",
        action="store_true",
        help="engine is an unoptimized coverage build: exclude KNOWN_ENGINE_UB cases",
    )
    parser.add_argument(
        "--target",
        choices=sorted(TARGET_DIFFERENCES),
        default="native",
        help="build target of the engine: selects the golden overlay for the"
        " cases listed in TARGET_DIFFERENCES",
    )
    parser.add_argument("--filter", default="", help="regex on case names")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    args = parser.parse_args()

    engine = Path(args.engine).resolve()
    if not engine.is_file() or not os.access(engine, os.X_OK):
        print("engine not found or not executable: %s" % engine, file=sys.stderr)
        return 2
    tools = {"engine": engine}

    if args.record and args.generated_count < max(GOLDEN_GENERATED_SEEDS):
        print("--record needs --generated-count >= %d" % max(GOLDEN_GENERATED_SEEDS))
        return 2

    groups = [
        ("samples", sample_cases()),
        ("upstream", upstream_cases()),
        ("generated", generated_cases(args.generated_count)),
        ("generator", generator_cases()),
        ("features", inline_cases("features", cases_inline.feature_cases())),
        ("errors", inline_cases("errors", cases_inline.error_cases(FIXTURES))),
    ]
    if args.engine_limits:
        limits = Path(args.engine_limits).resolve()
        if not limits.is_file() or not os.access(limits, os.X_OK):
            print("limits engine not found or not executable: %s" % limits, file=sys.stderr)
            return 2
        tools["limits"] = limits
        groups.append(("limits", inline_cases("limits", cases_inline.limit_cases(), "limits")))
    else:
        print("NOTE: limits group NOT RUN (no --engine-limits build given)")
    if args.roundtrip:
        roundtrip = Path(args.roundtrip).resolve()
        if not roundtrip.is_file():
            print("round-trip helper not found: %s" % roundtrip, file=sys.stderr)
            return 2
        tools["roundtrip"] = roundtrip
        groups.append(("roundtrip", roundtrip_cases()))
    else:
        print("NOTE: round-trip group NOT RUN (no --roundtrip helper given)")

    pattern = re.compile(args.filter) if args.filter else None
    all_cases = [
        case
        for _, group in groups
        for case in group
        if not pattern or pattern.search(case.name)
    ]

    excluded = []
    if args.instrumented:
        if args.record:
            print("--record is not allowed with --instrumented", file=sys.stderr)
            return 2
        excluded = [c for c in all_cases if c.name in KNOWN_ENGINE_UB]
        all_cases = [c for c in all_cases if c.name not in KNOWN_ENGINE_UB]

    print("engine: %s" % engine)
    print("cases: %d (%s)" % (
        len(all_cases),
        ", ".join("%s %d" % (name, len(group)) for name, group in groups),
    ))

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for outcome in pool.map(
            lambda c: run_case(c, tools, args.record, args.target), all_cases
        ):
            if outcome.error:
                failures.append(outcome)
        # The parity inputs are read from the golden files of the first pass,
        # so that a recording run checks the tournaments it has just written.
        parity = [
            case for case in parity_cases()
            if not pattern or pattern.search(case.name)
        ]
        print("parity cases: %d" % len(parity))
        for outcome in pool.map(
            lambda c: run_case(c, tools, args.record, args.target), parity
        ):
            if outcome.error:
                failures.append(outcome)
        all_cases += parity

    golden_count = sum(1 for c in all_cases if c.golden)

    # A golden file without a case means a case disappeared: fail loudly.
    # Only meaningful when every case was selected.
    complete = (
        not pattern
        and args.generated_count >= max(GOLDEN_GENERATED_SEEDS)
        and "roundtrip" in tools
        and "limits" in tools
    )
    if complete:
        golden_names = {c.name for c in all_cases + excluded if c.golden}
        roots = [(GOLDEN, golden_names)]
        if args.target != "native":
            # Listed cases that ran, and every case listed by its full name
            # (a generated seed above BBP_GENERATED_COUNT does not run).
            listed = {c.name for c in all_cases + excluded
                      if c.golden and target_difference(args.target, c.name)}
            listed |= {name for name in TARGET_DIFFERENCES[args.target]
                       if not name.endswith("/")}
            roots.append((GOLDEN_BY_TARGET[args.target], listed))
        for root, expected in roots:
            for path in sorted(root.rglob("*.txt")):
                name = path.relative_to(root).with_suffix("").as_posix()
                if name not in expected:
                    failures.append(Outcome(
                        Case(name, []),
                        "stale golden file (no such case): %s"
                        % path.relative_to(ROOT)))

    for outcome in sorted(failures, key=lambda o: o.case.name):
        print("\nFAIL %s\n%s" % (outcome.case.name, outcome.error))
    if args.record:
        recorded = sum(
            1 for c in all_cases
            if (c.golden and args.target == "native")
            or (args.target != "native" and target_difference(args.target, c.name)
                and (c.golden or c.name in TARGET_DIFFERENCES[args.target])))
        print("\nrecorded %d golden files under %s" % (
            recorded, GOLDEN_BY_TARGET[args.target].relative_to(ROOT)))
    differing = sorted({
        reason for c in all_cases
        if (reason := target_difference(args.target, c.name))})
    for reason in differing:
        print("\nTARGET DIFFERENCE (%s, compared with tests/golden-%s): %s"
              % (args.target, args.target, reason))
    for case in excluded:
        print("\nEXCLUDED on this instrumented build (known engine UB, runs on release builds): %s\n  %s"
              % (case.name, KNOWN_ENGINE_UB[case.name]))
    print(
        "\n%d passed, %d failed, %d excluded (%d golden comparisons)"
        % (len(all_cases) - len(failures), len(failures), len(excluded), golden_count)
    )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
