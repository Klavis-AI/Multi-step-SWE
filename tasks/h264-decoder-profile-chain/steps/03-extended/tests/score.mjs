// Scorer for one step of the H.264 decoder chain.
//
// Drives the candidate through the interface the instruction names -- the JM
// reference decoder's command line, `bin/ldecod -i <bitstream> -o <output.yuv>`
// -- over this step's scored bitstreams, and compares the decoded pictures
// against the expected frame digests recorded in the case manifest.
//
// Scoring is per output frame, not per stream, so a decoder that gets the first
// pictures right and then drifts earns the fraction it actually decoded. That
// is what makes partial credit meaningful on a task where a complete conformant
// decoder is out of reach in one sitting.
//
// Everything the verifier reads lives in /tests, which the candidate never sees.

import { createHash } from "node:crypto";
import { spawn } from "node:child_process";
import { readFileSync, writeFileSync, openSync, readSync, closeSync, statSync,
         existsSync, mkdirSync, rmSync, readdirSync, accessSync, constants,
         chownSync, chmodSync, realpathSync, fstatSync } from "node:fs";
import { join , sep } from "node:path";
import { cpus } from "node:os";
import { pathToFileURL } from "node:url";

const TESTS = "/tests";
const APP = "/app";
const SCRATCH = "/tmp/h264-score";
// The scorer is root (it writes /logs/verifier and reaps the decode); the DECODE
// is not. As root the decoder could overwrite /tests/nonet.mjs, so the
// capability layer it is supposed to obey is re-read from a file it controls,
// or replace /usr/local/bin/node so later cases run a planted binary -- either
// scores without writing a decoder. The verifier Dockerfile creates this
// user; these two must stay in step with it.
const DECODE_UID = 65533, DECODE_GID = 65533;
// sha256 of bin/ldecod as validated, re-checked before every spawn
const ENTRY_DIGEST = { sha: null };
const LOGDIR = "/logs/verifier";

const manifest = JSON.parse(readFileSync(join(TESTS, "cases", "manifest.json"), "utf8"));
const CASE_TIMEOUT_MS = (manifest.case_timeout_sec ?? 120) * 1000;
const WORKERS = Math.max(1, Math.min(cpus().length, manifest.workers ?? 7));

// ---------------------------------------------------------------- workspace scan
//
// Two deterministic gates, both stated in the instruction:
//   1. the decoder is JavaScript run by node -- no compiled artefact may be
//      carried into the workspace and executed;
//   2. it decodes in-process -- no subprocess, no network.
// The binary scan does the real work: it stops a vendored static
// ffmpeg/openh264 riding in through the artifact round trip. The identifier
// scan closes the "ship the blob as base64 text" variant.

const WASM_MAGIC = Buffer.from([0x00, 0x61, 0x73, 0x6d]);
const BINARY_MAGIC = [
  { name: "ELF", bytes: [0x7f, 0x45, 0x4c, 0x46] },
  { name: "wasm", bytes: [0x00, 0x61, 0x73, 0x6d] },
  { name: "Mach-O", bytes: [0xfe, 0xed, 0xfa, 0xce] },
  { name: "Mach-O64", bytes: [0xcf, 0xfa, 0xed, 0xfe] },
  // PE ("MZ") is deliberately absent: two bytes match raw luma about once in
  // 65,536 files, so a correct decoder writing a .yuv into /app could be failed
  // for nothing -- and a PE cannot execute in this Linux container regardless.
  // The remaining signatures are four bytes each.
];
// An ELF CORE DUMP is exempt from the offset-0 ELF rule. e_type
// is the 2-byte field at offset 16, in the byte order EI_DATA (offset 5)
// declares: 1 little-endian, 2 big-endian. ET_CORE is 4. Any other e_type, or
// an unknown byte order, is not a core and stays fatal.
const ET_CORE = 4;
const isElfCore = (h, n) => n >= 18 &&
  ((h[5] === 1 && (h[16] | (h[17] << 8)) === ET_CORE) ||
   (h[5] === 2 && ((h[16] << 8) | h[17]) === ET_CORE));

// Module-reference forms. PROSE IS EXEMPT from these too, as it is from the
// identifiers below. The ticket asks for a handoff note, so an agent writing
// `require('net')` while explaining the ban must not lose the step for
// following instructions.
const MOD = "(net|http2|https|http|dns|tls|dgram|cluster|undici|child_process|worker_threads|vm|repl|test|wasi|module)";
const FORBIDDEN_IMPORT = [
  new RegExp(`\\brequire\\(\\s*['"\`](node:)?${MOD}['"\`]\\s*\\)`),
  new RegExp(`\\bfrom\\s+['"\`](node:)?${MOD}['"\`]`),
  new RegExp(`\\bimport\\s*\\(\\s*['"\`](node:)?${MOD}['"\`]`),
];
// Bare identifiers, scanned only in code: prose may name them in order to
// forbid them, and an agent may quote the rule in its notes.
const FORBIDDEN_IDENT = [
  // `createRequire` is an IDENTIFIER, not an import form, so it belongs in this
  // prose-exempt list: the interface document names it in order to forbid it,
  // and an agent that edits or copies the spec it was told to read must not
  // score zero for that. A legitimate act must not zero a step.
  /\bcreateRequire\b/,
  /\bchild_process\b/, /\bWebAssembly\b/,
  /\bprocess\.dlopen\b/,
  // Node 22 ships a global fetch and WebSocket, so "network at decode time"
  // needs no import to ban. There is NO network_mode = "no-network" control
  // here: harbor rejects it off compose, and this task runs on GKE, so the
  // verifier environment is network_mode = "public". These identifiers and the
  // capability layer in nonet.mjs are therefore the ONLY control, not a second
  // layer behind one.
  // Bare identifiers, not call forms: `const send = fetch; send(url)` needs no
  // obfuscation to pass `/\bfetch\s*\(/`. The oracle contains none of these
  // (verified: 0 occurrences of each in solution/ldecod-impl.js).
  /\bfetch\b/, /\bWebSocket\b/, /\bEventSource\b/,
  // Code smuggled as data, then evaluated.
  /\beval\s*\(/, /\bnew\s+Function\b/, /\bimportScripts\b/,
  // Reached by alias without ever writing `process.binding(` plus a bad
  // argument: `const b = process.binding`, `process["binding"]`,
  // `Reflect.apply(process.binding, process, ["spawn_sync"])`.
  /\bprocess\s*\[\s*["']binding["']\s*\]/,
  // process.binding("constants") is how a Node filesystem shim reads O_* flags
  // and is harmless; process.binding("spawn_sync") is a subprocess escape. Ban
  // every argument except "constants" rather than the accessor itself.
  /\bprocess\.binding\b(?!\s*\(\s*["']constants["']\s*\))/,
];

// Nothing is pruned from the walk, `.git` included: `.git` ships in the
// artifact (artifacts exclude=[]), and a clean `#!/usr/bin/env node` entry point
// can `import()` a file hidden inside it, or spawn an ELF hidden inside it, so
// skipping it would defeat the magic-byte, subprocess and entry-point gates at
// once. A real .git holds compressed objects, which are binary and skipped by
// the NUL test below, so scanning it costs nothing.
const SKIP_DIRS = new Set([]);
// Above this, a NUL-free file is treated as a finding rather than scanned.
const MAX_TEXT_SCAN = 64 * 1024 * 1024;
// Documentation is not executable; scanning prose for API names only ever finds
// the document that names them in order to forbid them.
const PROSE_EXT = /\.(md|txt|rst|log)$/i;
// Files git itself writes into .git/hooks on `git init` (the agent image
// installs git, and push-to-checkout.sample contains `fetch`).
// Exempt from the SOURCE scans only when the bytes match exactly -- an edited
// hook diverges and is scanned like anything else, so .git stays a scanned
// directory rather than a hiding place.
// Digests are SHIPPED: the verifier image has no git to compare against, and
// absent or stale digests mean NO exemption, so this fails CLOSED.
const GIT_HOOK_SHA = (() => {
  try {
    const j = JSON.parse(readFileSync(join(TESTS, "git-template-digests.json"), "utf8"));
    return new Set(Object.values(j.hooks || {}));
  } catch { return new Set(); }
})();
// A hook may sit at any depth: `git init` one directory down,
// `.git/modules/<name>/` (submodules) and `.git/worktrees/<name>/` (git
// worktree add) all hold a FULL gitdir -- hooks, config, reflog. The document
// promises coverage "wherever in /app the repository sits", which includes these.
const isGitTemplateHook = (rel, bytes) =>
  // No `.git/` component is required: `git init --bare` and
  // `--separate-git-dir` write the same sample hooks without one. The SHIPPED
  // DIGEST is what makes this safe, so the path test only asks for a
  // hooks/*.sample name, which covers bare repos, separate gitdirs and any
  // nesting depth at once.
  /(^|\/)hooks\/[^/]+\.sample$/.test(rel) &&
  GIT_HOOK_SHA.size > 0 &&
  GIT_HOOK_SHA.has(createHash("sha256").update(bytes).digest("hex"));

// Git bookkeeping files. git writes these itself, and their content is either
// git's own or text the AGENT legitimately authored:
//   .git/config          `git remote add` writes `fetch = +refs/heads/*:...`
//   .git/COMMIT_EDITMSG  a commit message: "rename fetch() to nextBits()"
//   .git/logs/**         the reflog, which embeds those same messages
// All are 100% printable and extension-less; scanned like decoder source,
// `git clone` or any commit naming a banned word would zero the milestone.
//
// Exempt from the IDENTIFIER and IMPORT text scans ONLY. The magic, wasm-bytes
// and base64-wasm scans still apply to every one of them, and `.git` stays a
// scanned directory for everything else -- a planted .js under .git/ is
// caught. No digest is possible here: the bytes are the agent's own words.
//
// Only files that can legitimately hold a banned token are exempt, and never
// by directory alone: `.git/config` (`git remote add` writes
// `fetch = +refs/...`), the message files, and the reflog and ref files (they
// carry the AGENT's commit text and branch names), the last two gated on
// content. `info/` and `branches/` hold ignore patterns and ref names -- never
// a banned token -- so `.git/info/decoder.mjs` or `.git/branches/x.js` is
// scanned like anything else.
//
// The reflog keeps a wildcard because branch names are arbitrary, but it is
// gated on CONTENT: every non-empty line must open with two 40-hex object names.
// A planted `.git/logs/refs/evil.js` fails that and is scanned like anything else.
const HEX = "[0-9a-f]{40,64}";
// git permits ; = & % # , and every non-ASCII letter in a ref name. Because
// THIS predicate gates gitdir RECOGNITION, a narrower class would let a branch
// named `fetch#1` stop the directory being a gitdir at all -- so .git/config,
// which holds `fetch = +refs/...`, would become scanned source. git's own
// rule: any component except space ~ ^ : ? * [ \ and control characters.
const REFNAME = "[^\\s~^:?*\\[\\\\\\x00-\\x1f]+";
const REF_SHAPED_HEAD = new RegExp(`^(?:ref: refs\\/${REFNAME}|${HEX})\\s*$`);

function findGitDirs(relPaths, readText) {
  const all = new Set(relPaths);
  const dirs = new Set();
  for (const r of relPaths) {
    const m = /^(?:(.*)\/)?HEAD$/.exec(r);
    if (!m) continue;
    const d = m[1] ?? "";
    // Measured against real git: a WORKTREE gitdir (.git/worktrees/<name>/)
    // holds HEAD, ORIG_HEAD, commondir, gitdir, index and logs/HEAD -- and NO
    // config, because it shares the parent's. A submodule gitdir does have
    // config. Requiring config alone would miss every worktree, and
    // `git worktree add fetch-nals` writes the branch name into that HEAD.
    const sib = (n) => all.has(d ? `${d}/${n}` : n);
    if (!sib("config") && !sib("commondir") && !sib("gitdir")) continue;
    let head = "";
    try { head = readText(r); } catch { continue; }
    if (REF_SHAPED_HEAD.test(head)) dirs.add(d);
  }
  return dirs;
}
const inGitDir = (rel, dirs) => {
  let i = rel.lastIndexOf("/");
  while (i > 0) {
    if (dirs.has(rel.slice(0, i))) return rel.slice(i + 1);
    i = rel.lastIndexOf("/", i - 1);
  }
  return dirs.has("") ? rel : null;
};

const GIT_NAMES = new Set([
  "config", "description", "COMMIT_EDITMSG", "MERGE_MSG", "SQUASH_MSG",
  "TAG_EDITMSG", "BISECT_LOG",
  "rebase-merge/message", "rebase-merge/git-rebase-todo", "rebase-merge/done",
  "rebase-merge/head-name", "rebase-apply/final-commit", "rebase-apply/msg-clean",
  "sequencer/todo",
]);
// The whole `*_HEAD` family, as the document promises.
const REF_NAMES =
  /^(HEAD|ORIG_HEAD|FETCH_HEAD|MERGE_HEAD|CHERRY_PICK_HEAD|REVERT_HEAD|REBASE_HEAD|BISECT_HEAD|packed-refs|refs\/.*)$/;

const looksLikeReflog = (txt) => {
  const lines = txt.split("\n").filter((l) => l.trim().length);
  const re = new RegExp(`^${HEX} ${HEX} `);
  return lines.length > 0 && lines.every((l) => re.test(l));
};
// Narrowed to what git writes: any broader alternative is a superset of git's
// grammar and so also of some JavaScript.
const looksLikeRefFile = (txt) => {
  const lines = txt.split("\n").filter((l) => l.trim().length);
  // Ref names use the same wide class as REFNAME, for the same reason. This
  // admits ref lines that are also valid JS (`ref: refs/heads/a;b` is a label,
  // divisions, an identifier). Such a dual is exempt from IDENT/IMPORT only --
  // the magic, wasm and base64 scans still run on it.
  //
  // An exempt bookkeeping file IS reachable: a dynamic import() of an
  // extensionless .git/FETCH_HEAD loads AND executes under this scorer's own
  // flags (--disallow-code-generation-from-strings plus nonet.mjs). What such a
  // file may then do, measured:
  //     child_process REFUSED   net REFUSED   vm REFUSED
  //     fs OK (including write) zlib OK       os OK
  // (No-sandbox control grants all six, so the allowlist is doing the work.)
  // The bound is therefore nonet.mjs's allowlist, not unreachability and not
  // unparseability.
  const sym = new RegExp(`^ref: refs\\/${REFNAME}$`);
  // The tab alternative exists for FETCH_HEAD, so it matches FETCH_HEAD's own
  // field grammar, read off a file git produced: a looser ` ?\t.*` would admit
  // a payload behind one tab. Ref paths use git's own ref-name grammar, the
  // SAME complement REFNAME uses. `#`, `;`, `=`, `&`, `%`, `,`, quotes and
  // non-ASCII letters are all git-legal, and `git pack-refs` writes them
  // verbatim, as section 4 states.
  const REFPATH = REFNAME;
  // Section 4's rule is "two tab-separated fields, the first empty or
  // not-for-merge". The second is a description git composes; measured first
  // characters across six fetch shapes are / b f t, so a bare-path fetch
  // writing /home/... must not be refused. The cost: this admits a 40-digit
  // SHA + two tabs + a `+require(...)` continuation, which parses. No character
  // rule excludes that and still admits git.
  const FH = "\\t(?:|not-for-merge)\\t[^\\t]*";
  const obj = new RegExp(`^\\^?${HEX}(?:$| refs\\/${REFPATH}|${FH})$`);
  return lines.length > 0 &&
    lines.every((l) => sym.test(l) || obj.test(l) || /^# /.test(l));
};
// A linked worktree's `.git` is a FILE holding `gitdir: <path>`; the path
// carries the branch name, so `git worktree add fetch-nals` puts a banned token
// in its content. Content-gated like the rest.
// The class is WIDE (any single line), because a narrow one would make
// `git worktree add '../wt;fetch'` -- a path GIT writes -- score ZERO. The
// EXEMPTION is split instead: a pointer skips FORBIDDEN_IDENT, because a path
// carries branch and directory names, and still runs FORBIDDEN_IMPORT, because
// a path is never a require().
// The cost, measured: a pointer carrying `process.binding("spawn_sync")` IS
// permitted by this scan, does load, and does run, and nonet.mjs refuses the
// capability anyway. There is no defence in depth here; the runtime allowlist
// is the control, and TEST_INTERFACE.md section 4 says so.
const looksLikeGitLink = (txt) => /^gitdir: [^\n]+\n?$/.test(txt);
// `gitdir` and `commondir` inside a worktree gitdir hold a BARE path, with no
// `gitdir:` prefix -- and the worktree's path carries its branch name.
// `git worktree add "../my work tree"` writes a pointer containing spaces, so
// this accepts any single line for the reason above: git may write any byte a
// directory name may hold, and NO character class separates a path from a
// statement without also refusing paths git actually writes.
const looksLikePath = (txt) => /^[^\n]+\n?$/.test(txt);

const isGitBookkeeping = (rel, txt, dirs) => {
  if (/(^|\/)\.git$/.test(rel) && looksLikeGitLink(txt)) return "ident";
  const inner = inGitDir(rel, dirs);
  if (inner === null) return false;
  if (GIT_NAMES.has(inner)) return true;
  if (inner === "gitdir" || inner === "commondir") return looksLikePath(txt) ? "ident" : false;
  if (inner.startsWith("logs/")) return looksLikeReflog(txt);
  if (REF_NAMES.test(inner)) return looksLikeRefFile(txt);
  return false;
};

function walk(dir, out = [], seen = new Set(), root = null) {
  // readdir's entry types come from lstat, so a SYMLINK is neither isFile()
  // nor isDirectory() and would fall through unscanned -- while `import`
  // follows it happily. TEST_INTERFACE promises every file in /app is
  // examined, so symlinks are followed too. realpath + `seen` makes a symlink
  // loop terminate instead of recursing forever.
  let real;
  try { real = realpathSync(dir); } catch { return out; }
  if (root === null) root = real;          // the first call fixes the boundary
  if (seen.has(real)) return out;
  seen.add(real);
  let ents;
  try { ents = readdirSync(dir, { withFileTypes: true }); } catch { return out; }
  for (const e of ents) {
    const p = join(dir, e.name);
    if (e.isDirectory()) { if (!SKIP_DIRS.has(e.name)) walk(p, out, seen, root); }
    else if (e.isFile()) out.push(p);
    else if (e.isSymbolicLink()) {
      // A link may point OUT of /app -- into a system directory, or onto a
      // file whose contents the candidate does not control. Scanning is for
      // the candidate's own workspace, so a link is followed only while its
      // target stays inside the root, and ignored otherwise.
      let real;
      try { real = realpathSync(p); } catch { continue; }   // dangling: nothing to read
      if (real !== root && !real.startsWith(root + sep)) continue;
      let st;
      try { st = statSync(p); } catch { continue; }
      if (st.isFile()) out.push(p);
      else if (st.isDirectory() && !SKIP_DIRS.has(e.name)) walk(p, out, seen, root);
    }
  }
  return out;
}

function shippedDigests() {
  // Files this task staged into the workspace (practice harness, specs, the
  // format example). They are allowed to contain whatever they contain -- the
  // interface document names the forbidden APIs in order to forbid them, and
  // the practice runner legitimately spawns the decoder. Identified by content,
  // so an agent that edits one loses the exemption for it.
  try {
    const j = JSON.parse(readFileSync(join(TESTS, "cases", "shipped.json"), "utf8"));
    // (relative path, sha256) pairs. A copy of a shipped file at a NEW path is
    // not exempt -- otherwise a copy of the practice runner is a spawn
    // trampoline.
    return new Set((j.pairs ?? []).map((e) => `${e.path}\u0000${e.sha256}`));
  } catch { return new Set(); }
}

function scanWorkspace() {
  const problems = [];
  // Findings the scanner cannot tell from legitimate work. Reported in
  // report.advisories, never fatal: charging a whole milestone for a 4-byte
  // coincidence would make honest work score zero.
  const advisories = [];
  const shipped = shippedDigests();
  // Every ticket says "preserve the supplied documents, specifications and
  // handoff files", and the seal only covers a file that remains. Deletion is
  // reported, not fatal: removing a document does not break the decoder, but
  // the next milestone inherits a workspace the ticket said would be there.
  for (const pair of shipped) {
    const rel = pair.split("\u0000")[0];
    if (!existsSync(join(APP, rel))) {
      advisories.push(`shipped file removed from the workspace: ${rel}`);
    }
  }
  // The loop above walks only THIS step's seal, so the SPECs inherited from
  // earlier milestones -- which the ticket also tells the agent to preserve --
  // are checked here. Also an ADVISORY: removing a document must not zero a
  // step. The inherited set needs no new data: step k inherits every earlier step's
  // own SPEC, which is how the cumulative workspace is built.
  {
    const order = ["01-baseline", "02-main", "03-extended", "04-frext-high", "05-mvc"];
    const specOf = { "01-baseline": "SPEC/01-baseline.md", "02-main": "SPEC/02-main.md",
                     "03-extended": "SPEC/03-extended.md", "04-frext-high": "SPEC/04-frext.md",
                     "05-mvc": "SPEC/05-mvc.md" };
    const here = order.indexOf(manifest.step);
    const inherited = order.slice(0, here).map((s) => specOf[s]).filter(Boolean);
    // Fire only when the workspace LOOKS CUMULATIVE. A step's shipped workdir
    // carries only its own SPEC -- the earlier ones arrive by seeding from the
    // previous rung -- so an unconditional check would report "inherited
    // specification removed" on every ORACLE run, where nothing was removed and
    // the file was never there.
    //
    // Residual gap: deleting EVERY inherited spec leaves nothing to recognise
    // the workspace by, so that case is silent. The conservative direction --
    // no false positives -- is the right one for an advisory.
    const present = inherited.filter((rel) => existsSync(join(APP, rel)));
    if (present.length > 0) {
      for (const rel of inherited) {
        if (!existsSync(join(APP, rel))) {
          advisories.push(`inherited specification removed from the workspace: ${rel}`);
        }
      }
    }
  }
  // The seal is keyed `<rel>\0<digest>`, so a file whose PATH is not in it can
  // never match one and never needs to be read at all.
  const sealedPaths = new Set(
    [...shipped].map((k) => k.slice(0, k.indexOf("\u0000"))));
  // The gitdir set is derived from the SAME walk the scan uses, so
  // there is no second traversal to keep in sync. readText is lazy and failures
  // are non-fatal -- a directory that cannot be read is simply not a gitdir.
  const ALLFILES = walk(APP);
  const ALLREL = ALLFILES.map((f) => f.slice(APP.length + 1));
  // BOUNDED read. A gitdir is decided by HEAD's first line, and an agent can
  // create a multi-gigabyte file called HEAD, on which a whole-file read
  // throws ERR_FS_FILE_TOO_LARGE. 512 bytes is far more than any real HEAD and
  // cannot be made expensive.
  const headPeek = (r) => {
    let fd = -1;
    try {
      fd = openSync(join(APP, r), "r");
      const b = Buffer.alloc(512);
      const n = readSync(fd, b, 0, 512, 0);
      return b.slice(0, n).toString("utf8");
    } finally { if (fd >= 0) { try { closeSync(fd); } catch { /* gone */ } } }
  };
  const GITDIRS = findGitDirs(ALLREL, headPeek);
  for (const f of ALLFILES) {
    let st; try { st = statSync(f); } catch { continue; }
    if (st.size === 0) continue;
    const rel = f.slice(APP.length + 1);
    let sealed = false;
    // Only a file at a SEALED PATH can match a seal entry, so only those are
    // hashed. Hashing every file whole would read a decoder's own
    // multi-gigabyte .yuv, and a large file throws ERR_FS_FILE_TOO_LARGE, falls
    // through as UNSEALED, and is then text-scanned. A verbatim copy elsewhere
    // is NOT exempt: TEST_INTERFACE.md states that rule, names run_visible.mjs
    // as the file it matters for, and gives the supported alternative (a name
    // filter).
    if (shipped.size && sealedPaths.has(rel)) {
      try {
        const dg = createHash("sha256").update(readFileSync(f)).digest("hex");
        sealed = shipped.has(`${rel}\u0000${dg}`);
      } catch {}
    }
    // `openSync` sits INSIDE the try whose readSync/closeSync it feeds, so an
    // EACCES, ENFILE or a file vanishing between walk() and here cannot throw
    // out of scanWorkspace and zero the whole milestone. A file the scanner
    // cannot open is reported, not fatal.
    let fd = -1;
    const head = Buffer.alloc(Math.min(st.size, 8192));
    let n = 0;
    try {
      fd = openSync(f, "r");
      if (head.length) n = readSync(fd, head, 0, head.length, 0);
    } catch {
      advisories.push(`unreadable file in the workspace: ${rel}`);
      continue;
    } finally {
      if (fd >= 0) { try { closeSync(fd); } catch { /* already gone */ } }
    }
    // Every signature is looked for at offset 0 and at any offset within the
    // first 8 KiB, so ONE leading byte cannot hide an image from the scan. At
    // offset 0 an ELF or Mach-O match is fatal; deeper in, it is reported, as
    // TEST_INTERFACE.md section 4 states. Interior wasm is fatal via the
    // WASM_MAGIC search below.
    for (const m of BINARY_MAGIC) {
      if (n < m.bytes.length) continue;
      const sig = Buffer.from(m.bytes);
      if (m.bytes.every((b, i) => head[i] === b)) {
        if (m.name === "ELF" && isElfCore(head, n)) {
          // A crashing process -- node aborting on heap exhaustion, say --
          // dumps core into its working directory, and the agent's shell works
          // in /app, so ordinary debugging can leave a multi-gigabyte `core`
          // here. A core is not an
          // executable image: execve and dlopen accept only ET_EXEC/ET_DYN,
          // so it can carry nothing a .yuv could not. Reported, not fatal; the
          // wasm search and the text rules below still apply to it.
          advisories.push(`core dump (ELF ET_CORE, not an executable image): ${rel}`);
        } else {
          // At byte 0 this IS an executable image, whatever it is called.
          problems.push(`binary artefact (${m.name}): ${rel}`);
        }
      } else {
        const at = head.subarray(0, n).indexOf(sig);
        // wasm is in BINARY_MAGIC too, but an interior wasm hit is fatal via
          // the "WebAssembly module bytes inside" check below, so it is not also
          // reported here as an advisory; section 4 attributes interior
          // advisories to ELF and Mach-O only.
          if (at > 0 && m.name !== "wasm") {
          // Interior match. Real for a smuggled artefact, and ~7.6e-6 per file
          // for a decoder's own .yuv, whose luma bytes span every value. Report
          // it and let the milestone stand.
          advisories.push(`bytes resembling ${m.name} at offset ${at} in ${rel}`);
        }
      }
    }
    // A compiled module need not sit at byte 0. One prefix byte would hide an
    // entire wasm decoder from the offset-0 test above, and the file would then
    // be skipped by the printable-ratio rule before any text scan could see it
    // -- so this search belongs HERE, the one place every file passes through.
    if (n >= 4 && head.subarray(0, n).includes(WASM_MAGIC)) {
      problems.push(`WebAssembly module bytes inside ${f.slice(APP.length + 1)}`);
    }
    // Decide "could this be source" by content, not by name: the entry point
    // has no extension, and a candidate may put code anywhere. Sealed files are
    // exempt from the API scan only -- never from the magic-byte scan above.
    // Extension-based exemption would let a legal Node entry point smuggle the
    // whole denylist past the scan: `eval(readFileSync('/app/payload.yuv','utf8'))`.
    // Content decides instead -- the NUL test below skips genuine binaries.
    if (!sealed) {
      // Sample the head BEFORE reading the file whole. This loop visits every
      // file in /app, and a decoder may legitimately leave its own
      // multi-gigabyte .yuv output there -- readFileSync on those would put the
      // scanner itself at risk of OOM.
      // The head is already in hand: the guarded read above filled `head` with
      // this file's first 8 KiB and reported the file if it could not.
      // Decide by CHARACTER MIX, not by a single NUL (which would let a payload
      // whose first byte is 0x00 skip every scan): a decoder's own .yuv output
      // is dense non-printable, while a smuggled source payload is
      // overwhelmingly ASCII wearing one NUL. Only the dense case is skipped,
      // so the OOM guard above still holds.
      let printable = 0;
      for (let i = 0; i < head.length; i++) {
        const b = head[i];
        if (b === 9 || b === 10 || b === 13 || (b >= 32 && b < 127)) printable++;
      }
      if (head.length && printable / head.length < 0.90) continue;  // real binary
      // A large file with no NUL in its first 8 KiB is not plausible decoder
      // source; it is the shape a smuggled base64 or text payload takes. Report
      // it rather than read it.
      //
      // Prose is exempt from the oversize rule as it is from every other
      // content rule: oversized prose (.md/.txt/.rst/.log) is SKIPPED, not
      // fatal. The limit is stated in TEST_INTERFACE.md section 4.
      if (st.size > MAX_TEXT_SCAN) {
        if (!PROSE_EXT.test(f)) {
          problems.push(`oversized non-binary file (${st.size} bytes): ${rel}`);
        }
        continue;
      }
      let buf;
      try { buf = readFileSync(f); } catch { continue; }
      const txt = buf.toString("utf8");
      // `buf` is the WHOLE file, so this digest is over the real bytes.
      // A pointer file is IDENT-exempt (its path carries a branch or directory
      // name, so /\bfetch\b/ fires on legitimate content) but still
      // IMPORT-scanned (a path is never a require()). Every other exempt file is
      // exempt from both lists.
  const bk = isGitTemplateHook(rel, buf) ? true : isGitBookkeeping(rel, txt, GITDIRS);
  const exemptImport = bk === true;
  const exemptIdent  = bk === true || bk === "ident";
      // Prose is exempt from the IMPORT forms too, not only from the bare
      // identifiers below. The document promises that .md/.txt/.rst/.log "are
      // exempt, so prose may name the rule in order to state it", and the
      // ticket ASKS for a handoff note describing what was done -- so an agent
      // writing `require('net')` while explaining the module ban must not score
      // 0 for following instructions.
      //
      // Prose CAN reach execution -- `Module.prototype._compile` is callable and
      // runs a .md through the .js handler, and a file written to /tmp can be
      // require()d, neither of which --disallow-code-generation-from-strings
      // covers -- but the runtime allowlist is the control and this scan is
      // defence in depth, so code that reaches execution by any of those routes
      // still meets the module allowlist, the non-string type refusal and the
      // WebAssembly refusals.
      if (!PROSE_EXT.test(f) && !exemptImport) {
        for (const re of FORBIDDEN_IMPORT) {
          if (re.test(txt)) problems.push(`forbidden module import ${re} in ${rel}`);
        }
      }
      // A wasm decoder pasted in as base64 never shows the \0asm magic at byte
      // 0, so the magic scan cannot see it. Its base64 always begins "AGFzbQ".
      // WebAssembly cannot simply be removed at runtime (Node's undici needs
      // it), so this form is caught here.
      // ...and spelled as numbers: [0,97,115,109,...] or 0x00,0x61,0x73,0x6d
      //
      // Prose-exempt, like every other TEXT scan here. TEST_INTERFACE.md
      // contains the literal "AGFzbQ" in order to state this very rule, so an
      // agent that copies the spec into its own notes, or quotes this
      // paragraph, must not score zero for it. A legitimate act must not zero a
      // step.
      //
      // Nothing is lost by exempting prose: a base64 or byte-array payload in a
      // .md is inert. WebAssembly.* throws unconditionally, dlopen and execve
      // are removed, and child_process is refused, so there is no route from
      // bytes-in-prose to execution. The BINARY MAGIC test above still applies
      // to every file including prose -- a .md whose first bytes are ELF is not
      // prose -- and that is the check that matters for a real artefact.
      if (!PROSE_EXT.test(f)) {
        if (/\b0\s*,\s*97\s*,\s*115\s*,\s*109\b/.test(txt) ||
            /0x00\s*,\s*0x61\s*,\s*0x73\s*,\s*0x6d/i.test(txt)) {
          problems.push(`WebAssembly module as a byte array in ${rel}`);
        }
        if (/AGFzbQ/.test(txt)) {
          problems.push(`base64-encoded WebAssembly module in ${rel}`);
        }
      }
      if (!PROSE_EXT.test(f) && !exemptIdent) {
        for (const re of FORBIDDEN_IDENT) {
          if (re.test(txt)) problems.push(`forbidden API ${re} in ${rel}`);
        }
      }
    }
  }
  return { problems, advisories };
}

// ---------------------------------------------------------------- run one case

function runCase(c) {
  return new Promise((res) => {
    const dir = join(SCRATCH, c.name);
    // Guarded: unguarded inside the Promise executor, an ENOSPC or EBUSY here
    // would REJECT the chain and main().catch would then report 0 for every
    // case in the milestone. TEST_INTERFACE tells the candidate that /tmp is
    // not cleaned between cases, so a decoder that fills the disk must not turn
    // its own waste into a whole-step zero; `force: true` swallows ENOENT and
    // nothing else. Keep the failure LOCAL: this case scores 0, the milestone
    // runs on.
    try {
      rmSync(dir, { recursive: true, force: true });
      mkdirSync(dir, { recursive: true });
    } catch (e) {
      return res({ name: c.name, score: 0,
                   note: `scratch setup failed: ${e && (e.code || e.message)}` });
    }
    // The decode is unprivileged, so its working directory has to be its own --
    // this is where it writes the YUV the scorer then grades.
    try { chownSync(dir, DECODE_UID, DECODE_GID); chmodSync(dir, 0o700); } catch {}
    const outPath = join(dir, "out.yuv");
    const bs = join(TESTS, "cases", "streams", c.bitstream);
    const started = Date.now();
    // The workspace was validated before case 1. Anything that rewrites the
    // entry point afterwards -- truncating it into a shell wrapper around a
    // planted binary, say -- must not be re-spawned unchecked.
    if (ENTRY_DIGEST.sha) {
      let now = null;
      try { now = createHash("sha256").update(readFileSync(join(APP, "bin", "ldecod"))).digest("hex"); }
      catch { now = null; }
      if (now !== ENTRY_DIGEST.sha) {
        return res({ name: c.name, score: 0,
                     note: "bin/ldecod changed after validation" });
      }
    }
    const args = ["-i", bs, "-o", outPath, "-s", ...(c.extra_args ?? [])];
    let child;
    try {
      // The decode runs with the network capability removed from inside the
      // process. The platform cannot do it for us: Harbor's GKE provider only
      // advertises disable_internet in compose mode, and this task is a plain
      // Dockerfile, so `network_mode = "no-network"` is rejected outright.
      // A static scan alone cannot close it either -- Node 22 has a
      // global fetch, so `globalThis["fe"+"tch"]` needs no import and no banned
      // identifier. nonet.mjs redefines the globals non-writably and refuses the
      // network module specifiers for both `import` and `require`.
      child = spawn(join(APP, "bin", "ldecod"), args,
                    { cwd: dir, stdio: ["ignore", "pipe", "pipe"], detached: true,
                      // Unprivileged: /tests, /usr/local/bin and /usr/local/lib
                      // are root-owned and read-only to this uid, so neither the
                      // preload nor the interpreter can be rewritten mid-run.
                      uid: DECODE_UID, gid: DECODE_GID,
                      env: { ...process.env,
                             // --disallow-code-generation-from-strings makes
                             // eval() and new Function() throw at the VM level,
                             // including forms the /\beval\s*\(/ scan cannot
                             // see, such as `{ eval: true }`. Verified not to
                             // affect the oracle, which is an Emscripten asm.js
                             // build with no codegen.
                             NODE_OPTIONS: `${process.env.NODE_OPTIONS ?? ""} --disallow-code-generation-from-strings --import=${pathToFileURL(join(TESTS, "nonet.mjs")).href}`.trim() } });
    } catch (e) {
      return res({ name: c.name, score: 0, note: `spawn failed: ${e.message}` });
    }
    let killed = false;
    const killGroup = () => {
      try { process.kill(-child.pid, "SIGKILL"); } catch {}
      try { child.kill("SIGKILL"); } catch {}
    };
    const timer = setTimeout(() => { killed = true; killGroup(); }, CASE_TIMEOUT_MS);
    // The capability layer reports its own failures on fd 2. Keep a bounded
    // tail so a refusal, a nonet install failure or a decoder's own crash is
    // readable in report.json.
    // Bounded at 4 KiB per case and attached only where it explains something,
    // so 96 cases cannot bloat the report.
    let errTail = "";
    child.stderr.on("data", (b) => {
      if (errTail.length >= 4096) return;
      errTail += b.toString("utf8").slice(0, 4096 - errTail.length);
    });
    child.stdout.on("data", () => {});
    // A stream 'error' with no listener is an uncaughtException, which the
    // last-resort handler turns into reward 0 for the WHOLE step rather than
    // this case. Both pipes need one; `child.on("error")` does not cover them.
    child.stdout.on("error", () => {});
    child.stderr.on("error", () => {});
    child.on("error", () => {});
    child.on("close", (code) => {
      clearTimeout(timer);
      killGroup();          // reap anything the wrapper backgrounded before exec
      const ms = Date.now() - started;
      const r = gradeOutput(c, outPath);
      r.name = c.name;
      r.ms = ms;
      r.exit = code;
      if (killed) r.note = `timeout after ${CASE_TIMEOUT_MS / 1000}s`;
      if (errTail && (r.score === 0 || errTail.includes("nonet:"))) r.stderr_tail = errTail;
      else if (r.note === undefined && code !== 0) r.note = `exit ${code}`;
      // Also inside the "close" handler, so also outside main().catch. force:true
      // swallows ENOENT but not EBUSY or EACCES, and a throw here would zero the
      // whole step. Scratch that cannot be removed is not a scoring question.
      try { rmSync(dir, { recursive: true, force: true }); } catch { /* leave it */ }
      res(r);
    });
  });
}

function gradeOutput(c, outPath) {
  // MVC and other multi-output cases declare their output files explicitly.
  const outputs = c.outputs ?? [{ file: null, frame_size: c.frame_size,
                                  frame_md5: c.frame_md5,
                                  frame_index: c.frame_index,
                                  total_frames: c.total_frames }];
  let matched = 0, expected = 0, produced = 0, maskedDenom = 0, masked = false;
  for (const o of outputs) {
    const p = o.file ? join(SCRATCH, c.name, o.file) : outPath;
    expected += o.frame_md5.length;
    const fsz = o.frame_size;
    // `frame_index` scores a subset of the output: the frames for which a
    // trustworthy expectation exists (SP/SI, where the reference software's own
    // encoder and decoder disagree). `total_frames` keeps that from being a free
    // pass -- the decoder must still emit the whole sequence, at the right size.
    const idx = o.frame_index ?? null;
    // `masked` is per CASE, so `maskedDenom` accumulates unmasked outputs too:
    // a case mixing a masked output with an unmasked one must not count the
    // unmasked sibling's matches in the numerator while dropping its
    // expectations from the denominator. No case in this tree mixes the two
    // (03 is masked and single-output, 05 is multi-output and unmasked).
      if (idx) masked = true;
      if (!idx) maskedDenom += o.frame_md5.length;
    let nProduced = 0;
    // gradeOutput runs inside the child's "close" handler, which is OUTSIDE
    // main().catch -- so anything thrown here exits the scorer non-zero and
    // test.sh reports 0 for the WHOLE STEP instead of for this one case. An
    // out.yuv left as a DIRECTORY would do exactly that: existsSync passes,
    // statSync reports a size, Linux permits O_RDONLY on a directory, and
    // readSync throws EISDIR. One malformed output must not zero 110 good cases.
    // The output sits in a directory the decode uid owns,
    // and a process it detached can outlive the case. So the root scorer opens
    // it once, never through a symlink (O_NOFOLLOW) and never blocking on a
    // FIFO (O_NONBLOCK), and decides "regular file" from that same descriptor
    // (fstat) rather than from a path that can be swapped after a stat.
    let pst = null;
    let fd = -1;
    try {
      fd = openSync(p, constants.O_RDONLY | constants.O_NOFOLLOW | constants.O_NONBLOCK);
      pst = fstatSync(fd);
    } catch { pst = null; }
    if (!(pst && pst.isFile() && fsz) && fd >= 0) {
      try { closeSync(fd); } catch { /* already gone */ }
      fd = -1;
    }
    if (pst && pst.isFile() && fsz) {
      nProduced = Math.floor(pst.size / fsz);
      try {
        const buf = Buffer.alloc(fsz);
        for (let j = 0; j < o.frame_md5.length; j++) {
          const fi = idx ? idx[j] : j;
          if (fi >= nProduced) continue;
          readSync(fd, buf, 0, fsz, fi * fsz);
          if (createHash("md5").update(buf).digest("hex") === o.frame_md5[j]) matched++;
        }
      } catch {
        // An unreadable or truncated output scores whatever matched before the
        // error, which is normally nothing. Never rethrow from here.
      } finally {
        if (fd >= 0) { try { closeSync(fd); } catch { /* already gone */ } }
      }
    }
    produced += nProduced;
    if (idx) {
      const want = o.total_frames ?? o.frame_md5.length;
      maskedDenom += o.frame_md5.length + Math.abs(nProduced - want);
    }
  }
  const denom = (masked ? maskedDenom : Math.max(expected, produced)) || 1;
  return { score: Math.min(1, matched / denom), matched, expected, produced };
}

// ---------------------------------------------------------------- main

async function main() {
  mkdirSync(LOGDIR, { recursive: true });
  mkdirSync(SCRATCH, { recursive: true });
  // The decode does not run as root, so it must still be able to READ the
  // workspace it is being scored on. Best effort and deliberately read-only:
  // nothing here grants write access to /app.
  try {
    chmodSync(SCRATCH, 0o711);
    const openUp = (d) => {
      try { chmodSync(d, 0o755); } catch {}
      let ents = [];
      try { ents = readdirSync(d, { withFileTypes: true }); } catch { return; }
      for (const e of ents) {
        const p = join(d, e.name);
        if (e.isDirectory()) openUp(p);
        // NEVER chmod through a symlink. readdir's types come from lstat, so a
        // symlink is neither isDirectory() nor isFile() and would otherwise
        // fall into the branch below -- where chmodSync FOLLOWS it. A symlink
        // in /app pointing at /tests/cases/manifest.json would make the ROOT
        // scorer widen that file to 0644, handing the decode every case name
        // and expected MD5 and defeating the Dockerfile's chmod 600. A symlink
        // needs no widening anyway -- access is decided by its target's own mode.
        else if (e.isSymbolicLink()) { /* left exactly as the candidate made it */ }
        else {
          // preserve whatever execute bits the candidate set; only add read
          try {
            const m = statSync(p).mode & 0o777;
            chmodSync(p, (m | 0o444 | ((m & 0o100) ? 0o111 : 0)) & ~0o022);
          } catch {}
        }
      }
    };
    openUp(APP);
    // The executable bit on bin/ldecod is NOT restored: doing so would repair a
    // workspace the candidate had broken. A verifier reports the workspace it
    // is given; it never fixes it. openUp() only widens READ access so the
    // unprivileged decode can see the tree.
  } catch {}
  const report = { step: manifest.step, cases: [], reward: 0 };

  const entry = join(APP, "bin", "ldecod");
  if (!existsSync(entry)) { report.fatal = "no executable at /app/bin/ldecod"; return finish(report, 0); }
  try { accessSync(entry, constants.X_OK); }
  catch { report.fatal = "/app/bin/ldecod is not executable"; return finish(report, 0); }

  // The entry point is the one file guaranteed to execute. It must be a text
  // script, and any command it execs must be node -- otherwise "bin/ldecod" is
  // free to be a wrapper around a foreign decoder.
  const entryProblems = [];
  {
    let buf;
    try { buf = readFileSync(entry); } catch { buf = null; }
    if (!buf) entryProblems.push("bin/ldecod could not be read");
    else {
      for (let i = 0; i < Math.min(buf.length, 8192); i++) {
        if (buf[i] === 0) { entryProblems.push("bin/ldecod is not a text script"); break; }
      }
      const txt = buf.toString("utf8");
      const lines = txt.split("\n");
      const shebang = (lines[0] || "");
      // Exactly one legal shape: a Node script. A shell wrapper around
      // `exec node ...` is not accepted: in POSIX sh an assignment executes its
      // right-hand side, so a wrapper like this would pass every other check:
      //     _=$(base64 -d < /app/blob.b64 > /tmp/n; chmod +x /tmp/n; /tmp/n "$@")
      //     exec node -e 'process.exit(0)'
      // The blob is ASCII, so the magic-byte scan never fires; /tmp is outside
      // /app, so it is never walked. The oracle is a Node script, so nothing
      // legitimate needs a shell.
      // The kernel honours whatever path the shebang names, so a substring test
      // for "node" is not a test at all: `#!/app/src/node` naming a shell
      // script the candidate wrote passes it, and so does
      // `#!/usr/bin/env -S PATH=/app/bin:/usr/bin node`. Resolve the
      // interpreter instead: it must be one of a small set of absolute paths
      // OUTSIDE the candidate's tree, with no arguments of its own.
      const NODE_PATHS = new Set([
        "/usr/local/bin/node", "/usr/bin/node", "/bin/node",
      ]);
      const m = shebang.match(/^#!\s*(\S+)\s*(.*)$/);
      if (!m) {
        entryProblems.push("bin/ldecod has no #! line; it must be a node script");
      } else {
        const interp = m[1];
        const rest = (m[2] || "").trim();
        if (interp === "/usr/bin/env" || interp === "/bin/env") {
          if (rest !== "node") {
            entryProblems.push(
              `bin/ldecod: \`env\` may be given exactly \`node\` and nothing else ` +
              `(no -S, no assignments, no arguments); found: ${shebang.trim()}`);
          }
        } else if (!NODE_PATHS.has(interp)) {
          entryProblems.push(
            `bin/ldecod interpreter must be node: one of ` +
            `${[...NODE_PATHS].join(", ")} or \`/usr/bin/env node\`; ` +
            `found: ${interp}`);
        } else if (rest !== "") {
          entryProblems.push(
            `bin/ldecod: the node interpreter takes no arguments here; found: ${rest}`);
        }
      }
    }
  }

  // Pin what we just validated. The entry point is checked once, here, and then
  // spawned once per case -- so a decoder that truncates /app/bin/ldecod into a
  // shell wrapper on case 1 would be re-spawned 95 more times without another
  // look. runCase() compares against this before every spawn.
  try { ENTRY_DIGEST.sha = createHash("sha256").update(readFileSync(entry)).digest("hex"); }
  catch { ENTRY_DIGEST.sha = null; }

  const scan = scanWorkspace();
  const problems = scan.problems.concat(entryProblems);
  const advisories = scan.advisories;
  // Advisories and problems are kept in separate fields -- the fatal decision
  // below uses `problems` alone -- so a reader cannot mistake one for the
  // other.
  report.workspace_problems = problems;
  report.advisories = advisories;
  if (problems.length) {
    report.fatal = `workspace violates the stated implementation constraints: ${problems.slice(0, 8).join("; ")}`;
    return finish(report, 0);
  }

  // Verify the case data the scorer actually reads, rather than trusting that
  // the assembler produced what it printed. A case whose stream is missing, or
  // whose expectation is empty, would otherwise score 0 and look like a
  // candidate failure.
  const integrity = [];
  for (const c of manifest.cases) {
    const bs = join(TESTS, "cases", "streams", c.bitstream);
    if (!existsSync(bs)) integrity.push(`missing stream: ${c.bitstream}`);
    const outs = c.outputs ?? [{ frame_md5: c.frame_md5, frame_size: c.frame_size }];
    if (c.family !== undefined && c.family !== "new" && c.family !== "inherited") {
      integrity.push(`unknown family: ${c.name}`);
    }
    for (const o of outs) {
      if (!o.frame_md5 || o.frame_md5.length === 0) integrity.push(`empty expectation: ${c.name}`);
      if (!o.frame_size) integrity.push(`no frame_size: ${c.name}`);
    }
  }
  report.case_data_problems = integrity;
  if (integrity.length) {
    report.fatal = `case data is not usable: ${integrity.slice(0, 6).join("; ")}`;
    return finish(report, 0);
  }

  // Interleave by group so that a candidate which runs out of time loses a
  // slice of every family rather than whole families. With the cases grouped
  // conformance-then-generated, a slow decoder would score 0 on everything
  // generated purely because of ordering.
  const byGroup = new Map();
  for (const c of manifest.cases) {
    const k = c.group ?? "ungrouped";
    if (!byGroup.has(k)) byGroup.set(k, []);
    byGroup.get(k).push(c);
  }
  const lists = [...byGroup.values()];
  const queue = [];
  for (let i = 0; ; i++) {
    let added = false;
    for (const l of lists) if (i < l.length) { queue.push(l[i]); added = true; }
    if (!added) break;
  }
  const results = [];
  const deadline = Date.now() + (manifest.deadline_sec ?? 2400) * 1000;
  let skipped = 0;
  async function worker() {
    for (;;) {
      const c = queue.shift();
      if (!c) return;
      if (Date.now() > deadline) { skipped++; continue; }
      results.push(await runCase(c));
    }
  }
  await Promise.all(Array.from({ length: WORKERS }, worker));
  report.ran = results.length;
  report.total = manifest.cases.length;
  report.not_run = skipped;
  if (skipped) report.deadline_hit = true;

  // Product scoring: score = T1 x T2. Each case is classed in the manifest by
  // whether its bitstream needs a coding tool the previous milestone lacks
  // ("new", T1) or stays on inherited tools ("inherited", T2). Each factor is
  // the weighted mean of its own cases, with a case that never reported
  // counting 0; a step with no inherited case is T1 alone. tests/helpers.sh
  // turns this score into the binary step reward.
  const byName = new Map(results.map((r) => [r.name, r]));
  const fam = { new: [0, 0], inherited: [0, 0] };
  for (const c of manifest.cases) {
    const r = byName.get(c.name) ?? { score: 0, note: "missing result" };
    const w = c.weight ?? 1;
    const f = c.family === "inherited" ? "inherited" : "new";
    fam[f][0] += w * r.score;
    fam[f][1] += w;
    report.cases.push({ name: c.name, group: c.group, family: f, weight: w, score: +r.score.toFixed(6),
                        matched: r.matched, expected: r.expected, produced: r.produced,
                        ms: r.ms, note: r.note, exit: r.exit,
                        // Both are undefined on a clean pass, and JSON.stringify
                        // drops undefined keys, so 96 passing cases add nothing.
                        // stderr_tail is set only where it explains something,
                        // and has to be carried HERE to reach report.json.
                        stderr_tail: r.stderr_tail });
  }
  const t1 = fam.new[1] ? fam.new[0] / fam.new[1] : 0;
  const t2 = fam.inherited[1] ? fam.inherited[0] / fam.inherited[1] : 1;
  report.t1 = +t1.toFixed(6);
  report.t2 = +t2.toFixed(6);
  report.family_weights = { new: fam.new[1], inherited: fam.inherited[1] };
  finish(report, t1 * t2);
}

function finish(report, reward) {
  reward = Math.max(0, Math.min(1, reward));
  report.reward = +reward.toFixed(6);
  const g = {};
  for (const c of report.cases) {
    const k = c.group ?? "ungrouped";
    (g[k] ??= { n: 0, sum: 0 });
    g[k].n++; g[k].sum += c.score;
  }
  report.groups = Object.fromEntries(Object.entries(g)
    .map(([k, v]) => [k, { cases: v.n, mean: +(v.sum / v.n).toFixed(4) }]));
  try { writeFileSync(join(LOGDIR, "report.json"), JSON.stringify(report, null, 1)); } catch {}
  try { writeFileSync(join(LOGDIR, "reward.json"), JSON.stringify({ reward: report.reward }) + "\n"); } catch {}
  const lines = [`step ${report.step}  reward ${report.reward}`];
  if (report.fatal) lines.push(`FATAL: ${report.fatal}`);
  for (const [k, v] of Object.entries(report.groups)) lines.push(`  ${k}: ${v.cases} cases mean ${v.mean}`);
  // Candidate-controlled text reaches this stream: report.fatal interpolates
  // workspace filenames, and a filename may contain a newline. test.sh scans for
  // ^REWARD= and keeps the LAST match, so relying on the real line being
  // pushed after an injected "REWARD=1" would be defence by ordering alone.
  // Flatten every line that carries candidate text, then append the real
  // REWARD= after flattening, so the emitted stream cannot contain a REWARD=
  // line this function did not write.
  const flat = lines.map((l) => String(l).replace(/[\r\n]+/g, " "));
  flat.push(`REWARD=${report.reward}`);
  console.log(flat.join("\n"));
  process.exit(0);
}

// A LAST RESORT, so that "always writes reward.json and exits 0" is true by
// construction rather than by enumerating every callback.
//
// main().catch covers the promise chain only. An exception thrown inside an
// EVENT CALLBACK -- the child's "close" handler, a stream 'data' listener, a
// timer -- bypasses it completely, and the scorer then exits non-zero and
// test.sh reports 0 for the WHOLE milestone. This makes the property
// structural instead of depending on every trigger being guarded.
//
// It does NOT hide the failure: the fatal goes into report.json, so a rung that
// reports 0 with a fatal and no cases scored is identifiable as a VERIFIER crash
// rather than an agent miss.
const lastResort = (what) => (e) => {
  try {
    mkdirSync(LOGDIR, { recursive: true });
    writeFileSync(join(LOGDIR, "reward.json"), JSON.stringify({ reward: 0 }) + "\n");
    writeFileSync(join(LOGDIR, "report.json"),
      JSON.stringify({ fatal: `scorer ${what}: ${String((e && e.stack) || e)}` }, null, 1));
  } catch { /* nothing left to try */ }
  // Flattened for the same reason finish() flattens: test.sh keeps the LAST
  // `^REWARD=...$` on stdout, so any newline in text that reached this scorer
  // from the workspace could carry a forged line.
  try { console.log(`scorer ${what}:`,
    String((e && e.message) ?? "").replace(/[\r\n]+/g, " ")); } catch {}
  process.exit(0);
};
process.on("uncaughtException", lastResort("uncaughtException"));
process.on("unhandledRejection", lastResort("unhandledRejection"));

main().catch((e) => {
  try {
    mkdirSync(LOGDIR, { recursive: true });
    writeFileSync(join(LOGDIR, "reward.json"), JSON.stringify({ reward: 0 }) + "\n");
    writeFileSync(join(LOGDIR, "report.json"), JSON.stringify({ fatal: String((e && e.stack) || e) }, null, 1));
  } catch {}
  console.log("scorer crashed:",
    String((e && e.message) ?? "").replace(/[\r\n]+/g, " "));
  process.exit(0);
});
