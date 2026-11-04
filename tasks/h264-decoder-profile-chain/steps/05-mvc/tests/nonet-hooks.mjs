// ESM resolve hook installed by nonet.mjs. Throws rather than returning a stub,
// so the failure is loud and attributable instead of surfacing later as a wrong
// decode.
//
// AN ALLOWLIST, NOT A DENYLIST. Many different NAMES reach the same
// capability -- wasi, module, repl, test, _http_client/_tls_wrap
// (underscore-prefixed public builtins that the static patterns cannot even
// see), node:test's internal require("child_process"),
// node:inspector/promises. Enumerating names loses to whoever knows one
// more name. Enumerating POWERS does not: anything not needed to decode a
// bitstream is refused, including builtins that do not exist yet.
//
// The permitted set is what the reference decoder and a plausible hand-written
// one actually use -- measured from the shipped solution, not guessed -- plus
// the ordinary data/text surface.
const ALLOWED = new Set([
  "assert", "buffer", "console", "crypto", "events", "fs", "os", "path",
  "process", "punycode", "querystring", "stream", "string_decoder", "timers",
  "tty", "url", "util", "zlib",
]);

// Subpaths are allowed only when their head is: fs/promises yes, test/reporters
// no. An exact-string test would admit the subpath of a refused module.
function permitted(specifier) {
  if (!/^(node:)?[a-z_]/i.test(specifier)) return true;      // relative / absolute / URL
  const bare = specifier.replace(/^node:/i, "");
  if (!bare || bare.startsWith(".") || bare.startsWith("/")) return true;
  const head = bare.split("/")[0];
  return ALLOWED.has(head);
}

// Only two URL schemes make sense for a decoder: node: for the builtins above,
// and file: for its own source. Everything else is refused by SCHEME, which
// closes data: in particular -- `import("data:text/javascript," + src)` is a
// string-to-code path that --disallow-code-generation-from-strings does not
// cover and that the workspace identifier scan never sees, because the source
// is assembled at runtime.
const SCHEME = /^([a-z][a-z0-9+.\-]*):/i;
const SCHEME_OK = new Set(["node", "file"]);

export async function resolve(specifier, context, next) {
  const m = typeof specifier === "string" && SCHEME.exec(specifier);
  if (m && !SCHEME_OK.has(m[1].toLowerCase())) {
    throw new Error(
      `"${m[1]}:" specifiers are not available while scoring: the decoder decodes ` +
      `the bitstream it is given (see TEST_INTERFACE.md section 4)`);
  }
  // Only builtin-shaped specifiers are judged; a file path is not a module id.
  // The scheme is matched case-INSENSITIVELY. `import("NODE:test")` contains a
  // colon, and the default resolver lowercases the scheme when parsing; left
  // unjudged it would be stopped only inside translators.js, whose own `node:`
  // test is case-sensitive -- a gate resting on a Node internal this tree
  // neither pins nor asserts. The bare name is still matched exactly, so
  // NODE:test is refused HERE, by name.
  const looksBuiltin = /^node:/i.test(specifier) ||
    (!specifier.startsWith(".") && !specifier.startsWith("/") &&
     !specifier.includes(":") && !specifier.startsWith("file:"));
  if (looksBuiltin && !permitted(specifier)) {
    throw new Error(
      `module "${specifier}" is not available while scoring: the decoder decodes ` +
      `the bitstream it is given (see TEST_INTERFACE.md section 4)`);
  }
  // JUDGE THE RESULT TOO. A "#"-prefixed specifier is not builtin-shaped, so
  // nothing above judges it -- and package.json "imports" maps it straight
  // onto a builtin. Measured in the container with
  // {"imports":{"#cp":"child_process"}}: unjudged, import("#cp") returns the
  // real child_process. The CJS loader judges its resolution; this is the same
  // rule for ESM, and it closes every mapping that lands on a builtin, not just
  // one spelling.
  const result = await next(specifier, context);
  // A non-string url is REFUSED, not coerced to "" and waived: coerced, an
  // unexpected type would become "", fail startsWith("node:"), and return
  // unjudged. Node's own resolve chain always yields a string url, so nothing
  // legitimate is refused.
  if (!result || typeof result.url !== "string") {
    throw new Error(
      `a specifier of type "${typeof specifier}" resolved to a result this ` +
      `scorer cannot judge, so it is not available while scoring ` +
      `(see TEST_INTERFACE.md section 4)`);
  }
  const url = result.url;
  if (url.startsWith("node:")) {
    const head = url.slice(5).split("/")[0];
    if (!ALLOWED.has(head)) {
      throw new Error(
        `"${specifier}" resolves to the built-in module "${head}", which is not ` +
        `available while scoring: the decoder decodes the bitstream it is given ` +
        `(see TEST_INTERFACE.md section 4)`);
    }
  }
  return result;
}
