// Reports the shape of the build output, for the gates that have to read the
// artifact rather than the sources that produced it.
//
// Three things come out of dist/ that no source scan can tell you:
//
//   what shipped   - the source-map sources[] of every chunk is the module graph
//                    the bundler actually followed.  A component file that still
//                    exists but is no longer reachable from the entry is not
//                    migrated, it is orphaned, and this is what distinguishes
//                    the two.  It is also where a retired package shows up when
//                    it has been dropped from `dependencies` and is still being
//                    imported through somebody else.
//   how much       - chunk count and byte budgets.  Code splitting is a claim
//                    about the artifact, and the only place to check it.  The
//                    count that answers it is `js.reachable`, not every `.js`
//                    under dist/: Vite copies `public/` in verbatim, so this
//                    project's hand-vendored `lib/js/es5-shim.js` and
//                    `lib/js/html5shiv.js` arrive as `.js` files no page loads,
//                    and a single application bundle beside them would pass for
//                    a split build.  Reachability is gates/reach.mjs, the same
//                    walk provenance.mjs uses, so the two instruments cannot
//                    disagree about what the page reaches.
//   self-contained - every local asset index.html references has to exist inside
//                    dist/.  The scorer serves the candidate's dist/ from its own
//                    pristine copy of the mock server, so a build that expects a
//                    file someone copied in by hand afterwards - which is exactly
//                    what this project's README tells you to do with lib/ - is a
//                    build that does not stand on its own.
//
// What this deliberately does not report is whether dist/ was compiled from the
// tree.  Everything under dist/ is written by the candidate's build script, the
// source maps included, so no comparison between two files down here can
// establish that one produced the other: a map naming the tree's own paths and
// carrying its own bytes is a few lines to forge.  That question is answered by
// re-running the build instead - see gates/provenance.mjs.
//
// usage: node artifacts.mjs /app/dist [--json out.json]
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { reachableJs, htmlSansComments } from './reach.mjs';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};
const ROOT = path.resolve(argv.find((a) => !a.startsWith('--')) || '/app/dist');
const JSON_OUT = arg('json', null);

const files = [];
const walk = (dir) => {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const e of entries.sort((a, b) => (a.name < b.name ? -1 : 1))) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) {
      walk(full);
    } else if (e.isFile()) {
      const buf = fs.readFileSync(full);
      files.push({
        path: path.relative(ROOT, full),
        bytes: buf.length,
        gzip: zlib.gzipSync(buf, { level: 9 }).length,
      });
    }
  }
};
if (fs.existsSync(ROOT)) walk(ROOT);

const byExt = (ext) => files.filter((f) => f.path.endsWith(ext));
const sum = (list, key) => list.reduce((n, f) => n + f[key], 0);

// Source maps give the module graph.  Vite emits them only when asked, so their
// absence is reported rather than treated as an empty graph - a gate that reads
// an empty graph as "no retired package shipped" would pass for the wrong
// reason.
const sources = new Set();
let mapCount = 0;
for (const f of byExt('.map')) {
  let map;
  try {
    map = JSON.parse(fs.readFileSync(path.join(ROOT, f.path), 'utf8'));
  } catch {
    continue;
  }
  if (!Array.isArray(map.sources)) continue;
  mapCount++;
  for (const s of map.sources) {
    // Vite writes these relative to the chunk, with a leading ../ per level and
    // sometimes a \0 marker for virtual modules.
    if (typeof s !== 'string' || s.startsWith('\0')) continue;
    sources.add(s.replace(/^(\.\.\/)+/, '').replace(/^\.\//, ''));
  }
}

// The package a vendor source belongs to, taken from the path segment after the
// last node_modules/ - scoped names included.
const vendor = {};
for (const s of sources) {
  const i = s.lastIndexOf('node_modules/');
  if (i === -1) continue;
  const rest = s.slice(i + 'node_modules/'.length).split('/');
  const name = rest[0].startsWith('@') ? `${rest[0]}/${rest[1]}` : rest[0];
  vendor[name] = (vendor[name] || 0) + 1;
}

// Local assets index.html asks for, and beside them the ones it asks another
// host for.  The absolute URLs used to be passed over silently - "the
// application's business, not the build's" - which is true of the byte count
// and not of the page: a `<script src="https://cdn.example/echarts.min.js">`
// is javascript the visitor waits for that no walk of dist/ can see, and a
// tree with no echarts import left in it then reads as a small entry that
// reaches nothing.  So they are recorded rather than dropped, and
// `assets-self-contained` is where that is judged: a site that fetches part of
// itself from somewhere else is not a site that stands on its own.
const missingAssets = [];
const referenced = [];
const external = [];
const indexPath = path.join(ROOT, 'index.html');
if (fs.existsSync(indexPath)) {
  // Read through htmlSansComments: a tag a browser never requests is not a
  // reference, so the three IE<9 shims the project carries in a conditional
  // comment are not a missing asset.
  const html = htmlSansComments(fs.readFileSync(indexPath, 'utf8'));
  // Attribute names anchored, as every attribute test is (reach.mjs): a
  // `data-src` or `data-href` is not a request.  (wave H 2026-10-14, H-CR8,
  // final review G2-R5b)
  const re = /(?<![\w-])(?:src|href)\s*=\s*["']([^"']+)["']/g;
  let m;
  while ((m = re.exec(html))) {
    const url = m[1];
    if (/^([a-z]+:)?\/\//i.test(url)) { external.push(url); continue; }
    if (url.startsWith('data:') || url.startsWith('#')) continue;
    const rel = url.replace(/[?#].*$/, '').replace(/^\.?\//, '');
    if (!rel) continue;
    referenced.push(rel);
    if (!fs.existsSync(path.join(ROOT, rel))) missingAssets.push(rel);
  }
}

// What the page loads before it renders anything: the JavaScript index.html
// asks for - the entry script, and any chunk the build preloads beside it - plus
// everything those reach through a *static* import, which the browser fetches
// and runs before the entry module's first line. Everything behind a dynamic
// import is fetched when something needs it, so it is outside this number: this
// is what a Suspense boundary moves, and what a `lazy` beside a static import of
// the same module does not.
//
// Not "the file index.html names", which was the first shape of this and is a
// byte count a build config can walk away from: `manualChunks` with
// `modulePreload: false` splits the same modules into files index.html never
// mentions and the entry statically imports, so the visitor waits for every one
// of them and the entry script is small. The static closure is the set the
// browser blocks on however the chunks are arranged.
const entryReach = reachableJs(ROOT, null, { staticOnly: true });
const entryPaths = entryReach.files;
const entryFiles = entryPaths.map((p) => files.find((f) => f.path === p)).filter(Boolean);
const entryWhole = entryPaths.length > 0 && entryFiles.length === entryPaths.length;

// The JavaScript the built page actually reaches, from index.html through the
// specifiers the chunks use to reach one another.  `js.count` stays beside it
// and is still reported - it is what the byte budgets are summed over - but the
// chunk-count gate reads this.
const reach = reachableJs(ROOT);

// Whether the React Compiler compiled the build that is being served, read out
// of the build rather than out of the configuration that asked for it.  A
// compiled function allocates a memo cache and initialises each slot by
// comparing it against `Symbol.for("react.memo_cache_sentinel")`, which is the
// runtime contract between the compiler's output and `react/compiler-runtime`:
// the string survives minification because it is the argument to `Symbol.for`,
// and it is emitted whatever route enabled the compiler - a Babel plugin, a
// bundler plugin's own option, or a SWC pass.  Reading the configuration
// instead would score the intention; reading the chunks scores the artifact.
//
// What is read is that comparison and not the symbol's name on its own.  The
// slot guard is a construct the compiler emits once per cache slot, so a chunk
// that merely spells the string out - a literal, a table of names, a hundred
// copies of it concatenated together - is not counted as compiled output.  That
// is a narrowing and not a proof: a chunk that writes the guard itself out by
// hand is indistinguishable here from one the compiler wrote, and no reading of
// an artifact can be otherwise, because the artifact is the one thing a forger
// gets to choose.  What bounds the forgery is the arithmetic rather than the
// regex.  The pair of fields below is read by one scored check and one key to a
// withheld family, and the key beside it asks the source to have stopped
// breaking the rules of React, which a forged chunk does nothing about - so
// writing the guard out by hand buys exactly what declaring the plugin and
// letting it run buys, for a great deal more work.  It is also why the
// untouched tree is a true zero: React's own DOM package names the symbol where
// it defines it, and a definition is an assignment, not a slot being
// initialised.  Counted over the chunks the page reaches, the same set the
// chunk-count gate uses, so a build that emits a compiled chunk nothing loads
// is not credited.
//
// Written as two halves and assembled, because the two things that vary between
// one minifier and the next are which side of the comparison the slot is on and
// which of the four equality operators is used to make it, and neither is a
// difference in what the compiler did.  A build whose chunks came out the other
// way round is still a compiled build, and a gate that read only one spelling
// would score it at zero.
const SENTINEL_CALL =
  String.raw`(?:[A-Za-z_$][\w$]*)(?:\.[A-Za-z_$][\w$]*)*\.for\(\s*(?:"react\.memo_cache_sentinel"|'react\.memo_cache_sentinel')\s*\)`;
const CACHE_SLOT = String.raw`\[\s*\d+\s*\]`;
const EQUALITY = String.raw`\s*[=!]==?\s*`;
const COMPILER_SENTINEL_SLOT = new RegExp(
  `${CACHE_SLOT}${EQUALITY}${SENTINEL_CALL}|${SENTINEL_CALL}${EQUALITY}${CACHE_SLOT}`, 'g');
let compilerMemoizedFiles = 0;
let compilerMemoizedSites = 0;
for (const rel of reach.files) {
  let src;
  try {
    src = fs.readFileSync(path.join(ROOT, rel), 'utf8');
  } catch {
    continue;
  }
  const n = (src.match(COMPILER_SENTINEL_SLOT) || []).length;
  if (n > 0) {
    compilerMemoizedFiles += 1;
    compilerMemoizedSites += n;
  }
}

const result = {
  root: ROOT,
  exists: fs.existsSync(ROOT),
  fileCount: files.length,
  files,
  js: {
    count: byExt('.js').length,
    bytes: sum(byExt('.js'), 'bytes'),
    gzip: sum(byExt('.js'), 'gzip'),
    reachable: { count: reach.files.length, paths: reach.files },
    compilerMemoizedFiles,
    compilerMemoizedSites,
  },
  css: { count: byExt('.css').length, bytes: sum(byExt('.css'), 'bytes'), gzip: sum(byExt('.css'), 'gzip') },
  total: { bytes: sum(files, 'bytes'), gzip: sum(files, 'gzip') },
  entry: {
    count: entryFiles.length,
    paths: entryPaths,
    // Null, not zero, when the page asks for no javascript or asks for a file
    // that is not there: a byte budget read off a tree with no entry to measure
    // would be met by the emptiest possible build.
    bytes: entryWhole ? sum(entryFiles, 'bytes') : null,
    gzip: entryWhole ? sum(entryFiles, 'gzip') : null,
  },
  maps: mapCount,
  sources: [...sources].sort(),
  vendor,
  indexHtml: {
    present: fs.existsSync(indexPath),
    referenced: referenced.sort(),
    missing: missingAssets.sort(),
    external: external.sort(),
  },
};

if (JSON_OUT) fs.writeFileSync(JSON_OUT, JSON.stringify(result, null, 1) + '\n');
else process.stdout.write(JSON.stringify(result, null, 1) + '\n');
process.stderr.write(`artifacts: ${files.length} files, ${result.js.count} js `
  + `(${reach.files.length} reached by the page, ${result.js.bytes} bytes, `
  + `${result.js.gzip} gzip), ${result.entry.count} in the entry `
  + `(${result.entry.gzip} gzip), ${mapCount} maps, `
  + `${result.sources.length} sources, ${Object.keys(vendor).length} vendor packages\n`);
