// Preloaded into EVERY decode process by the scorer, via
// NODE_OPTIONS=--import. It removes the decode-time network capability from
// inside the process, because the platform will not remove it from outside:
// Harbor's GKE provider advertises disable_internet only in compose mode
// (environments/gke.py: `disable_internet=self._compose_mode`), and
// base.py::validate_network_policy_support rejects the task outright
// otherwise. This task ships environment/Dockerfile with no
// docker-compose.yaml, so `network_mode = "no-network"` is not available to it.
//
// A static scan of the workspace cannot close this on its own: Node 22 has a
// global fetch, so `globalThis["fe"+"tch"]("http://...")` needs no import and
// no banned identifier. The scan stays as the first layer -- it fails the
// milestone loudly and early -- and this is the capability layer beneath it.
import Module from "node:module";
import { register } from "node:module";

// AN ALLOWLIST, NOT A DENYLIST. Many different NAMES reach the same capability
// -- wasi, module, repl, test, _http_client / _tls_wrap (underscore-prefixed
// PUBLIC builtins the static patterns cannot even see), node:test's internal
// require("child_process"), node:inspector/promises. A list of names loses to
// whoever knows one more.
// Enumerating POWERS does not: anything a decoder does not need is refused,
// including builtins that do not exist yet.
//
// The set is what the shipped solution actually uses (measured):
// fs, path, crypto and tty in the implementation, fs/path/url in the entry
// point -- plus the ordinary data and text surface.
const ALLOWED = new Set([
  "assert", "buffer", "console", "crypto", "events", "fs", "os", "path",
  "process", "punycode", "querystring", "stream", "string_decoder", "timers",
  "tty", "url", "util", "zlib",
]);

// Subpaths follow their head: fs/promises yes, test/reporters no. An
// exact-string test would admit the subpath of a refused module.
const moduleAllowed = (id) => {
  // A non-string is REFUSED, not waived: a fail-open default here reaches both
  // child_process and net. Callers that can describe the type more usefully
  // check before calling; this is the backstop behind them.
  if (typeof id !== "string") return false;
  const bare = id.replace(/^node:/i, "");   // NODE:test must not slip the gate
  // "#" is a package-internal specifier resolved through package.json "imports".
  // It is not builtin-shaped, and the ESM hook lets it through, so it is
  // admitted here too and both module paths follow one rule. What protects
  // both paths is the resolution guard: "#cp" -> "child_process" is refused on
  // the RESULT.
  if (!bare || bare.startsWith(".") || bare.startsWith("/") || bare.startsWith("#")) return true;
  return ALLOWED.has(bare.split("/")[0]);
};

// 1. The globals. Defined non-writable and non-configurable, so a candidate
//    cannot restore them, and reached through any spelling -- computed access
//    lands on the same property.
const deny = (name) => function denied() {
  throw new Error(
    `${name}() is not available while scoring: the decoder decodes the ` +
    `bitstream it is given (see TEST_INTERFACE.md section 4)`);
};
for (const g of ["fetch", "WebSocket", "EventSource"]) {
  try {
    Object.defineProperty(globalThis, g, {
      value: deny(g), writable: false, configurable: false, enumerable: false,
    });
  } catch { /* already non-configurable: nothing to do */ }
}

// 2. ESM specifiers. `register` installs a resolve hook for `import` and
//    `await import()`, including computed specifiers -- which is exactly the
//    form a text scan cannot see.
// A bare catch here would leave `import` completely unguarded and say nothing.
// Module._load and getBuiltinModule do not cover ESM, so this hook is the ONLY
// thing between a computed `await import("node:child_process")` and the
// network. If the hook cannot be installed, scoring stops rather than scoring
// an unguarded decode.
try {
  register("./nonet-hooks.mjs", import.meta.url);
} catch (e) {
  process._rawDebug(`nonet: FAILED to register the ESM resolve hook: ${e && e.message}`);
  throw new Error("nonet: could not install the ESM resolve hook");
}

// Our hook is installed; now close the door behind it. Node runs resolve hooks
// LIFO and `{ shortCircuit: true }` skips BOTH earlier hooks and the default
// resolver, so a hook registered after ours can map a harmless specifier to
// node:https and this preload never sees it. Neutering register/registerHooks
// is the only way to keep our hook last-resort. The oracle is unaffected: its
// only `register` symbols are Emscripten's TTY.register.
try {
  const _M = Module;
  for (const k of ["register", "registerHooks"]) {
    if (typeof _M[k] === "function") {
      Object.defineProperty(_M, k, {
        value: function denied() {
          throw new Error(`module.${k}() is not available while scoring`);
        },
        writable: false, configurable: false,
      });
    }
  }
} catch { /* already locked down */ }

// 3. CommonJS specifiers. `register` hooks do not cover `require`, so patch
//    the CJS loader too. The original is captured here and never handed out,
//    so it cannot be put back.
const origLoad = Module._load;
// defineProperty, not assignment: like every other capability patch in this
// file it is non-writable and non-configurable, since any CJS file reaches the
// same object as `module.constructor`.
const guardedLoad = function (request, parent, isMain) {
  // 2a. The TYPE of the request.
  //
  // Node's loader decides the request's identity with ToString:
  // StringPrototypeStartsWith(specifier, "node:") coerces, and that fast path
  // returns builtin exports WITHOUT consulting Module._resolveFilename at all,
  // so a non-string request would bypass the resolution guard below. Measured
  // in the container without this check:
  //     _load(new String("node:child_process"))           -> execSync present
  //     _load({toString(){return "node:child_process"}})  -> execSync present
  //     _load(new String("node:net"))                      -> reached
  // while a plain-string require of the same name was refused. The literal never
  // appears in source, so the lexical scan stays silent too.
  //
  // REFUSE, do not coerce. String(request) would run the candidate's own
  // toString and the loader would then run it AGAIN -- two reads of an
  // attacker-controlled function. Refusal has no such surface and costs a
  // legitimate decoder nothing: Node's own require() validates its argument as
  // a string, Module.runMain passes process.argv[1], and wrapModuleLoad passes
  // a validated string.
  //
  // The message must NOT interpolate `request` -- that calls its toString and
  // hands the candidate reentrancy inside the throw path. `typeof` is a
  // primitive and safe to report.
  if (typeof request !== "string") {
    throw new Error(
      `a module request of type "${typeof request}" is not available while ` +
      `scoring: name the module with a plain string ` +
      `(see TEST_INTERFACE.md section 4)`);
  }
  if (!moduleAllowed(request)) {
    throw new Error(`module "${request}" is not available while scoring`);
  }
  return origLoad.apply(this, arguments);
};
try {
  Object.defineProperty(Module, "_load", {
    value: guardedLoad, writable: false, configurable: false,
  });
} catch { /* fall through to the check */ }
if (Module._load !== guardedLoad) {
  process._rawDebug("nonet: FAILED to install the CJS module guard");
  throw new Error("nonet: could not secure Module._load");
}

// 3a. The SINGLE decision point: Module._resolveFilename.
//
// Resolving the request in guardedLoad, checking that answer, then calling
// origLoad with the ORIGINAL request would resolve it AGAIN. _pathCache's
// CONTENTS stay candidate-writable by design (require fills them on every
// load), and a property of that object can be an ACCESSOR. Measured in the
// container against that design:
//     alternating getter      ** child_process REACHED **
//     throwing-first getter   ** child_process REACHED **
// The first returns a harmless /app path to the check and "child_process" to
// the loader. The second throws on the first read, which a bare `catch {}`
// turns into a waiver, and the loader's own read decides unchecked.
//
// Checking a value and then letting something else resolve independently is a
// time-of-check/time-of-use gap; more checking on the OUTSIDE cannot close it.
// _load calls _resolveFilename exactly once and uses what it returns, so
// guarding that function is authoritative: it sees the value the loader will
// use, whatever an accessor chooses to return, and a getter that throws
// propagates its error instead of waiving the check.
//
// Installed BEFORE lockSurface, which then finds it already non-writable and
// non-configurable and leaves it alone.
const origResolveFilename = Module._resolveFilename;
const guardedResolveFilename = function _resolveFilename(request) {
  const resolved = origResolveFilename.apply(this, arguments);
  // A non-string RESOLUTION is refused too, not waived: _findPath returns a
  // _pathCache entry verbatim on truthiness, so a planted object can arrive
  // here, and wrapResolveFilename would then .toString() it into a builtin id.
  if (typeof resolved !== "string") {
    throw new Error(
      `a module request resolved to a value of type "${typeof resolved}", ` +
      `which is not available while scoring ` +
      `(see TEST_INTERFACE.md section 4)`);
  }
  // PROSE IS NOT LOADED AS A MODULE. Node's findLongestRegisteredExtension
  // falls back to the .js handler for any extension it does not recognise, so
  // `require` runs a .md whose bytes happen to be valid JavaScript -- measured:
  // a notes.md exporting an object was EXECUTED. Locking Module._extensions
  // stops a handler being ADDED; it does not remove that fallback.
  //
  // This guard sits on the RESOLVER, so it is not complete:
  // `Module.prototype._compile` is still callable and will run a .md through
  // the .js handler without passing through here, as will a file the decode
  // writes to /tmp and require()s. Neither is covered by
  // --disallow-code-generation-from-strings. The guard closes the resolver
  // path, which is the route an ordinary `require('./notes.md')` takes, but it
  // is defence in depth, not a property. What bounds prose-borne code is the
  // runtime layer it still meets: the module allowlist, the non-string type
  // refusal and the WebAssembly refusals.
  //
  // A legitimate decoder loses nothing: it does not require a .md, and data
  // files are read with fs, which is untouched. ESM already refuses these extensions with
  // ERR_UNKNOWN_FILE_EXTENSION.
  if (/\.(md|txt|rst|log)$/i.test(resolved)) {
    throw new Error(
      `"${resolved}" is a prose file and cannot be loaded as a module while ` +
      `scoring: keep code in .js/.mjs/.cjs and read data with fs ` +
      `(see TEST_INTERFACE.md section 4)`);
  }
  // `request` is deliberately absent from the message below: _resolveFilename is
  // reachable directly, so it may itself be a candidate object whose toString
  // would run inside the throw.
  if (!moduleAllowed(resolved)) {
    throw new Error(
      `a module request resolved to "${resolved}", which is not available ` +
      `while scoring: the decoder decodes the bitstream it is given ` +
      `(see TEST_INTERFACE.md section 4)`);
  }
  return resolved;
};
try {
  Object.defineProperty(Module, "_resolveFilename", {
    value: guardedResolveFilename, writable: false, configurable: false,
  });
} catch { /* verified next */ }
if (Module._resolveFilename !== guardedResolveFilename) {
  process._rawDebug("nonet: FAILED to install the resolution guard");
  throw new Error("nonet: could not secure Module._resolveFilename");
}

// 3b. The REST of Module. Locking `_load` by name alone would leave the
//     resolver it calls wide open. In Node 22 `_load` resolves through
//     `Module._resolveFilename(...)` -- a property lookup AT CALL TIME -- which
//     calls `Module._findPath(...)`, both ordinary writable assignments. After
//     resolution `_load` returns builtin exports when the resolved name is a
//     bare builtin id. So, unlocked,
//         module.constructor._resolveFilename = () => "child_" + "process";
//         require("./anything");
//     returns the real child_process: the request "./anything" is relative, so
//     moduleAllowed passes it, and the ALLOWLIST NEVER SEES THE RESOLUTION.
//
//     So the surface is ENUMERATED rather than hardened by a hand-written list
//     of names: do not guess which sibling matters.
//
//     The fix differs in KIND from the `process` one below, though. Those
//     methods are capabilities a decoder never needs, so they become throwers.
//     These must keep WORKING -- resolution is how every legitimate require
//     succeeds. So they are made IMMUTABLE rather than removed: a payload
//     cannot replace them, and legitimate loading is untouched. _cache and _extensions are locked as
//     PROPERTIES (they cannot be swapped wholesale) while their contents stay
//     mutable, because require() legitimately writes into _cache on every load.
const lockSurface = (obj, label) => {
  if (!obj) return;
  for (const k of Object.getOwnPropertyNames(obj)) {
    let d;
    try { d = Object.getOwnPropertyDescriptor(obj, k); } catch { continue; }
    if (!d || d.get || d.set) continue;              // accessors: leave alone
    // Any own DATA property is walked, not a type test plus hand-named
    // properties: a function-only filter would skip `Module.prototype` (an
    // object, non-configurable but WRITABLE). Accessors are left alone above.
    if (!("value" in d)) continue;
    if (d.writable === false && d.configurable === false) continue;   // already locked
    if (!d.configurable) {
      // A property that cannot be REDEFINED can still be REASSIGNED. Flipping
      // writable true -> false is the one transition the spec allows on a
      // non-configurable data property, so take it rather than walk past.
      if (d.writable === true) {
        try { Object.defineProperty(obj, k, { writable: false }); } catch { /* verified next */ }
        const frozen = Object.getOwnPropertyDescriptor(obj, k);
        if (!frozen || frozen.writable !== false) {
          process._rawDebug(`nonet: FAILED to freeze ${label}.${k}`);
          throw new Error(`nonet: could not secure ${label}.${k}`);
        }
      }
      continue;
    }
    try {
      Object.defineProperty(obj, k, { value: d.value, writable: false, configurable: false });
    } catch { /* verified below */ }
    const now = Object.getOwnPropertyDescriptor(obj, k);
    if (!now || now.writable !== false || now.configurable !== false) {
      process._rawDebug(`nonet: FAILED to lock ${label}.${k}`);
      throw new Error(`nonet: could not secure ${label}.${k}`);
    }
  }
};
lockSurface(Module, "Module");
lockSurface(Module.prototype, "Module.prototype");

// 4. Routes that reach a builtin WITHOUT a loader, so neither the resolve hook
//    nor Module._load ever sees them. All three were measured reachable
//    without the patches below.
//
//    process.getBuiltinModule(id) returns the builtin directly, which makes
//    `process.getBuiltinModule("node:" + atob("Y2hpbGRfcHJvY2Vzcw=="))` a
//    complete bypass of a spelling-based module ban. It is wrapped rather than
//    deleted so that legitimate non-forbidden use keeps working.
const origGBM = process.getBuiltinModule;
if (typeof origGBM === "function") {
  Object.defineProperty(process, "getBuiltinModule", {
    value: function getBuiltinModule(id) {
      // Same type rule as guardedLoad, and the same reason the message reports
      // only `typeof`: interpolating a candidate object calls its toString.
      if (typeof id !== "string") {
        throw new Error(
          `a module request of type "${typeof id}" is not available while ` +
          `scoring: name the module with a plain string ` +
          `(see TEST_INTERFACE.md section 4)`);
      }
      if (!moduleAllowed(id)) {
        throw new Error(`module "${id}" is not available while scoring`);
      }
      return origGBM.apply(this, arguments);
    },
    writable: false, configurable: false,
  });
}

//    process.binding() is an unsupported internal door -- `process.binding(
//    "spawn_sync")` reaches subprocess machinery with no module involved. It
//    is deprecated, and the one argument a decoder needs is "constants":
//    process.binding("constants") is how an Emscripten build reads its
//    filesystem constants at static-init time -- this task's own oracle does
//    exactly that, and the STATIC scan exempts that one argument:
//        /\bprocess\.binding\b(?!\s*\(\s*["']constants["']\s*\))/
//    The runtime layer must match the static one, or it fails the very
//    solution it is meant to protect. Everything else -- "spawn_sync" above
//    all -- is refused. (process.dlopen, which loads a native addon -- the
//    same problem in C -- is removed in section 5.)
const BINDING_OK = new Set(["constants"]);
const origBinding = process.binding;
try {
  Object.defineProperty(process, "binding", {
    value: function binding(name) {
      if (!BINDING_OK.has(name)) {
        throw new Error(`process.binding(${JSON.stringify(name)}) is not available while scoring`);
      }
      return origBinding.apply(this, arguments);
    },
    writable: false, configurable: false,
  });
} catch { /* verified below */ }
// Verified, like every other patch here: a guard which cannot report its own
// failure is a guard you only believe you have.
{
  const bd = Object.getOwnPropertyDescriptor(process, "binding");
  if (!bd || bd.writable !== false || bd.configurable !== false ||
      typeof bd.value !== "function" || bd.value === origBinding) {
    process._rawDebug("nonet: FAILED to lock process.binding");
    throw new Error("nonet: could not secure process.binding");
  }
}
// 5. Capabilities hanging off `process` itself. process.execve, for one,
//    replaces this process image with a fresh node that carries no
//    NODE_OPTIONS, so nothing in this file is loaded into the replacement.
//    Measured in the verifier container without this section:
//
//      ESCAPE REACHED: WebAssembly.instantiate=function  fetch=function
//                      NODE_OPTIONS=null  pid-preserved=true
//
//    execve preserves pid, uid, cwd and fds 0/1/2, so the scorer's timeout, its
//    process-group kill and its grading all see an ordinary decode. A module
//    allowlist cannot reach this: `process` is not a module, and it is a global
//    besides, so no resolution ever fires.
//
//    Rather than guess at execve's siblings, `process` was ENUMERATED: 60 own
//    function properties, 16 capability-bearing, 12 of them open. Those twelve
//    go. Each either reaches OUTSIDE this process
//    (kill, _debugProcess) or restores a capability the scorer removed (execve,
//    dlopen), and a decoder needs none of them.
//
//    Four capability-bearing names are KEPT, because measurement said so:
//      * reallyExit -- process.exit() is implemented ON TOP of it. Hardening it
//        makes every legitimate `process.exit(0)` throw "reallyExit hardened"
//        (measured in the container) -- a fix that breaks the solution it
//        exists to protect.
//      * _fatalException -- the runtime's own uncaught-exception path.
//      * _rawDebug -- how this file reports its own install failures, below.
//      * abort -- terminates this process and confers nothing on the caller.
//
//    The `process` global, `node:process`, getBuiltinModule("process") and
//    require("node:process") are all the SAME object (measured), so locking own
//    properties here closes every route to them at once.
//
//    A failed defineProperty is not swallowed: a guard which cannot report its
//    own failure is a guard you only believe you have.
const CAP_REMOVED = ["execve", "_debugProcess", "kill", "_kill",
                     "setuid", "setgid", "seteuid", "setegid",
                     "initgroups", "setgroups", "dlopen", "_linkedBinding"];
for (const k of CAP_REMOVED) {
  if (typeof process[k] !== "function") continue;
  const refuse = function denied() {
    throw new Error(`process.${k}() is not available while scoring: it reaches outside this process, or restores a capability the scorer removed.`);
  };
  try {
    Object.defineProperty(process, k, { value: refuse, writable: false, configurable: false });
  } catch { /* verified below -- never assumed */ }
  if (process[k] !== refuse) {
    process._rawDebug(`nonet: FAILED to remove process.${k}`);
    throw new Error(`nonet: could not secure process.${k}`);
  }
}

// 5b. Capabilities hanging off PERMITTED MODULES.
//
// All 18 permitted modules -- 661 exported names -- were enumerated for
// members that load native code or reach another process. That found four:
//
//   crypto.setEngine  loads a native OpenSSL engine: the process.dlopen class,
//                     reached through a module the allowlist permits. Measured
//                     unpatched: the call reached ERR_CRYPTO_ENGINE_UNKNOWN,
//                     i.e. it got as far as trying to load one.
//   crypto.setFips    flips process-global crypto state. No decoder needs it.
//   os.setPriority    takes a PID and changes another process's scheduling: the
//                     process.kill class. Measured present and accepted.
//   os.getPriority    takes a PID and reads it. An information leak about other
//                     processes rather than a capability, removed with its twin.
//
// fs's chmod/symlink/unlink/watch family is LEFT: a decoder writes its own
// output, and those act only where the decode can already reach. Removing them
// would break the solution this layer exists to protect.
const MODULE_CAPS = [["crypto", ["setEngine", "setFips"]],
                     ["os", ["setPriority", "getPriority"]]];
for (const [modName, names] of MODULE_CAPS) {
  let mod;
  try { mod = process.getBuiltinModule(modName); } catch { continue; }
  if (!mod) continue;
  for (const k of names) {
    if (typeof mod[k] !== "function") continue;
    const refuse = function denied() {
      throw new Error(
        `${modName}.${k}() is not available while scoring: it loads native code ` +
        `or reaches another process (see TEST_INTERFACE.md section 4)`);
    };
    try {
      Object.defineProperty(mod, k, { value: refuse, writable: false, configurable: false });
    } catch { /* verified next */ }
    if (mod[k] !== refuse) {
      process._rawDebug(`nonet: FAILED to remove ${modName}.${k}`);
      throw new Error(`nonet: could not secure ${modName}.${k}`);
    }
  }
}

// ---------------------------------------------------------------------------
// WebAssembly: refused outright, after letting our own pending work settle.
//
// Admitting selected callers does not work: the one legitimate caller, undici,
// has no claim on this process except that this file started it and did not
// wait for it. Measured, in the verifier container:
//
//   * with ONLY a WebAssembly wrapper and none of this file, a decode calls
//     WebAssembly ZERO times and exits 0. The decoder has no use for it.
//   * bisecting this file, section 1 alone -- replacing globalThis.fetch /
//     WebSocket / EventSource -- is what loads undici.
//   * undici COMPILES llhttp synchronously as it loads, and INSTANTIATES in the
//     continuation of that compile promise. That continuation is a microtask,
//     so it lands after this module's synchronous body has finished, i.e.
//     during the decode -- so refusing immediately would make every case exit
//     1 at lazyllhttp.
//
// So: drain the pending turns here, while this preload is still the only code
// that exists. The instantiate completes, and from then on NOTHING in the
// process needs WebAssembly, so it can be refused with no exceptions.
//
// What that buys:
//   - no stack is consulted, so Error.prepareStackTrace cannot be forged, no
//     internal trampoline (process.nextTick, async_hooks runInAsyncScope) can
//     donate a frame, and no undici-implemented global (Headers, Request,
//     Response, FormData) can lend one through a WebIDL conversion;
//   - no verdict is computed, so there is no String.prototype.startsWith to
//     patch and no CallSite object to leak;
//   - the constructors are replaced outright rather than wrapped, so there is
//     no Guarded.prototype = Ctor.prototype handing the original back through
//     WebAssembly.Module.prototype.constructor;
//   - Memory, Table and Global are refused too, so the import object for
//     `new Instance(mod, imports)` cannot be built.
// Drain, then OBSERVE: under 7-way concurrency a fixed sleep is an unchecked
// race. Yield, then watch a
// settling window for further WebAssembly work; if any arrives after the
// refusal is installed it is the verifier's doing and must be attributable
// rather than silently costing a case.
{
  const _oi = globalThis.WebAssembly && globalThis.WebAssembly.instantiate;
  let _late = 0;
  if (_oi) globalThis.WebAssembly.instantiate = function () { _late++; return _oi.apply(this, arguments); };
  for (let _i = 0; _i < 20; _i++) await new Promise((r) => setTimeout(r, 0));
  const _b = _late;
  for (let _i = 0; _i < 10; _i++) await new Promise((r) => setTimeout(r, 0));
  if (_late !== _b) {
    process._rawDebug(`nonet: ${_late - _b} WebAssembly call(s) arrived after the drain; ` +
      "a refusal below may be refusing a Node internal. This line is the attribution.");
  }
  if (_oi) globalThis.WebAssembly.instantiate = _oi;
}

// ---------------------------------------------------------------------------
// The socket constructor, which no module ban can take away.
//
// There is a complete outbound escape INSIDE the allowlist, and it does not
// import anything: the scorer spawns the decode with pipe stdio, so
// process.stdout is a net.Socket INSTANCE and process.stdout.constructor is the
// Socket constructor itself. node:tty reaches the same object through
// Object.getPrototypeOf(tty.ReadStream) -- measured identical to net.Socket by
// both routes. Socket.prototype.connect mints its handle from
// internalBinding("tcp_wrap"), which nothing here touches, and skips DNS for a
// literal IP. No forbidden module loads and no banned identifier appears, so
// every module gate and the whole static scan stay silent while the decode
// proxies the bitstream to a decoder elsewhere.
//
// A module allowlist cannot fix this: `process` is not a module, and tty is one
// a decoder legitimately uses. So the CAPABILITY goes rather than the name.
// MEASURED: a legitimate decode calls connect() zero times -- pipes arrive already
// connected, and writing to stdout/stderr triggers none -- so removing it costs
// a decoder nothing.
{
  // Taking the constructor off process.stdout is correct ONLY under pipe
// stdio. Under a TTY, process.stdout.constructor is tty.WriteStream, which
// EXTENDS net.Socket. `typeof ctor.prototype.connect === "function"` is
// therefore true by inheritance, and defining an OWN `connect` on
// tty.WriteStream.prototype would shadow it only there, leaving
// net.Socket.prototype.connect intact and reachable -- a guard that passes its
// own check while guarding nothing (measured with `docker run -t`).
//
// So: find the prototype that OWNS connect by walking the chain, and prefer the
// tty route, which reaches net.Socket regardless of what stdio the scorer
// chose. Refuse to run at all if neither route finds an owner.
const ownerOfConnect = (ctor) => {
  let proto = ctor && ctor.prototype;
  while (proto && !Object.prototype.hasOwnProperty.call(proto, "connect")) {
    proto = Object.getPrototypeOf(proto);
  }
  return proto || null;
};
let sockProto = null;
try {
  const tty = process.getBuiltinModule("tty");
  sockProto = ownerOfConnect(tty && Object.getPrototypeOf(tty.ReadStream));
} catch { /* fall through */ }
if (!sockProto) sockProto = ownerOfConnect(process.stdout && process.stdout.constructor);
if (!sockProto) {
  process._rawDebug("nonet: could not reach the prototype that owns connect");
  throw new Error("nonet: could not secure Socket.prototype.connect");
}
const refuseConnect = function connect() {
  throw new Error(
    "opening a socket is not available while scoring: the decoder decodes " +
    "the bitstream it is given (see TEST_INTERFACE.md section 4)");
};
try {
  Object.defineProperty(sockProto, "connect", {
    value: refuseConnect, writable: false, configurable: false,
  });
} catch { /* fall through to the check */ }
if (sockProto.connect !== refuseConnect) {
  process._rawDebug("nonet: FAILED to remove Socket.prototype.connect");
  throw new Error("nonet: could not secure Socket.prototype.connect");
}
}   // closes the scoping block opened above this section

const _W = globalThis.WebAssembly;
if (_W) {
  const REFUSED = (what) =>
    `WebAssembly.${what} is not available to this program: the decoder must be ` +
    `implemented in JavaScript, not compiled to WebAssembly`;
  // `validate` is refused too. With every entry point refusing, a wasm payload
  // read with fs from any file -- a .txt included, which prose
  // non-executability does not cover -- is inert at runtime, and the source
  // scan is pure defence in depth that is free to skip prose.
  for (const k of ["instantiate", "compile", "instantiateStreaming", "compileStreaming",
                   "validate",
                   "Module", "Instance", "Memory", "Table", "Global"]) {
    if (!(k in _W)) continue;
    const refuse = function () { throw new Error(REFUSED(k)); };
    try { Object.defineProperty(refuse, "name", { value: k }); } catch { /* cosmetic */ }
    try {
      Object.defineProperty(_W, k, { value: refuse, writable: false, configurable: false });
    } catch { /* fall through to the check below */ }
    // A bare catch here would leave the gate OPEN on a Node where the define
    // failed, with no signal anywhere: a check that passes when its input is
    // bad is not a check.
    if (_W[k] !== refuse) {
      process._rawDebug(`nonet: FAILED to install the refusal for WebAssembly.${k}`);
      throw new Error(`nonet: could not secure WebAssembly.${k}`);
    }
  }
  try {
    Object.defineProperty(globalThis, "WebAssembly", {
      value: _W, writable: false, configurable: false,
    });
  } catch { /* already locked down */ }
}
