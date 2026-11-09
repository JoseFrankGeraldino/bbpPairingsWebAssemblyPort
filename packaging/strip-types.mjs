/*
 * Part of bbpPairingsWebAssemblyPort (MIT licence, see LICENSE).
 *
 * Turns the bridge's TypeScript files into JavaScript for the package, with
 * Node's own type stripping (node:module stripTypeScriptTypes, no packages):
 *
 *   node packaging/strip-types.mjs <input.ts> <output.js>
 *
 * The bridge uses erasable syntax only, so stripping is the whole
 * compilation: types become spaces, the code keeps its lines and columns.
 * Relative imports of the bridge's own files are renamed from .ts to .js;
 * any other ".ts" module specifier left over is an error.
 */

import { readFileSync, writeFileSync } from "node:fs";
import { basename } from "node:path";
import { stripTypeScriptTypes } from "node:module";

const [input, output] = process.argv.slice(2);
if (!input || !output) {
  console.error("usage: node packaging/strip-types.mjs <input.ts> <output.js>");
  process.exit(2);
}

const OWN_MODULES = /(["'])\.\/(bbp-bridge|bbp-worker)\.ts\1/g;

const source = readFileSync(input, "utf8");
const stripped = stripTypeScriptTypes(source, { mode: "strip" })
  .replace(OWN_MODULES, "$1./$2.js$1");
const leftover = stripped.match(/["'][^"'\n]*\.ts["']/);
if (leftover) {
  console.error(input + ": TypeScript module specifier left after stripping: " + leftover[0]);
  process.exit(1);
}
const header = "// Generated from " + basename(input)
  + " by packaging/strip-types.mjs (Node type stripping); edit the .ts file.\n";
writeFileSync(output, header + stripped);
