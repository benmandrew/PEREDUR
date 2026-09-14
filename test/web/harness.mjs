/* Loads the dashboard page's script for testing.
 *
 * The page ships as a single self-contained HTML file, so there is no module to
 * import. Rather than keep a copy of the script that could drift from it, this
 * extracts the <script> block from the file that is actually served and
 * evaluates it. `document` does not exist under node, which is the signal the
 * script uses to export its functions instead of booting against the DOM. */

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PAGE = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "web",
                  "dashboard.html");

export function loadDashboard() {
  const html = readFileSync(PAGE, "utf8");
  const match = /<script>([\s\S]*?)<\/script>/.exec(html);
  if (!match) {
    throw new Error(`no <script> block in ${PAGE}`);
  }
  /* Compiled into this realm rather than a node:vm context, so the values it
     returns are ordinary arrays and objects that deepStrictEqual will accept.
     `document` is not a global under node, which is the branch the script tests
     to decide whether to boot or to export. */
  const shim = { exports: {} };
  new Function("module", match[1])(shim);
  const exported = shim.exports;
  if (!exported || typeof exported.parse !== "function") {
    throw new Error("script did not export its functions; did the boot guard " +
                    "at the end of dashboard.html change?");
  }
  return exported;
}

/** A JSONL log built from records, as the page would fetch it. */
export function log(...records) {
  return records.map((r) => JSON.stringify(r)).join("\n") + "\n";
}

/* Record builders. The field names match DashboardWriter (src/dashboard.cpp);
   real-run.jsonl below is a captured log that fails these tests if they drift
   apart. */

export function runStart(extra = {}) {
  return {
    type: "run_start", input: "spec.json", generations: 10, population: 100,
    seed: 7, objectives: ["syntactic", "semantic"],
    stages: ["breed", "dedup", "score", "select"], ...extra
  };
}

export function stage(gen, i, name, extra = {}) {
  return {
    type: "stage", gen, i, name, n_in: 10, n_out: 10, elapsed_s: 0.5, ...extra
  };
}

export function generation(gen, extra = {}) {
  return {
    type: "generation", gen, elapsed_s: 1.0, best_fitness: 0.8,
    mean_fitness: 0.5, objectives: { syntactic: 0.4, semantic: 0.6 },
    population: 100, ...extra
  };
}

/** A log captured from a real `peredur --dashboard` run, trimmed to its first
 *  two generations. Guards the page against a change to the writer's schema. */
export function realRun() {
  return readFileSync(join(dirname(fileURLToPath(import.meta.url)), "fixtures",
                           "real-run.jsonl"), "utf8");
}
