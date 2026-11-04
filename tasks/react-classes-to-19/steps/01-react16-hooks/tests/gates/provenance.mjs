// Measures whether the site the build serves is a function of this tree's
// source, by building the tree twice more and watching what moves.
//
// The question this answers - "was dist/ compiled from the source that is being
// scanned?" - cannot be answered by reading dist/.  Everything in dist/ is
// written by the candidate's build script, including the source maps, so any
// static comparison of two artifacts the candidate controls is a comparison of
// two things the candidate wrote.  A map whose `sourcesContent` is the tree's
// own bytes and whose `mappings` is `AAAA` is a few lines to forge, and it sits
// next to a dist/ built once from the untouched application.
//
// So stop reading what the tree says about its build and make the build
// demonstrate the dependency.  Two more runs of the candidate's own build
// command, on scratch copies:
//
//   probe A   every own JS file in the copy gets one inert line appended,
//             `;globalThis["__PROV_<i>__"]="<nonce>";`, where <i> is the file's
//             index in a sorted list.  Build it.  Walk the chunks the built
//             index.html reaches and collect the markers in them.
//   probe B   the same copy and the same injection, plus the module the page's
//             HTML loads truncated to empty.  Build it.  Collect the same way.
//
// The modules whose markers are in A and not in B are the modules whose
// contribution to the served page depends on the entry module - which is what
// "built from this source" means.  Both halves carry weight.  A alone is
// satisfied by a tree that smuggles a prebuilt bundle in as JS files: those get
// injected too and are copied straight through, so they appear in both probes.
// A build script that reinstates a stashed dist/ produces byte-identical output
// with and without an entry module, so its difference is 0.
//
// Measured on the trees this step is calibrated against: the reference solution
// 76 / 0 / 76, a build script that copies a stashed dist/ 42 / 42 / 0, and one
// that runs the real build and then overwrites its output with the stash
// 42 / 42 / 0.  The reference has 77 own JS files under src/; the one whose
// marker does not survive is imported by nothing.
//
// Three rules about probe B are worth stating here, because each was a hole:
//
//   * B's markers are what B's build *emitted*, never what its exit status
//     claims.  A build that writes its output and then exits non-zero - `cp -r
//     .cache/keep dist && node -e "...process.exit(3)"` - was recorded as "B
//     reached nothing", which handed the whole of A to the difference and made
//     a copied dist/ score as a compiled one.  So B is read whenever it left an
//     index.html to walk.
//   * a second test does not involve exit codes at all: if A's and B's
//     reachable-file hash multisets are equal, the difference is 0 whatever the
//     markers say.  A build whose output does not change when the page's entry
//     module is emptied is not compiling the entry module.
//   * and B producing *nothing* is not, by itself, evidence of anything.
//     Swapping the two operators of the cheat above - check the entry, exit
//     before the stash is copied - leaves B with no page at all, and treating
//     that absence as "B is empty" handed the whole of A to the difference
//     again.  B's failure is the candidate's build script's choice, so it
//     cannot be the thing that decides.  In that one case the difference counts
//     only on positive evidence from A: the entry module's own marker has to be
//     in A's page.
//
// This file does the looking.  runner/provenance.sh runs the two builds, since
// a build is candidate code and has to go through run_unprivileged.
//
// usage: node provenance.mjs --mode inject  --root DIR --nonce S --index OUT
//        node provenance.mjs --mode blank   --root DIR
//        node provenance.mjs --mode reach   --dist DIR
//        node provenance.mjs --mode record  --out OUT --index F --entry-json F
//                            --nonce S --a F --b F --a-status N --b-status N
//                            [--a-outdir DIR] [--b-outdir DIR]
//                            [--budget N] [--scored-seconds N]
//                            [--dist-restored yes|no] [--error TEXT]
import fs from 'node:fs';
import path from 'node:path';
import { parse } from '@babel/parser';
import {
  reachableJs, htmlSansComments, SCRIPT_TAG, MODULE_TYPE, SRC_ATTR, HAS_SRC, VITE_NOT_JS,
} from './reach.mjs';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};
const need = (n) => {
  const v = arg(n, null);
  if (v == null) {
    process.stderr.write(`provenance: --${n} is required\n`);
    process.exit(2);
  }
  return v;
};

// The same dialect ast-scan reads, so "parseable" means the same thing to both.
const PLUGINS = [
  'jsx', 'typescript', 'classProperties', 'classPrivateProperties',
  'classPrivateMethods', 'classStaticBlock', 'decorators-legacy',
  'objectRestSpread', 'asyncGenerators', 'dynamicImport', 'exportDefaultFrom',
  'exportNamespaceFrom', 'functionBind', 'importAssertions', 'optionalChaining',
  'nullishCoalescingOperator', 'topLevelAwait', 'importMeta',
];

// Three dialects, tried in order: TypeScript with JSX (the list above), JSX
// alone, then plain JavaScript.  Babel's TypeScript dialect rejects some valid
// JavaScript - `a ? (b) : c => d` reads as an arrow with a return type - and a
// `.js` file it rejected was neither read by ast-scan nor marked here while
// Vite compiled it into the page (final review G2-R1c).  The first dialect that
// parses with no error wins; failing that, the first that did not throw; and if
// every one throws, the first dialect's error is the one raised.  A file the
// first dialect parses cleanly is parsed exactly as before.  ast-scan.mjs
// carries the same function.  (wave H 2026-10-14, H-CR6)
// Two more are appended, plain JavaScript and then TypeScript with JSX, each
// with the current decorators proposal in place of the legacy one and with the
// two import-phase proposals.  Rollup 4.63 (the version every reference lock
// resolves) accepts `export @dec class` and `import defer * as ns`, and the
// three dialects above reject both, so such a file was unmarked when its name
// was not SOURCE's and unread when it was (final review G2-R1d).  Appended
// rather than merged: "first clean dialect wins", so every file the first three
// read is read exactly as before.  (wave H 2026-10-14, H-CR7)
const PROPOSALS = (list) => [
  ...list.filter((p) => p !== 'decorators-legacy'),
  ['decorators', { version: '2023-11' }], 'deferredImportEvaluation', 'sourcePhaseImports',
];
const DIALECTS = [
  PLUGINS,
  PLUGINS.filter((p) => p !== 'typescript'),
  PLUGINS.filter((p) => p !== 'typescript' && p !== 'jsx'),
  PROPOSALS(PLUGINS.filter((p) => p !== 'typescript' && p !== 'jsx')),
  PROPOSALS(PLUGINS),
];
function parseAnyDialect(src, options) {
  let recovered = null;
  let firstError = null;
  for (const plugins of DIALECTS) {
    let ast;
    try {
      ast = parse(src, { ...options, plugins });
    } catch (e) {
      if (!firstError) firstError = e;
      continue;
    }
    if (!ast.errors || ast.errors.length === 0) return ast;
    if (!recovered) recovered = ast;
  }
  if (recovered) return recovered;
  throw firstError;
}

// Installed packages, the build's own output and version-control metadata.
// Everything else in the tree is fair game, deliberately: a bundle smuggled in
// under .cache/ is JavaScript in this repository and gets a marker like any
// other file, which is how probe B tells a copied bundle from a compiled one.
// `node_modules` and `.git` are not this tree's own source at any depth, and
// the JavaScript under either (below the root for node_modules; anywhere for
// .git) gets the foreign marker instead - nestedPackageSources below.
// (`.git`: wave H 2026-10-13, C-1)
const SKIP_DIRS = new Set(['node_modules', 'dist', '.git']);
// One path is skipped as well, and it is the exception that proves the rule
// above.  `server/demo1/public` is a copy of an earlier packaged build - the
// project's README says so, and ast-scan.mjs already treats it as not-source -
// so its 2017 bundles are JavaScript no build in this tree compiles.  Marking
// them costs nothing in strength, since an unmarked file contributes to neither
// A nor B and the difference is unchanged either way; what it costs is noise,
// in `files[]` and in the difference set of any tree whose build copies that
// directory along.
const SKIP_PATHS = new Set([path.join('server', 'demo1', 'public')]);
const SOURCE = /\.(js|jsx|mjs|cjs|ts|tsx|mts|cts)$/;
const DECLARATION = /\.d\.(ts|mts|cts)$/;
const HTML = /\.html$/i;
// Files the build may compile although no SOURCE name says so: Vite bundles an
// imported file it has no other handler for as JavaScript, with no loader
// config - `.es6`, `.md`, a dotfile, a file with no extension (final review
// G2-R1) - and no source gate reads it.  Such a file is marked like code under a
// nested node_modules, so one that reaches the page fails `built-from-source`.
// Every regular file whose name is not SOURCE's is considered, outside the
// exemption set (nestedPackageSources), and three kinds are left alone:
//   * Vite's own non-JavaScript types, as Vite 5.4.21 defines them (every
//     reference package-lock resolves `vite ^5.4.19` to 5.4.21; tests/
//     package.json pins none, because the build is the tree's own): its
//     static-asset list (KNOWN_ASSET_TYPES - images, media, fonts,
//     webmanifest, pdf, txt), its CSS languages (CSS_LANGS_RE), `.json`,
//     `.html` and `.wasm`.  Case-sensitive, as Vite's own tests are.
//   * content that is valid JSON.  A JSON value carries no code, and a build
//     may read such a file as data (a `define` built from `routes.dat`), which
//     an appended line would break (final review G2-R3).
//   * a file that parses as a script in none of the dialects, with no
//     error recovery: a shell script or a README the build runs or reads is
//     not a module, and appending to it could only break an honest build.
// A file over the cap is marked without being read or parsed.
// (wave H 2026-10-14, H-CR6; replaces round 3's extension denylist, which
// left dotfiles, extensionless names, `.md` and its eleven siblings, files
// over the cap and TS-dialect failures unmarked)
// VITE_NOT_JS itself is defined in reach.mjs, so ast-scan.mjs reads the same
// list (H-CR8).
const ODD_SCRIPT_CAP = 8 << 20;
const isOddScript = (name, full) => {
  if (SOURCE.test(name) || VITE_NOT_JS.test(name)) return false;
  let st;
  try {
    st = fs.lstatSync(full);
  } catch {
    return false;
  }
  if (!st.isFile()) return false;
  if (st.size > ODD_SCRIPT_CAP) return true;
  let text;
  try {
    text = fs.readFileSync(full, 'utf8');
  } catch {
    return true;
  }
  try {
    JSON.parse(text.replace(/^\uFEFF/, ''));
    return false;
  } catch { /* not JSON */ }
  try {
    parseAnyDialect(text, { sourceType: 'unambiguous' });
    return true;
  } catch {
    return false;
  }
};

const walkTree = (root, keep) => {
  const out = [];
  const walk = (dir) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (SKIP_DIRS.has(e.name)) continue;
      const full = path.join(dir, e.name);
      if (SKIP_PATHS.has(path.relative(root, full))) continue;
      // Not through a symlink: the installed tree is linked in as
      // node_modules, and a link that points out of the copy is not this
      // tree's source however it is spelled.
      if (e.isSymbolicLink()) continue;
      if (e.isDirectory()) walk(full);
      else if (e.isFile() && keep(e.name)) out.push(full);
    }
  };
  walk(root);
  return out.sort();
};

const ownSources = (root) => walkTree(root, (n) => SOURCE.test(n) && !DECLARATION.test(n));

// JavaScript under a `node_modules` below the tree root -- the one directory
// name every source gate skips at any depth (ast-scan.mjs, eslint-run.mjs) --
// and under `server/demo1/public`, the one path they skip by name. A tree whose
// page is built from either is built from code no source gate reads; the
// application's own source never imports from them. (server/demo1/public:
// wave F 2026-10-13)
// The top-level one is a symlink in each probe copy and is not walked. These
// files get a marker of their own (`__PROVX_<j>__`); one that reaches the
// rebuilt page is code the build compiled from a place no source gate reads,
// and `built-from-source` fails on it (run-gates.mjs). A package install the
// candidate made for the mock server never reaches the page, so it costs an
// honest tree nothing. (wave D 2026-10-12, final review C1)
// And under any directory named `.git`, the repository root's own included.
// The root one is version-control metadata that the source gates skip, and
// /app ships with none, so JavaScript the page loads from under a `.git` is
// code the build compiled from where the scored source is not; a `.git` below
// the root used to be skipped here and by every source gate, and a component
// parked in `src/.git/` reached the page unread and unmarked. git keeps no
// `.js` file of its own there, so this too costs an honest tree nothing.
// (wave H 2026-10-13, C-1)
// And, anywhere outside the exemption set, a file isOddScript says the build
// could compile as a script under a name that is not SOURCE's - nested
// node_modules and nested .git directories included.  The exemption set is the
// root node_modules, dist and .git, and server/demo1/public, and nothing else
// (user decision 2026-10-14): git's own files and the 2017 packaged build are
// not given a line they were never meant to carry.  The root dist is not
// walked at all; JavaScript under the other three still gets the marker above.
const ODD_EXEMPT = new Set(['node_modules', '.git', path.join('server', 'demo1', 'public')]);
// Two exact file names are not given the marker either, at any depth: the
// configuration files browserslist reads (Babel and autoprefixer read them
// through it).  A browserslist line such as `defaults` or `chrome >= 87` parses
// as JavaScript, so the marker went onto an honest `.browserslistrc`, and in a
// build that runs Babel - every build from step 07, which the React Compiler
// runs through Babel - browserslist then failed on the marker line and so did
// the probe build (final review G2-R4; coordinator decision 2026-10-14: this
// configuration is read as configuration, not as a module, and only these two
// names are exempt).  Such a file gets PROBE_COMMENT instead (modeInject): a
// `#` line, which browserslist strips as a comment and which no JavaScript
// parser accepts.  A build that reads the file as browserslist configuration is
// unaffected; a build that compiles it as a module, by whatever path the module
// graph reaches it, fails its probe build, and `built-from-source` is 0 for a
// tree that did not rebuild.
const CONFIG_ONLY = new Set(['.browserslistrc', 'browserslist']);
const PROBE_COMMENT = '\n# provenance probe - a browserslist comment and not JavaScript\n';
function nestedPackageSources(root, configOnly = []) {
  const out = [];
  const walk = (dir, inside, exempt) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (e.isSymbolicLink()) continue;
      const full = path.join(dir, e.name);
      const rel = path.relative(root, full);
      if (!inside && rel === 'dist') continue;
      if (e.isDirectory()) {
        walk(full, inside || e.name === 'node_modules' || e.name === '.git' || SKIP_PATHS.has(rel),
          exempt || ODD_EXEMPT.has(rel));
      } else if (!e.isFile()) continue;
      else if (SOURCE.test(e.name)) {
        if (inside && !DECLARATION.test(e.name)) out.push(full);
      } else if (exempt) continue;
      else if (CONFIG_ONLY.has(e.name)) configOnly.push(full);
      else if (isOddScript(e.name, full)) out.push(full);
    }
  };
  walk(root, false, false);
  configOnly.sort();
  return out.sort();
}

// HTML is read with regexes rather than parsed, so the attribute forms have to
// be spelled out.  Quoted and unquoted values both: `<script type=module
// src=/src/main.js>` is legal HTML and a tree that writes it is not cheating.
// The patterns (SCRIPT_TAG, MODULE_TYPE, SRC_ATTR, HAS_SRC) live in reach.mjs,
// shared with ast-scan.mjs and eslint-run.mjs, with attribute names anchored
// so that `data-src` is not `src` (final review G2-R5b; wave H 2026-10-14,
// H-CR8).

// The module one HTML file loads, or null if it loads none this tree contains.
// A regular file at `p`, reached through no symbolic link below `root`. This
// runs as root and `--mode blank` writes to what it returns, so a planted link
// (or a FIFO, which would block the read) must not stand in for a file of the
// tree. (wave D 2026-10-12, class check: root follows no candidate symlink)
function plainFileIn(root, p) {
  try {
    if (!fs.lstatSync(p).isFile()) return false;
    return path.relative(fs.realpathSync(root), fs.realpathSync(p)) === path.relative(root, p);
  } catch {
    return false;
  }
}

function entryFromHtml(root, htmlPath) {
  let html;
  if (!plainFileIn(root, htmlPath)) return null;
  try {
    html = fs.readFileSync(htmlPath, 'utf8');
  } catch {
    return null;
  }
  const entryHtml = path.relative(root, htmlPath);
  // Commented-out markup is not the entry.  An agent that leaves the old
  // `<script type="module">` behind in a comment while adding the new one would
  // otherwise have probe B blank a module the page does not load; A and B would
  // then agree, the difference would collapse, and an honest tree would fail
  // built-from-source for a dead line in its HTML.
  for (const m of htmlSansComments(html).matchAll(SCRIPT_TAG)) {
    if (!MODULE_TYPE.test(m[1])) continue;
    const src = SRC_ATTR.exec(m[1]);
    if (!src) {
      if (!m[2].trim()) continue;
      return { kind: 'inline', spec: null, file: entryHtml, entryHtml };
    }
    const spec = src[1] ?? src[2] ?? src[3];
    if (!spec || /^([a-z]+:)?\/\//i.test(spec)) continue;
    // Anything that is not `/`-rooted is relative to the HTML file that names
    // it, which is what the browser does and what the bundler does, and it is
    // what makes Vite's documented `root: 'src'` layout resolve to the module
    // the build compiles.
    //
    // A `/`-rooted specifier is relative to the *project* root, and the project
    // root is not always the tree root - the same `root: 'src'` layout resolves
    // `/main.jsx` against src/.  Both are tried, tree root first, and the first
    // that names a file in the tree wins.  Trying two is safe in the direction
    // that matters: choosing the wrong one blanks a module that is not the
    // entry, probe B then still compiles the entry, A and B agree and the
    // difference collapses - the failure is closed, not open.
    const spots = spec.startsWith('/')
      ? [path.join(root, spec.slice(1)), path.resolve(path.dirname(htmlPath), `.${spec}`)]
      : [path.resolve(path.dirname(htmlPath), spec)];
    let file = null;
    for (const p of spots) {
      if (!p.startsWith(root + path.sep)) continue;
      if (!plainFileIn(root, p)) continue;
      file = p;
      break;
    }
    if (!file) continue;
    return { kind: 'src', spec, file: path.relative(root, file), entryHtml };
  }
  return null;
}

// Where a page's JavaScript starts.  Vite's default is a `<script
// type="module">` in a repo-root index.html and that is tried first, but it is
// a default and not a contract: `root: 'src'` is in Vite's own documentation,
// and the instruction states no layout at all.  So when the default resolves to
// nothing, every HTML file in the tree is a candidate, shallowest first, and
// the first one that names a module this tree contains is the entry.
//
// Returned rather than assumed, so the record says which HTML file was read and
// which module the second probe removed.
function findEntry(root) {
  const rootIndex = path.join(root, 'index.html');
  const candidates = [];
  if (fs.existsSync(rootIndex)) candidates.push(rootIndex);
  const depth = (p) => p.split(path.sep).length;
  for (const p of walkTree(root, (n) => HTML.test(n))
    .filter((p) => p !== rootIndex)
    .sort((a, b) => depth(a) - depth(b) || (a < b ? -1 : a > b ? 1 : 0))) {
    candidates.push(p);
  }
  for (const html of candidates) {
    const entry = entryFromHtml(root, html);
    if (entry) return entry;
  }
  // Fail closed.  The tempting fallback here is to accept `|A| >= min` without
  // B - the probes did run, after all, and A's markers are real.  It must not
  // be taken: `cheat-app`, whose build script copies a stashed dist/ and
  // compiles nothing, has A = 42 measured, and would pass it.  A without B is
  // not a measurement of anything.
  return {
    error: candidates.length
      ? "no HTML file in this tree loads a module this tree contains, so the page's entry module is unknown"
      : 'the tree has no HTML file, so the module the page loads is unknown',
  };
}

function modeInject() {
  const root = path.resolve(need('root'));
  const nonce = need('nonce');
  const indexOut = need('index');
  const kept = [];
  const unparseable = [];
  for (const f of ownSources(root)) {
    let src;
    try {
      src = fs.readFileSync(f, 'utf8');
    } catch {
      continue;
    }
    // An unparseable file is left alone.  It cannot be a module the bundler
    // compiled, and appending to it would only change which error it reports.
    // "Unparseable" means in every dialect: valid JavaScript that the
    // TypeScript dialect rejects is marked like any other file. (G2-R1c)
    try {
      parseAnyDialect(src, {
        sourceType: 'unambiguous',
        allowReturnOutsideFunction: true,
        errorRecovery: true,
      });
    } catch {
      unparseable.push(path.relative(root, f));
      continue;
    }
    kept.push(f);
  }
  kept.forEach((f, i) => {
    // After a newline, so a file whose last line is a `//` comment stays valid.
    fs.appendFileSync(f, `\n;globalThis["__PROV_${i}__"]=${JSON.stringify(nonce)};\n`);
  });
  const files = kept.map((f) => path.relative(root, f));
  // The foreign marker is an indirect eval of a string literal, not an
  // assignment through `globalThis`: a module-scope `const globalThis = {}` in
  // the marked file turned the old assignment into a write to a local object,
  // and Rollup dropped it as dead code (final review G2-R1b).  `eval` is the
  // one name no module can bind - module code is strict, and strict code may
  // not declare, import or assign `eval` - so `(0,eval)` is always the global
  // function, and a call to it is a side effect Rollup and the minifier keep.
  // What it evaluates is a string literal, so the line does nothing if the
  // file is ever run.  No nonce: a foreign marker can only cost the tree that
  // carries it, so a stale one proves nothing a fresh one would not.
  // (wave H 2026-10-14, H-CR6)
  const foreign = [];
  const configFiles = [];
  for (const f of nestedPackageSources(root, configFiles)) {
    try {
      fs.appendFileSync(f, `\n;(0,eval)(${JSON.stringify(`'__PROVX_${foreign.length}__'`)});\n`);
      foreign.push(path.relative(root, f));
    } catch { /* unwritable: nothing to look for */ }
  }
  const configOnly = [];
  for (const f of configFiles) {
    try {
      fs.appendFileSync(f, PROBE_COMMENT);
      configOnly.push(path.relative(root, f));
    } catch { /* unwritable: left as it was */ }
  }
  fs.writeFileSync(indexOut, JSON.stringify({ root, nonce, files, unparseable, foreign, configOnly }, null, 1) + '\n');
  process.stderr.write(`provenance: marked ${files.length} file(s)`
    + (unparseable.length ? `, skipped ${unparseable.length} that do not parse` : '')
    + (foreign.length ? `, and ${foreign.length} under a nested node_modules, a .git or server/demo1/public, or with no JavaScript extension` : '')
    + (configOnly.length ? `; ${configOnly.length} browserslist file(s) got a comment line instead` : '') + '\n');
}

function modeBlank() {
  const root = path.resolve(need('root'));
  const entry = findEntry(root);
  if (entry.error) {
    process.stdout.write(JSON.stringify(entry) + '\n');
    process.exit(3);
  }
  if (entry.kind === 'inline') {
    const index = path.join(root, entry.entryHtml);
    const html = fs.readFileSync(index, 'utf8');
    // Only the module script's body goes; the tag and everything around it
    // stay, so the build still has an HTML entry to process.
    const blanked = html.replace(SCRIPT_TAG, (all, attrs, body) =>
      (MODULE_TYPE.test(attrs) && !HAS_SRC.test(attrs) && body.trim()
        ? `<script${attrs}></script>`
        : all));
    fs.writeFileSync(index, blanked);
  } else {
    fs.writeFileSync(path.join(root, entry.file), '');
  }
  process.stdout.write(JSON.stringify(entry) + '\n');
}

// The markers in the chunks the built page actually reaches, and a digest of
// each of those chunks.  The walk itself is gates/reach.mjs, shared with
// artifacts.mjs so that "reachable" means one thing in this verifier: a marker
// in a file nothing on the page loads is not evidence that the page was built
// from it, and neither is a `.js` file under dist/ that no page loads evidence
// that the build split its output.
//
// A foreign marker counts only where a reached chunk carries it as code: an
// identifier, or a string whose whole value is the marker - the literal the
// `(0,eval)` call above evaluates, however a minifier re-quotes it.  The same
// characters inside a longer string are text: an honest `import tpl from
// './card.hbs?raw'` puts the marked file's bytes, marker line and all, into one
// string literal, and is not code from that file (final review G2-R3).  A
// chunk no dialect parses is read as text, as before, so the fallback can only
// count more.  (wave H 2026-10-14, H-CR6)
const FOREIGN_TEXT = /__PROVX_(\d+)__/g;
const FOREIGN_NAME = /^('?)__PROVX_(\d+)__\1$/;
const NOT_CHILDREN = new Set(['loc', 'extra', 'leadingComments', 'trailingComments',
  'innerComments', 'comments', 'tokens', 'errors']);
function foreignInCode(text) {
  const found = new Set();
  if (!text.includes('__PROVX_')) return found;
  let ast;
  try {
    ast = parseAnyDialect(text, { sourceType: 'unambiguous', allowReturnOutsideFunction: true });
  } catch {
    for (const m of text.matchAll(FOREIGN_TEXT)) found.add(Number(m[1]));
    return found;
  }
  const stack = [ast.program];
  while (stack.length) {
    const n = stack.pop();
    let value = null;
    if (n.type === 'Identifier') value = n.name;
    else if (n.type === 'StringLiteral' || n.type === 'DirectiveLiteral') value = n.value;
    else if (n.type === 'TemplateLiteral' && n.expressions.length === 0) value = n.quasis[0].value.cooked;
    if (typeof value === 'string') {
      const m = FOREIGN_NAME.exec(value);
      if (m) found.add(Number(m[2]));
    }
    for (const k of Object.keys(n)) {
      if (NOT_CHILDREN.has(k)) continue;
      const c = n[k];
      if (Array.isArray(c)) {
        for (const x of c) if (x && typeof x === 'object') stack.push(x);
      } else if (c && typeof c === 'object') stack.push(c);
    }
  }
  return found;
}

function modeReach() {
  const dist = path.resolve(need('dist'));
  const markers = new Set();
  const foreign = new Set();
  const r = reachableJs(dist, (rel, text) => {
    for (const m of text.matchAll(/__PROV_(\d+)__/g)) markers.add(Number(m[1]));
    for (const j of foreignInCode(text)) foreign.add(j);
  });
  process.stdout.write(JSON.stringify({
    markers: [...markers].sort((a, b) => a - b),
    foreignMarkers: [...foreign].sort((a, b) => a - b),
    chunks: r.files,
    hashes: r.hashes,
    indexHtml: r.indexHtml,
  }) + '\n');
}

const readJson = (p, fallback = null) => {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch {
    return fallback;
  }
};

function modeRecord() {
  const out = need('out');
  const index = readJson(arg('index', ''), null);
  const entry = readJson(arg('entry-json', ''), null);
  const a = readJson(arg('a', ''), null);
  const b = readJson(arg('b', ''), null);
  // An absent exit status is null, not 0.  The record is also written for the
  // cases where the probes never ran at all, and a default of 0 would report
  // two successful builds in a record whose whole content is that there were
  // none.
  const status = (n) => {
    const raw = arg(n, null);
    if (raw === null || raw === '') return null;
    const v = Number(raw);
    return Number.isFinite(v) ? v : null;
  };
  const aStatus = status('a-status');
  const bStatus = status('b-status');
  const aMarkers = a && Array.isArray(a.markers) ? a.markers : [];
  // B's markers are what B's build emitted, not what its exit status claims.
  // Reading them only when `bStatus === 0` was a hole worth a near-full pass: a
  // build script that copies a stashed dist/ and then exits 3 was recorded as
  // "B reached nothing", so the difference became the whole of A and a tree
  // that compiled nothing scored 0.911290.  The measurement is the output.
  //
  // When B produced no index.html at all, its marker set is empty - but that is
  // not read as "B is empty" any more.  See `entryCompiled` below.
  const bProduced = !!(b && b.indexHtml);
  const bMarkers = bProduced && Array.isArray(b.markers) ? b.markers : [];
  const bSet = new Set(bMarkers);

  // The second test, and the one that needs no markers and no exit status: the
  // multiset of digests of the files each rebuilt page reaches.  If emptying the
  // entry module changed nothing about what the build emits, the build is not
  // compiling the entry module, whatever its markers or its exit code say.
  const digests = (x) => (x && Array.isArray(x.hashes) ? [...x.hashes].sort() : null);
  const ah = digests(a);
  const bh = digests(b);
  const identicalOutput = !!(ah && bh && ah.length === bh.length
    && ah.every((h, i) => h === bh[i]));

  const files = index && Array.isArray(index.files) ? index.files : [];
  // Files under a nested node_modules whose marker reached probe A's page.
  const foreignFiles = index && Array.isArray(index.foreign) ? index.foreign : [];
  const unscannedReached = (a && Array.isArray(a.foreignMarkers) ? a.foreignMarkers : [])
    .map((j) => foreignFiles[j]).filter((f) => typeof f === 'string');

  // The third rule, and the one case where B has nothing to say: B produced no
  // page at all.  Its absence is not evidence either way - a build that checks
  // its entry and exits *before* it copies a stash, `node -e
  // "...process.exit(3)" && cp -r .cache/keep dist`, fails probe B by its own
  // choice and produced 42 markers of copied bundle in A.  Read as "B is empty"
  // that scored 0.911290 for a build that compiled nothing.
  //
  // So in that case the difference counts only on positive evidence from A, the
  // probe that did succeed: the entry module's own marker has to be among the
  // markers A's page reaches.  If it is, the build demonstrably compiled the
  // module index.html loads, and B's failure is a fact about a build that needs
  // that module.  That evidence cannot be manufactured - the nonce is drawn once
  // per run and appended to the entry file in the probe copy, so a stash written
  // at any earlier time cannot carry it, and emitting it means reading the entry
  // at build time, which is the property this whole file is about.
  //
  // `entryIndex` is null, and `entryCompiled` with it, when there is no marker
  // to look for: an inline `<script type="module">` body is not a file and has
  // no index, and an entry the injector skipped because it does not parse is not
  // in `files`.  Neither is positive evidence, so neither may fall back to the
  // old rule - an inline entry would otherwise be the way around this.
  const entryIndex = entry && entry.kind === 'src' && typeof entry.file === 'string'
    && files.indexOf(entry.file) !== -1
    ? files.indexOf(entry.file)
    : null;
  const entryCompiled = entryIndex === null ? null : aMarkers.includes(entryIndex);
  const entryUnproven = !bProduced && entryCompiled !== true;

  const difference = identicalOutput || entryUnproven
    ? []
    : aMarkers.filter((i) => !bSet.has(i)).sort((x, y) => x - y);

  const budget = status('budget');
  const scoredSeconds = status('scored-seconds');

  let error = arg('error', null);
  if (!error && entry && entry.error) error = entry.error;
  if (!error && aStatus === null) {
    error = 'the build was not re-run, so what it compiles could not be measured';
  }
  if (!error && aStatus !== 0) {
    // 124 is timeout(1)'s.  The budget is a multiple of what the scored build
    // itself took, so the sentence can state the number and where it came from
    // rather than claiming, as it used to, that this is "the time the scored
    // build had" - the scored build has no cap at all.
    error = aStatus === 124
      ? (budget === null
        ? 'the tree did not finish rebuilding within the time it was given'
        : scoredSeconds === null
          ? `the tree did not finish rebuilding within ${budget} s, the probe's default budget`
          : `the tree did not finish rebuilding within ${budget} s, four times the `
            + `${scoredSeconds} s the scored build took plus a minute (held between 300 and 900)`)
      : `the tree did not rebuild (the build that produced the scored site exited 0, this one exited ${aStatus})`;
  }
  if (!error && !(a && a.indexHtml)) {
    error = 'the rebuild produced no index.html, so the page it serves could not be walked';
  }

  const record = {
    entry: entry && entry.file ? entry.file : null,
    entrySpec: entry && entry.spec !== undefined ? entry.spec : null,
    entryHtml: entry && entry.entryHtml ? entry.entryHtml : null,
    nonce: arg('nonce', null),
    files,
    markers: { A: aMarkers, B: bMarkers },
    difference,
    identicalOutput,
    // Whether probe B left a page to walk at all, and whether the entry module's
    // own marker survived into probe A's - the two facts the rule above is
    // stated in, recorded so run-gates.mjs can name it rather than report a bare
    // count of 0.
    bProduced,
    entryIndex,
    entryCompiled,
    modules: difference.map((i) => files[i]).filter((p) => typeof p === 'string'),
    unscannedReached,
    chunks: { A: (a && a.chunks) || [], B: (b && b.chunks) || [] },
    builds: {
      A: { status: aStatus, ok: aStatus === 0 },
      B: { status: bStatus, ok: bStatus === 0 },
    },
    // Which directory each probe's page was read from.  A tree that sets
    // `build.outDir` to the absolute /app/dist the instruction names writes
    // there instead of into the scratch copy, and the runner has to say which
    // it found rather than assume.
    outDir: { A: arg('a-outdir', null) || null, B: arg('b-outdir', null) || null },
    budget: { seconds: budget, scoredBuildSeconds: scoredSeconds },
    // The runner snapshots /app/dist before the probes and restores it after,
    // because a probe build that writes an absolute outDir overwrites it.
    // Nothing scored reads /app/dist after the probes - the artifact scan runs
    // before them - so this is a statement about the state the run leaves
    // behind, recorded rather than left silent.
    scoredDistRestored: arg('dist-restored', null),
    error: error || null,
  };
  fs.writeFileSync(out, JSON.stringify(record, null, 1) + '\n');
  try {
    fs.chmodSync(out, 0o644);
  } catch {}
  process.stderr.write(`provenance: entry ${record.entry || '(unresolved)'}, `
    + `A ${aMarkers.length}, B ${bMarkers.length}, difference ${difference.length}`
    + (identicalOutput ? ' (both rebuilds emitted identical files)' : '')
    + (entryUnproven ? ' (B emitted no page, and the entry module\'s own marker is not in A)' : '')
    + (record.error ? ` - ${record.error}` : '') + '\n');
}

switch (arg('mode', '')) {
  case 'inject': modeInject(); break;
  case 'blank': modeBlank(); break;
  case 'reach': modeReach(); break;
  case 'record': modeRecord(); break;
  default:
    process.stderr.write('provenance: --mode must be inject, blank, reach or record\n');
    process.exit(2);
}
