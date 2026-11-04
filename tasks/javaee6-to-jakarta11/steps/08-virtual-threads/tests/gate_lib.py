"""The reusable migrated-ness gates, shared by every step's score.py.

Per-step copy: every step's tests/ holds an identical copy of this file; keep
them in sync.

score_lib is the plumbing -- running things, reading scans, weighting results.
This file is the checks themselves: the handful of questions that get asked at
almost every rung of the ladder, each one written once so that ten steps cannot
drift into ten slightly different definitions of "gone".

Three habits run through all of them:

  * Scan everything, exclude nothing. A construct moved into a directory the
    scan skips is still in the shipped program.
  * Ask the artifact and the AST, never the text. A dependency reached through
    a bom is invisible in the pom, a commented-out import is not an import, and
    `grep -c` cannot tell either of those apart from the real thing.
  * A suppressed finding is a finding. Nothing here consults a baseline file,
    a nolint comment or an exclusion list the candidate can write.
"""

import json
import os
import re

import score_lib as S


# ---------------------------------------------------------------------------
# everything the shell stages produced
# ---------------------------------------------------------------------------

class Bundle(object):
    """One step's evidence, read once.

    Every field has a defined value even when the stage that fills it never
    ran, because a gate that throws is a verifier crash and a verifier crash is
    a step that vanishes from the mean.
    """

    def __init__(self, out=None, app=None):
        out = out or S.OUT
        # The root-held copy scan.sh takes of /app before any candidate code
        # runs again (helpers.sh, score_setup); /app itself stays writable by
        # the servers and the second build until score.py runs.
        app = app or os.environ.get("SCORE_APP") or "/app"
        self.out = out
        self.app = app
        self.art = S.Artifact(os.path.join(out, "artifact.jsonl"))
        self.ast = S.Ast(os.path.join(out, "ast.jsonl"))
        self.build_rc = _int(_read(os.path.join(out, "build.rc")), 1)
        self.tests_rc = _int(_read(os.path.join(out, "tests.rc")), 1)
        self.artifact_path = _read(os.path.join(out, "artifact.path"))
        self.tests = _tsv(os.path.join(out, "tests.tsv"))
        self.deps = _lines(os.path.join(out, "deps.txt"))
        self.deps_rc = _int(_read(os.path.join(out, "deps.rc")), 1)
        self.behaviour = _json(os.path.join(out, "behaviour.json"),
                               {"total": 0, "passed": 0})

    def behaviour_pair(self):
        return int(self.behaviour.get("passed", 0)), \
            int(self.behaviour.get("total", 0))

    def sources(self):
        """Every .java the AST scan parsed, as repository-relative paths."""
        return sorted(f.get("p", "") for f in self.ast.files())


def _read(p):
    try:
        with open(p, "r", encoding="utf-8") as fh:
            return fh.read().strip()
    except (IOError, OSError):
        return ""


def _lines(p):
    return [ln.rstrip("\n") for ln in _read(p).splitlines() if ln.strip()]


def _int(s, default):
    try:
        return int(s)
    except (TypeError, ValueError):
        return default


def _json(p, default):
    try:
        with open(p, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:                                   # noqa: BLE001
        return default


def _tsv(p):
    rows = []
    for ln in _lines(p):
        f = ln.split("\t")
        if len(f) >= 5:
            rows.append({"name": f[0], "tests": _int(f[1], 0),
                         "failures": _int(f[2], 0), "errors": _int(f[3], 0),
                         "skipped": _int(f[4], 0)})
    return rows


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------

def tests_ship_and_pass(g, b, min_tests, classes, weight=1.0):
    """The tests the tree already had still exist, still run, and still pass.

    This is the gate that keeps a migration from being done by deletion. It
    asks three separate questions, because each one fails in its own way:
    `mvn test` exiting 0 with nothing to run, the count quietly shrinking, and
    a class disappearing while the count is made up elsewhere. `classes` is
    matched on the simple name, so moving a test between packages is allowed
    and dropping it is not.
    """
    total = sum(r["tests"] for r in b.tests)
    bad = sum(r["failures"] + r["errors"] for r in b.tests)
    skipped = sum(r["skipped"] for r in b.tests)
    have = set()
    for r in b.tests:
        have.add(r["name"].rsplit(".", 1)[-1])
    missing = sorted(c for c in classes if c not in have)
    ok = (b.tests_rc == 0 and total >= min_tests and bad == 0
          and skipped == 0 and not missing)
    detail = ("rc=%s ran=%d (need >=%d) failed=%d skipped=%d"
              % (b.tests_rc, total, min_tests, bad, skipped))
    if missing:
        detail += " missing=" + ",".join(missing)
    return g.add("tests-ship-and-pass", ok, detail, weight,
                 {"rc": b.tests_rc, "ran": total, "need": min_tests,
                  "failed": bad, "skipped": skipped, "missing": missing,
                  "reports": len(b.tests)})


def class_release(g, b, major, weight=1.0, mode="exactly"):
    """The classes this project compiled target the release the step names.

    Own classes only: a class that arrived inside a dependency jar is evidence
    about that dependency, not about the release the project targets. `mode` is
    "exactly" for a step that pins a baseline and "at_least" for one that only
    raises a floor.
    """
    vers = b.art.class_versions()
    if not vers:
        return g.add("class-release", False, "no compiled classes found",
                     weight, {"count": 0})
    if mode == "at_least":
        bad = {n: v for n, v in vers.items() if v < major}
    else:
        bad = {n: v for n, v in vers.items() if v != major}
    hist = {}
    for v in vers.values():
        hist[S.release_of(v)] = hist.get(S.release_of(v), 0) + 1
    detail = "%d class(es), versions %s, want %s %s" % (
        len(vers), ",".join("%s:%d" % kv for kv in sorted(hist.items())),
        mode, S.release_of(major))
    if bad:
        detail += "; offenders " + ", ".join(sorted(bad)[:6])
    return g.add("class-release", not bad, detail, weight,
                 {"histogram": hist, "offenders": sorted(bad)[:200]})


def no_refs(g, b, name, prefixes, weight=1.0, allow=(), own=False,
            floor=None):
    """No class in anything the build produced still names these types.

    The constant pool is the whole answer: descriptors, signatures, annotation
    types, supertypes and dotted string literals all land in the scan, so a
    reference the compiler erased and one built by Class.forName are both here.
    `allow` is for classes whose own name matches the prefix under test, never
    for a construct the step asked to remove. `own` is documented on
    Artifact.refs_by_class and is a per-gate decision: a removal that is about
    the deployable asks the whole thing, one that is about the candidate's own
    code asks its own classes.

    `floor` is `(minimum, origin_substring)` and is the answer to the way an
    absence gate lies: "no class references junit.framework" is also what a
    tree with no compiled classes says. Where the absence is about test code
    the candidate might simply have deleted, the gate is given the universe it
    must have searched, and reports a failure that names the emptiness rather
    than a pass that hides it.
    """
    hits = b.art.refs_by_class(prefixes, own=own)
    hits = {k: v for k, v in hits.items() if k not in set(allow)}
    if floor:
        n, where = floor
        seen = len(b.art.own_classes(where))
        if seen < n:
            return g.add(name, False,
                         "VACUOUS: %d own class(es) under %s, need >=%d before "
                         "an absence here means anything" % (seen, where, n),
                         weight, {"count": 0, "universe": seen, "floor": n,
                                  "where": where, "sites": []})
    return g.absent(name, hits, weight)


def uses_refs(g, b, name, prefixes, minimum=1, weight=1.0, own=False):
    """The replacement is actually on the classpath and actually referenced.

    Half of every removal gate. Without it, deleting the module that used the
    old API passes the removal and the step scores for work it did not do.
    """
    return g.present(name, b.art.refs_by_class(prefixes, own=own), minimum,
                     weight)


def uses_refs_beside(g, b, name, prefixes, beside, minimum=1, weight=1.0):
    """The replacement is referenced, and referenced beside the old form.

    `uses_refs` is the right shape when the gate is one half of a pair and the
    other half is a removal: whatever a candidate adds elsewhere, the old
    form's count still has to come down. Some idioms have no falling old form
    to pair against -- NIO.2 is one, because `File` and `Path` interoperate by
    design and a class that does its work through `Files` still hands a `File`
    to every library API that takes one -- and for those a present-only count
    is satisfied by two `Paths.get` lines in a class that never touched a file.

    `beside` does the job the removal half would have done, by asking *where*
    the new form landed: a class counts only when its own bytecode references
    both names, which is the signature of code that does filesystem work
    through the new API and meets the old one at a library boundary. A class
    that names only the new one is the dummy `Path`; a class that names only
    the old one has not been converted.

    Asked this way round rather than against a list of the classes that did
    the work in the tree the step was written from, because such a list is a
    fact about that tree and not a property of the candidate's: it cannot be
    printed in a ticket without naming the answer, cannot be recovered from
    the workspace by a candidate who tries, and refuses the candidate who moves
    the same work into a class of their own. This gate is the property.

    Inner classes count for their outer, so a `Path` in a class and a `File`
    in its anonymous inner class is one class doing both, and the same class
    converted in two places is still one.

    One release valve, for the candidate who goes further than the tree this
    was calibrated against: when *no* own class names the old form anywhere,
    there is nothing left for the new one to stand beside, and that absence
    carries the `beside` half instead. Without it the gate would fail a tree
    that had retired the old API completely, which is more than the thing it
    is asking for.

    Nothing and not `< minimum`, deliberately. The attack this gate exists to
    refuse -- two `Paths.get` lines in classes that never touched a file --
    leaves every class that did the filesystem work still naming the old form,
    so any threshold above zero would hand the attack a second way in on a
    small tree. At zero the valve costs strictly more work than the honest
    path: every last old-form reference has to go, including the ones at
    library boundaries the step does not ask anyone to move.
    """
    def by_outer(px):
        out = {}
        for cls, refs in b.art.refs_by_class(px, own=True).items():
            out.setdefault(cls.split("$", 1)[0], set()).update(refs)
        return out

    new, old = by_outer(prefixes), by_outer(beside)
    retired = not old
    hits = ["%s (%s)" % (k, ", ".join(sorted(v)[:4]))
            for k, v in sorted(new.items()) if retired or k in old]
    if retired:
        S.record("%s.beside-waived" % name,
                 "%d own class(es) reference %s" % (len(old), "/".join(beside)))
    return g.present(name, hits, minimum, weight)


def no_source_types(g, b, name, prefixes, weight=1.0):
    """No .java in the repository names these types.

    Both halves of how a type can be written: the import list, and any type use
    written with its package inline. A use of an imported simple name is
    invisible here by design -- the import that made it legal is not, and the
    bytecode scan sees the reference either way. This gate exists to point at
    the file to open, not to be the only witness.
    """
    sites = {}
    for f in b.ast.files():
        for im in f.get("imports") or []:
            nm = im.split(" ")[0].rstrip(".*").rstrip(".")
            if S.under(nm, prefixes):
                sites["%s (import %s)" % (f.get("p", "?"), nm)] = nm
    for u in b.ast.typeuses(prefixes):
        sites["%s:%s %s" % (u.get("p", "?"), u.get("line", 0),
                            u.get("n", "?"))] = u.get("n")
    return g.absent(name, sites, weight)


def no_nodes(g, b, name, node_type, weight=1.0, limit_to=None):
    """A construct class the step named is gone from the source.

    `limit_to` narrows to a set of repository-relative path prefixes, for the
    steps whose instruction scopes a construct to one area. Everything else is
    scanned.
    """
    sites = {}
    for n in b.ast.nodes(node_type):
        p = n.get("p", "?")
        if limit_to and not p.startswith(tuple(limit_to)):
            continue
        sites["%s:%s" % (p, n.get("line", 0))] = n
    return g.absent(name, sites, weight)


def nodes_at_most(g, b, name, node_type, limit, weight=1.0):
    """A construct the step thins out rather than eliminates.

    Used where zero is not the honest target -- a cast the language still
    requires, a `synchronized` block guarding something Java has no other word
    for. The limit is derived from the reference solution and recorded in
    the calling score.py next to the measurement it came from.
    """
    sites = sorted("%s:%s" % (n.get("p", "?"), n.get("line", 0))
                   for n in b.ast.nodes(node_type))
    ok = len(sites) <= limit
    detail = "%d site(s), allow <=%d" % (len(sites), limit)
    if not ok:
        detail += ": " + ", ".join(sites[:10])
    return g.add(name, ok, detail, weight,
                 {"count": len(sites), "limit": limit, "sites": sites[:200]})


def converted(g, b, name, new_types, old_type, minimum, old_max, weight=1.0,
              limit=8):
    """One construct replaced by another: the new form appears AND the old falls.

    A present-only gate is satisfied by writing the new form somewhere it was
    never needed -- four lambdas in a throwaway class pass a "lambda >= 4" gate
    over a tree where every anonymous class is still an anonymous class. The
    count that cannot be faked that way is the old form's, because nothing a
    candidate *adds* makes it go down.

    `old_max` is therefore the baseline count minus `minimum`: it says exactly
    "at least `minimum` of the sites that were there became the new form", and
    it is the weakest cap that closes the hole. `new_types` may name more than
    one node type where the instruction offers more than one answer -- a lambda
    and a method reference retire the same construct.
    """
    if isinstance(new_types, str):
        new_types = (new_types,)
    got = sorted("%s:%s" % (n.get("p", "?"), n.get("line", 0))
                 for t in new_types for n in b.ast.nodes(t))
    old = sorted("%s:%s" % (n.get("p", "?"), n.get("line", 0))
                 for n in b.ast.nodes(old_type))
    ok_new = len(got) >= minimum
    ok_old = len(old) <= old_max
    detail = ("%s %d (need >=%d), %s %d (allow <=%d)"
              % ("+".join(new_types), len(got), minimum,
                 old_type, len(old), old_max))
    if not ok_new and got:
        detail += "; have: " + ", ".join(got[:limit])
    if not ok_old:
        detail += "; left: " + ", ".join(old[:limit])
    return g.add(name, ok_new and ok_old, detail, weight,
                 {"count": len(got), "need": minimum,
                  "old": len(old), "old_limit": old_max,
                  "sites": got[:200], "old_sites": old[:200]})


def suppressions_at_most(g, b, limit, weight=1.0, kinds=None):
    """Warning suppressions, counted by site and by argument.

    A suppressed finding is a finding. One @SuppressWarnings on a
    class with four arguments is four suppressions at one site; consolidating
    four annotations into one does not reduce the count, which is the point.
    """
    pairs = []
    for s in b.ast.suppressions():
        args = s.get("args") or ["?"]
        for a in args:
            if kinds and a not in kinds:
                continue
            pairs.append("%s:%s(%s)" % (s.get("p", "?"), s.get("line", 0), a))
    pairs.sort()
    ok = len(pairs) <= limit
    detail = "%d suppression(s) at %d site(s), allow <=%d" % (
        len(pairs), len(set(p.rsplit("(", 1)[0] for p in pairs)), limit)
    if not ok:
        detail += ": " + ", ".join(pairs[:10])
    return g.add("suppressions", ok, detail, weight,
                 {"count": len(pairs), "limit": limit, "sites": pairs[:200]})


def parses_clean(g, b, weight=1.0):
    """Every .java in the tree parsed.

    Not a style check. A file the scanner could not read contributes zero to
    every other gate on this page, so a tree that does not parse would look
    maximally migrated. This gate is what stops that reading.
    """
    errs = b.ast.parse_errors()
    sites = sorted("%s: %s" % (e.get("p", "?"), (e.get("msg") or "")[:80])
                   for e in errs)
    return g.absent("source-parses", sites, weight)


def sources_survive(g, b, min_files, min_types, weight=1.0):
    """The tree the step was handed is still there.

    "Preserve the supplied source" is a requirement of every step and, left
    unmeasured, it is the requirement every cap gate quietly pays for: a cap on
    plain `try` statements or on anonymous classes counts occurrences, and
    deleting the file they live in lowers the count exactly as converting them
    does. This gate is what makes deletion the more expensive of the two.

    Counted in files and in declared types, because they fail differently --
    emptying a file keeps the file and loses the types, and both floors sit
    just under the reference solution's own numbers so that a judgement call
    about one file is not a failure while clearing the tree is.
    """
    files = len(b.ast.files())
    types = len(b.ast.kind("type"))
    ok = files >= min_files and types >= min_types
    detail = ("%d source file(s) (need >=%d), %d declared type(s) (need >=%d)"
              % (files, min_files, types, min_types))
    return g.add("sources-survive", ok, detail, weight,
                 {"files": files, "files_min": min_files,
                  "types": types, "types_min": min_types})


def no_files(g, b, name, names=(), suffixes=(), weight=1.0, where=None):
    """A file class the step retires -- web.xml, an XML bean definition.

    Located by walking the tree, so a descriptor moved to another module is
    still found. `where` narrows to a subtree when the step scopes it.
    """
    root = where or b.app
    hits = [os.path.relpath(p, b.app)
            for p in S.find_files(root, names=set(names) or None,
                                  suffixes=tuple(suffixes) or None)]
    return g.absent(name, hits, weight)


def _xml_root(path):
    """(namespace, local-name) of an XML document's root, or (None, error)."""
    import xml.etree.ElementTree as ET
    try:
        tag = ET.parse(path).getroot().tag
    except Exception as e:                              # noqa: BLE001
        return None, str(e)[:100]
    if tag.startswith("{"):
        ns, _, local = tag[1:].partition("}")
        return (ns, local), None
    return ("", tag), None


# ---------------------------------------------------------------------------
# the build's own configuration
#
# Two questions the artifact cannot answer. What release a class file was
# compiled for is in its header, but *how* the build asked for it is not: `-source
# 11 -target 11` and `--release 11` produce byte-identical class files on a
# JDK 11 toolchain and differ only in whether the JDK's own API surface was
# restricted. Likewise, which parent pom a module inherits leaves no trace in
# anything it compiles. Both are read by parsing every pom -- never by grepping
# one, because a coordinate can arrive through a property and a commented-out
# block is not a declaration.
# ---------------------------------------------------------------------------

def _local(tag):
    return tag.rpartition("}")[2]


def _pom_docs(b):
    """Every pom.xml in the tree, as (relative path, root element or None, text).

    The raw text comes back alongside the parse so that a gate can decide for
    itself what an unparsable pom means -- the same problem xml_descriptors
    has, and it is not the same answer for every gate. See no_parent and
    compiler_release, which resolve it in opposite directions and say why.
    """
    import xml.etree.ElementTree as ET
    out = []
    for p in S.find_files(b.app, names={"pom.xml"}):
        rel = os.path.relpath(p, b.app)
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read(1 << 20)
        except (IOError, OSError) as e:
            out.append((rel, None, "!unreadable: %s" % e))
            continue
        try:
            out.append((rel, ET.parse(p).getroot(), text))
        except Exception as e:                          # noqa: BLE001
            out.append((rel, None, text))
            S.record("pom.unparsable", "%s: %s" % (rel, str(e)[:100]))
    return out


def _pom_properties(docs):
    """Every <properties> entry in the reactor, merged, name -> set of values.

    Merged across modules rather than resolved per-module on purpose: a child
    inherits its parent's properties, the parent may be any pom in the tree,
    and working out the real inheritance would mean reimplementing Maven. The
    cost of the merge is that an unused value from another module can satisfy a
    lookup; every gate below is written so that this can only make it stricter,
    never more permissive.
    """
    props = {}
    for _rel, root, _text in docs:
        if root is None:
            continue
        for el in root.iter():
            if _local(el.tag) != "properties":
                continue
            for kid in el:
                txt = (kid.text or "").strip()
                if txt:
                    props.setdefault(_local(kid.tag), set()).add(txt)
    return props


def _resolve(text, props, depth=3):
    """Expand ${...} against the merged property map, as far as it goes.

    A value that stays unexpanded is returned as written, so a gate reports
    `${javaVersion}` rather than silently treating it as absent.
    """
    seen = set()
    for _ in range(depth):
        m = re.fullmatch(r"\$\{([^}]+)\}", text.strip())
        if not m or m.group(1) in seen:
            return text.strip()
        seen.add(m.group(1))
        vals = props.get(m.group(1))
        if not vals or len(vals) != 1:
            return text.strip()
        text = next(iter(vals))
    return text.strip()


def no_parent(g, b, name, coords, weight=1.0):
    """No module in the reactor inherits from one of these parent poms.

    `coords` are "groupId:artifactId" strings. The version is reported but not
    matched: a step that retires `org.sonatype.oss:oss-parent` retires every
    version of it.

    A pom that does not parse is reported only when its raw bytes still name
    one of the coordinates under test -- xml_descriptors' rule, for the same
    reason: a parent is not retired by breaking the pom's syntax, and a gate
    that silently drops what it cannot read rewards exactly that. Broken poms
    that name none of them are somebody else's problem, and the build gate is
    where they surface.
    """
    docs = _pom_docs(b)
    props = _pom_properties(docs)
    want = set(coords)
    hits = {}
    for rel, root, text in docs:
        if root is None:
            for ga in want:
                gid, _, aid = ga.partition(":")
                if gid in text and aid in text:
                    hits["%s (unparsable, text names %s)" % (rel, ga)] = True
            continue
        for el in root:
            if _local(el.tag) != "parent":
                continue
            got = {}
            for kid in el:
                got[_local(kid.tag)] = _resolve(kid.text or "", props)
            ga = "%s:%s" % (got.get("groupId", "?"), got.get("artifactId", "?"))
            if ga in want:
                hits["%s (<parent> %s:%s)"
                     % (rel, ga, got.get("version", "?"))] = True
    return g.absent(name, hits, weight)


RELEASE_TAGS = ("release", "maven.compiler.release")
LEGACY_TAGS = ("source", "target", "maven.compiler.source",
               "maven.compiler.target")

# What a Java release looks like written down: 6, 11, 21, or the 1.x spelling.
# `<source>` and `<target>` are not reserved words -- build-helper's add-source
# takes `<source>src/gen/java</source>` and antrun's whole Ant script lives
# inside a `<target>` -- so a bare element with either name is only compiler
# configuration when its value is release-shaped. Without this, a tree that
# added a generated-source directory would fail `compiler-release` for a reason
# the instruction never stated.
RELEASE_VALUE = re.compile(r"^(?:1\.[0-9]|[0-9]{1,2})$")


def compiler_release(g, b, want, weight=1.0, name="compiler-release"):
    """The build asks javac for `--release <want>`, and nothing asks for less.

    Both halves matter and they fail differently. Without the first, a tree
    that only bumped `<source>`/`<target>` passes -- and on a JDK 11 toolchain
    that compiles against JDK 11's *whole* API, so the same source would stop
    compiling the moment the toolchain moved, which is the failure `--release`
    exists to prevent. Without the second, a leftover `<source>1.6</source>` in
    a module nobody looked at passes because some other module named the right
    release.

    This is configuration, and configuration is the weaker witness: it is asked
    next to class_release, which reads the class-file headers the build
    actually produced. Neither is the only evidence for the rung.

    A pom that does not parse fails this gate outright, which is the opposite
    of what no_parent does with one and is deliberate. no_parent makes a
    negative claim, and a text search is enough to refute it. This one makes a
    positive claim about the whole reactor, and a pom nobody can read is a
    module whose compiler configuration is unknown -- "I could not tell" is a
    failure of a positive claim, not a pass. Recording "unparsable" as if it
    were a release value would let a broken pom satisfy it.
    """
    docs = _pom_docs(b)
    props = _pom_properties(docs)
    rel, old, broken = {}, {}, []
    for relpath, root, _text in docs:
        if root is None:
            broken.append(relpath)
            continue
        for el in root.iter():
            tag = _local(el.tag)
            txt = (el.text or "").strip()
            if not txt:
                continue
            if tag in RELEASE_TAGS:
                rel.setdefault(_resolve(txt, props), set()).add(relpath)
            elif tag in LEGACY_TAGS:
                val = _resolve(txt, props)
                if RELEASE_VALUE.match(val):
                    old.setdefault(val, set()).add(relpath)
    stale = {v: sorted(w) for v, w in old.items() if v != want}
    ok = want in rel and not stale and not broken
    detail = "release=%s legacy source/target=%s, want release %s" % (
        ",".join(sorted(rel)) or "none",
        ",".join(sorted(old)) or "none", want)
    if stale:
        detail += "; stale in " + ", ".join(
            "%s (%s)" % (v, ";".join(w[:3])) for v, w in sorted(stale.items()))
    if broken:
        detail += "; unparsable: " + ", ".join(sorted(broken)[:4])
    return g.add(name, ok, detail, weight,
                 {"release": {k: sorted(v) for k, v in rel.items()},
                  "legacy": {k: sorted(v) for k, v in old.items()},
                  "unparsable": sorted(broken), "want": want,
                  "poms": len(docs)})


def xml_descriptors(g, b, name, roots=(), namespaces=(), weight=1.0,
                    where=None):
    """No XML document in the tree *is* one of these descriptors.

    The companion to no_files, and the one to prefer wherever the thing being
    retired is a document rather than a filename. `web.xml` is always called
    web.xml, but a Spring bean definition is whatever the person who wrote it
    decided to call it, so a gate that matches on the name retires the
    convention and not the construct. This one parses every .xml the tree ships
    and asks what the root element declares -- `{...schema/beans}beans`,
    `{...}web-app` -- which is the same question the framework asks at runtime.

    Unparsable XML is reported rather than skipped, but only when its raw text
    still names one of the namespaces under test: a descriptor is not retired
    by breaking its syntax, and a gate that silently drops what it cannot read
    rewards exactly that. Broken XML elsewhere in the tree is not this gate's
    business.
    """
    base = where or b.app
    hits = {}
    for p in S.find_files(base, suffixes=(".xml",)):
        rel = os.path.relpath(p, b.app)
        got, err = _xml_root(p)
        if got is None:
            try:
                with open(p, "r", encoding="utf-8", errors="replace") as fh:
                    text = fh.read(65536)
            except (IOError, OSError):
                text = ""
            if any(n in text for n in namespaces):
                hits["%s (unparsable: %s)" % (rel, err)] = True
            continue
        ns, local = got
        if local in roots or any(ns.startswith(n) for n in namespaces):
            hits["%s (<%s> in %s)" % (rel, local, ns or "no namespace")] = True
    return g.absent(name, hits, weight)


# ---------------------------------------------------------------------------
# probes: questions the tree cannot be asked, only the build or the running
# process can
# ---------------------------------------------------------------------------

def _unpriv_env():
    """The environment an unprivileged build needs, as helpers.sh sets it.

    score_lib.run's unprivileged branch prefixes `env`, which *adds* to the
    inherited environment rather than replacing it -- so without this the
    build would run as nobody with root's HOME and fail creating ~/.m2, which
    reads exactly like a candidate whose build is broken.
    """
    return {"HOME": "/score/home", "TMPDIR": "/score/tmp",
            "JAVA_HOME": os.environ.get("JAVA_HOME", ""),
            "PATH": os.environ.get("PATH", ""),
            "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8", "TZ": "UTC",
            "MAVEN_OPTS": "-Xmx3g -Djava.awt.headless=true"}


WERROR_PROBE = """package probe.werror;

import java.util.ArrayList;
import java.util.List;

/** Written by the verifier. Compiles with warnings and without errors. */
public final class WErrorProbe {
    private WErrorProbe() {
    }

    @SuppressWarnings("unused")
    public static List<String> probe() {
        List raw = new ArrayList();
        raw.add("probe");
        return raw;
    }
}
"""


def build_rejects(g, b, name, rel_path, source, weight=1.0, goal="compile",
                  timeout=1500):
    """Inject a fault into a copy of the tree and require the build to reject it.

    The only honest way to ask whether a build enforces something. A pom can
    say `-Werror` in a profile that is never active, in a pluginManagement
    block no module inherits, or next to an `-Xlint:none` that makes it vacuous
    -- and all three read identically to any gate that inspects configuration.
    This one asks the compiler.

    `source` must compile cleanly under a permissive configuration and produce
    warnings under a strict one; WERROR_PROBE above is that file for the
    rawtypes/unchecked pair. That first half is load-bearing: an injected
    file with a syntax error would fail every build and this gate would pass
    for every tree. What licenses the conclusion is the paired control -- the
    same probe against a `-Xlint:none` build, which must come back rc=0.

    Three things have to be true, and the third is the one that matters:

      * the untouched tree built (b.build_rc == 0), so a tree that was already
        broken cannot pass by failing again;
      * the probe build failed;
      * **the failure names the probe file.** Without that, a build that fell
        over on an unrelated resolution error would score as a build that
        enforces warnings. Success has to show up in a number that has to
        change, and the number here is how many of the injected copies the
        compiler actually objected to.
    """
    if b.build_rc != 0:
        return g.add(name, False, "clean tree did not build (rc=%s); "
                     "probe would not be meaningful" % b.build_rc, weight,
                     {"build_rc": b.build_rc})

    probe_dir = "/score/probe"
    rc, _ = S.run(["sh", "-c",
                   "rm -rf %s && cp -a /app %s && chown -R %d:%d %s"
                   % (probe_dir, probe_dir, S.UID, S.GID, probe_dir)],
                  log="probe-copy.log", unprivileged=False, timeout=600)
    if rc != 0:
        return g.add(name, False, "could not copy /app (rc=%s)" % rc, weight)

    roots = []
    for base, dirs, _files in os.walk(probe_dir):
        dirs[:] = sorted(d for d in dirs if d not in ("target", ".git"))
        if base.endswith(os.path.join("src", "main", "java")):
            roots.append(base)
    if not roots:
        return g.add(name, False, "no src/main/java in the tree", weight)

    for r in sorted(roots):
        dest = os.path.join(r, rel_path)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "w", encoding="utf-8") as fh:
            fh.write(source)
        os.chown(dest, S.UID, S.GID)

    # --fail-at-end, so the count below means something. Maven's default stops
    # the reactor at the first failed module, and one module is then the most
    # the compiler can ever object to however many were injected. Modules that
    # depend on a failed one are still skipped, which is why the pass condition
    # is "at least one", not "all" -- the two numbers are both reported.
    m2 = os.environ.get("M2_REPO", "/opt/m2")
    rc, out = S.run(["mvn", "-B", "-ntp", "--fail-at-end",
                     "-Dmaven.repo.local=" + m2,
                     "-Dstyle.color=never", "-DskipTests", goal],
                    cwd=probe_dir, env=_unpriv_env(), log="probe-build.log",
                    timeout=timeout)

    leaf = os.path.basename(rel_path)
    blamed = set()
    for ln in out.splitlines():
        if leaf not in ln:
            continue
        if "ERROR" in ln or "error:" in ln or "] error" in ln:
            frag = ln.split(leaf)[0]
            blamed.add(frag[frag.find(probe_dir):].strip(" [(") or "?")
    ok = rc != 0 and bool(blamed)
    detail = ("probe build rc=%s, %d of %d injected copies named in the "
              "failure" % (rc, len(blamed), len(roots)))
    if rc == 0:
        detail += "; the build accepted " + leaf
    elif not blamed:
        detail += "; build failed but not because of " + leaf
    return g.add(name, ok, detail, weight,
                 {"rc": rc, "roots": len(roots), "blamed": sorted(blamed)[:20],
                  "tail": out.splitlines()[-25:]})


def take_thread_dump(pid, json_path, print_path):
    """Ask a live JVM what its threads are. Called from a step's probe.sh.

    It has to happen while the server that just answered the behaviour suite is
    still running, which is the one thing score.py cannot do -- by the time the
    gates run, run_behaviour.sh's EXIT trap has killed every instance. So the
    probe takes the dumps and threads_in reads them back.

    Two views, because neither alone answers the question:

      Thread.dump_to_file -format=json   every thread, virtual ones included
      Thread.print                       platform threads only

    The difference between them *is* the set of virtual threads, on every JDK,
    including the ones whose JSON carries no flag to read. See threads_in.
    """
    # As nobody, with the launcher's TMPDIR: HotSpot's attach mechanism checks
    # the peer's credentials against the target's, and the server runs as
    # nobody, so a root jcmd is refused. It also locates the target through
    # hsperfdata under java.io.tmpdir, which launch.sh set to /score/tmp.
    #
    # The target JVM writes the JSON dump itself, as nobody, so it goes to a
    # file under /score/work and root takes it into json_path by reading it as
    # nobody (helpers.sh, copy_as_unprivileged): root's evidence directory is
    # not writable by the server, and a symlink in the dump's place reaches
    # nothing the server could not already read.
    work_json = "/score/work/thread-dump.json"
    S.run(["rm", "-f", work_json], env=_unpriv_env(), timeout=60)
    rc1, _ = S.run(["jcmd", str(pid), "Thread.dump_to_file", "-overwrite",
                    "-format=json", work_json], log="jcmd-json.log",
                   env=_unpriv_env(), timeout=120)
    rc2, out = S.run(["jcmd", str(pid), "Thread.print"], log="jcmd-print.log",
                     env=_unpriv_env(), timeout=120)
    rc3, dump = S.run(["cat", "--", work_json], env=_unpriv_env(), timeout=120)
    if rc1 == 0 and rc3 == 0:
        with open(json_path, "w", encoding="utf-8") as fh:
            fh.write(dump)
    if rc2 == 0:
        with open(print_path, "w", encoding="utf-8") as fh:
            fh.write(out)
    S.record("thread-dump", "pid=%s json=%s print=%s" % (pid, rc1, rc2))
    return rc1 == 0 and rc2 == 0 and os.path.exists(json_path)


PLATFORM_LINE = re.compile(r'^"(?P<name>.*)"\s+#(?P<tid>\d+)\b')


def threads_in(json_path, print_path=None):
    """A recorded pair of thread dumps as a list of thread records.

    Each record is {"tid", "name", "container", "virtual", "stack"}. Names are
    not keys: several virtual threads are legitimately unnamed, and a dict
    would silently merge them into one. Nor are names evidence of what a thread
    is doing -- measured on this ladder's own tree, JGit's `InterruptTimer`
    names a *platform* helper after the command it is timing, and
    `BaseCommand$TaskThunk.run` renames whatever thread it is on for the
    duration of the command -- so `stack` is carried too, as the frame strings
    the dump prints, and `threads_by_frames` asks about those.

    Telling virtual from platform is the whole difficulty, and the JSON alone
    does not do it on every JDK:

      * From JDK 24 or so each thread carries `"virtual": true`. Read it.
      * On JDK 21 -- which this ladder's last two rungs pin -- there is no such
        key, and the obvious substitute is wrong. Threads there are grouped
        into containers, but the non-root containers are not the virtual ones:
        a dump of a plain virtual-thread executor also shows
        `ForkJoinPool-1/...SharedThreadContainer` holding the *carrier*
        threads, a `ScheduledThreadPoolExecutor` holding `VirtualThread-
        unparker`, and a `ThreadPoolExecutor` -- all platform. Classifying by
        container would have reported the carriers as virtual, which is the
        opposite of what the rung is about.

    So the fallback is the set difference: Thread.print lists exactly the
    platform threads, by the same thread id the JSON uses, and a thread in the
    JSON that Thread.print does not mention is virtual. Rehearsed against a
    process with a known mix on both 21 and 25; the two methods agree on 25,
    where both are available.

    With neither a flag nor a Thread.print to difference against, this returns
    nothing rather than guessing. A gate reading a misclassification is worse
    than a gate reading a blank.
    """
    doc = _json(json_path, {})
    recs = []
    for c in (doc.get("threadDump") or {}).get("threadContainers") or []:
        for t in c.get("threads") or []:
            recs.append({"tid": str(t.get("tid", "")),
                         "name": t.get("name") or "",
                         "container": c.get("container") or "",
                         "virtual": t.get("virtual"),
                         "stack": list(t.get("stack") or [])})
    if not recs:
        return []
    if any(r["virtual"] is not None for r in recs):
        for r in recs:
            r["virtual"] = bool(r["virtual"])
        return recs

    platform = set()
    for ln in _lines(print_path or ""):
        m = PLATFORM_LINE.match(ln)
        if m:
            platform.add(m.group("tid"))
    if not platform:
        return []
    for r in recs:
        r["virtual"] = r["tid"] not in platform
    return recs


def virtual_threads(g, b, name, minimum=1, weight=1.0, dump="threads.json",
                    prints="threads.txt"):
    """The server that answered the suite was running virtual threads.

    Deliberately weak on its own -- it asks that at least `minimum` of the
    threads alive at the end of the suite were virtual, and says how the mix
    came out. The rung that needs a stricter reading adds it next to this one
    with a number taken from its own reference solution.
    """
    recs = threads_in(os.path.join(b.out, dump), os.path.join(b.out, prints))
    if not recs:
        return g.add(name, False, "no usable thread dump", weight, {})
    v = [r for r in recs if r["virtual"]]
    p = [r for r in recs if not r["virtual"]]
    ok = len(v) >= minimum
    detail = "%d virtual / %d platform (need >=%d virtual)" % (
        len(v), len(p), minimum)
    return g.add(name, ok, detail, weight,
                 {"virtual": len(v), "platform": len(p), "need": minimum,
                  "virtual_names": sorted(set(r["name"] for r in v))[:40],
                  "platform_names": sorted(set(r["name"] for r in p))[:60]})


def on_stack(rec, groups):
    """Does this thread's stack show every one of `groups`?

    `groups` is a sequence of frame-prefix tuples and reads as an AND of ORs:
    the thread matches when, for every group, at least one of its frames starts
    with one of that group's prefixes. Two groups is usually the right shape --
    "this project's own code" and "the library it called" -- because either
    alone is reachable from somewhere else. A dump frame is a plain string,
    `com.google.gerrit.sshd.AbstractGitCommand.service(AbstractGitCommand.java:113)`,
    with JDK frames carrying a `java.base/` module prefix, so a prefix test on
    the frame string is a test on the declaring class.
    """
    for group in groups:
        if not any(f.startswith(pfx) for f in rec.get("stack") or ()
                   for pfx in group):
            return False
    return True


def threads_by_frames(g, b, name, groups, kind="virtual", minimum=None,
                      maximum=None, total_min=1, weight=1.0,
                      dump="threads.json", prints="threads.txt", doing=""):
    """How many threads of one kind were doing a particular thing, live.

    The instrument is a pair of thread dumps a step's probe.sh took while the
    server was still answering, and the question is about work in progress, so
    the load that puts the work in progress is the probe's job and the
    classification is this function's.

    `kind` is "virtual" or "platform"; `minimum` and `maximum` bound how many
    threads of that kind were inside `groups` (see on_stack). Either may be
    None.

    `total_min` is what keeps an absence row honest. `maximum=0` over platform
    threads is satisfied both by a server that moved the work onto virtual
    threads and by a server that was not doing the work at all -- a dead sshd,
    a probe whose load never connected -- and those two must not read the same.
    So the row also requires `total_min` matching threads of *either* kind: at
    least that much of the work has to have been in flight for the absence to
    mean anything. A missing or unreadable dump fails for the same reason,
    rather than passing vacuously.
    """
    recs = threads_in(os.path.join(b.out, dump), os.path.join(b.out, prints))
    what = doing or name
    if not recs:
        return g.add(name, False, "no usable thread dump", weight, {})
    hits = [r for r in recs if on_stack(r, groups)]
    virt = [r for r in hits if r["virtual"]]
    plat = [r for r in hits if not r["virtual"]]
    mine = virt if kind == "virtual" else plat
    ok = len(hits) >= total_min
    why = []
    if not ok:
        why.append("no thread of either kind was %s" % what)
    if minimum is not None and len(mine) < minimum:
        ok = False
        why.append("%d %s thread(s) %s, need >=%d" % (len(mine), kind, what,
                                                      minimum))
    if maximum is not None and len(mine) > maximum:
        ok = False
        why.append("%d %s thread(s) %s, allowed <=%d" % (len(mine), kind, what,
                                                         maximum))
    detail = ("%d virtual and %d platform thread(s) %s"
              % (len(virt), len(plat), what))
    if why:
        detail += "; " + "; ".join(why)
    return g.add(name, ok, detail, weight,
                 {"virtual": len(virt), "platform": len(plat),
                  "kind": kind, "min": minimum, "max": maximum,
                  "total_min": total_min, "threads": len(recs),
                  "virtual_names": sorted(set(r["name"] for r in virt))[:20],
                  "platform_names": sorted(set(r["name"] for r in plat))[:20],
                  "frames": [(r["name"], (r["stack"] or [])[:6])
                             for r in hits[:4]]})


DEP = re.compile(r"^\s*([\w.\-]+):([\w.\-]+):([\w.\-]+):([\w.\-]+)")


def deps(b):
    """The resolved dependency graph as (group, artifact, version) triples.

    Asked of Maven in the scorer's own invocation, because a version that
    arrives through dependency management or a bom does not appear in any pom
    the candidate wrote, and it is the resolved one that is on the classpath.
    """
    out = set()
    for ln in b.deps:
        m = DEP.match(ln)
        if m:
            out.add((m.group(1), m.group(2), m.group(4)))
    return out


# A coordinate with its scope on the end, as `dependency:list` prints one.
# Five colon-separated fields, or six when the artifact has a classifier.
SCOPED_DEP = re.compile(r"^[\w.\-]+(?::[\w.\-]+){4,5}$")


def scoped_deps(b):
    """The resolved dependency graph as (group, artifact, version, scope).

    `deps` answers what is on the classpath. This answers at what scope, which
    is the only thing separating a servlet API the container provides from one
    the deployable ships -- two declarations with identical coordinates.

    Maven prints the coordinate two ways, `group:artifact:type:version:scope`
    and, where there is a classifier, `group:artifact:type:classifier:version:
    scope`. Counted from the left those put the version in different fields;
    counted from the right they do not, so the version is read as the
    second-to-last field and the scope as the last, which is the one reading
    that is true of both. The coordinate is picked out of the line by shape
    rather than by position, so a `[INFO]` prefix or a trailing `-- module ...`
    changes nothing.

    A second reader rather than a wider `DEP`: every other dependency gate in
    every step is built on `deps`, and giving that regex a fifth group would
    change what all of them mean.
    """
    out = set()
    for ln in b.deps:
        for tok in ln.split():
            if SCOPED_DEP.match(tok):
                f = tok.split(":")
                out.add((f[0], f[1], f[-2], f[-1]))
                break
    return out


def _deps_unusable(g, name, b, weight):
    """Fail a dependency gate outright when the graph was never resolved.

    The failure mode this exists for is silent and one-directional: when
    `dependency:list` stops early, `b.deps` is short rather than empty, and a
    gate asking "is junit on the classpath anywhere" finds nothing and passes.
    On this tree a standalone listing leaves five of seven modules unread, and
    the two absent-gates would report a clean pass.

    So the rc is checked before the question is asked, and an unresolved graph
    is a failed gate and not a passed one. A candidate reaching this has
    already failed build-and-artifact -- scan.sh resolves in the same session
    that packages -- so this never converts a working tree into a low score.
    """
    return g.add(name, False,
                 "dependency graph not resolved (dependency:list rc=%s, "
                 "%d line(s))" % (b.deps_rc, len(b.deps)), weight,
                 {"deps_rc": b.deps_rc, "deps_lines": len(b.deps)})


def dep_absent(g, b, name, group_prefixes=(), artifact_prefixes=(),
               weight=1.0):
    if b.deps_rc != 0:
        return _deps_unusable(g, name, b, weight)
    hits = sorted("%s:%s:%s" % t for t in deps(b)
                  if t[0].startswith(tuple(group_prefixes) or ("\0",))
                  or t[1].startswith(tuple(artifact_prefixes) or ("\0",)))
    return g.absent(name, hits, weight)


def dep_present(g, b, name, group_prefixes=(), artifact_prefixes=(),
                minimum=1, weight=1.0, version_re=None, require_artifacts=()):
    """The replacement resolves -- in numbers, and where the step says by name.

    `minimum` asks how many coordinates matched and `require_artifacts` asks
    which. Most steps want the count and nothing else: a library arrives as
    however many jars its own packaging decided on, and naming them would be a
    fact about one release rather than a property of the port. The exception is
    the step whose sentence names a capability that lives in one particular
    jar -- "enough of them to stand up a web application context" is not
    answered by three artifacts under the same groupId that the tree could have
    resolved without ever taking the capability. There the artifactId is the
    question and the count is only the floor under it.

    The two are ANDed, and each named artifactId has to appear among the same
    matched coordinates as everything else -- so a required artifact resolved
    off the version the gate was given is missing here rather than present. A
    failure names which one, because "one short of three" and "three of the
    wrong three" are different repairs. The requirement is journalled whether
    it holds or not, so a passing run still records what was asked.
    """
    if b.deps_rc != 0:
        return _deps_unusable(g, name, b, weight)
    hits = []
    for gid, aid, ver in sorted(deps(b)):
        if not (gid.startswith(tuple(group_prefixes) or ("\0",))
                or aid.startswith(tuple(artifact_prefixes) or ("\0",))):
            continue
        if version_re and not re.match(version_re, ver):
            continue
        hits.append("%s:%s:%s" % (gid, aid, ver))
    if not require_artifacts:
        return g.present(name, hits, minimum, weight)
    resolved = {h.split(":")[1] for h in hits}
    missing = [a for a in require_artifacts if a not in resolved]
    S.record("%s.required" % name,
             "%s -> %s" % (", ".join(require_artifacts),
                           "missing " + ", ".join(missing) if missing
                           else "all present"))
    if missing:
        return g.add(name, False,
                     "%d site(s), need >=%d, and %d required artifact(s) "
                     "absent: %s" % (len(hits), minimum, len(missing),
                                     ", ".join(missing)),
                     weight,
                     {"count": len(hits), "need": minimum,
                      "required": list(require_artifacts), "missing": missing,
                      "sites": hits[:200]})
    return g.present(name, hits, minimum, weight)


def jdk_internals(g, b, weight=1.0):
    """Nothing reaches into a JDK internal API.

    `jdeps --jdk-internals` over the same roots the artifact scan used. A step
    that raises the Java baseline is exactly where a sun.* call stops being a
    warning and starts being a link error, so this is asked from step 1 on.
    """
    roots = _lines(os.path.join(b.out, "scan-roots.txt"))
    roots = [r for r in roots if os.path.isdir(os.path.join(r, "classes"))]
    if not roots:
        return g.add("no-jdk-internals", False, "no compiled classes", weight)
    rc, out = S.run(["jdeps", "--multi-release", "base", "--jdk-internals"]
                    + [os.path.join(r, "classes") for r in roots],
                    log="jdeps.log", unprivileged=False)
    bad = [ln.strip() for ln in out.splitlines()
           if "->" in ln and re.search(r"->\s+(sun|jdk\.internal|com\.sun)\.",
                                       ln)]
    return g.absent("no-jdk-internals", sorted(set(bad)), weight)
