#!/usr/bin/env bash
# Golden solution. Installs a conformant H.264 decoder in JavaScript at the
# interface the milestone specifies: /app/bin/ldecod, driven as
# `bin/ldecod -i <in> -o <out> [-s]`.
#
# The entry point is a Node script, not a shell wrapper. It decides for itself
# whether the bitstream carries more than one view -- a subset sequence
# parameter set (NAL unit type 15) means multiview, and all of its views are
# written, one file per view. No flag selects that; the bitstream does, which is
# how H.264 signals everything else too.
#
# This is the oracle only; it is never present in the agent's environment.
set -euo pipefail

mkdir -p /app/bin /app/src
cp "$(dirname "$0")/ldecod-impl.js" /app/src/ldecod.js

cat > /app/bin/ldecod <<'ENTRY'
#!/usr/bin/env node
// Annex B scan for a subset SPS (nal_unit_type 15, Table 7-1).
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const args = process.argv.slice(2);
const i = args.indexOf("-i");
let multiview = false;
if (i >= 0 && args[i + 1]) {
  try {
    const d = readFileSync(args[i + 1]);
    for (let k = 0; k + 3 < d.length; k++) {
      if (d[k] === 0 && d[k + 1] === 0 && d[k + 2] === 1 && (d[k + 3] & 0x1f) === 15) {
        multiview = true;
        break;
      }
    }
  } catch {}
}

const impl = join(dirname(dirname(fileURLToPath(import.meta.url))), "src", "ldecod.js");
process.argv = [process.argv[0], impl, ...args, ...(multiview ? ["-mpr", "1"] : [])];
await import(impl);
ENTRY
chmod +x /app/bin/ldecod
echo "installed /app/bin/ldecod"
