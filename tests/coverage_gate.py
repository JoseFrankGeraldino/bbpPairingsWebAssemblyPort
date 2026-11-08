#!/usr/bin/env python3
"""Coverage report and gate for the engine sources (part of bbpPairingsWebAssemblyPort, MIT).

Runs gcovr over the .gcda files of the instrumented engine build, writes an
HTML report, a JSON/text summary, a Cobertura XML file and a per-file list of
uncovered lines, then fails when
  * no coverage data was collected (no .gcda files, or zero measurable lines), or
  * line coverage over third_party/bbpPairings/src is below --min-lines.

Scope (decided): every upstream source under third_party/bbpPairings/src;
nothing else is excluded. Our own code (tests/) is not in the scope.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--gcovr", default="gcovr")
    parser.add_argument("--min-lines", type=float, required=True)
    parser.add_argument("--engine-dir", required=True)
    parser.add_argument("--object-dir", action="append", required=True,
                        help="object directory of an instrumented build (repeatable)")
    parser.add_argument("--report-dir", required=True)
    args = parser.parse_args()

    engine_dir = Path(args.engine_dir).resolve()
    object_dirs = [Path(d).resolve() for d in args.object_dir]
    report_dir = Path(args.report_dir).resolve()

    for object_dir in object_dirs:
        gcda = list(object_dir.rglob("*.gcda")) if object_dir.is_dir() else []
        if not gcda:
            print("COVERAGE GATE FAILED: no coverage data (.gcda) found under %s" % object_dir)
            return 1

    report_dir.mkdir(parents=True, exist_ok=True)
    summary_json = report_dir / "summary.json"
    full_json = report_dir / "coverage.json"
    command = [
        args.gcovr,
        "--root", str(engine_dir),
        "--filter", str(engine_dir / "src") + "/",
        "--html-details", str(report_dir / "index.html"),
        "--html-title", "bbpPairings engine coverage (third_party/bbpPairings/src)",
        "--json-summary-pretty", "--json-summary", str(summary_json),
        "--json", str(full_json),
        "--cobertura", str(report_dir / "cobertura.xml"),
        "--txt", str(report_dir / "summary.txt"),
        *[str(d) for d in object_dirs],
    ]
    print("+ " + " ".join(command))
    result = subprocess.run(command)
    if result.returncode != 0:
        print("COVERAGE GATE FAILED: gcovr exited %d" % result.returncode)
        return 1

    summary = json.loads(summary_json.read_text())
    total = summary.get("line_total", 0)
    covered = summary.get("line_covered", 0)
    if not total:
        print("COVERAGE GATE FAILED: gcovr measured no lines (no data collected)")
        return 1
    percent = 100.0 * covered / total

    # Per-file uncovered lines, most uncovered first.
    details = json.loads(full_json.read_text())
    rows = []
    for entry in details.get("files", []):
        missing = [
            line["line_number"]
            for line in entry.get("lines", [])
            if not line.get("gcovr/noncode") and line.get("count", 0) == 0
        ]
        measured = [line for line in entry.get("lines", []) if not line.get("gcovr/noncode")]
        rows.append((len(missing), len(measured), entry["file"], missing))
    rows.sort(key=lambda row: (-row[0], row[2]))

    lines_out = ["%-52s %8s %8s %7s" % ("file", "lines", "missed", "cover")]
    for missed, measured, name, _ in rows:
        cover = 100.0 * (measured - missed) / measured if measured else 100.0
        lines_out.append("%-52s %8d %8d %6.1f%%" % (name, measured, missed, cover))
    lines_out.append("%-52s %8d %8d %6.1f%%" % ("TOTAL", total, total - covered, percent))
    table = "\n".join(lines_out)
    (report_dir / "per-file.txt").write_text(table + "\n")
    with (report_dir / "uncovered-lines.txt").open("w") as handle:
        for missed, _measured, name, missing in rows:
            if missed:
                handle.write("%s: %s\n" % (name, ",".join(str(n) for n in missing)))

    print(table)
    print("\nline coverage %.2f%% (%d/%d), gate %.1f%%; report: %s"
          % (percent, covered, total, args.min_lines, report_dir / "index.html"))
    if percent < args.min_lines:
        print("COVERAGE GATE FAILED: %.2f%% < %.1f%%" % (percent, args.min_lines))
        return 1
    print("coverage gate passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
