/*
 * Part of bbpPairingsWebAssemblyPort (MIT licence, see LICENSE).
 *
 * Tests of the browser bridge (bridge/bbp-bridge.ts, bridge/bbp-worker.ts)
 * on the browser builds (build/wasm/web), run by Node (type stripping, Node
 * built-ins only):
 *
 *   node tests/bridge/bridge_test.ts --modules build/wasm/web \
 *     --engine build/native/bbpPairings --wasm-engine build/wasm/bbpPairings \
 *     --tiebreak build/native/tiebreak [--bridge <extracted package folder>]
 *
 * Every run through the bridge is compared byte for byte with the same run of
 * a command-line program in a working directory: the native engine and the
 * native tie-break tool for pairing, checking, tie-breaks and errors; the
 * WebAssembly engine's Node command line for the generator, whose random
 * distributions are those of Emscripten's libc++ (tests/run_tests.py,
 * TARGET_DIFFERENCES). Build information and the program path are
 * normalised, as in tests/run_tests.py.
 */

import { execFileSync, spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { parseArgs } from "node:util";

import type { ModuleFactory, RunRequest, RunResult } from "../../bridge/bbp-bridge.ts";
import type { WorkerRequest, WorkerResponse } from "../../bridge/bbp-worker.ts";

const ROOT = resolve(import.meta.dirname, "..", "..");
const { values: options } = parseArgs({
  options: {
    modules: { type: "string" },
    engine: { type: "string" },
    "wasm-engine": { type: "string" },
    tiebreak: { type: "string" },
    bridge: { type: "string" },
  },
});
for (const name of ["modules", "engine", "wasm-engine", "tiebreak"] as const) {
  if (!options[name]) {
    console.error("missing --" + name);
    process.exit(2);
  }
}
const modules = resolve(options.modules as string);
const nativeEngine = resolve(options.engine as string);
const wasmEngine = resolve(options["wasm-engine"] as string);
const nativeTiebreak = resolve(options.tiebreak as string);

// The bridge under test: the TypeScript sources in bridge/, or with --bridge
// the JavaScript of an extracted package (make test-package), whose worker
// then also loads the modules next to it.
const packaged = options.bridge !== undefined;
const bridgeDir = packaged ? resolve(options.bridge as string) : join(ROOT, "bridge");
const extension = packaged ? ".js" : ".ts";
const { check, generate, pair, runProgram, tiebreak } = await import(
  pathToFileURL(join(bridgeDir, "bbp-bridge" + extension)).href
) as typeof import("../../bridge/bbp-bridge.ts");
const { createWorkerClient, handleRequest, transferables } = await import(
  pathToFileURL(join(bridgeDir, "bbp-worker" + extension)).href
) as typeof import("../../bridge/bbp-worker.ts");

const BUILD_INFO = /BBP Pairings \(https:\/\/github\.com\/BieremaBoyzProgramming\/bbpPairings\) - [^\n]*?\(Built [^)\n]*\)/g;

function text(bytes: Uint8Array, program: string, name: string): string {
  return Buffer.from(bytes).toString("latin1")
    .replace(BUILD_INFO, "BBP Pairings <build info>")
    .split(program).join(name);
}

/** The same request run by a command-line program in a temporary directory. */
function runCli(program: string, request: RunRequest): RunResult {
  const work = mkdtempSync(join(tmpdir(), "bbp-bridge-"));
  try {
    for (const [name, data] of Object.entries(request.files ?? {})) {
      writeFileSync(join(work, name), data);
    }
    const proc = spawnSync(program, request.args, { cwd: work });
    const files: Record<string, Uint8Array> = {};
    for (const name of request.outputs ?? []) {
      if (existsSync(join(work, name))) {
        files[name] = readFileSync(join(work, name));
      }
    }
    return {
      exitCode: proc.status ?? -1,
      stdout: proc.stdout,
      stderr: proc.stderr,
      files,
    };
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
}

let passed = 0;
const failures: string[] = [];

function compare(
  label: string,
  ours: RunResult,
  reference: RunResult,
  program: string,
  name: string
): void {
  const problems: string[] = [];
  if (ours.exitCode !== reference.exitCode) {
    problems.push("exit " + ours.exitCode + ", expected " + reference.exitCode);
  }
  for (const stream of ["stdout", "stderr"] as const) {
    const a = text(ours[stream], name, name);
    const b = text(reference[stream], program, name);
    if (a !== b) {
      problems.push(stream + " differs:\n--- bridge\n" + a.slice(0, 800)
        + "\n--- reference\n" + b.slice(0, 800));
    }
  }
  const names = new Set([...Object.keys(ours.files), ...Object.keys(reference.files)]);
  for (const file of names) {
    const a = ours.files[file];
    const b = reference.files[file];
    if (!a || !b) {
      problems.push("file " + file + " only on one side");
    } else if (text(a, name, name) !== text(b, program, name)) {
      problems.push("file " + file + " differs");
    }
  }
  if (problems.length > 0) {
    failures.push(label + ": " + problems.join("; "));
  } else {
    passed += 1;
  }
}

function expect(label: string, condition: boolean, detail = ""): void {
  if (condition) {
    passed += 1;
  } else {
    failures.push(label + (detail ? ": " + detail : ""));
  }
}

async function loadFactory(file: string): Promise<ModuleFactory> {
  const imported = await import(pathToFileURL(join(modules, file)).href);
  return imported.default as ModuleFactory;
}

const engine = await loadFactory("bbpPairings.mjs");
const tiebreakTool = await loadFactory("tiebreak.mjs");

// --- engine: pairing and checking, every sample, both systems -------------------

const samples = readdirSync(join(ROOT, "tests", "fixtures", "samples"))
  .filter((name) => name.endsWith(".trf"))
  .sort();
for (const sample of samples) {
  const trf = readFileSync(join(ROOT, "tests", "fixtures", "samples", sample));
  for (const system of ["dutch", "burstein"] as const) {
    const paired = await pair(engine, trf, system);
    compare(
      "pair " + system + " " + sample,
      paired,
      runCli(nativeEngine, {
        args: ["--" + system, "input.trf", "-p", "pairings.txt"],
        files: { "input.trf": trf },
        outputs: ["pairings.txt"],
      }),
      nativeEngine,
      "bbpPairings"
    );
    compare(
      "check " + system + " " + sample,
      await check(engine, trf, system),
      runCli(nativeEngine, {
        args: ["--" + system, "input.trf", "-c"],
        files: { "input.trf": trf },
      }),
      nativeEngine,
      "bbpPairings"
    );
  }
}

// --- engine: usage, an unknown option, a missing file ---------------------------

for (const args of [[], ["--nonsense"], ["--dutch", "missing.trf", "-p"]]) {
  compare(
    "engine " + JSON.stringify(args),
    await runProgram(engine, { args }),
    runCli(nativeEngine, { args }),
    nativeEngine,
    "bbpPairings"
  );
}

// --- engine: the generator, against the WebAssembly command line ----------------

const config = readFileSync(
  join(ROOT, "tests", "fixtures", "generator", "small-10-players-7-rounds.cfg"));
for (const seed of ["1", "2", "20261108"]) {
  for (const withConfig of [false, true]) {
    const args = ["--dutch", "-g"];
    const files: Record<string, Uint8Array> = {};
    if (withConfig) {
      args.push("config.txt");
      files["config.txt"] = config;
    }
    args.push("-o", "generated.trf", "-s", seed);
    compare(
      "generate seed " + seed + (withConfig ? " with config" : ""),
      await generate(engine, seed, withConfig ? config : undefined),
      runCli(wasmEngine, { args, files, outputs: ["generated.trf"] }),
      wasmEngine,
      "bbpPairings"
    );
  }
}

// --- tie-break tool: individual samples and a generated team event --------------

const teamTrf = execFileSync("python3", [
  "-c",
  "import sys; sys.path.insert(0, 'tests');"
    + " import tiebreak_team_inputs as t;"
    + " sys.stdout.buffer.write(dict(t.team_feature_inputs())['team-byes-and-forfeits'])",
], { cwd: ROOT });
const tiebreakInputs: [string, Uint8Array, string[]][] = [
  ...samples
    .filter((name) => !name.includes("team"))
    .map((name): [string, Uint8Array, string[]] => [
      name,
      readFileSync(join(ROOT, "tests", "fixtures", "samples", name)),
      ["PTS", "DE", "BH/C1", "BH", "SB", "WIN", "ARO", "TPR"],
    ]),
  ["team event", teamTrf, ["MPTS", "GPTS", "EDE", "BH:MP", "EMGSB", "BC", "TBR", "SSSC/K4"]],
];
for (const [name, trf, descriptors] of tiebreakInputs) {
  for (const extra of [[], ["-c"], ["-n", "2"]]) {
    const args = ["-i", "input.trf", "-d", "T", ...extra, "-t", ...descriptors];
    compare(
      "tiebreak " + name + " " + extra.join(" "),
      await tiebreak(tiebreakTool, trf, descriptors, extra),
      runCli(nativeTiebreak, { args, files: { "input.trf": trf } }),
      nativeTiebreak,
      "tiebreak"
    );
  }
}
compare(
  "tiebreak usage",
  await runProgram(tiebreakTool, { args: [] }, { programName: "tiebreak" }),
  runCli(nativeTiebreak, { args: [] }),
  nativeTiebreak,
  "tiebreak"
);

// --- bridge behaviour ------------------------------------------------------------

const sampleTrf = readFileSync(
  join(ROOT, "tests", "fixtures", "samples", "synthetic-pairing-input.trf"));
const first = await pair(engine, sampleTrf);
const second = await pair(engine, sampleTrf);
expect(
  "fresh instance per run",
  Buffer.from(first.files["pairings.txt"]).equals(Buffer.from(second.files["pairings.txt"]))
    && first.exitCode === 0 && second.exitCode === 0
);
const compiled = await WebAssembly.compile(readFileSync(join(modules, "bbpPairings.wasm")));
const reused = await pair(engine, sampleTrf, "dutch", { wasmModule: compiled });
expect(
  "shared compiled module",
  Buffer.from(reused.files["pairings.txt"]).equals(Buffer.from(first.files["pairings.txt"]))
);
expect("host process exit code untouched", process.exitCode === undefined,
  "exitCode " + String(process.exitCode));

// --- worker -----------------------------------------------------------------------

const loader = async (program: string) => (program === "tiebreak" ? tiebreakTool : engine);
const request: WorkerRequest = {
  id: 7,
  program: "bbpPairings",
  request: {
    args: ["--dutch", "input.trf", "-p", "pairings.txt"],
    files: { "input.trf": sampleTrf },
    outputs: ["pairings.txt"],
  },
};
const response = await handleRequest(request, loader);
expect(
  "worker result",
  "result" in response && response.id === 7
    && Buffer.from(response.result.files["pairings.txt"])
      .equals(Buffer.from(first.files["pairings.txt"]))
);
expect("worker transferables", transferables(response).length === 3);
if (packaged) {
  // The default loader: bbpPairings.mjs next to bbp-worker.js in the package.
  const shipped = await handleRequest({ ...request, id: 9 });
  expect(
    "packaged worker loads its modules",
    "result" in shipped
      && Buffer.from(shipped.result.files["pairings.txt"])
        .equals(Buffer.from(first.files["pairings.txt"])),
    "error" in shipped ? shipped.error : ""
  );
}
const unknown = await handleRequest(
  { id: 8, program: "nothing" as never, request: { args: [] } }, loader);
expect("worker unknown program", "error" in unknown && unknown.id === 8);

// A fake worker: the client posts, the "worker" answers through handleRequest.
const listeners: ((event: { data: WorkerResponse }) => void)[] = [];
const client = createWorkerClient({
  postMessage(message: WorkerRequest) {
    handleRequest(message, loader).then((answer) => {
      for (const listener of listeners) listener({ data: answer });
    });
  },
  addEventListener(_type, listener) {
    listeners.push(listener);
  },
});
const [viaClient, failed] = await Promise.allSettled([
  client.run("tiebreak", { args: ["-i", "input.trf", "-d", "T"], files: { "input.trf": sampleTrf } }),
  client.run("nothing" as never, { args: [] }),
]);
expect("worker client result", viaClient.status === "fulfilled" && viaClient.value.exitCode === 0);
expect("worker client error", failed.status === "rejected");

for (const failure of failures) {
  console.log("FAIL " + failure + "\n");
}
console.log("bridge tests: " + passed + " passed, " + failures.length + " failed");
process.exitCode = failures.length > 0 ? 1 : 0;
