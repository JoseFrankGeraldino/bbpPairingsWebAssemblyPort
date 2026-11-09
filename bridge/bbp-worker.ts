/*
 * Part of bbpPairingsWebAssemblyPort (MIT licence, see LICENSE).
 *
 * A Web Worker that runs the WebAssembly programs off the UI thread, and the
 * client the page uses to talk to it. Start it as a module worker next to
 * the built modules:
 *
 *   const worker = new Worker(new URL("./bbp-worker.ts", import.meta.url), { type: "module" });
 *   const client = createWorkerClient(worker);
 *   const result = await client.run("bbpPairings", { args: [...], files: {...} });
 *
 * Messages are plain objects ({ id, program, request } in, { id, result } or
 * { id, error } out); the byte arrays of the result are transferred.
 */

import {
  runProgram,
  type ModuleFactory,
  type RunRequest,
  type RunResult,
} from "./bbp-bridge.ts";

export type ProgramName = "bbpPairings" | "tiebreak";

export interface WorkerRequest {
  id: number;
  program: ProgramName;
  request: RunRequest;
}

export type WorkerResponse =
  | { id: number; result: RunResult }
  | { id: number; error: string };

/** Loads a program's module factory (module URL relative to the worker). */
export type FactoryLoader = (program: ProgramName) => Promise<ModuleFactory>;

const MODULES: Record<ProgramName, { file: string; factory: string }> = {
  bbpPairings: { file: "./bbpPairings.mjs", factory: "default" },
  tiebreak: { file: "./tiebreak.mjs", factory: "default" },
};

/** The default loader: import the module next to this worker. */
export const importFactory: FactoryLoader = async (program) => {
  const entry = MODULES[program];
  const imported = await import(new URL(entry.file, import.meta.url).href);
  return imported[entry.factory] as ModuleFactory;
};

/** Handle one request message; never throws (failures become { error }). */
export async function handleRequest(
  message: WorkerRequest,
  load: FactoryLoader = importFactory
): Promise<WorkerResponse> {
  try {
    if (!(message.program in MODULES)) {
      throw new Error("Unknown program: " + String(message.program));
    }
    const factory = await load(message.program);
    const result = await runProgram(factory, message.request, {
      programName: message.program,
    });
    return { id: message.id, result };
  } catch (error) {
    return {
      id: message.id,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

/** The byte arrays a response hands over to the page without copying. */
export function transferables(response: WorkerResponse): ArrayBuffer[] {
  if (!("result" in response)) {
    return [];
  }
  const { stdout, stderr, files } = response.result;
  return [stdout, stderr, ...Object.values(files)].map(
    (bytes) => bytes.buffer as ArrayBuffer
  );
}

/** The page side: one promise per request, matched by id. */
export function createWorkerClient(worker: {
  postMessage(message: WorkerRequest): void;
  addEventListener(type: "message", listener: (event: { data: WorkerResponse }) => void): void;
}) {
  let nextId = 1;
  const pending = new Map<number, {
    resolve: (result: RunResult) => void;
    reject: (error: Error) => void;
  }>();
  worker.addEventListener("message", (event) => {
    const response = event.data;
    const waiting = pending.get(response.id);
    if (!waiting) {
      return;
    }
    pending.delete(response.id);
    if ("result" in response) {
      waiting.resolve(response.result);
    } else {
      waiting.reject(new Error(response.error));
    }
  });
  return {
    run(program: ProgramName, request: RunRequest): Promise<RunResult> {
      const id = nextId++;
      return new Promise<RunResult>((resolve, reject) => {
        pending.set(id, { resolve, reject });
        worker.postMessage({ id, program, request });
      });
    },
  };
}

// Inside a worker, answer the page's messages.
declare const WorkerGlobalScope: unknown;
if (typeof WorkerGlobalScope !== "undefined") {
  const scope = globalThis as unknown as {
    onmessage: (event: { data: WorkerRequest }) => void;
    postMessage(message: WorkerResponse, transfer: ArrayBuffer[]): void;
  };
  scope.onmessage = (event) => {
    handleRequest(event.data).then((response) => {
      scope.postMessage(response, transferables(response));
    });
  };
}
