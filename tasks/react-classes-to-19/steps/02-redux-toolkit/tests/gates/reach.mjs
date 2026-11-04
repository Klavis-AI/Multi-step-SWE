// One definition of "the JavaScript the built page reaches", shared by every
// instrument that has to ask the question.
//
// Two gates ask it, for different reasons, and they must not be allowed to
// answer it differently.  provenance.mjs asks which chunks carry the markers it
// injected, so it needs the page's own reachable set rather than everything
// under dist/ - a marker in a file nothing on the page loads is not evidence
// that the page was built from it.  artifacts.mjs asks how many JavaScript
// chunks the build emits, and has the same problem from the other side: `public/`
// is copied into dist/ verbatim, so this project's hand-vendored
// `lib/js/es5-shim.js` and `lib/js/html5shiv.js` land there as `.js` files that
// no page loads, and counting them would let a single application bundle pass
// for a split build.
//
// The walk starts at the JavaScript index.html names - `<script src>` and
// `<link rel=modulepreload href>` alike - and follows the specifiers chunks use
// to reach one another.  Nothing outside dist/ is followed, and nothing is
// executed: this reads text.  What sits inside an HTML comment is not named at
// all; see htmlSansComments below.
//
// Returned alongside the paths is a sha256 per file, because "did this build's
// output change at all?" is a question one caller needs and neither caller can
// answer from a path list.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

// The markup a browser acts on.  A tag inside a comment is not a reference: no
// request is made for it, and the file it names need not exist.  This matters
// here because the project's own index.html carries three IE<9 shims in a
// downlevel-hidden conditional comment - `<!--[if lt IE 9]>` ... `<![endif]-->`
// - and names them at the tree root, where the files have never been.  They
// dangle in the application as it stands, no browser has ever fetched one, and
// a migration that preserves the markup verbatim is doing what it was asked.
// Every instrument that reads an HTML file reads it through here, so none of
// them can disagree with the browser or with each other.
//
// A downlevel-*revealed* block keeps its body: its markers are `<!--[if !IE]>-->`
// and `<!--<![endif]-->`, each closed before the content begins, so the
// non-greedy match removes the two markers and leaves the tags between them -
// which is exactly what a modern browser does with that block.
export function htmlSansComments(html) {
  return html.replace(/<!--[\s\S]*?-->/g, '');
}

// The script elements of an HTML file, read one way for every gate that reads
// them: ast-scan.mjs scans the inline bodies, eslint-run.mjs lints them, and
// provenance.mjs finds the page's entry and blanks it with the same patterns,
// so no two of them can disagree about which script is which.  Quoted and
// unquoted attribute values both; attribute names anchored as SEED's are, so
// `data-src` and `data-type` are neither `src` nor `type` (final review G2-R5b:
// a module script carrying `data-src` was compiled by the bundler and skipped
// as if it had a source file).  (wave H 2026-10-14, H-CR8)
export const SCRIPT_TAG = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;
export const MODULE_TYPE = /(?<![\w-])type\s*=\s*(?:"\s*module\s*"|'\s*module\s*'|module(?=[\s/>]|$))/i;
export const SRC_ATTR = /(?<![\w-])src\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+))/i;
export const HAS_SRC = /(?<![\w-])src\s*=/i;
const TYPE_ATTR = /(?<![\w-])type\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+))/i;
// The HTML standard's JavaScript MIME types: a script whose `type` is absent,
// empty or one of these runs as a classic script.  Any other type (JSON, an
// import map, a template) is not run as JavaScript and is not read.
const JS_MIME = new Set(['application/ecmascript', 'application/javascript',
  'application/x-ecmascript', 'application/x-javascript', 'text/ecmascript',
  'text/javascript', 'text/javascript1.0', 'text/javascript1.1',
  'text/javascript1.2', 'text/javascript1.3', 'text/javascript1.4',
  'text/javascript1.5', 'text/jscript', 'text/livescript', 'text/x-ecmascript',
  'text/x-javascript']);

// Every script element, comments removed: its `src` (null without one), its
// body, and whether it runs as a module, as a classic script, or not at all.
export function htmlScripts(html) {
  const out = [];
  for (const m of htmlSansComments(html).matchAll(SCRIPT_TAG)) {
    const attrs = m[1];
    const src = SRC_ATTR.exec(attrs);
    const type = TYPE_ATTR.exec(attrs);
    const t = type ? (type[1] ?? type[2] ?? type[3] ?? '').trim().toLowerCase() : '';
    const kind = MODULE_TYPE.test(attrs) ? 'module' : (t === '' || JS_MIME.has(t) ? 'classic' : 'other');
    out.push({ kind, src: src ? (src[1] ?? src[2] ?? src[3] ?? '') : null, body: m[2] });
  }
  return out;
}

// The scripts whose code is the body itself: no `src`, a non-empty body, run
// as a module or as a classic script.  Each is named for where it sits, the
// module ones `inline-module-<n>` and the classic ones `inline-script-<n>`,
// numbered separately, so the names ast-scan reported before classic scripts
// were read are unchanged.  (final review G2-R5 and G2-R7a)
export function inlineScripts(html) {
  const n = { module: 0, classic: 0 };
  const out = [];
  for (const s of htmlScripts(html)) {
    if (s.src !== null || s.kind === 'other' || !s.body.trim()) continue;
    n[s.kind] += 1;
    out.push({ name: `inline-${s.kind === 'module' ? 'module' : 'script'}-${n[s.kind]}`, kind: s.kind, body: s.body });
  }
  return out;
}

// Vite's own non-JavaScript types (5.4.21): its static-asset list, its CSS
// languages, `.json`, `.html` and `.wasm`.  A file of one of these types is
// never compiled as a module, so provenance.mjs leaves it unmarked and
// ast-scan.mjs does not charge an import of it.  provenance.mjs says where
// each part of the list comes from.  (moved here from provenance.mjs, H-CR8)
export const VITE_NOT_JS = /\.(?:apng|bmp|png|jpe?g|jfif|pjpeg|pjp|gif|svg|ico|webp|avif|mp4|webm|ogg|mp3|wav|flac|aac|opus|mov|m4a|vtt|woff2?|eot|ttf|otf|webmanifest|pdf|txt|css|less|sass|scss|styl|stylus|pcss|postcss|sss|json|html|wasm)$/;

// An attribute name is matched only where one can begin - not after a word
// character or a hyphen - so `data-src` and `data-href` are not `src` and
// `href`: a browser fetches neither.  (wave H 2026-10-14, H-CR8, final review
// G2-R5b; every attribute test in these gates is anchored the same way)
const SEED = /(?<![\w-])(?:src|href)\s*=\s*["']([^"']+\.js)["']/g;
const SPECIFIER = /(?:\bfrom\s*|\bimport\s*\(?\s*)["']([^"']+)["']/g;

// The same walk with the dynamic half of that pattern taken out: `from "x"` and
// a bare side-effect `import "x"`, but never `import("x")`.  A caller asking
// `staticOnly` gets the chunks the browser fetches before it renders anything,
// which is not the same set as the chunks it can ever fetch, and is the set a
// byte budget about what a visitor waits for has to be summed over.  The
// negative lookahead is what separates the two spellings: `import{a}from"./x"`
// is reached through the `from` branch, `import("./x")` is reached through
// neither.
const STATIC_SPECIFIER = /(?:\bfrom\s*|\bimport\s*(?!\())["']([^"']+)["']/g;

// `onText` is called with (relativePath, text) for every file the walk reaches,
// in the order it reaches them, so a caller that needs the contents does not
// have to read them a second time.
export function reachableJs(distDir, onText, opts = {}) {
  const dist = path.resolve(distDir);
  const indexPath = path.join(dist, 'index.html');
  let html;
  try {
    html = fs.readFileSync(indexPath, 'utf8');
  } catch {
    return { dist, indexHtml: false, files: [], hashes: [] };
  }

  const resolve = (from, spec) => {
    if (/^([a-z]+:)?\/\//i.test(spec) || spec.startsWith('data:')) return null;
    const clean = spec.replace(/[?#].*$/, '');
    if (!clean) return null;
    const p = clean.startsWith('/')
      ? path.join(dist, clean.slice(1))
      : path.resolve(path.dirname(from), clean);
    if (p !== dist && !p.startsWith(dist + path.sep)) return null;
    try {
      return fs.statSync(p).isFile() ? p : null;
    } catch {
      return null;
    }
  };

  const seen = new Set();
  const queue = [];
  for (const m of htmlSansComments(html).matchAll(SEED)) {
    const p = resolve(indexPath, m[1]);
    if (p) queue.push(p);
  }

  const found = [];
  while (queue.length) {
    const f = queue.shift();
    if (seen.has(f)) continue;
    seen.add(f);
    const rel = path.relative(dist, f);
    let buf;
    try {
      buf = fs.readFileSync(f);
    } catch {
      // Reached but unreadable.  It still counts as a chunk the page asks for,
      // and its digest is made unique per path so two unreadable files never
      // compare equal to each other.
      found.push({ rel, hash: `unreadable:${rel}` });
      continue;
    }
    found.push({ rel, hash: crypto.createHash('sha256').update(buf).digest('hex') });
    const text = buf.toString('utf8');
    if (onText) onText(rel, text);
    for (const m of text.matchAll(opts.staticOnly ? STATIC_SPECIFIER : SPECIFIER)) {
      const p = resolve(f, m[1]);
      if (p && !seen.has(p)) queue.push(p);
    }
  }

  found.sort((a, b) => (a.rel < b.rel ? -1 : a.rel > b.rel ? 1 : 0));
  return {
    dist,
    indexHtml: true,
    files: found.map((f) => f.rel),
    hashes: found.map((f) => f.hash),
  };
}
