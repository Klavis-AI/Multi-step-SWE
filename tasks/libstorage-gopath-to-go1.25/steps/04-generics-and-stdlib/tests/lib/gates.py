"""The migrated-ness gates.

Every gate answers one question about the tree at `/app` and returns a score in
[0, 1] together with the evidence for it. Count-based gates are graded against
the baseline count, so a partial migration earns partial credit and only a
complete one earns the point:

    score = 1 - remaining / baseline          (clamped to [0, 1])

Boolean gates return 0 or 1.

Scope. Two scopes, both derived, neither written down as a path list.

* Syntactic gates (`gofmt`, everything reading the AST census) scan **every
  directory in the repository that holds Go source**, walked from `/app`. That
  includes files the current platform cannot build -- a package behind a build
  constraint is still source in the repository -- and vendored source, if the
  candidate chose to keep any.
* Type-aware gates (`go vet`, staticcheck, the untyped-container analyzer, the
  import-graph gates) scan what the toolchain can load: `go list [-deps] ./...`.
  A type checker has nothing to say about a file it cannot compile.

If the tree still carries vendored third-party source, it is in the repository
and is scanned; if the dependencies come from the module graph instead, there is
nothing of theirs in the repository to scan. That is one rule with no carve-outs,
and which of the two applies is the candidate's own vendoring decision.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

import toolenv


class Result(object):
    """One gate's outcome."""

    def __init__(self, gid, score, detail):
        self.id = gid
        self.score = max(0.0, min(1.0, float(score)))
        self.detail = detail

    def as_dict(self):
        return {"id": self.id, "score": round(self.score, 6),
                "detail": self.detail}


def graded(gid, remaining, baseline, detail=None, floor=0):
    """Score a count-based gate against its baseline count.

    `floor` is how many of the baseline's findings are outside this step's
    scope, because a later step retires the construct they belong to. The gate
    is satisfied at the floor and grades the distance from the baseline down to
    it; a candidate that goes further than asked clamps at 1.0 rather than
    being penalised for it. Every floor is justified in the step's gate
    specification, and a floor of zero -- the default -- means the step owns the
    whole population.
    """
    floor = max(0, int(floor))
    baseline = int(baseline)
    span = max(1, baseline - floor)
    score = 1.0 - (float(remaining) - floor) / span
    info = {"remaining": remaining, "baseline": baseline}
    if floor:
        info["floor"] = floor
    if detail:
        info.update(detail)
    return Result(gid, score, info)


def boolean(gid, ok, detail=None):
    return Result(gid, 1.0 if ok else 0.0, detail or {})


# --------------------------------------------------------------------------
# Toolchain queries. Each one is a standard command of the target toolchain.
# --------------------------------------------------------------------------

def go_list_packages(env):
    """`go list -json ./...` -- the module's own packages."""
    out = toolenv.run(["go", "list", "-e", "-json", "./..."], env=env)
    return _decode_stream(out.stdout)


def go_list_deps(env):
    """`go list -deps -json ./...` -- the compiled import graph."""
    out = toolenv.run(["go", "list", "-e", "-deps", "-json", "./..."], env=env)
    return _decode_stream(out.stdout)


def _decode_stream(text):
    """Read the concatenated JSON objects `go list -json` emits."""
    decoder = json.JSONDecoder()
    items, idx = [], 0
    text = text.lstrip()
    while idx < len(text):
        try:
            obj, end = decoder.raw_decode(text, idx)
        except ValueError:
            break
        items.append(obj)
        idx = end
        while idx < len(text) and text[idx] in " \t\r\n":
            idx += 1
    return items


def repo_packages(deps, root):
    """The subset of a `go list` result whose source lives in the repository."""
    root = os.path.realpath(root)
    inside = []
    for pkg in deps:
        directory = pkg.get("Dir")
        if not directory:
            continue
        real = os.path.realpath(directory)
        if real == root or real.startswith(root + os.sep):
            inside.append(pkg)
    return inside


def own_packages(pkgs):
    """Packages of the main module, which is what `go list ./...` returns."""
    return [p for p in pkgs if not p.get("Standard")]


def loadable_packages(pkgs):
    """The subset `go list` could actually resolve to a directory of source.

    `go list -e` answers even when there is nothing to answer about: run in a
    tree with no `go.mod`, it emits one synthetic entry for the pattern itself,
    carrying an error and no `Dir`. Counting that as a package is how a tree the
    toolchain cannot load scores full marks on every gate that counts findings --
    the tool reports nothing because it never ran, and "nothing" reads as clean.
    A package that loads but does not compile is kept: that is a real package
    with a real problem, and the gates have things to say about it.
    """
    return [p for p in pkgs if p.get("Dir")]


# --------------------------------------------------------------------------
# The gates.
# --------------------------------------------------------------------------

def gate_module(ctx, spec):
    """A module rooted at the repository, with at least the named directive.

    The directive is read from the built binary rather than from `go.mod`, so a
    `go.mod` that says one thing while the build says another cannot pass.
    """
    detail = {}
    gomod = os.path.join(ctx.root, "go.mod")
    if not os.path.exists(gomod):
        return boolean("module", False, {"error": "no go.mod at the repo root"})
    with open(gomod) as fh:
        text = fh.read()
    path = re.search(r"(?m)^module\s+(\S+)", text)
    directive = re.search(r"(?m)^go\s+(\d+)\.(\d+)(?:\.(\d+))?", text)
    detail["module"] = path.group(1) if path else None
    detail["go"] = directive.group(0).split()[1] if directive else None
    if not path or not directive:
        return boolean("module", False, detail)
    if path.group(1) != spec["module"]:
        return boolean("module", False, detail)

    # The directive is exact, not a minimum. Each step of the ladder names the
    # language version its constructs need, and a module that declares a later
    # one is not further ahead -- it has taken the whole ladder in one line
    # while the constructs it enables are still absent from the tree.
    want = tuple(int(x) for x in spec["go"].split("."))
    got = (int(directive.group(1)), int(directive.group(2)))
    if got != want:
        detail["want_go"] = spec["go"]
        return boolean("module", False, detail)

    # `go version -m` prints the module's build information, including the
    # toolchain the binary was compiled with and the main module's path.
    binary = ctx.binary
    if not binary or not os.path.exists(binary):
        detail["error"] = "no binary to inspect"
        return boolean("module", False, detail)
    out = toolenv.run(["go", "version", "-m", binary], env=ctx.env)
    detail["version_m"] = _mod_lines(out.stdout)
    if ("path\t" + spec["module"]) not in out.stdout and \
            ("mod\t" + spec["module"]) not in out.stdout:
        detail["error"] = "built binary does not report the module path"
        return boolean("module", False, detail)
    if re.search(r"build\s+-?[Gg]o(version|)=?", out.stdout) is None and \
            "go1." not in out.stdout:
        detail["error"] = "built binary reports no toolchain"
        return boolean("module", False, detail)
    return boolean("module", True, detail)


def _mod_lines(text):
    return [line.strip() for line in text.splitlines()
            if line.startswith("\t")][:12]


# A directory the current platform has no package in. `./...` is right to skip
# these -- the darwin driver, the windows main -- and the probe below has to
# tell them apart from a package that genuinely does not compile.
_NO_PACKAGE_RE = re.compile(
    r"build constraints exclude all Go files"
    r"|no Go files in"
    r"|no non-test Go files in")


def gate_build_all(ctx, spec):
    """`go build ./...` compiles every package in the repository.

    Distinct from the single-binary build the behavior family needs: a package
    no `main` reaches can stop compiling without the server binary noticing it,
    and the repository is what the step migrates, not one command inside it.
    The module cache is warm from that earlier build, so what this costs is a
    compile of the tree and nothing more.

    The wildcard alone does not mean "every package", which is why the build is
    followed by a cover check. `go.mod`'s `ignore` directive removes a
    directory from every `./...` pattern, and the Go 1.25 toolchain honours it
    in a module that declares `go 1.17` -- measured in this image, not assumed
    -- so a package that will not compile can be made to vanish from the thing
    that was supposed to notice. Naming a directory explicitly bypasses
    `ignore`, so every first-party directory the filesystem walk found and
    `go list ./...` does not report is built by name. A platform directory
    answers "build constraints exclude all Go files" and is exactly what the
    wildcard was right to skip; anything else is a package that does not build.
    """
    out = toolenv.run(["go", "build", "./..."], env=ctx.env, timeout=900)
    if out.code != 0:
        return boolean("build-all", False,
                       {"code": out.code, "stderr": out.stderr[-600:]})
    listed = toolenv.run(["go", "list", "-e", "-f", "{{.Dir}}", "./..."],
                         env=ctx.env, timeout=300)
    covered = set()
    for line in listed.stdout.splitlines():
        line = line.strip()
        if line:
            covered.add(os.path.realpath(line))
    failures = []
    unlisted = []
    for path in sorted(ctx.own_dirs):
        if os.path.realpath(path) in covered:
            continue
        rel = os.path.relpath(path, ctx.root)
        unlisted.append(rel)
        # -o /dev/null so probing a main package cannot leave a binary behind
        # in the tree the rest of the gates are still reading.
        probe = toolenv.run(["go", "build", "-o", "/dev/null", "./" + rel],
                            env=ctx.env, timeout=300)
        if probe.code == 0 or _NO_PACKAGE_RE.search(probe.stderr):
            continue
        failures.append({"dir": rel, "stderr": probe.stderr[-300:]})
    return boolean("build-all", not failures,
                   {"code": 0, "unlisted": unlisted[:20],
                    "unlisted_count": len(unlisted),
                    "failures": failures[:10]})


# `replace` and `exclude` decide dependency versions by hand; `ignore` decides
# which of the repository's own directories exist as far as every `./...`
# pattern is concerned.
_GOMOD_DIRECTIVE_RE = re.compile(r"(?m)^(replace|exclude|ignore)\b")


def gate_gomod_directives(ctx, spec):
    """`go.mod` neither overrides version selection nor hides a directory.

    Three directives can make the module something other than what the
    repository says it is, and the step states the requirement behind each:
    `replace` and `exclude` pick versions by hand, against "at the versions
    minimal version selection picks", and `ignore` drops a directory out of
    `go build ./...`, `go vet ./...` and `go test ./...` at once, against "the
    repository builds". The build gate above closes `ignore` for itself by
    naming directories explicitly; this one says so as a requirement rather
    than leaving it as a property of one gate's implementation.

    Read as text, because that is the shape the directives have: `go list -m`
    reports the result of applying them, not whether they were written.
    """
    gomod = os.path.join(ctx.root, "go.mod")
    if not os.path.exists(gomod):
        return boolean("gomod-directives", False,
                       {"error": "no go.mod at the repo root"})
    with open(gomod) as fh:
        text = fh.read()
    forbidden = spec.get("directives") or ["replace", "exclude", "ignore"]
    present = sorted({m.group(1) for m in _GOMOD_DIRECTIVE_RE.finditer(text)}
                     & set(forbidden))
    return boolean("gomod-directives", not present,
                   {"present": present, "forbidden": sorted(forbidden)})


# Files smaller than this, once whitespace is removed, are not evidence of
# anything: a two-line `package x` shim matches another two-line `package x`
# shim in some dependency and means nothing by it.
_COPY_MIN_BYTES = 500

# The module cache is the same in every run of an image, so its digest table is
# computed once per process rather than once per gate.
_MODCACHE_DIGESTS = {}


def _copy_digest(path):
    """A file's content with every whitespace byte removed, hashed.

    Whitespace is dropped so that a copy which has been through `gofmt`, had
    its indentation changed or gained and lost blank lines still matches the
    original. Comments are kept: stripping them would start matching files
    that differ only in what they say about themselves, and the question here
    is whether this is the same source, not whether it is similar source.
    """
    try:
        with open(path, "rb") as handle:
            raw = handle.read()
    except (IOError, OSError):
        return None, 0
    stripped = b"".join(raw.split())
    if len(stripped) < _COPY_MIN_BYTES:
        return None, len(stripped)
    return hashlib.sha256(stripped).hexdigest(), len(stripped)


def _modcache_digests(env):
    """Digest -> module-relative path, over every `.go` file in the cache.

    The verifier image bakes the module cache, so this is the source of every
    dependency this repository is allowed to reach, extracted and on disk. The
    download cache underneath it holds zips and metadata rather than source and
    is skipped.
    """
    root = env.get("GOMODCACHE") or ""
    if not root or not os.path.isdir(root):
        return {}
    if root in _MODCACHE_DIGESTS:
        return _MODCACHE_DIGESTS[root]
    table = {}
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d != "cache"]
        for name in files:
            if not name.endswith(".go"):
                continue
            full = os.path.join(base, name)
            digest, _ = _copy_digest(full)
            if digest and digest not in table:
                table[digest] = os.path.relpath(full, root)
    _MODCACHE_DIGESTS[root] = table
    return table


def gate_no_vendored_source(ctx, spec):
    """No third-party source is carried in the repository.

    Two readings of the same requirement, because a dependency can come back
    into a repository two ways.

    The first is the import graph, not the presence of a directory: a
    dependency copied to `third_party/` and rewired with a `replace` directive
    is still third-party source in the repository, and the graph says so
    because the package it resolves to does not belong to the main module.

    The second is the content. Source copied *into* the main module -- given a
    first-party import path, its `package` clause left alone -- belongs to the
    main module by every question the graph can be asked, and the graph will
    never report it. So the repository's own files are hashed against the
    module cache the image ships, which is every line of every dependency this
    module is allowed to reach. A file that matches one of them is that
    dependency's source, whatever path it has been given.
    """
    deps = ctx.deps
    if not deps:
        return boolean("no-vendored-source", False,
                       {"error": "the import graph is empty; nothing was "
                                 "checked, which is not the same as nothing "
                                 "being wrong"})
    main_module = spec["module"]
    offenders = []
    for pkg in repo_packages(deps, ctx.root):
        if pkg.get("Standard"):
            continue
        module = (pkg.get("Module") or {}).get("Path")
        if module != main_module:
            offenders.append(pkg.get("ImportPath"))

    table = _modcache_digests(ctx.env)
    copied = []
    for path in ctx.own_files:
        digest, _ = _copy_digest(path)
        if digest and digest in table:
            copied.append({"file": os.path.relpath(path, ctx.root),
                           "copy_of": table[digest]})

    return boolean("no-vendored-source", not offenders and not copied,
                   {"offenders": sorted(offenders)[:20],
                    "count": len(offenders),
                    "copied": copied[:20],
                    "copied_count": len(copied)})


def gate_gofmt(ctx, spec):
    """`gofmt -l -s` reports nothing over the tree.

    Given explicit file paths, never a directory: `gofmt` recurses into any
    directory it is handed, so passing the walk's directory list would put
    vendored source back inside a scope meant to exclude it -- and would report
    the same file more than once besides. The files come from the filesystem
    walk, not from a package list, because `gofmt` is a syntactic tool with no
    reason to stop at what the current platform can build.
    """
    files = ctx.own_files if spec.get("scope") == "own" else ctx.repo_files
    if not files:
        return boolean("gofmt", False, {"error": "no Go source found"})
    bad, failed = [], []
    for chunk in _chunks(files, 400):
        out = toolenv.run(["gofmt", "-l", "-s"] + chunk, env=ctx.env)
        if out.code != 0:
            # gofmt exits 0 whether or not it lists files. Anything else is a
            # run that did not finish -- killed, timed out, or a file it could
            # not parse -- and its silence is not a clean tree (wave H, G1).
            failed.append({"code": out.code, "timedout": out.timedout,
                           "stderr": out.stderr[-400:]})
        bad.extend(line for line in out.stdout.splitlines() if line.strip())
    if failed:
        return Result("gofmt", 0.0,
                      {"error": "gofmt did not run to completion",
                       "runs": failed[:3], "scanned": len(files)})
    rel = sorted(os.path.relpath(f, ctx.root) for f in bad)
    return graded("gofmt", len(bad), spec["baseline"], floor=spec.get("floor", 0),
                  detail={"files": rel[:20], "count": len(bad),
                          "scanned": len(files)})


def gate_vet(ctx, spec):
    """`go vet ./...` reports nothing.

    Later steps add analyzers; the spec names them so the gate grows with the
    ladder instead of being rewritten.
    """
    args = ["go", "vet"]
    for name in spec.get("analyzers") or []:
        args.append("-" + name)
    args.append("./...")
    out = toolenv.run(args, env=ctx.env)
    findings = _vet_findings(out.stderr + out.stdout)
    if out.code != 0 and not findings:
        # vet exits non-zero both when it reports and when it cannot run. No
        # findings and a non-zero exit is the second case, and a tool that did
        # not run is not a clean tree.
        return Result("vet", 0.0, {"error": "go vet did not run",
                                   "stderr": out.stderr[-600:]})
    return graded("vet", len(findings), spec["baseline"],
                  floor=spec.get("floor", 0), detail={"findings": findings[:20]})


_VET_LINE = re.compile(r"^[^\s].*\.go:\d+:\d+: ")


def _vet_findings(text):
    return [line for line in text.splitlines() if _VET_LINE.match(line)]


def gate_staticcheck(ctx, spec):
    """staticcheck reports nothing in the named check classes."""
    checks = spec.get("checks") or "SA1019"
    out = toolenv.run(
        [ctx.tool("staticcheck"), "-checks", checks, "-f", "json", "./..."],
        env=ctx.env)
    findings, broken, parsed = [], [], 0
    for line in out.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            item = json.loads(line)
        except ValueError:
            continue
        parsed += 1
        if item.get("severity") == "ignored":
            # A suppressed finding is a finding: the suppression gate counts
            # the comment and this gate keeps counting the problem.
            pass
        code = item.get("code")
        if code == "compile":
            # Not a check: the loader failed. Counting it alongside the checks
            # would let a tree that does not build outscore one that does.
            broken.append(item.get("message", "")[:200])
            continue
        findings.append("%s: %s" % (code,
                                    (item.get("location") or {}).get("file")))
    if out.timedout or out.code not in (0, 1) or (out.code == 1
                                                  and not parsed):
        # staticcheck exits 0 with nothing to report and 1 when it reports
        # something. Any other exit, or a 1 with no report read back, is a run
        # that did not finish -- killed, timed out, or its output lost -- and
        # its silence is not a clean tree (wave H, G1).
        return Result("staticcheck", 0.0,
                      {"error": "staticcheck did not run to completion",
                       "code": out.code, "timedout": out.timedout,
                       "stderr": out.stderr.splitlines()[:5],
                       "checks": checks})
    if broken:
        return Result("staticcheck", 0.0,
                      {"error": "staticcheck could not load the packages",
                       "compile": broken[:5], "checks": checks})
    return graded("staticcheck", len(findings), spec["baseline"],
                  floor=spec.get("floor", 0),
                  detail={"checks": checks, "findings": findings[:20],
                          "stderr": out.stderr.splitlines()[:5]})


def gate_ifacescan(ctx, spec):
    """No empty-interface fields or parameters in the inventory packages.

    The inventory is the authoring-time allowlist named by the task line; it is
    passed as a list of import-path patterns.
    """
    patterns = spec["inventory"]
    out = toolenv.run(
        [ctx.tool("ifacescan"), "-tests=" + str(spec.get("tests", True)).lower()]
        + patterns, env=ctx.env)
    findings = _vet_findings(out.stderr + out.stdout)
    return graded("ifacescan", len(findings), spec["baseline"],
                  floor=spec.get("floor", 0),
                  detail={"findings": findings[:20]})


def gate_dupscan(ctx, spec):
    """No declarations left that differ only in the types they name.

    The typed-clone scanner reports groups of declarations whose bodies are
    identical once types are abstracted and locals are renamed -- the exact
    shape a type parameter removes. It is a count gate like the scanners above,
    with one difference that matters: a tree the scanner cannot type-check is
    not a tree with no clones in it, so exit 3 scores zero rather than reporting
    an empty population. A candidate cannot retire a clone group by breaking the
    build.
    """
    argv = [ctx.tool("dupscan"),
            "-tests=" + str(spec.get("tests", True)).lower(),
            "-min-group", str(spec.get("min_group", 2)),
            "-min-nodes", str(spec.get("min_nodes", 12)),
            "-root", ctx.root]
    out = toolenv.run(argv + list(spec.get("packages") or ["./..."]),
                      env=ctx.env)
    if out.code not in (0, 1):
        return Result("dupscan", 0.0,
                      {"error": "dupscan could not analyse the tree",
                       "code": out.code,
                       "stderr": out.stderr.splitlines()[:5]})
    findings = [ln for ln in out.stdout.splitlines() if ": typed-clone: " in ln]
    return graded("dupscan", len(findings), spec["baseline"],
                  floor=spec.get("floor", 0),
                  detail={"findings": findings[:20]})


_ERRSCAN_RUNS = {}
_ERRSCAN_CAT = re.compile(r"\.go:\d+:\d+: ([a-z][a-z0-9-]*): ")


def errscan_run(ctx, spec):
    """Run the error-handling pass once and bucket its findings by category.

    One invocation type-checks the whole module graph, which is the expensive
    part, and answers five separate questions. Each question belongs to its own
    gate -- they retire different constructs and a candidate can plausibly
    finish one and not another -- so the run is memoised on the arguments that
    change what it looks at, and the gates only say which bucket they grade.

    A line the category regex does not recognise is not a finding of this pass:
    it is the loader telling us it could not type-check something. That makes
    every bucket unreadable rather than empty, and the gates that read this run
    say zero, because "the analyzer found no comparisons" and "the analyzer
    could not look" are the same number and not the same answer.
    """
    patterns = list(spec.get("packages") or ["./..."])
    tests = bool(spec.get("tests", False))
    key = (tests, tuple(patterns))
    if key in _ERRSCAN_RUNS:
        return _ERRSCAN_RUNS[key]

    out = toolenv.run(
        [ctx.tool("errscan"), "-tests=" + str(tests).lower()] + patterns,
        env=ctx.env)
    buckets, broken = {}, []
    for line in _vet_findings(out.stderr + out.stdout):
        match = _ERRSCAN_CAT.search(line)
        if match:
            buckets.setdefault(match.group(1), []).append(line)
        else:
            broken.append(line)
    if out.code != 0 and not buckets and not broken:
        broken.append("errscan exited %d without reporting: %s"
                      % (out.code, (out.stderr or out.stdout).strip()[-400:]))
    run = {"buckets": buckets, "broken": broken}
    _ERRSCAN_RUNS[key] = run
    return run


def gate_errscan(ctx, spec):
    """One category of the error-handling pass, graded against its baseline.

    `category` names the question: whether errors are compared by identity or
    by the text of `Error()`, whether they are classified by a switch or an
    assertion on a concrete type, or whether a cause is rendered into a new
    message and then dropped. Each is a way of reading an answer off the error
    in front of the caller instead of off the chain behind it, and each stops
    being correct the moment anything in between adds context.
    """
    category = spec["category"]
    run = errscan_run(ctx, spec)
    gid = spec.get("id") or ("errscan:" + category)
    if run["broken"]:
        return Result(gid, 0.0,
                      {"error": "the analyzer could not type-check this tree, "
                                "so an empty result is not a clean one",
                       "loader": run["broken"][:10]})
    findings = run["buckets"].get(category, [])
    return graded(gid, len(findings), spec["baseline"],
                  floor=spec.get("floor", 0),
                  detail={"category": category, "findings": findings[:20]})


def _package_imports(pkg):
    """Everything a package names, including from its tests."""
    imports = set(pkg.get("Imports") or [])
    imports |= set(pkg.get("TestImports") or [])
    imports |= set(pkg.get("XTestImports") or [])
    return imports


def gate_import_absent(ctx, spec):
    """An import path is absent from the repository's compiled import graph.

    `strict` grades the requirement as the property it is rather than as
    progress towards it: one package still importing the retired library scores
    the same as sixteen, because a requirement phrased "the repository's own
    code no longer uses it" is not nine-tenths met by moving every use behind
    one first-party package that does. A single re-export file is the cheapest
    tree that satisfies the graded reading -- fifteen of sixteen importers
    converted, no other line of the milestone done -- and it is the one tree
    the requirement names. Where a step wants the progress reading it leaves
    `strict` off; where a sibling census already grades the same work per file,
    the finer gradient is there and this gate can state the endpoint.
    """
    target = spec["import"]
    repo = repo_packages(ctx.deps, ctx.root)
    offending = set()
    for pkg in repo:
        path = pkg.get("ImportPath")
        if path and target in _package_imports(pkg):
            offending.add(path)

    offenders = sorted(offending)
    if spec.get("strict"):
        return boolean("import-absent:" + target, not offenders,
                       {"importers": offenders[:20],
                        "count": len(offenders),
                        "baseline": spec["baseline"]})
    return graded("import-absent:" + target, len(offenders), spec["baseline"],
                  floor=spec.get("floor", 0),
                  detail={"importers": offenders[:20]})


def gate_import_present(ctx, spec):
    """An import path is reached by the repository's compiled import graph.

    The complement of the gate above: it is how "the new semantics are live" is
    checked without naming a test. A compatibility shim that keeps the old
    package and merely re-exports it fails this and the absence gate together.

    `packages` names the import paths that each have to be among the importers,
    and is what a requirement naming particular packages asks for: any five
    packages satisfy a count of five, which is not the same statement. It is
    graded proportionally, so migrating four of the five earns four fifths.
    `min_packages` remains for a specification that only has a number to give.
    """
    target = spec["import"]
    users = []
    for pkg in repo_packages(ctx.deps, ctx.root):
        imports = set(pkg.get("Imports") or [])
        imports |= set(pkg.get("TestImports") or [])
        imports |= set(pkg.get("XTestImports") or [])
        if target in imports:
            users.append(pkg.get("ImportPath"))
    if spec.get("packages"):
        required = list(spec["packages"])
        found = [p for p in required if p in set(users)]
        missing = [p for p in required if p not in set(users)]
        return Result("import-present:" + target,
                      float(len(found)) / max(1, len(required)),
                      {"users": sorted(users)[:20], "required": required,
                       "found": found, "missing": missing})
    need = int(spec.get("min_packages", 1))
    return Result("import-present:" + target,
                  min(1.0, float(len(users)) / max(1, need)),
                  {"users": sorted(users)[:20], "min_packages": need})


def _census_value(ctx, spec):
    """Read one number out of the AST census. Returns (value, label, census)."""
    census = ctx.census_own if spec.get("scope") == "own" else ctx.census_repo
    if "selector" in spec:
        return (census.get("selectors", {}).get(spec["selector"], 0),
                spec["selector"], census)
    if "import" in spec:
        return (census.get("imports", {}).get(spec["import"], 0),
                spec["import"], census)
    return census.get(spec["field"], 0), spec["field"], census


def gate_count(ctx, spec):
    """A count from the AST census, graded against its baseline.

    The census is syntactic and filesystem-scoped (see `score.census`), so a
    construct parked behind a build constraint the platform does not satisfy
    still counts.

    `field` names a key of the census; `selector` names an entry of its
    per-selector map. `direction` is "down" (the default -- the construct is
    being retired) or "up" (the construct is being adopted, and `baseline` is
    the number of sites the migration is expected to reach).
    """
    value, label, census = _census_value(ctx, spec)
    gid = spec.get("id") or ("count:" + label)
    if not census:
        return Result(gid, 0.0, {"error": "the census produced nothing; a "
                                          "count of zero here would mean the "
                                          "walk failed, not that the tree is "
                                          "clean"})
    if spec.get("direction") == "up":
        return Result(gid, min(1.0, float(value) / max(1, spec["baseline"])),
                      {"count": value, "target": spec["baseline"]})
    return graded(gid, value, spec["baseline"], floor=spec.get("floor", 0),
                  detail={"construct": label})


def gate_census_floor(ctx, spec):
    """An anti-weakening bound on a census count.

    Where `count` grades progress towards a target, this grades the absence of
    regress: the baseline is a bound the candidate has to stay on the right side
    of, and staying exactly there is full marks. `mode: "max"` is a ceiling --
    suppression comments and skipped tests, which the tree may not acquire more
    of. `mode: "min"` is a floor -- test functions, ginkgo specs, assertions and
    test files, which the corpus may not shed. Either way the score falls off in
    proportion to the baseline rather than collapsing, so a test file merged into
    another costs a slice of one gate and not the whole regression multiplier.

    `strict: true` turns that proportionality off: any excursion past the bound
    scores 0 rather than a slice. It is for a bound the instruction states as an
    absolute rather than as a budget, and it exists because proportional scoring
    cannot express one when the baseline is large. Measured: a tree that tagged a
    single source file out of the build scored 124 against a baseline of 125,
    which is 0.992 of one entry -- 0.000246 of the reward at weight 1, and only
    0.0006 at weight 3. At that scale the fall-off is not a deterrent, it is a
    receipt. Reserve it for counts where any movement is a defect; the corpus
    floors are budgets and must stay proportional.
    """
    value, label, census = _census_value(ctx, spec)
    gid = spec.get("id") or ("floor:" + label)
    if not census:
        return Result(gid, 0.0, {"error": "the census produced nothing"})
    baseline = int(spec["baseline"])
    scale = max(1, abs(baseline))
    ok = value >= baseline if spec.get("mode") == "min" else value <= baseline
    strict = bool(spec.get("strict"))
    if ok:
        score = 1.0
    elif strict:
        score = 0.0
    elif spec.get("mode") == "min":
        score = float(value) / scale
    else:
        score = 1.0 - float(value - baseline) / scale
    return Result(gid, score, {"count": value, "bound": baseline,
                               "mode": spec.get("mode", "max"),
                               "strict": strict, "construct": label})


def gate_constraint_set(ctx, spec):
    """The set of constrained files, and what each one's constraint selects.

    The two build-tag counts grade the *form* a constraint is written in; this
    grades its *identity*. The census renders both halves of a file's
    constraint -- what its name implies and what its header comments parse to
    -- from the parsed expression rather than from the comment text, so a file
    converted from `// +build` to `//go:build` produces the value it produced
    before and this gate stays still. What moves it is a constrained file that
    is gone, or one whose constraint now selects a different set of platforms.
    Keys the candidate added are not held against it: a new platform file is
    not a regression, and this is a bound on loss.
    """
    census = ctx.census_own if spec.get("scope") == "own" else ctx.census_repo
    gid = spec.get("id") or "constraint-set"
    found = census.get("build_constraints") if census else None
    if found is None:
        return Result(gid, 0.0,
                      {"error": "the census produced no build_constraints "
                                "map; no constrained files here would mean "
                                "the walk failed, not that every constraint "
                                "survived"})
    want = spec["constraints"]
    missing, changed = [], []
    for path in sorted(want):
        if path not in found:
            missing.append(path)
        elif found[path] != want[path]:
            changed.append({"path": path, "expected": want[path],
                            "actual": found[path]})
    kept = len(want) - len(missing) - len(changed)
    return Result(gid, float(kept) / max(1, len(want)),
                  {"baseline": len(want), "kept": kept,
                   "missing": missing[:20], "changed": changed[:20]})


def gate_files_absent(ctx, spec):
    """Named files are gone from the repository."""
    present = [rel for rel in spec["paths"]
               if os.path.exists(os.path.join(ctx.root, rel))]
    return graded("files-absent:" + spec["id"], len(present),
                  len(spec["paths"]), {"present": present})


def gate_race(ctx, spec):
    """`go test -race` is clean over the module under a seeded schedule."""
    env = dict(ctx.env)
    env["GOFLAGS"] = env.get("GOFLAGS", "")
    out = toolenv.run(
        ["go", "test", "-race", "-count", str(spec.get("count", 1)),
         "-timeout", spec.get("timeout", "20m"), "-vet=off", "./..."],
        env=env, timeout=spec.get("seconds", 1500))
    failed = [line for line in out.stdout.splitlines()
              if line.startswith("FAIL") or line.startswith("--- FAIL")]
    races = out.stdout.count("WARNING: DATA RACE")
    ok = out.code == 0 and races == 0
    return boolean("race", ok, {"failed": failed[:20], "races": races,
                                "code": out.code})


# Where the verifier's own probe packages live inside the image, and the one
# throwaway copy of the candidate's tree they are compiled into.
PROBE_SRC = "/tests/probe"
_PROBE_TREE = []

_PROBE_PASS = re.compile(r"^\s*--- PASS: (\S+)", re.M)
_PROBE_FAIL = re.compile(r"^\s*--- FAIL: (\S+)", re.M)


def probe_tree(ctx):
    """A throwaway copy of the gated tree, made once and shared by every probe.

    The probes are compiled *inside* the module so they can import the
    repository's own packages by their real import paths, with the candidate's
    own go.mod deciding every version. That means adding a directory to the
    tree, which is why it happens to a copy: the gated tree (root's copy of
    /app, toolenv.GATED_ROOT) is what every other gate measures, and a gate
    that edited it would be changing the answer the next one reads.
    """
    if _PROBE_TREE:
        return _PROBE_TREE[0]
    dest = os.path.join(tempfile.gettempdir(), "probetree")
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(ctx.root, dest, symlinks=True)
    _PROBE_TREE.append(dest)
    return dest


_PROBE_RUNS = {}


def probe_run(ctx, spec):
    """Compile and run one probe package, once, however many gates read it.

    A probe package is a scoring run's most expensive gate -- it compiles the
    repository and then spends real seconds watching timeouts fire -- and one
    package usually answers two different questions: the tests that only pass
    once the step is done, and the controls that have to keep passing either
    way. Those belong in different families, which means two gate entries over
    one run, so the run is memoised by directory.

    How the package is *run* therefore cannot live on a gate entry: two entries
    over one run would each name a timeout and only the first one reached would
    have any effect. It lives in the specification's top-level `probes` map,
    keyed by the same directory, so there is exactly one place that says how
    long this package may take and the gate entries only say what they grade.
    """
    key = spec["dir"]
    if key in _PROBE_RUNS:
        return _PROBE_RUNS[key]

    cfg = (ctx.spec.get("probes") or {}).get(key) or {}
    tree = probe_tree(ctx)
    name = "verifierprobe_" + re.sub(r"[^a-z0-9]+", "", key.lower())
    pkgdir = os.path.join(tree, name)
    shutil.rmtree(pkgdir, ignore_errors=True)
    shutil.copytree(os.path.join(PROBE_SRC, key), pkgdir)

    out = toolenv.run(["go", "test", "-count=1", "-v", "-timeout",
                       cfg.get("timeout", "4m"), "./" + name],
                      env=ctx.env, cwd=tree,
                      timeout=int(cfg.get("hard_timeout", 330)))
    text = out.stdout + "\n" + out.stderr
    run = {"passed": set(_PROBE_PASS.findall(text)),
           "failed": set(_PROBE_FAIL.findall(text)),
           "text": text, "timedout": out.timedout}
    _PROBE_RUNS[key] = run
    return run


def gate_probe(ctx, spec):
    """Grade one verifier-owned test package against the candidate's tree.

    This is how a behavioural requirement is graded without naming a check in
    the instruction: the probe is ordinary Go, it imports the repository the
    way any consumer would, and it asserts what the milestone promises rather
    than how the candidate spelled it.

    `tests` is the denominator, and it comes from the specification rather than
    from the output on purpose -- a package that does not compile prints no
    test names at all, and dividing by what ran would score that as a clean
    sweep of an empty set. A compile failure is a zero, with the compiler's own
    words as the evidence. Write it as a list of test function names to grade
    that subset, which is how one probe package feeds both families, or as a
    number to grade whatever the package declares.
    """
    run = probe_run(ctx, spec)
    passed, failed = run["passed"], run["failed"]

    names = spec["tests"] if isinstance(spec["tests"], list) else None
    if names is not None:
        want = len(names)
        got = len([n for n in names if n in passed])
        detail = {"graded": sorted(names),
                  "passed": sorted(n for n in names if n in passed),
                  "failed": sorted(n for n in names if n not in passed),
                  "want": want}
    else:
        want = int(spec["tests"])
        got = len(passed)
        detail = {"passed": sorted(passed), "failed": sorted(failed),
                  "want": want}

    if not passed and not failed:
        detail["error"] = ("the probe produced no test results; it did not "
                           "build against this tree")
        detail["output"] = run["text"].strip()[-2000:]
    elif got < want:
        detail["output"] = run["text"].strip()[-2000:]
    if run["timedout"]:
        detail["timedout"] = True
    return Result(spec.get("id") or ("probe:" + spec["dir"]),
                  float(got) / max(1, want), detail)


GATES = {
    "module": gate_module,
    "build-all": gate_build_all,
    "gomod-directives": gate_gomod_directives,
    "no-vendored-source": gate_no_vendored_source,
    "gofmt": gate_gofmt,
    "vet": gate_vet,
    "staticcheck": gate_staticcheck,
    "ifacescan": gate_ifacescan,
    "dupscan": gate_dupscan,
    "errscan": gate_errscan,
    "import-absent": gate_import_absent,
    "import-present": gate_import_present,
    "count": gate_count,
    "census-floor": gate_census_floor,
    "constraint-set": gate_constraint_set,
    "files-absent": gate_files_absent,
    "race": gate_race,
    "probe": gate_probe,
}


def run_gate(ctx, spec):
    fn = GATES[spec["kind"]]
    try:
        return fn(ctx, spec)
    except Exception as err:                       # a broken gate scores zero
        return Result(spec.get("id") or spec["kind"], 0.0,
                      {"error": "%s: %s" % (type(err).__name__, err)})


def _chunks(items, size):
    for i in range(0, len(items), size):
        yield items[i:i + size]
