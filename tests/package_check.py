#!/usr/bin/env python3
"""Checks of the release packages (bbpPairingsWebAssemblyPort, MIT).

    python3 tests/package_check.py --packages dist/packages --version 0.1.0 \
        --native dist/native-linux-x64 --wasm dist/wasm --extract build/package-test

Checks the archives packaging/make_packages.py wrote: SHA256SUMS, the exact
list of entries, their modes, owner and time, every file byte-identical to
dist/, package.json (name, version, licence, every export present), the
bridge's JavaScript (no TypeScript import left), and that both packages come
from the same commits (VERSION). Then extracts both archives into --extract
(emptied first), where the Makefile runs the suites on the shipped files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "packaging"))

import make_packages as mp  # noqa: E402  (the packaging script's constants)

failures: list[str] = []


def expect(label: str, condition: bool, detail: str = "") -> None:
    """Record a failed check."""
    if not condition:
        failures.append(label + (": " + detail if detail else ""))


def version_fields(data: bytes) -> dict[str, str]:
    """key=value lines of a VERSION file."""
    fields = {}
    for line in data.decode("utf-8").splitlines():
        key, _, value = line.partition("=")
        fields[key] = value
    return fields


def read_archive(path: Path) -> dict[str, tuple[bytes, tarfile.TarInfo]]:
    """Every entry of a .tar.gz, checking the reproducibility fields."""
    entries: dict[str, tuple[bytes, tarfile.TarInfo]] = {}
    times = set()
    with tarfile.open(path, "r:gz") as tar:
        names = tar.getnames()
        expect(f"{path.name}: entries sorted", names == sorted(names))
        for info in tar.getmembers():
            expect(f"{path.name}: {info.name} is a regular file", info.isfile())
            expect(f"{path.name}: {info.name} relative, no '..'",
                   not info.name.startswith("/") and ".." not in info.name.split("/"))
            expect(f"{path.name}: {info.name} owner 0",
                   info.uid == 0 and info.gid == 0 and not info.uname and not info.gname)
            times.add(info.mtime)
            extracted = tar.extractfile(info)
            entries[info.name] = (extracted.read() if extracted else b"", info)
    expect(f"{path.name}: one time for every entry", len(times) == 1, str(sorted(times)))
    raw = path.read_bytes()
    # gzip header: no file name (flag 0x08) and time 0.
    expect(f"{path.name}: gzip without name or time",
           raw[3] & 0x08 == 0 and raw[4:8] == b"\0\0\0\0")
    return entries


def check_sums(packages: Path, names: list[str]) -> None:
    """SHA256SUMS lists exactly the archives with their digests."""
    expected = sorted(
        f"{hashlib.sha256((packages / n).read_bytes()).hexdigest()}  {n}\n" for n in names)
    actual = (packages / "SHA256SUMS").read_text(encoding="ascii")
    expect("SHA256SUMS", actual == "".join(expected), actual)


def check_wasm(entries: dict[str, tuple[bytes, tarfile.TarInfo]], wasm: Path,
               version: str) -> None:
    """The npm package: files, manifest, JavaScript bridge."""
    expected = {"package/" + n for n in mp.WASM_DIST_FILES}
    expected |= {"package/bbp-bridge.js", "package/bbp-worker.js", "package/package.json"}
    expect("wasm package entries", set(entries) == expected,
           f"missing {sorted(expected - set(entries))}, extra {sorted(set(entries) - expected)}")
    for name, (data, info) in entries.items():
        expect(f"wasm package: {name} mode 644", info.mode == 0o644, oct(info.mode))
        source = wasm / name.removeprefix("package/")
        if source.name in mp.WASM_DIST_FILES:
            expect(f"wasm package: {name} equals dist", data == source.read_bytes())

    manifest = json.loads(entries["package/package.json"][0])
    expect("package.json name", manifest.get("name") == mp.PACKAGE_NAME)
    expect("package.json version", manifest.get("version") == version, manifest.get("version"))
    expect("package.json private", manifest.get("private") is True)
    expect("package.json licence", manifest.get("license") == mp.WASM_LICENSE)
    expect("package.json type module", manifest.get("type") == "module")
    for key, target in manifest.get("exports", {}).items():
        expect(f"package.json export {key} present", "package/" + target[2:] in entries, target)
    expect("package.json main present", "package/" + manifest["main"][2:] in entries)

    for name in ("package/bbp-bridge.js", "package/bbp-worker.js"):
        script = entries[name][0].decode("utf-8")
        expect(f"{name}: no TypeScript specifier",
               re.search(r"""["'][^"'\n]*\.ts["']""", script) is None)
        expect(f"{name}: generated header", script.startswith("// Generated from "))
    expect("bbp-worker.js imports bbp-bridge.js",
           '"./bbp-bridge.js"' in entries["package/bbp-worker.js"][0].decode("utf-8"))


def check_native(entries: dict[str, tuple[bytes, tarfile.TarInfo]], native: Path,
                 version: str) -> None:
    """The native archive: one top folder, the dist files, modes."""
    prefix = f"{mp.NATIVE_NAME}-{version}/"
    expected = {prefix + n for n in mp.NATIVE_DIST_FILES}
    expect("native archive entries", set(entries) == expected,
           f"missing {sorted(expected - set(entries))}, extra {sorted(set(entries) - expected)}")
    for name, (data, info) in entries.items():
        base = name.removeprefix(prefix)
        mode = 0o755 if base in mp.NATIVE_EXECUTABLES else 0o644
        expect(f"native archive: {name} mode {oct(mode)}", info.mode == mode, oct(info.mode))
        if (native / base).is_file():
            expect(f"native archive: {name} equals dist", data == (native / base).read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--wasm", type=Path, required=True)
    parser.add_argument("--extract", type=Path, required=True)
    args = parser.parse_args()

    wasm_name = f"{mp.PACKAGE_NAME}-{args.version}.tgz"
    native_name = f"{mp.NATIVE_NAME}-{args.version}.tar.gz"
    found = sorted(p.name for p in args.packages.iterdir())
    expect("packages folder", found == sorted([wasm_name, native_name, "SHA256SUMS"]), str(found))
    if failures:
        print("\n".join("FAIL " + f for f in failures))
        return 1
    check_sums(args.packages, [wasm_name, native_name])
    wasm_entries = read_archive(args.packages / wasm_name)
    native_entries = read_archive(args.packages / native_name)
    check_wasm(wasm_entries, args.wasm, args.version)
    check_native(native_entries, args.native, args.version)

    wasm_version = version_fields(wasm_entries["package/VERSION"][0])
    native_version = version_fields(
        native_entries[f"{mp.NATIVE_NAME}-{args.version}/VERSION"][0])
    for key in ("engine_commit", "port_repo_commit", "tiebreak_reference"):
        expect(f"VERSION {key} same in both packages",
               wasm_version.get(key) == native_version.get(key) and key in wasm_version,
               f"{wasm_version.get(key)} / {native_version.get(key)}")

    if not failures:
        if args.extract.exists():
            shutil.rmtree(args.extract)
        for name in (wasm_name, native_name):
            target = args.extract / ("wasm" if name == wasm_name else "native")
            with tarfile.open(args.packages / name, "r:gz") as tar:
                tar.extractall(target, filter="data")
    for failure in failures:
        print("FAIL " + failure)
    print(f"package checks: {'failed' if failures else 'passed'}"
          f" ({len(wasm_entries)} + {len(native_entries)} entries)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
