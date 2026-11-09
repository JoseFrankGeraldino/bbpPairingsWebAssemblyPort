#!/usr/bin/env python3
"""Build the release packages from dist/ (bbpPairingsWebAssemblyPort, MIT).

    python3 packaging/make_packages.py --native dist/native-linux-x64 \
        --wasm dist/wasm --version 0.1.0 --mtime <epoch> --out dist/packages

Writes into --out:

- bbppairings-wasm-<version>.tgz: an npm package (top folder "package/",
  installable with "npm install <file>"): the browser modules and .wasm
  files, the bridge as JavaScript (packaging/strip-types.mjs) and as
  TypeScript source, package.json, every licence file and VERSION.
- bbppairings-native-linux-x64-<version>.tar.gz: the folder
  dist/native-linux-x64 under "bbppairings-native-linux-x64-<version>/".
- SHA256SUMS: sha256sum format, for both archives.

The archives are reproducible: entries sorted, one fixed time (--mtime, the
commit time in the Makefile), owner 0, fixed modes, gzip without name or
time, so the same inputs give the same bytes.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGE_NAME = "bbppairings-wasm"
NATIVE_NAME = "bbppairings-native-linux-x64"
REPOSITORY = "https://github.com/JoseFrankGeraldino/bbpPairingsWebAssemblyPort"

# SPDX expression of everything in the WebAssembly package: our code (MIT),
# the engine (Apache-2.0), the Emscripten runtime in the .mjs loaders (MIT or
# NCSA), libc++/libc++abi (Apache-2.0 WITH LLVM-exception) and musl (MIT)
# compiled into the .wasm files. THIRD_PARTY_NOTICES.md lists each one.
WASM_LICENSE = "MIT AND Apache-2.0 AND (MIT OR NCSA) AND Apache-2.0 WITH LLVM-exception"

# Files of dist/wasm, exactly; anything else there is an error.
WASM_DIST_FILES = (
    "Apache-2.0.txt", "EMSCRIPTEN-LICENSE.txt", "GACRUX-LICENSE.txt",
    "LIBCXX-LICENSE.txt", "LIBCXXABI-LICENSE.txt", "LICENSE-tiebreak.txt",
    "LICENSE.txt", "MUSL-COPYRIGHT.txt", "NOTICE", "VERSION",
    "bbp-bridge.ts", "bbp-worker.ts",
    "bbpPairings.mjs", "bbpPairings.wasm", "tiebreak.mjs", "tiebreak.wasm",
)
NATIVE_DIST_FILES = (
    "Apache-2.0.txt", "GACRUX-LICENSE.txt", "LICENSE-tiebreak.txt",
    "LICENSE.txt", "NOTICE", "VERSION", "bbpPairings", "tiebreak",
)
NATIVE_EXECUTABLES = ("bbpPairings", "tiebreak")
BRIDGE_SOURCES = ("bbp-bridge.ts", "bbp-worker.ts")


def package_json(version: str) -> bytes:
    """The package's manifest. "private" keeps it off the npm registry."""
    manifest = {
        "name": PACKAGE_NAME,
        "version": version,
        "private": True,
        "description": (
            "The bbpPairings engine (FIDE Dutch system) and a FIDE tie-break"
            " tool compiled to WebAssembly, with a bridge and a Web Worker"
        ),
        "license": WASM_LICENSE,
        "type": "module",
        "main": "./bbp-bridge.js",
        "exports": {
            ".": "./bbp-bridge.js",
            "./worker": "./bbp-worker.js",
            "./bbpPairings.mjs": "./bbpPairings.mjs",
            "./bbpPairings.wasm": "./bbpPairings.wasm",
            "./tiebreak.mjs": "./tiebreak.mjs",
            "./tiebreak.wasm": "./tiebreak.wasm",
            "./package.json": "./package.json",
        },
        "sideEffects": ["./bbp-worker.js"],
        "homepage": REPOSITORY,
        "repository": {"type": "git", "url": "git+" + REPOSITORY + ".git"},
    }
    return (json.dumps(manifest, indent=2) + "\n").encode("utf-8")


def check_exact(folder: Path, expected: tuple[str, ...]) -> None:
    """Fail unless the folder holds exactly the expected files."""
    found = sorted(p.name for p in folder.iterdir())
    if found != sorted(expected):
        missing = sorted(set(expected) - set(found))
        extra = sorted(set(found) - set(expected))
        sys.exit(f"{folder}: missing {missing}, unexpected {extra}")


def strip_types(node: str, source: Path) -> bytes:
    """The JavaScript of one bridge file (packaging/strip-types.mjs)."""
    with tempfile.TemporaryDirectory() as work:
        target = Path(work) / source.with_suffix(".js").name
        result = subprocess.run(
            [node, str(ROOT / "packaging" / "strip-types.mjs"), str(source), str(target)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            sys.exit(f"strip-types failed for {source}:\n{result.stderr}")
        return target.read_bytes()


def write_archive(path: Path, entries: dict[str, tuple[bytes, int]], mtime: int) -> None:
    """A reproducible .tar.gz of {name: (bytes, mode)}, folders implied."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for name in sorted(entries):
            data, mode = entries[name]
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = mode
            info.mtime = mtime
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            tar.addfile(info, io.BytesIO(data))
    with path.open("wb") as out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=out, mtime=0) as packed:
            packed.write(raw.getvalue())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--wasm", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--mtime", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()

    check_exact(args.wasm, WASM_DIST_FILES)
    check_exact(args.native, NATIVE_DIST_FILES)

    wasm: dict[str, tuple[bytes, int]] = {}
    for name in WASM_DIST_FILES:
        wasm["package/" + name] = ((args.wasm / name).read_bytes(), 0o644)
    for name in BRIDGE_SOURCES:
        script = strip_types(args.node, (args.wasm / name).resolve())
        wasm["package/" + Path(name).with_suffix(".js").name] = (script, 0o644)
    wasm["package/package.json"] = (package_json(args.version), 0o644)

    native: dict[str, tuple[bytes, int]] = {}
    prefix = f"{NATIVE_NAME}-{args.version}/"
    for name in NATIVE_DIST_FILES:
        mode = 0o755 if name in NATIVE_EXECUTABLES else 0o644
        native[prefix + name] = ((args.native / name).read_bytes(), mode)

    args.out.mkdir(parents=True, exist_ok=True)
    archives = {
        f"{PACKAGE_NAME}-{args.version}.tgz": wasm,
        f"{NATIVE_NAME}-{args.version}.tar.gz": native,
    }
    sums = []
    for name, entries in archives.items():
        write_archive(args.out / name, entries, args.mtime)
        digest = hashlib.sha256((args.out / name).read_bytes()).hexdigest()
        sums.append(f"{digest}  {name}\n")
    (args.out / "SHA256SUMS").write_text("".join(sorted(sums)), encoding="ascii")
    for line in sorted(sums):
        print(line, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
