// Copies the canonical, committed benchmark JSON (docs/benchmarks/*.json -- the real Phase
// 4 measurements, see docs/design/07-aws-demo.md sec.3.3) into web/public/benchmarks/ so
// Vite serves them as static assets. Run automatically before every build (package.json
// "prebuild") -- docs/benchmarks/ stays the single source of truth, this is a copy, not a
// second copy someone has to remember to keep in sync by hand.

import { copyFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(here, "..", "..");
const src = join(repoRoot, "docs", "benchmarks");
const dest = join(here, "..", "public", "benchmarks");

mkdirSync(dest, { recursive: true });
for (const file of ["scaling.json", "false_sharing.json"]) {
  copyFileSync(join(src, file), join(dest, file));
  console.log(`synced ${file}`);
}
