#!/usr/bin/env python3
"""Engine-agnostic black-box test suite for bbpPairings builds.

Part of bbpPairingsWebAssemblyPort (MIT). Python 3 standard library only.

The engine under test is any executable that behaves like the bbpPairings
command line (``--engine``): the native build in E1, a Node runner script for
the WebAssembly build in E2. Every case copies its input files into a fresh
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

STEP_TIMEOUT_SECONDS = 900

# Seeds whose generated tournaments are golden (the "fixed subset").
GOLDEN_GENERATED_SEEDS = range(1, 21)
GOLDEN_GENERATED_BURSTEIN_SEEDS = range(1, 6)

# Cases whose input hits undefined behaviour in the pinned engine (README.md,
# "Known engine issues"). They always run, golden-compared, against release
# builds. On an unoptimized instrumented build the UB crashes the engine, so
# --instrumented excludes them and prints them in the summary. Remove an entry
# when the fork fixes the bug (reviewed pin bump).
KNOWN_ENGINE_UB = {
    "features/history10-bye-column-dutch-check":
        "dutch.cpp:723 dereferences sortedPlayers.front() when no player is left to pair",
    "upstream/dutch_2025_C5-other-mode":
        "dutch.cpp:723 dereferences sortedPlayers.front() when no player is left to pair",
    "generator/model-issue_15-dutch":
        "trf.cpp writeFile(model) reads the uninitialised 'bool roundsLine': the release"
        " build omits the '142' line, unoptimized builds write it",
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


def upstream_command(test_source: str, input_name: str) -> tuple[list[str], bool]:
    """Mirror the command in an upstream test/tests/*.cpp: system flag and mode.

    Returns (arguments, output goes to stdout)."""
    system = "--burstein" if '" --burstein "' in test_source else "--dutch"
    if '" -c > "' in test_source:
        return [system, input_name, "-c"], True
    if '" -p "' in test_source:
        return [system, input_name, "-p", "out.txt"], False
    raise ValueError("unrecognised upstream test command")


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
        expected = read_bytes(path.with_suffix(".output.expected"))
        source = path.with_suffix(".cpp").read_text()
        args, to_stdout = upstream_command(source, path.name)
        name = path.stem
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
        if path.name.startswith("synthetic-trf2026-team"):
            continue  # team tournaments cannot be read (see errors group)
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


def inline_cases(group: str, specs: list[dict]) -> list[Case]:
    return [
        Case(
            "%s/%s" % (group, spec["name"]),
            spec["steps"],
            spec["inputs"],
            outputs=spec["outputs"],
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


def run_case(case: Case, tools: dict[str, Path], record: bool) -> Outcome:
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
    if case.check:
        semantic = case.check([r for r in results], files)
        if semantic:
            errors.append(semantic)

    if case.golden:
        actual = render(results, files)
        golden_path = GOLDEN / (case.name + ".txt")
        if record:
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
        for outcome in pool.map(lambda c: run_case(c, tools, args.record), all_cases):
            if outcome.error:
                failures.append(outcome)

    golden_count = sum(1 for c in all_cases if c.golden)

    # A golden file without a case means a case disappeared: fail loudly.
    # Only meaningful when every case was selected.
    complete = (
        not pattern
        and args.generated_count >= max(GOLDEN_GENERATED_SEEDS)
        and "roundtrip" in tools
    )
    if complete:
        expected = {c.name for c in all_cases + excluded if c.golden}
        for path in sorted(GOLDEN.rglob("*.txt")):
            name = path.relative_to(GOLDEN).with_suffix("").as_posix()
            if name not in expected:
                failures.append(Outcome(
                    Case(name, []),
                    "stale golden file (no such case): %s" % path.relative_to(ROOT)))

    for outcome in sorted(failures, key=lambda o: o.case.name):
        print("\nFAIL %s\n%s" % (outcome.case.name, outcome.error))
    if args.record:
        print("\nrecorded %d golden files under %s" % (golden_count, GOLDEN.relative_to(ROOT)))
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
