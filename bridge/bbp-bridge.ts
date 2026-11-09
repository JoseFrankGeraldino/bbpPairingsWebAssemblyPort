/*
 * Part of bbpPairingsWebAssemblyPort (MIT licence, see LICENSE).
 *
 * Runs the WebAssembly builds of the bbpPairings engine (bbpPairings.mjs,
 * Apache 2.0, see LICENSE.txt and NOTICE) and of our tie-break tool
 * (tiebreak.mjs) with their command-line contract: the same arguments as the
 * native programs, files in a working directory, the program's exit code,
 * its standard output and error as bytes, and the files it wrote.
 *
 * Every run gets a fresh module instance, so no state of one run reaches the
 * next; the compiled WebAssembly code can be shared through `wasmModule`.
 * Erasable TypeScript only (no enums, no namespaces), so that it runs as is
 * under Node's type stripping as well as through any TypeScript compiler.
 */

/** The part of an Emscripten module (MODULARIZE build) the bridge uses. */
export interface EmscriptenModule {
  callMain(args: string[]): number;
  FS: {
    init(
      input: () => number | null,
      output: (code: number | null) => void,
      error: (code: number | null) => void
    ): void;
    mkdir(path: string): void;
    chdir(path: string): void;
    writeFile(path: string, data: Uint8Array | string): void;
    readFile(path: string): Uint8Array;
    analyzePath(path: string): { exists: boolean };
  };
}

/** The factory a MODULARIZE build exports (createBbpPairings, createTiebreak). */
export type ModuleFactory = (options: Record<string, unknown>) => Promise<EmscriptenModule>;

export interface RunRequest {
  /** Command-line arguments, as for the native program (without argv[0]). */
  args: string[];
  /** Input files by name, written into the working directory first. */
  files?: Record<string, Uint8Array | string>;
  /** Names of files to read back after the run (missing ones are omitted). */
  outputs?: string[];
}

export interface RunResult {
  exitCode: number;
  stdout: Uint8Array;
  stderr: Uint8Array;
  files: Record<string, Uint8Array>;
}

export interface RunOptions {
  /** Program name the run sees as argv[0] (usage and error messages). */
  programName?: string;
  /** A compiled module, to skip compiling the .wasm file on every run. */
  wasmModule?: WebAssembly.Module;
  /** Where the .wasm file is, when not next to the .mjs module. */
  locateFile?: (path: string, prefix: string) => string;
}

const WORK_DIR = "/work";

/** Emscripten's ExitStatus, thrown when a program ends through exit(). */
function exitStatus(error: unknown): number | undefined {
  if (
    typeof error === "object" && error !== null
    && (error as { name?: unknown }).name === "ExitStatus"
  ) {
    return (error as { status: number }).status;
  }
  return undefined;
}

/**
 * Run one program on a fresh instance.
 *
 * @throws the program's error when it fails in a way that is not an exit
 *   (an Emscripten abort: a trap or an uncaught exception).
 */
export async function runProgram(
  factory: ModuleFactory,
  request: RunRequest,
  options: RunOptions = {}
): Promise<RunResult> {
  const stdout: number[] = [];
  const stderr: number[] = [];
  const moduleOptions: Record<string, unknown> = {
    thisProgram: options.programName ?? "bbpPairings",
    // The program's exit must not end the host (process.exit under Node).
    quit: (_status: number, toThrow: unknown) => {
      throw toThrow;
    },
    preRun: [
      (module: EmscriptenModule) => {
        module.FS.init(
          () => null,
          (code) => {
            if (code !== null) stdout.push(code);
          },
          (code) => {
            if (code !== null) stderr.push(code);
          }
        );
      },
    ],
    // The bridge reports output through RunResult only.
    print: () => undefined,
    printErr: () => undefined,
  };
  if (options.locateFile) {
    moduleOptions.locateFile = options.locateFile;
  }
  if (options.wasmModule) {
    const compiled = options.wasmModule;
    moduleOptions.instantiateWasm = (
      imports: WebAssembly.Imports,
      receive: (instance: WebAssembly.Instance, module: WebAssembly.Module) => void
    ) => {
      WebAssembly.instantiate(compiled, imports).then(
        (instance) => receive(instance, compiled)
      );
      return {};
    };
  }
  const module = await factory(moduleOptions);
  module.FS.mkdir(WORK_DIR);
  module.FS.chdir(WORK_DIR);
  for (const [name, data] of Object.entries(request.files ?? {})) {
    module.FS.writeFile(WORK_DIR + "/" + name, data);
  }
  // Under Node, Emscripten's exit sets process.exitCode: keep the host's.
  const host = (globalThis as { process?: { exitCode?: number | string } }).process;
  const hostExitCode = host?.exitCode;
  let exitCode: number;
  try {
    // callMain changes the array it is given (it adds argv[0]).
    exitCode = module.callMain([...request.args]);
  } catch (error) {
    const status = exitStatus(error);
    if (status === undefined) {
      throw error;
    }
    exitCode = status;
  } finally {
    if (host) {
      host.exitCode = hostExitCode;
    }
  }
  const files: Record<string, Uint8Array> = {};
  for (const name of request.outputs ?? []) {
    const path = WORK_DIR + "/" + name;
    if (module.FS.analyzePath(path).exists) {
      files[name] = module.FS.readFile(path);
    }
  }
  return {
    exitCode,
    stdout: Uint8Array.from(stdout),
    stderr: Uint8Array.from(stderr),
    files,
  };
}

/** Pairs the next round: bbpPairings (--dutch | --burstein) input -p output. */
export function pair(
  factory: ModuleFactory,
  trf: Uint8Array | string,
  system: "dutch" | "burstein" = "dutch",
  options: RunOptions = {}
): Promise<RunResult> {
  return runProgram(
    factory,
    {
      args: ["--" + system, "input.trf", "-p", "pairings.txt"],
      files: { "input.trf": trf },
      outputs: ["pairings.txt"],
    },
    options
  );
}

/** Checks every round of a tournament: bbpPairings (system) input -c. */
export function check(
  factory: ModuleFactory,
  trf: Uint8Array | string,
  system: "dutch" | "burstein" = "dutch",
  options: RunOptions = {}
): Promise<RunResult> {
  return runProgram(
    factory,
    { args: ["--" + system, "input.trf", "-c"], files: { "input.trf": trf } },
    options
  );
}

/**
 * Generates a random tournament: bbpPairings (system) -g [config] -o
 * generated.trf -s seed. The file comes back as files["generated.trf"].
 */
export function generate(
  factory: ModuleFactory,
  seed: string,
  config?: Uint8Array | string,
  system: "dutch" | "burstein" = "dutch",
  options: RunOptions = {}
): Promise<RunResult> {
  const args = ["--" + system, "-g"];
  const files: Record<string, Uint8Array | string> = {};
  if (config !== undefined) {
    args.push("config.txt");
    files["config.txt"] = config;
  }
  args.push("-o", "generated.trf", "-s", seed);
  return runProgram(factory, { args, files, outputs: ["generated.trf"] }, options);
}

/**
 * Computes tie-breaks: tiebreak -i input.trf -d T [-t descriptors...] [more
 * arguments, such as -c or -n <round>]. Output is the tab-separated table.
 */
export function tiebreak(
  factory: ModuleFactory,
  trf: Uint8Array | string,
  descriptors: string[] = [],
  extraArgs: string[] = [],
  options: RunOptions = {}
): Promise<RunResult> {
  const args = ["-i", "input.trf", "-d", "T", ...extraArgs];
  if (descriptors.length > 0) {
    args.push("-t", ...descriptors);
  }
  return runProgram(
    factory,
    { args, files: { "input.trf": trf } },
    { programName: "tiebreak", ...options }
  );
}
