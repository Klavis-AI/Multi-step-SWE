#!/usr/bin/env python3
"""AST-level facts about how the tree uses OpenSSL, from the compile database.

    astscan.py --compdb DIR [--root DIR] [--out FILE] [--jobs N] [--quiet]
               [--include DIR] [--call-in CALLER:CALLEE ...] [--calls NAME ...]
               [--reach-deep]
               [--text NAME ...]

Parses exactly what the build compiled, with the flags it compiled them with,
and reports counts that no amount of reformatting can move:

  member_access   a field read or write on a struct that belongs to OpenSSL.
                  "Opaque" is not a property of the spelling, so this asks the
                  AST who owns the record rather than what it is called: a
                  record counts if its declaration lives under an
                  .../openssl/ include directory -- which covers the vendored
                  1.0.2 copy in the repo and the installed headers alike -- or
                  if its tag is one the installed headers declare.  The second
                  clause is what stops "copy OpenSSL's private struct into a
                  header of my own and keep the field accesses" from reading as
                  a clean tree.  A field declared *inside* an OpenSSL header is
                  OpenSSL declaring its own struct and is not a finding;
                  deleting that vendored copy is what the link gate measures.
  value_decl      a variable, field, or parameter holding BY VALUE an OpenSSL
                  record whose layout the *installed* OpenSSL does not publish
                  -- the stack-allocated context pattern, for exactly those
                  types where sizes have stopped being knowable.
  deprecated_call a call whose callee the headers mark deprecated.  Same
                  authority as ossldep.py, resolved per call site.
  suppression     a site that hides a diagnostic instead of fixing it.
  call_sites      a call to CALLEE appearing inside the body of CALLER, for
                  each --call-in pair asked for.  Nothing is asked for by
                  default: which function must call what is a property of the
                  step, not of the scanner, and a step asks for its own.
  call_reach      one entry per --call-in pair that CALLER actually reaches:
                  directly, or through one function the tree defines itself,
                  whose name the entry carries as `via`.
  call_reach_deep with --reach-deep, the same question asked with no bound on
                  how many of the tree's own functions the path goes through.
                  `via` is still the first hop and `hops` is how many were
                  needed.  call_reach stops at one because a gate that asks
                  "does this function do the work" wants the work near it;
                  this one exists because a gate that asks "is the work done
                  on this entry point's account" does not care how the port
                  divided its own helpers up, and answering that with a bound
                  fails a correct tree for its factoring.  Both are emitted,
                  and each gate reads the one its sentence means.
  call_graph      with --reach-deep, every call edge between two functions the
                  tree itself defines.  The raw material for the walk above,
                  and nothing else reads it.
  called          of the names asked about, by --calls or as a --call-in
                  callee, those some function the tree defines really calls.
                  A linked object importing a symbol says a translation unit
                  mentioned it; this says source the build compiled calls it.
  text/text_names of the names asked about with --text, those that appear in
                  the text of the sources the build compiled, comments taken
                  out and string literals left in.  `called` cannot see a name
                  that is only ever a string, and neither can an import table;
                  a step that bans an API rather than a call asks here too.

The last of those exists because "this function initialises the library" is a
statement about where a call is, and the built object cannot answer it: an
import proves some translation unit mentions the symbol, not that the engine's
own entry point reaches it.  The probe starts from the named function, so a
call from anywhere else -- another function nothing calls, a dead one, a file
linked in for the purpose -- is not it.

It stops at one hop rather than zero because factoring `OPENSSL_init_crypto(0,
NULL)` into a one-line helper and calling the helper is the same program, and
a tree that did it has done what an instruction asking its entry point to
initialise the library asked for.  It stops at one hop rather than at all of
them because reachability through four frames of unrelated code is no longer a
statement about the entry point.  Both hops must be definitions in the tree,
so "some library function I call happens to call it" is not a path.

"Does the installed OpenSSL publish this layout" is read out of the installed
headers themselves, the way ossldep.py reads the deprecated set out of them: one
synthetic translation unit that includes every public header, walked for record
definitions.  Nothing is hand-listed, so the answer moves when the step's
OpenSSL version moves, and it is specific rather than sweeping -- `AES_KEY` is a
complete public struct at every version in this migration and so is never a
value_decl finding, while `EVP_MD_CTX` stopped being one at 1.1.0 and always is.
A version where a type goes opaque is the version at which the count for it
appears, which is the whole point.

Counted as expressions and sites, not as files or blocks: merging ten
accesses into one function still reports ten, and one file with four pragmas
reports four.  Findings carry file:line so a verifier can quote them back.

Suppressions come from two structural places: the compile database's own
arguments (-Wno-..., -U OPENSSL_NO_DEPRECATED), and a text scan of the files
each translation unit actually included.  Pragmas are a preprocessor artifact
with no AST node, so the text of the real include set is as structural as it
gets -- and it is the include set, not a directory walk, so a file that is no
longer compiled cannot hide a suppression in it either.
"""
import json
import multiprocessing
import os
import re
import sys

LIBCLANG = os.environ.get("LIBCLANG", "/usr/lib/llvm-14/lib/libclang.so.1")

# Flags that turn a diagnostic off, or take away a definition that turns
# diagnostics on.  -Werror=... is not one: it makes a warning stricter.
FLAG_SUPPRESS = re.compile(
    r"^(-Wno-(?!error$).+|-Wno-error(=.*)?|-U\s*OPENSSL_NO_DEPRECATED"
    r"|-DOPENSSL_SUPPRESS_DEPRECATED(=.*)?|-fpermissive)$")

TEXT_SUPPRESS = re.compile(
    r"#\s*pragma\s+(?:GCC|clang)\s+diagnostic"
    r"|NOLINT"
    r"|OPENSSL_SUPPRESS_DEPRECATED"
    r"|no_sanitize")

# Comments, for the text scan below to take out before it looks.  String
# literals are deliberately left in: a name handed to the loader at run time is
# a string and nothing else, and that is the case the text scan exists for.
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def configure():
    """clang.cindex, pointed at libclang once per process.

    Workers are forked after the parent has already configured it, and
    set_library_file refuses to run twice, so the second call is a no-op.
    """
    import clang.cindex as C
    if not C.Config.loaded:
        C.Config.set_library_file(LIBCLANG)
    return C


def under(path, root):
    try:
        return os.path.commonpath([os.path.realpath(path), root]) == root
    except ValueError:
        return False


def is_openssl(path):
    """A header that belongs to OpenSSL, vendored copy included."""
    p = os.path.realpath(path).replace(os.sep, "/")
    return "/openssl/" in p or p.endswith("/ossl_typ.h")


def probe_installed(incdir):
    """What the installed OpenSSL headers declare, and what they define.

    Returns (declared, defined, errors): two sets of record tags and the
    parse diagnostics.  `declared` is every struct/union tag the headers
    mention, forward declarations included -- `evp_md_ctx_st` is in it because
    ossl_typ.h forward-declares it, which is precisely how a type announces
    itself as OpenSSL's without publishing a layout.  `defined` is the subset
    the headers actually give a body to.

    The difference between the two sets is the opaque surface, read off the
    installed release rather than asserted by an author.
    """
    C = configure()
    heads = sorted(f for f in os.listdir(os.path.join(incdir, "openssl"))
                   if f.endswith(".h"))

    # Some releases ship tombstones -- a header whose entire body is
    # `#error "This file is obsolete"`.  Including one is not a failure to
    # report, it is a header saying it has nothing in it, so a file that
    # answers with an error is dropped and the probe is taken again.  Dropping
    # is driven by which file the diagnostic points at, so no release's list of
    # tombstones has to be known in advance; a second round of errors is a real
    # failure and is returned.
    drop, tu, errors = set(), None, []
    for _ in range(2):
        src = "".join("#include <openssl/%s>\n" % h
                      for h in heads if h not in drop)
        tu = C.Index.create().parse(
            "ossl_probe.c", args=["-I" + incdir],
            unsaved_files=[("ossl_probe.c", src)])
        errors, blame = [], set()
        for d in tu.diagnostics:
            if d.severity < 3:
                continue
            errors.append("%s: %s" % (d.location.file, d.spelling))
            if d.location.file is not None:
                blame.add(os.path.basename(str(d.location.file)))
        blame &= set(heads) - drop
        if not blame:
            break
        drop |= blame
    if drop:
        sys.stderr.write("astscan: installed headers that refuse to be "
                         "included, skipped: %s\n" % ", ".join(sorted(drop)))

    declared, defined = set(), set()
    stack = [tu.cursor]
    while stack:
        n = stack.pop()
        stack.extend(n.get_children())
        if n.kind not in (C.CursorKind.STRUCT_DECL, C.CursorKind.UNION_DECL):
            continue
        if not n.spelling or n.location.file is None:
            continue
        if not is_openssl(str(n.location.file)):
            continue
        declared.add(n.spelling)
        if n.is_definition():
            defined.add(n.spelling)
    return declared, defined, errors


def suppressions_in(path, root):
    out = []
    try:
        with open(path, errors="replace") as fh:
            for i, line in enumerate(fh, 1):
                if TEXT_SUPPRESS.search(line):
                    out.append({"file": os.path.relpath(path, root),
                                "line": i, "what": line.strip()[:90]})
    except OSError:
        pass
    return out


def text_hits_in(path, root, rx):
    """Which asked-about names appear in this file's text, and where.

    The call graph answers "is it called"; this answers the instruction's own
    question, "does it appear in the sources the build compiles".  They are not
    the same question, and the gap between them is a string: a name reached
    through the loader is never a CALL_EXPR and never an undefined reloc, so
    neither the AST half nor the ELF half sees it.

    Comments come out first, blanked rather than deleted so a reported line
    number still points where it says.  A tree that documents which API it
    removed is describing the migration, not doing it -- the golden's own
    CpriHash.c says in as many words that EVP_MD_CTX_get0_md_data() returns
    NULL for a provided digest, and a scan that failed it for the sentence
    would be scoring the prose.
    """
    out = []
    try:
        with open(path, errors="replace") as fh:
            src = fh.read()
    except OSError:
        return out
    src = COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), src)
    for i, line in enumerate(src.split("\n"), 1):
        for m in rx.finditer(line):
            out.append({"file": os.path.relpath(path, root), "line": i,
                        "name": m.group(0)})
    return out


def scan_one(job):
    """One translation unit.  Run in its own process: parsing dominates."""
    C = configure()
    src, directory, filename, argv, root, declared, defined, calls, \
        callees, deep = job

    def owner_is_openssl(rec):
        """Whose record is this -- OpenSSL's, or the tree's own?

        Declared in an OpenSSL header, or carrying a tag the installed headers
        declare.  The second clause costs nothing on an honest tree and is what
        a transcribed-layout header has to get past.
        """
        f = rec.location.file
        return (f is not None and is_openssl(str(f))) or rec.spelling in declared

    found = {"member_access": [], "value_decl": [], "deprecated_call": [],
             "suppression": [], "call_sites": [], "call_out": [],
             "call_into": [], "call_graph": [], "defines": [],
             "parsed": [src], "errors": [], "files": []}
    rel = os.path.relpath(src, root)

    args, skip = [], False
    for a in argv[1:]:
        if skip:
            skip = False
            continue
        if FLAG_SUPPRESS.match(a):
            found["suppression"].append(
                {"file": rel, "line": 0, "what": "compile flag " + a})
        # -c and -o confuse a syntax-only parse; every other flag is kept so
        # the TU sees the macros and include path the build really used.
        if a in ("-c", "-o"):
            skip = a == "-o"
            continue
        if a == filename or os.path.realpath(
                os.path.join(directory, a)) == src:
            continue
        args.append(a)

    tu = C.Index.create().parse(src, args=args)
    for d in tu.diagnostics:
        if d.severity >= 3:
            found["errors"].append("%s: %s" % (rel, d.spelling))

    # The files this TU really pulled in: the suppression scan's scope.
    found["files"] = [src] + [str(i.include) for i in tu.get_includes()
                              if i.include and under(str(i.include), root)]

    # One node, one visit.  An anonymous record inside a typedef is reachable
    # both from the translation unit and from the typedef that names it, so an
    # undeduped walk reports that union's field four times over -- and the
    # count is of sites, not of paths through the tree.  Two
    # genuinely distinct expressions keep their own hashes, including the two
    # `(a)->top` a single 1.0.2 BN_is_one expands to.
    stack, visited = [tu.cursor], set()
    while stack:
        n = stack.pop()
        h = n.hash
        if h in visited:
            continue
        visited.add(h)
        stack.extend(n.get_children())
        loc = n.location.file
        if loc is None:
            continue
        path = str(loc)
        if not under(path, root) or is_openssl(path):
            continue
        here = {"file": os.path.relpath(path, root), "line": n.location.line}

        if n.kind in (C.CursorKind.MEMBER_REF_EXPR, C.CursorKind.MEMBER_REF):
            decl = n.referenced
            owner = decl.semantic_parent if decl is not None else None
            if owner is not None and owner_is_openssl(owner):
                found["member_access"].append(
                    dict(here, what="%s.%s" % (owner.spelling or "<anon>",
                                               n.spelling)))
        elif n.kind in (C.CursorKind.VAR_DECL, C.CursorKind.FIELD_DECL,
                        C.CursorKind.PARM_DECL):
            t = n.type
            while t.kind == C.TypeKind.CONSTANTARRAY:
                t = t.element_type
            rec = t.get_canonical().get_declaration()
            # By value, OpenSSL's, and a layout the installed release does not
            # publish.  The last clause is the one that matters: holding a
            # complete public struct by value is ordinary C, and only the types
            # this OpenSSL has actually made opaque have to become pointers.
            if rec is not None and rec.kind in (C.CursorKind.STRUCT_DECL,
                                                C.CursorKind.UNION_DECL) \
                    and owner_is_openssl(rec) and rec.spelling not in defined:
                found["value_decl"].append(
                    dict(here, what="%s %s" % (t.spelling, n.spelling)))
        elif n.kind == C.CursorKind.CALL_EXPR:
            callee = n.referenced
            if callee is not None and \
                    callee.availability == C.AvailabilityKind.DEPRECATED:
                found["deprecated_call"].append(
                    dict(here, what=callee.spelling))

    # The edges the --call-in pairs are decided on, walked from the function
    # definitions themselves rather than from the callee: the question is
    # whether THIS function reaches it, so a walk that started at the
    # translation unit and then asked which function an expression was in would
    # be answering a different one -- and semantic_parent does not answer it
    # for an expression anyway.
    #
    # Three things come out of the same walk:
    #
    #   call_sites  CALLEE called directly in CALLER's body, with file:line, so
    #               a verifier can quote the site back.
    #   call_out    every function CALLER calls, by name.  The first hop.
    #   call_into   every function the tree defines whose body calls CALLEE.
    #               Where the second hop has to land -- and, for a name asked
    #               about with --calls and no caller at all, the whole answer:
    #               "source the build compiled calls this".
    #
    # scan() folds the three into call_reach and called once all the
    # translation units are in, because the helper a caller goes through need
    # not be compiled in the same one.
    #
    # Only a definition in the tree counts, at both hops.  A prototype has no
    # body, and a function of the same name inside an OpenSSL header is
    # OpenSSL's, so "a library function I call calls it" is not a path.
    # Nothing here asks whose declaration the callee is: a tree that defined
    # its own OPENSSL_init_crypto would satisfy this probe and fail the import
    # half of the same gate, and the gate wants both.
    callers = {c for c, _ in calls}
    pairs = set(calls)
    for fn in (tu.cursor.get_children() if callers or callees else ()):
        if fn.kind != C.CursorKind.FUNCTION_DECL or not fn.is_definition():
            continue
        fnfile = fn.location.file
        if fnfile is None or not under(str(fnfile), root) \
                or is_openssl(str(fnfile)):
            continue
        # Of the names --call-in asked about, which ones this tree still has
        # a body for.  A gate that asks a question about a helper needs to be
        # able to tell "the helper does not do it" from "the helper is not
        # there any more", because a port is allowed to inline a helper away
        # and the obligation then belongs to whoever called it.
        if fn.spelling in callers or fn.spelling in callees:
            found["defines"].append(fn.spelling)
        is_caller = fn.spelling in callers
        body, body_seen = [fn], set()
        while body:
            n = body.pop()
            if n.hash in body_seen:
                continue
            body_seen.add(n.hash)
            body.extend(n.get_children())
            if n.kind != C.CursorKind.CALL_EXPR:
                continue
            ref = n.referenced
            if ref is None or not ref.spelling:
                continue
            if is_caller:
                found["call_out"].append(
                    {"from": fn.spelling, "to": ref.spelling})
            if ref.spelling in callees:
                found["call_into"].append(
                    {"from": fn.spelling, "to": ref.spelling})
            # The edges between the tree's own functions, kept only when a
            # gate has asked for the unbounded walk.  Filtered to callees the
            # tree declares for itself: a path through OpenSSL's own source is
            # not a path the port is answerable for, and libc is not either.
            if deep:
                rf = ref.location.file
                if rf is not None and under(str(rf), root) \
                        and not is_openssl(str(rf)):
                    found["call_graph"].append(
                        {"from": fn.spelling, "to": ref.spelling})
            if (fn.spelling, ref.spelling) in pairs:
                # A call written inside a macro has no file of its own; the
                # function it sits in is the honest location for it.
                at = n.location.file or fnfile
                found["call_sites"].append(
                    {"file": os.path.relpath(str(at), root),
                     "line": n.location.line,
                     "what": "%s calls %s" % (fn.spelling, ref.spelling)})
    return found


def scan(compdb_dir, root, jobs, incdir, calls, names, texts, deep=False):
    C = configure()
    callees = {c for _, c in calls} | set(names)

    declared, defined, probe_errors = probe_installed(incdir)
    # No usable reading of the installed headers means no usable answer about
    # which layouts are private.  Say so and stop, rather than report counts
    # taken against an empty opaque set -- which would read as a clean tree.
    if len(declared) < 50 or not defined:
        sys.stderr.write(
            "astscan: could not read the installed OpenSSL headers under %s "
            "(%d tags declared, %d defined)\n"
            % (incdir, len(declared), len(defined)))
        sys.exit(2)
    sys.stderr.write("astscan: installed headers under %s declare %d record "
                     "tags and define %d; %d are opaque\n"
                     % (incdir, len(declared), len(defined),
                        len(declared - defined)))

    db = C.CompilationDatabase.fromDirectory(compdb_dir)
    work, seen = [], set()
    for cmd in db.getAllCompileCommands():
        src = os.path.realpath(os.path.join(cmd.directory, cmd.filename))
        if src in seen or not under(src, root):
            continue
        seen.add(src)
        work.append((src, cmd.directory, cmd.filename, list(cmd.arguments),
                     root, declared, defined, calls, callees, deep))
    if not work:
        sys.stderr.write("astscan: the compile database names no source under "
                         "%s -- the build did not compile the repo\n" % root)
        sys.exit(2)

    out = {"member_access": [], "value_decl": [], "deprecated_call": [],
           "suppression": [], "call_sites": [], "call_out": [],
           "call_into": [], "call_graph": [], "defines": [], "parsed": [],
           "errors": []}
    files = set()
    with multiprocessing.Pool(jobs) as pool:
        for r in pool.imap_unordered(scan_one, work):
            for k in out:
                out[k].extend(r[k])
            files.update(r["files"])
    # Added after the reduce, not before it: these describe the instrument
    # rather than the tree, so nothing accumulates into them per translation
    # unit.
    out["probe_errors"] = probe_errors
    out["opaque_tags"] = sorted(declared - defined)

    # A function is compiled once but can be walked many times -- every
    # translation unit that includes the file it lives in sees the same
    # definition -- so the same call site arrives once per TU.  Sites are
    # what is counted, so fold them back to one.
    for k in ("call_sites", "call_out", "call_into", "call_graph"):
        out[k] = [dict(t) for t in sorted(
            {tuple(sorted(f.items())) for f in out[k]})]
    out["defines"] = sorted(set(out["defines"]))

    # Reachability, decided here rather than per translation unit because the
    # helper a caller goes through can be compiled in a different one.  One
    # entry per --call-in pair that is satisfied, so a gate reads a count: 0 is
    # "the entry point does not reach it", and `via` says how it does when it
    # does.  Direct calls are read back out of call_out, which holds every name
    # the caller calls, the callee's own among them.
    out["call_reach"] = []
    out["called"] = sorted({e["to"] for e in out["call_into"]})
    outset = {(e["from"], e["to"]) for e in out["call_out"]}
    intoset = {(e["from"], e["to"]) for e in out["call_into"]}
    for caller, callee in sorted(set(calls)):
        if (caller, callee) in outset:
            out["call_reach"].append(
                {"caller": caller, "callee": callee, "via": ""})
            continue
        for mid in sorted(b for a, b in outset if a == caller):
            if (mid, callee) in intoset:
                out["call_reach"].append(
                    {"caller": caller, "callee": callee, "via": mid})
                break

    # The same question with the bound taken off.  A port is entitled to divide
    # its own helpers up however it likes -- one that builds its key objects in
    # a function its padding helper calls is doing the work on the entry
    # point's account just as much as one that builds them in the entry point
    # -- so a gate whose sentence is "this entry point's operation goes through
    # X" reads this and a gate whose sentence is "X is not near this entry
    # point any more" reads the bounded one.  Walking the tree's own edges
    # only: the first hop out of the tree is the answer, not a node.
    out["call_reach_deep"] = []
    if deep:
        graph = {}
        for e in out["call_graph"]:
            graph.setdefault(e["from"], set()).add(e["to"])
        for caller, callee in sorted(set(calls)):
            if (caller, callee) in outset:
                out["call_reach_deep"].append(
                    {"caller": caller, "callee": callee, "via": "", "hops": 0})
                continue
            first, seen, frontier, hit, hops = {}, {caller}, [caller], None, 0
            while frontier and hit is None:
                hops += 1
                nxt = []
                for n in sorted(frontier):
                    for m in sorted(graph.get(n, ())):
                        if m in seen:
                            continue
                        seen.add(m)
                        first[m] = first.get(n, m)
                        nxt.append(m)
                        if (m, callee) in intoset or (m, callee) in outset:
                            hit = m
                            break
                    if hit is not None:
                        break
                frontier = nxt
            if hit is not None:
                out["call_reach_deep"].append(
                    {"caller": caller, "callee": callee,
                     "via": first[hit], "hops": hops})

    # One pass per file, not per (file, TU): a header included by 80 TUs holds
    # its pragma once, and sites are what is counted.  `files` is what the parse
    # really pulled in from under the tree, so the text scan below asks its
    # question of exactly the sources the build compiles -- not of a glob, and
    # not of files the build ignores.
    textrx = re.compile(r"\b(%s)\b" % "|".join(re.escape(t)
                                               for t in sorted(set(texts)))) \
        if texts else None
    out["text"] = []
    for f in sorted(files):
        if is_openssl(f):
            continue
        out["suppression"].extend(suppressions_in(f, root))
        if textrx is not None:
            out["text"].extend(text_hits_in(f, root, textrx))
    out["text_names"] = sorted({h["name"] for h in out["text"]})
    out["parsed"] = sorted(os.path.relpath(p, root) for p in out["parsed"])
    return out


def main(argv):
    if "--compdb" not in argv:
        sys.stderr.write(__doc__)
        return 2
    compdb = argv[argv.index("--compdb") + 1]
    root = os.path.realpath(
        argv[argv.index("--root") + 1] if "--root" in argv else ".")
    jobs = int(argv[argv.index("--jobs") + 1]) if "--jobs" in argv else \
        min(7, os.cpu_count() or 1)
    # Where the opaque set is read from.  The default is the prefix every step
    # image installs its OpenSSL into; it is an option so the gate's own
    # rehearsal can point it at a fixture.
    incdir = argv[argv.index("--include") + 1] if "--include" in argv \
        else "/usr/local/include"
    # Repeatable, and empty unless asked: a step names the call its own gate is
    # about, and no default here would be right for every step.
    calls, names, texts = [], [], []
    for i, a in enumerate(argv):
        if a == "--calls":
            if i + 1 >= len(argv) or not argv[i + 1]:
                sys.stderr.write("astscan: --calls wants a function name\n")
                return 2
            names.append(argv[i + 1])
            continue
        if a == "--text":
            if i + 1 >= len(argv) or not argv[i + 1]:
                sys.stderr.write("astscan: --text wants an identifier\n")
                return 2
            texts.append(argv[i + 1])
            continue
        if a != "--call-in":
            continue
        pair = argv[i + 1].split(":") if i + 1 < len(argv) else []
        if len(pair) != 2 or not all(pair):
            sys.stderr.write("astscan: --call-in wants CALLER:CALLEE\n")
            return 2
        calls.append(tuple(pair))
    r = scan(compdb, root, jobs, incdir, calls, names, texts,
             "--reach-deep" in argv)
    counts = {k: len(v) for k, v in r.items()}
    if "--out" in argv:
        with open(argv[argv.index("--out") + 1], "w") as fh:
            json.dump(r, fh, indent=1)
    if "--quiet" not in argv:
        sys.stderr.write("astscan: %d TU(s) under %s\n"
                         % (counts["parsed"], root))
        for k in ("member_access", "value_decl", "deprecated_call",
                  "suppression", "call_sites", "call_reach",
                  "call_reach_deep", "call_graph", "text", "errors",
                  "probe_errors"):
            sys.stderr.write("  %-16s %d\n" % (k, counts[k]))
        if names or calls:
            sys.stderr.write("    called          %s\n"
                             % (", ".join(r["called"]) or "(none)"))
        for f in r["call_reach"]:
            sys.stderr.write("    call_reach %s -> %s%s\n"
                             % (f["caller"], f["callee"],
                                " via " + f["via"] if f["via"] else " direct"))
        for k in ("member_access", "value_decl", "deprecated_call",
                  "suppression", "call_sites"):
            for f in r[k][:6]:
                sys.stderr.write("    %s %s:%s %s\n"
                                 % (k, f["file"], f["line"], f["what"]))
    print(json.dumps(counts))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
