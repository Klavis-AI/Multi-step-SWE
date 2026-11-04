// Verifier-side guard, preloaded with `node --require`.  It makes the graded
// process incapable of delegating the work to something that is not the
// candidate's own JavaScript:
//   * no WebAssembly,
//   * no child processes, worker threads, native addons, or dynamic linking,
//   * no module resolution outside the workspace root,
//   * no network.
// The reference solution runs under exactly the same guard.
//
// This file is the in-process layer.  An in-process fence can be bypassed
// (process.getBuiltinModule, `require('node:vm')`, ESM `import`), so these
// layers do not depend on it:
//   * score.py starts node with --jitless and --freeze-flags-after-init:
//     V8 has no WebAssembly in ANY context, and a runtime flag change
//     (v8.setFlagsFromString('--no-jitless'), which would re-enable it) aborts
//     the process instead;
//   * --permission: no child processes, workers, addons or WASI;
//   * the verifier container has no network (task.toml).
'use strict';

const Module = require('module');
const path = require('path');
const { fileURLToPath } = require('url');

const ROOT = process.env.GUARD_ROOT || '/app';

// Everything the fence below calls is captured now, before any candidate code
// runs: candidate code can replace Set.prototype.has or
// String.prototype.startsWith and turn every check into a no-op otherwise.
const RApply = Reflect.apply;
const SetHas = Set.prototype.has;
const StrStartsWith = String.prototype.startsWith;
const StrEndsWith = String.prototype.endsWith;
const StrSlice = String.prototype.slice;
const StrIndexOf = String.prototype.indexOf;
const has = (set, x) => RApply(SetHas, set, [x]);
const startsWith = (s, p) => RApply(StrStartsWith, s, [p]);
const endsWith = (s, p) => RApply(StrEndsWith, s, [p]);
const isBuiltin = Module.isBuiltin;
const fail = (msg) => { throw new Error(`guard: ${msg}`); };

// Node 22 can die in undici's teardown AFTER the tool has written correct
// output.  stdout to a pipe is async, so anything still buffered past the 64 KiB
// pipe buffer is LOST, and the scorer sees a short string and calls it a
// mismatch.  Measured: a 121511-byte SVG arrived as exactly 65536 bytes.
// Blocking writes complete before the process can die.  The reference solution
// runs under this same guard, so this is not a candidate-specific allowance.
try {
  if (process.stdout._handle && process.stdout._handle.setBlocking) {
    process.stdout._handle.setBlocking(true);
  }
} catch (e) {}

// 1. WebAssembly cannot be reached at all.
try { delete globalThis.WebAssembly; } catch (e) {}
Object.defineProperty(globalThis, 'WebAssembly', {
  configurable: false, enumerable: false, writable: false, value: undefined,
});

// 2. Network globals.
// Each is deleted BEFORE it is redefined.  Node 22 exposes WebSocket as a
// lazily-materialised data property: redefining it in place loads the bundled
// undici, whose module init compiles llhttp with WebAssembly.  Under --jitless
// that rejects, and the unhandled rejection kills the process at the first
// macrotask -- before any fs.readFile / process.stdin / setTimeout callback
// can run -- so an engine that reads its input asynchronously prints nothing.
// `delete` does not materialise the value (Node's own
// --no-experimental-websocket path deletes it the same way), so undici is
// never loaded and the event loop is usable.
for (const name of ['fetch', 'WebSocket', 'XMLHttpRequest', 'EventSource']) {
  try { delete globalThis[name]; } catch (e) {}
  try {
    Object.defineProperty(globalThis, name, {
      configurable: false, enumerable: false, writable: false, value: undefined,
    });
  } catch (e) {}
}

// 3. Forbidden built-in modules, and any specifier that leaves the workspace.
// Matched on the module's base name, so `node:` prefixes and subpaths
// (`dns/promises`, `inspector/promises`) are covered by their parent.
const DENY = new Set([
  'child_process', 'worker_threads', 'cluster',
  'http', 'http2', 'https', 'net', 'dgram', 'dns', 'tls',   // every network module
  'inspector', 'vm', 'v8', 'repl', 'wasi', 'perf_hooks',
]);

function deniedBuiltin(id) {
  let s = startsWith(id, 'node:') ? RApply(StrSlice, id, [5]) : id;
  const slash = RApply(StrIndexOf, s, ['/']);
  if (slash >= 0) s = RApply(StrSlice, s, [0, slash]);
  return has(DENY, s);
}

const isBinary = (s) => endsWith(s, '.node') || endsWith(s, '.wasm');

const realRoot = require('fs').realpathSync(ROOT);
const inRoot = (p) => p === realRoot || startsWith(p, realRoot + path.sep);

function check(request, parent) {
  // A non-string would be coerced again, differently, by the loader after
  // this check has passed it.
  if (typeof request !== 'string') fail('module specifiers must be strings');
  if (deniedBuiltin(request)) fail(`module '${request}' is not available`);
  if (isBinary(request)) fail(`loading '${request}' is not available`);
  if (!startsWith(request, '.') && !startsWith(request, '/')) {
    // A bare specifier: only Node built-ins that are not denied may resolve.
    if (!RApply(isBuiltin, Module, [request])) {
      fail(`third-party module '${request}' is not available`);
    }
    return;
  }
  const base = parent && parent.filename ? path.dirname(parent.filename) : process.cwd();
  const resolved = path.resolve(base, request);
  if (!inRoot(resolved)) fail(`'${request}' resolves outside ${realRoot}`);
}

const origResolve = Module._resolveFilename;
Module._resolveFilename = function (request, parent, ...rest) {
  check(request, parent);
  const file = origResolve.call(this, request, parent, ...rest);
  if (typeof file === 'string' && isBinary(file)) fail(`loading '${file}' is not available`);
  return file;
};

// Module._load serves `node:`-prefixed requests without ever calling
// _resolveFilename, so `require('node:vm')`, createRequire(...)('node:net'),
// module.constructor._load('vm') and process.mainModule.require all go
// straight past the hook above.  Every one of them goes through _load.
const origLoad = Module._load;
Module._load = function (request, parent, ...rest) {
  check(request, parent);
  return RApply(origLoad, this, [request, parent, ...rest]);
};

// process.getBuiltinModule (Node 22.3+) hands out any built-in without the
// module loader at all.
const origGetBuiltin = process.getBuiltinModule;
if (typeof origGetBuiltin === 'function') {
  Object.defineProperty(process, 'getBuiltinModule', {
    configurable: false, enumerable: true, writable: false,
    value: function getBuiltinModule(id) {
      if (typeof id !== 'string') fail('module ids must be strings');
      if (deniedBuiltin(id)) fail(`module '${id}' is not available`);
      return RApply(origGetBuiltin, process, [id]);
    },
  });
}

// ES modules -- static `import`, dynamic `import()`, `require()` of an ES
// module -- are resolved by the ESM loader, which never calls the CommonJS
// hooks above.  An in-thread resolve hook sees every specifier, including
// the imports inside a `data:` URL module.  Without the hook API the fence
// cannot be built, so the guard refuses to run rather than run open.
if (typeof Module.registerHooks !== 'function') {
  fail('this Node has no module.registerHooks; the ES-module fence cannot be installed');
}
Module.registerHooks({
  resolve(specifier, context, nextResolve) {
    if (typeof specifier !== 'string') fail('module specifiers must be strings');
    if (deniedBuiltin(specifier)) fail(`module '${specifier}' is not available`);
    if (isBinary(specifier)) fail(`loading '${specifier}' is not available`);
    const r = nextResolve(specifier, context);
    const url = r && r.url;
    if (typeof url !== 'string') fail(`'${specifier}' did not resolve`);
    if (startsWith(url, 'node:')) {
      if (deniedBuiltin(url)) fail(`module '${specifier}' is not available`);
      return r;
    }
    if (startsWith(url, 'file:')) {
      const file = fileURLToPath(url);
      if (!inRoot(file)) fail(`'${specifier}' resolves outside ${realRoot}`);
      if (isBinary(file)) fail(`loading '${specifier}' is not available`);
      return r;
    }
    fail(`'${specifier}' resolves to a ${RApply(StrSlice, url, [0, 8])}... URL, which is not available`);
  },
});
// ...and nothing registered after it may run first and short-circuit it.
for (const name of ['registerHooks', 'register']) {
  Object.defineProperty(Module, name, {
    configurable: false, enumerable: true, writable: false,
    value: function () { fail(`module.${name} is not available`); },
  });
}

process.dlopen = function () {
  throw new Error('guard: native addons are not available');
};

// The raw binding layers sit underneath every module above: process.binding
// hands out 'contextify' (vm), 'tcp_wrap' (sockets), 'fs' and more.
for (const name of ['binding', '_linkedBinding']) {
  Object.defineProperty(process, name, {
    configurable: false, enumerable: false, writable: false,
    value: function () { fail(`process.${name} is not available`); },
  });
}

// 4. The graded expectations live in THIS directory.  score.py runs the
// candidate with `node --require <this file>`, so __dirname is the verifier's
// own tests directory, and tests/Dockerfile COPYs every graded JSON into it.
// Module resolution is fenced above, but `fs` is not, so without this fence
// candidate code could simply read the answers: a stub with no engine at all,
// whose hb-shape.js echoes shape-hidden.json, would score full marks on the
// shaping groups.
//
// Fonts are handed to the candidate as <tests>/corpus/<dir>/<font> and must
// stay readable, as must this file, which the candidate's own process preloads.
// Set GUARD_ALLOW_TESTS_READ=1 to disable this fence (for testing it only).
if (!process.env.GUARD_ALLOW_TESTS_READ) {
  const fs = require('fs');
  const TESTS_DIR = __dirname;
  const GUARD_SELF = __filename;
  const CORPUS_DIR = path.join(TESTS_DIR, 'corpus') + path.sep;
  // fs accepts a string, a Buffer/Uint8Array, or a file: URL.  A plain
  // path.resolve(String(p)) turns a URL into '<cwd>/file:/tests/...', which
  // never matches, while fs itself opens the real /tests path -- the whole
  // answer key could be read through one `new URL('file:///tests/...')`.
  // Normalise each form exactly as fs does, and fence anything that cannot be
  // normalised.  This fence is the second line: score.py runs the tool as an
  // unprivileged uid that the filesystem itself denies the answers, under
  // Node's permission model.
  // fileURLToPath: required at the top of this file.
  const asPath = (p) => {
    if (typeof p === 'string') return p;
    if (typeof p === 'number') return null;            // an fd: opened via a fenced call
    if (p instanceof URL || (p && typeof p === 'object' && typeof p.href === 'string')) {
      return fileURLToPath(p);                         // throws on a non-file: URL
    }
    if (p instanceof Uint8Array) return Buffer.from(p).toString('utf8');
    return String(p);
  };
  const fenced = (p) => {
    let r;
    try {
      const s = asPath(p);
      if (s === null) return false;
      r = path.resolve(s);
    } catch (e) { return true; }
    if (r === GUARD_SELF || r.startsWith(CORPUS_DIR)) return false;
    return r === TESTS_DIR || r.startsWith(TESTS_DIR + path.sep);
  };
  const wrap = (obj, name) => {
    const orig = obj && obj[name];
    if (typeof orig !== 'function') return;
    obj[name] = function (p, ...rest) {
      if (fenced(p)) throw new Error(`guard: reading '${p}' is not available`);
      return orig.call(this, p, ...rest);
    };
  };
  for (const n of ['readFileSync', 'openSync', 'readdirSync', 'createReadStream',
                   'statSync', 'lstatSync', 'existsSync', 'realpathSync',
                   'readFile', 'open', 'readdir', 'stat', 'lstat', 'copyFileSync',
                   // opendirSync and globSync enumerate the directory beneath
                   // the wrappers above.
                   'opendirSync', 'opendir', 'globSync', 'glob', 'readlinkSync',
                   'cpSync', 'accessSync', 'access']) {
    wrap(fs, n);
  }
  if (fs.promises) for (const n of ['readFile', 'open', 'readdir', 'stat', 'lstat',
                                    'opendir', 'glob', 'access', 'readlink']) wrap(fs.promises, n);

  // process.binding('fs') hands out the raw syscall layer UNDERNEATH every
  // wrapper above and reads file contents straight through this fence.  It is
  // neutered (non-writably, and whatever GUARD_ALLOW_TESTS_READ says) with
  // process._linkedBinding in section 3.
}
