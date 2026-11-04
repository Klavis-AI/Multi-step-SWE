// Practice runner: decode the visible conformance streams with your decoder and
// report, per stream, how many output frames match the reference.
//
// Usage:  node tests/visible/run_visible.mjs [name-substring]
//
// This is the comparison TEST_INTERFACE.md section 3a describes -- decoded
// pictures in output order, split into frames and hashed -- run over a small
// practice set of streams, not the full range the decoder must handle.

import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { readFileSync, openSync, readSync, closeSync, statSync, existsSync,
         mkdtempSync, rmSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { tmpdir } from "node:os";

const here = dirname(fileURLToPath(import.meta.url));
const app = join(here, "..", "..");
const exp = JSON.parse(readFileSync(join(here, "expected.json"), "utf8"));
const filter = process.argv[2];

let totM = 0, totE = 0;
const scratch = mkdtempSync(join(tmpdir(), "h264-visible-"));
for (const s of exp.streams) {
  if (filter && !s.name.includes(filter)) continue;
  const out = join(scratch, "out.yuv");
  rmSync(out, { force: true });
  // A stream may declare more than one output file. Each entry says which file
  // it checks; a stream that names none is checked from `-o` itself. This
  // follows TEST_INTERFACE.md section 3a exactly, so this practice run agrees
  // with that section about where the pictures are.

  // expected.json puts frame_index/total_frames on the STREAM for a
  // single-output stream. Dropping them here made practice compare frames
  // 0..N-1 sequentially instead of the listed indices -- so a correct decoder
  // failed its own documented local check. The default object below copies
  // them off the stream.
  const outputs = s.outputs ?? [{ file: null, frame_size: s.frame_size,
                                  frame_md5: s.frame_md5,
                                  frame_index: s.frame_index,
                                  total_frames: s.total_frames }];
  for (const o of outputs) if (o.file) rmSync(join(scratch, o.file), { force: true });
  const t0 = Date.now();
  const r = spawnSync(join(app, "bin", "ldecod"),
                      ["-i", join(here, "streams", s.bitstream), "-o", out, "-s"],
                      { cwd: scratch, timeout: (exp.case_timeout_sec ?? 420) * 1000,
                          stdio: ["ignore", "ignore", "pipe"] });
  const ms = Date.now() - t0;
  let matched = 0, produced = 0, expected = 0;
  const missing = [];
  for (const o of outputs) {
    // A masked entry checks only the frames whose expectation is trustworthy
    // (frame_index), while still requiring the full sequence (total_frames),
    // exactly as TEST_INTERFACE.md section 3a states.
    const idx = o.frame_index ?? null;
    const wantFrames = o.total_frames ?? o.frame_md5.length;
    expected += o.frame_md5.length;
    const path = o.file ? join(scratch, o.file) : out;
    if (!existsSync(path)) { missing.push(o.file ?? "out.yuv"); continue; }
    const size = statSync(path).size;
    const n = Math.floor(size / o.frame_size);
    produced += n;
    const fd = openSync(path, "r");
    try {
      const buf = Buffer.alloc(o.frame_size);
      for (let k = 0; k < o.frame_md5.length; k++) {
        const i = idx ? idx[k] : k;
        if (i >= n) break;
        readSync(fd, buf, 0, o.frame_size, i * o.frame_size);
        if (createHash("md5").update(buf).digest("hex") === o.frame_md5[k]) matched++;
      }
    } finally { closeSync(fd); }
  }
  totM += matched; totE += expected;
  const pct = (100 * matched / expected).toFixed(0).padStart(3);
  // produced is compared against the FULL sequence, not the checked subset: a
  // decoder that emits only the trustworthy frames has not decoded the stream.
  const wantProduced = outputs.reduce(
    (t, o) => t + (o.total_frames ?? o.frame_md5.length), 0);
  const flag = matched === expected && produced === wantProduced ? "PASS" : "    ";
  console.log(`${flag} ${s.name.padEnd(28)} ${pct}%  ${matched}/${expected} frames` +
              `  produced=${produced}  ${ms}ms` +
              (missing.length ? `  MISSING: ${missing.join(", ")}` : "") +
              (r.status !== 0 ? `  exit=${r.status}` : ""));
}
rmSync(scratch, { recursive: true, force: true });
console.log(`\ntotal: ${totM}/${totE} frames (${(100 * totM / Math.max(1, totE)).toFixed(1)}%)`);
// This runner runs your
// decoder with NO capability layer, so passing every stream here while the
// decoder still breaks a section 4 rule is possible and would be baffling
// without this line.
console.log(
  "\nNote: this runner checks output format and frame accuracy only. It does not\n" +
  "apply the capability layer described in TEST_INTERFACE.md section 4 (a\n" +
  "restricted module set, removed capabilities, an immutable CommonJS resolver,\n" +
  "and refused URL schemes), which the decoder must also work under. Passing here\n" +
  "does not prove that it does \u2014 read section 4 before assuming a green total\n" +
  "is the whole story.");
