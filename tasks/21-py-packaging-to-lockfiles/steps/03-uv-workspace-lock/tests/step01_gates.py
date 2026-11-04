#!/usr/bin/env python3
"""Step 01 gates: is this a PEP 517/518 build of the bento distribution?

Emits one tab-separated `family<TAB>label<TAB>ok<TAB>detail` line per check, which
`test.sh` turns into a `record` call. Nothing here executes candidate code: the
tree is read, the two archives are opened as archives, and the metadata is
parsed. The one subprocess started from here runs the *pinned* tree's parser
over the candidate's `bento.info` with the candidate's directory off `sys.path`,
so it stays a read too. It runs as root with the candidate's tree as its working
directory, so the interpreter is started with `-P`: without it `python -c` puts
that directory first on `sys.path`, and the snippet's own `import json` -- which
runs before the snippet can take the directory off the path -- would import a
`json.py` the candidate left at the repository root, as root. The parts that
must *run* something -- `python -m build`, `validate-pyproject`, `twine check`,
the venv -- are driven from `test.sh`, which runs them unprivileged.

Every expectation about what the metadata should say is read from
`/tests/data/meta-expected.json`, which `lib/record_meta.py` produces by asking
the *pinned tree's own* generators what the distribution declares about itself
(run against `data/pristine.tar.gz` it reproduces the shipped file byte for
byte). None of it is transcribed here.

The one property that is not scored against that recording is
`wheel.declared-packages`: what a distribution is made of is a thing later
milestones move, so the list is re-read from the tree in front of us with the
pinned parser (`declared_library`) rather than frozen. The instrument is fixed;
the input is the candidate's.
"""
import configparser
import email.utils
import json
import os
import platform
import re
import subprocess
import sys

sys.path.insert(0, "/tests/lib")
import gatelib  # noqa: E402

try:
    import tomllib
except ImportError:                                  # pragma: no cover
    tomllib = None

ROWS = []


def row(family, label, ok, detail=""):
    ROWS.append((family, label, "1" if ok else "0", str(detail).replace("\t", " ")[:300]))


def flush():
    for family, label, ok, detail in ROWS:
        sys.stdout.write("\t".join((family, label, ok, detail)) + "\n")


# --------------------------------------------------------------------------
# the declaration
# --------------------------------------------------------------------------

def read_pyproject(tree):
    """(data, note). `data` is None when there is nothing usable to read."""
    path = os.path.join(tree, "pyproject.toml")
    if not os.path.isfile(path):
        return None, "pyproject.toml not found at the repository root"
    with open(path, "rb") as fh:
        raw = fh.read()
    try:
        return tomllib.loads(raw.decode("utf-8")), "%d bytes" % len(raw)
    except Exception as exc:
        return None, "%s: %s" % (type(exc).__name__, exc)


def gate_declaration(tree, data, note):
    """The five things that make a tree PEP 517-buildable at all.

    Half of the precondition; `gate_migrated` is the other half. See
    `main`'s `precondition` mode for why the build is not attempted when
    either half fails.
    """
    row("01", "pyproject.present", data is not None or "not found" not in note,
        note)
    row("01", "pyproject.parses", data is not None, note)
    table = (data or {}).get("build-system") or {}
    requires = table.get("requires")
    ok_requires = isinstance(requires, list) and bool(requires) and \
        all(isinstance(x, str) and x.strip() for x in requires)
    row("01", "build-system.requires", ok_requires, repr(requires)[:200])
    backend = table.get("build-backend")
    ok_backend = isinstance(backend, str) and bool(backend.strip())
    row("01", "build-system.backend", ok_backend, repr(backend))

    # The build must not need the distribution it is building. `setup.py` was
    # `from bento.distutils.monkey_patch import setup`, so bento built itself;
    # a PEP 517 build declares a backend that "exists independently of this
    # project", which rules out three arrangements rather than one:
    #
    #   * naming the distribution itself in `requires` or as the backend;
    #   * `backend-path`, which is precisely PEP 517's in-tree backend hook --
    #     a backend shipped inside the tree being built cannot have existed
    #     before it; and
    #   * a `build-backend` whose top-level module is a file or a directory at
    #     the repository root, which is the same thing declared without the
    #     hook: the backend resolves to the tree because the tree is on the
    #     path the frontend hands the backend.
    #
    # Without the last two, a tree that keeps computing its metadata by running
    # `bento.core` -- from a backend module sitting next to `pyproject.toml` --
    # is a PEP 517 build of exactly the thing this step replaces.
    self_named = []
    if ok_requires:
        for spec in requires:
            name = re.split(r"[\s\[<>=!~;(]", spec.strip(), 1)[0].lower()
            if name.replace("_", "-") == "bento":
                self_named.append("requires names the distribution itself: %s" % spec)
    if ok_backend and backend.split(".", 1)[0].split(":", 1)[0] == "bento":
        self_named.append("build-backend names the distribution itself: %s" % backend)
    if "backend-path" in table:
        self_named.append("backend-path declares an in-tree backend: %r"
                          % (table.get("backend-path"),))
    if ok_backend:
        top = backend.split(".", 1)[0].split(":", 1)[0]
        in_tree = []
        if os.path.isfile(os.path.join(tree, top + ".py")):
            in_tree.append(top + ".py")
        if os.path.isfile(os.path.join(tree, top, "__init__.py")):
            in_tree.append(top + "/__init__.py")
        elif os.path.isdir(os.path.join(tree, top)):
            in_tree.append(top + "/")
        if in_tree:
            self_named.append("build-backend %r resolves inside the tree: %s"
                              % (backend, ", ".join(in_tree)))
    row("01", "build-system.not-self", ok_backend and ok_requires and not self_named,
        "; ".join(self_named)[:290] if self_named else "no self-reference")
    return ok_requires and ok_backend and not self_named


# --------------------------------------------------------------------------
# migrated-ness
# --------------------------------------------------------------------------

def _canonical_name(value):
    """PEP 503 name comparison, so `Bento` and `bento` are one name."""
    return re.sub(r"[-_.]+", "-", (value or "").strip()).lower()


def _stated_version(value, dynamic, computed_prefixes=()):
    """(ok, what-it-found): is the version stated in the file, or promised?

    The *value* is not compared against anything. What the version
    says is scored by `meta.version`, `dist.wheel-name` and `dist.sdist-name`,
    three rows across two families, and that is where a wrong value belongs. The
    property this half tests is the one the instruction states: the version is
    declared outright rather than marked to be produced at build time.
    """
    if "version" in dynamic:
        return False, "version is listed in dynamic, to be produced at build time"
    if value is None:
        return False, "no version is stated"
    if not isinstance(value, str):
        return False, "version is %r, which is not a string" % (value,)
    if not value.strip():
        return False, "version is empty"
    for prefix in computed_prefixes:
        if value.strip().startswith(prefix):
            return False, ("version %r starts with %r, which is code or a file "
                           "read rather than a declaration" % (value, prefix))
    return True, "version=%r" % (value,)


def declared_identity(tree, data, expected):
    """(ok, detail): the identity is declared, and the name is this one's.

    The property is "read, not computed": a file the backend reads states who
    this distribution is, rather than code computing it at build time. A
    `[project]` table carrying the name and nothing else is not that -- the
    version still has to come from somewhere, and the only somewhere left is
    code -- so both fields are tested, but not in the same way.

    The name is compared against what the pinned tree's own generators recorded.
    "Migrated, not renamed" is the instruction section's own title and it says
    "this distribution's name", so the name's value is the property.

    The version is tested for staticness only: present, a non-empty string, not
    listed in `dynamic`, and in `setup.cfg` not produced by `attr:` (an import)
    or `file:` (a file read). Its value is scored downstream by `meta.version`,
    `dist.wheel-name` and `dist.sdist-name`.

    This row is scored and does not gate the build. The build precondition's
    job is to stop an *unmigrated* tree from harvesting the downstream families
    through `setup.py`, and `no-setup-py` and `no-monkey-patch` are what do
    that. A tree that lists its version in `dynamic` (read from `bento.info` at
    build time, say) breaks the rule this row states but is not an unmigrated
    tree, so it is charged this row, not the build, the metadata, the contents
    and the behaviour of every step.
    """
    name = expected["name"]
    misses = []

    # Either file may carry the declaration; a wrong one in the first does not
    # rule out a right one in the second, so both are tried before failing.
    project = (data or {}).get("project") or {}
    if project:
        dynamic = project.get("dynamic")
        dynamic = set(dynamic) if isinstance(dynamic, list) else set()
        got_name, got_version = project.get("name"), project.get("version")
        ok_name = (_canonical_name(got_name) == _canonical_name(name)
                   and "name" not in dynamic)
        ok_version, why_version = _stated_version(got_version, dynamic)
        if ok_name and ok_version:
            return True, "pyproject [project] states name=%r, %s" % (
                got_name, why_version)
        misses.append("pyproject [project] name=%r%s, %s"
                      % (got_name, "" if ok_name else " (not this distribution)",
                         why_version))
    else:
        misses.append("pyproject.toml has no [project] table")

    cfg = os.path.join(tree, "setup.cfg")
    if os.path.isfile(cfg):
        parser = configparser.ConfigParser()
        try:
            parser.read(cfg, encoding="utf-8")
        except Exception as exc:
            misses.append("setup.cfg unreadable: %s: %s" % (type(exc).__name__, exc))
        else:
            got_name = parser.get("metadata", "name", fallback=None)
            got_version = parser.get("metadata", "version", fallback=None)
            ok_name = _canonical_name(got_name) == _canonical_name(name)
            # `attr:` imports a module and reads an attribute off it; `file:`
            # reads another file. Neither is this file stating the version.
            ok_version, why_version = _stated_version(
                got_version, set(), ("attr:", "file:"))
            if ok_name and ok_version:
                return True, "setup.cfg [metadata] states name=%r, %s" % (
                    got_name, why_version)
            misses.append("setup.cfg [metadata] name=%r%s, %s"
                          % (got_name, "" if ok_name else " (not this distribution)",
                             why_version))
    else:
        misses.append("there is no setup.cfg")

    return False, ("no file declares this distribution (%s) with a stated "
                   "version: %s" % (name, "; ".join(misses)))


def gate_migrated(tree, data, expected):
    """The three `migrated.` rows read off the tree, without building anything.

    Returns True when the tree is migrated in the structural sense -- the two
    rows in `MIGRATED_STRUCT_LABELS`; `declarative-metadata` is emitted here and
    scored, but does not gate (see `declared_identity`). Those two are the
    other half of the precondition: while `setup.py` is still there, setuptools
    builds the distribution from it -- and for this repository that produces a
    *correct* wheel, because `setup.py` is `bento.distutils`'s self-build and
    the metadata it computes from `bento.info` is the metadata the step asks
    for. Every downstream family would then be measuring Bento's own build
    system rather than the candidate's declaration.
    """
    setup_py = os.path.join(tree, "setup.py")
    row("01", "migrated.no-setup-py", not os.path.exists(setup_py),
        "setup.py present" if os.path.exists(setup_py) else "absent")

    # AST, not text: a docstring that mentions the module is not a finding, and
    # an import inside a function or a `try` is.
    #
    # The compatibility package is excluded from its own scan by `in_package`
    # and not by a path prefix: the instruction's property is about modules
    # *outside* `bento/distutils/`, and where that directory sits in the tree is
    # not part of the property. A `rel.startswith(...)` test would silently stop
    # excluding it the moment the package moved, and this row is half of the
    # build precondition -- reporting the package against itself would cost the
    # whole step.
    sites, unparsable = gatelib.import_sites(tree)
    hits = [(rel, line, name) for rel, line, name in sites
            if name.startswith("bento.distutils")
            and not gatelib.in_package(rel, "bento.distutils")]
    hits += [(rel, line, name) for rel, line, name in
             gatelib.dynamic_import_sites(tree, ["bento"])
             if "distutils" in name
             and not gatelib.in_package(rel, "bento.distutils")]
    # `import_module(".".join(["bento", "distutils"]))` is the same import with
    # the literal taken apart, and the line above cannot see it. This reports an
    # import-like call whose module argument is computed and that still carries
    # the needle in a string constant of its own subtree. The needle is
    # `distutils` alone: `bento` would make every computed import whose subtree
    # mentions the distribution a finding -- `import_module(f"bento.{name}")` is
    # an ordinary plugin lookup, not a monkey-patch -- and a false positive in a
    # precondition row costs the whole step. `distutils` still catches the call
    # this was written for, because it is one of that call's own constants.
    # Measured on the pinned tree: 12 import-like calls have a computed first
    # argument and none of them carries `distutils`, so this adds 0 findings to
    # the baseline.
    hits += [(rel, line, name) for rel, line, name in
             gatelib.opaque_import_sites(tree, ("distutils",))
             if not gatelib.in_package(rel, "bento.distutils")]
    row("01", "migrated.no-monkey-patch", not hits,
        "%d site(s): %s" % (len(hits), hits[:4]) if hits
        else "0 sites (%d imports scanned, %d files unparsable)"
             % (len(sites), len(unparsable)))

    # The metadata has to live in a file that is read, not in code that is run:
    # a file that states this distribution's name and its version outright.
    ok_declared, why_declared = declared_identity(tree, data, expected)
    row("01", "migrated.declarative-metadata", ok_declared, why_declared)

    return all(ok == "1" for _f, label, ok, _d in ROWS
               if label in MIGRATED_STRUCT_LABELS)


def gate_isolated_build(built_ok):
    row("01", "migrated.isolated-build", built_ok,
        "an isolated build produced both artifacts" if built_ok
        else "no isolated build produced both artifacts")


def gate_escapes(tree):
    """The suppression rule, as a penalty rather than a check.

    The pinned tree has zero suppression sites of any kind, so `no findings` is
    free here and cannot be a check the baseline fails. What is not free is
    *adding* one: `--no-build-isolation`, `--skip-dependency-check` and
    `SETUPTOOLS_USE_DISTUTILS` each turn the property this step states into one
    the tooling stops checking. Those three, and only those three: the
    instruction's "No escapes" paragraph names build isolation, build-dependency
    checking and the distutils shim.
    Returns (penalty, sites).
    """
    wanted = {"no-build-isolation", "skip-dependency-check", "distutils-hack"}
    sites = [s for s in gatelib.suppression_sites(tree) if s[0] in wanted]
    others = [s for s in gatelib.suppression_sites(tree) if s[0] not in wanted]
    penalty = max(0.0, 1.0 - 0.25 * len(sites))
    row("01", "suppression.build-escapes", not sites,
        "%d escape site(s) -> penalty %.2f%s"
        % (len(sites), penalty,
           "; " + "; ".join("%s %s:%d" % (k, r, n) for k, r, n, _t in sites[:4])
           if sites else ""))
    if others:
        row("01", "suppression.other", False,
            "%d silencing comment(s) added: %s"
            % (len(others), ["%s %s:%d" % (k, r, n) for k, r, n, _t in others[:4]]))
    return penalty


# --------------------------------------------------------------------------
# the artifacts
# --------------------------------------------------------------------------

# The scored label set, declared once. `test.sh` weighs four groups by label
# prefix and divides by the size of each group, so a label that appears on one
# path and not another silently reweights the score. `main` asserts what it
# emitted against these lists before printing anything.
DIST_LABELS = ["dist.one-wheel", "dist.one-sdist", "dist.wheel-name",
               "dist.sdist-name"]
META_LABELS = ["meta.metadata-version", "meta.requires-python",
               "meta.name", "meta.version",
               "meta.summary", "meta.author", "meta.maintainer", "meta.url",
               "meta.keywords", "meta.classifiers", "meta.license",
               "meta.description", "meta.console-script", "meta.sdist-agrees"]
# One row per named file, not one row for all five. The instruction names the
# five Windows payloads individually, and a single check would price dropping
# all five exactly the same as dropping one of them.
CONTENT_LABELS = ["wheel.declared-packages", "wheel.core-packages",
                  "wheel.data.cli-exe",
                  "wheel.data.wininst-7.1", "wheel.data.wininst-8.0",
                  "wheel.data.wininst-9.0", "wheel.data.wininst-9.0-amd64",
                  "wheel.licence-file", "sdist.sources", "sdist.rebuilds"]
# Three of the four `migrated.` checks are read off the tree, and two of those
# gate the build; the fourth is the build's own verdict. Split so that both
# scoring paths emit all four exactly once between them. `declarative-metadata`
# is read off the tree with the other two but is scored only, not a gate
# (see `declared_identity`).
MIGRATED_STRUCT_LABELS = ["migrated.no-setup-py", "migrated.no-monkey-patch"]
MIGRATED_DECLARED_LABELS = ["migrated.declarative-metadata"]
MIGRATED_BUILD_LABELS = ["migrated.isolated-build"]
DECLARATION_LABELS = ["pyproject.present", "pyproject.parses",
                      "build-system.requires", "build-system.backend",
                      "build-system.not-self"]


def gate_artifacts(distdir, expected):
    wheels = sorted(f for f in os.listdir(distdir) if f.endswith(".whl")) \
        if os.path.isdir(distdir) else []
    sdists = sorted(f for f in os.listdir(distdir) if f.endswith(".tar.gz")) \
        if os.path.isdir(distdir) else []
    row("01", "dist.one-wheel", len(wheels) == 1, repr(wheels))
    row("01", "dist.one-sdist", len(sdists) == 1, repr(sdists))

    name, version = expected["name"], expected["version"]
    ok_wheel_name = len(wheels) == 1 and wheels[0].startswith("%s-%s-" % (name, version))
    row("01", "dist.wheel-name", ok_wheel_name,
        "want %s-%s-*.whl, got %r" % (name, version, wheels))
    ok_sdist_name = len(sdists) == 1 and sdists[0] == "%s-%s.tar.gz" % (name, version)
    row("01", "dist.sdist-name", ok_sdist_name,
        "want %s-%s.tar.gz, got %r" % (name, version, sdists))
    return (os.path.join(distdir, wheels[0]) if len(wheels) == 1 else None,
            os.path.join(distdir, sdists[0]) if len(sdists) == 1 else None)


def field(metadata, key):
    if metadata is None:
        return None
    value = metadata.get(key)
    if value in ("UNKNOWN", ""):
        return None
    return value


def gate_metadata(wheel, sdist, expected):
    metadata = wheelnames = None
    why = "no wheel was built"
    if wheel:
        try:
            metadata, _wheelfile, wheelnames = gatelib.wheel_metadata(wheel)
        except Exception as exc:
            why = "%s: %s" % (type(exc).__name__, exc)
    if metadata is None:
        # Not `row(... "meta.readable" ...)` on the exception path: a
        # conditional extra row would change the size of the `meta.` group, and
        # the group is scored as a fraction of its own size.
        for label in META_LABELS:
            row("01", label, False, why)
        return

    try:
        mv = tuple(int(p) for p in (metadata.get("Metadata-Version") or "0").split("."))
    except ValueError:
        mv = (0,)
    row("01", "meta.metadata-version", mv >= (2, 1),
        "Metadata-Version: %s" % metadata.get("Metadata-Version"))
    # "installable on the interpreter this workspace runs, CPython 3.11".
    # Declaring nothing admits every interpreter and is what the pinned tree
    # does, so absent passes; declaring a floor above this one is a
    # distribution that contradicts its own instruction. Compared against the
    # running interpreter rather than a literal, because this module runs on
    # the reference interpreter by construction -- the verifier image is
    # `python:3.11-slim-bookworm`, the same base as the agent image.
    requires_python = field(metadata, "Requires-Python")
    if not requires_python:
        rp_ok, rp_why = True, "no Requires-Python declared, which admits every interpreter"
    else:
        try:
            from packaging.specifiers import SpecifierSet
            rp_ok = SpecifierSet(requires_python).contains(
                platform.python_version(), prereleases=True)
            rp_why = "Requires-Python %r %s the reference interpreter %s" % (
                requires_python, "admits" if rp_ok else "excludes",
                platform.python_version())
        except Exception as exc:
            rp_ok = False
            rp_why = "Requires-Python %r: %s: %s" % (requires_python,
                                                     type(exc).__name__, exc)
    row("01", "meta.requires-python", rp_ok, rp_why)
    row("01", "meta.name", field(metadata, "Name") == expected["name"],
        "%r vs %r" % (field(metadata, "Name"), expected["name"]))
    row("01", "meta.version", field(metadata, "Version") == expected["version"],
        "%r vs %r" % (field(metadata, "Version"), expected["version"]))
    row("01", "meta.summary", field(metadata, "Summary") == expected["summary"],
        "%r vs %r" % (field(metadata, "Summary"), expected["summary"]))
    # `Author` + `Author-email` and a single RFC 822 `Name <addr>` in
    # `Author-email` are the same statement: PEP 621 `authors = [{name, email}]`
    # produces the second and omits the first. The check is on the pair, not on
    # which field carries it.
    for role in ("author", "maintainer"):
        head = role.capitalize()
        people = set()
        plain = field(metadata, head)
        if plain:
            people.add((plain, field(metadata, head + "-email") or ""))
        for name, addr in email.utils.getaddresses(
                metadata.get_all(head + "-email") or []):
            people.add((name or plain or "", addr))
        want = (expected[role], expected[role + "_email"])
        row("01", "meta." + role, want in people,
            "want %r, METADATA carries %r" % (want, sorted(people)))

    urls = [field(metadata, "Home-page") or ""] + \
        [v.split(",", 1)[-1].strip() for v in metadata.get_all("Project-URL") or []]
    row("01", "meta.url", expected["url"] in urls,
        "%r not among %r" % (expected["url"], urls))

    keywords = re.split(r"[,\s]+", (field(metadata, "Keywords") or "").strip())
    row("01", "meta.keywords",
        all(k in keywords for k in expected["keywords"]),
        "%r vs declared %r" % (keywords, expected["keywords"]))

    # The `License ::` trove classifiers are not required: PEP 639
    # forbids them next to a `License-Expression`, and step 6 adds one. Every
    # other classifier the pinned tree declares has to survive all six steps.
    want = [c for c in expected["classifiers"] if not c.startswith("License ::")]
    have = metadata.get_all("Classifier") or []
    missing = [c for c in want if c not in have]
    row("01", "meta.classifiers", not missing,
        "missing %r" % (missing,) if missing else "all %d present" % len(want))

    # The licence the pinned tree declares, not merely some licence. Any of the
    # three carriers will do, and the test is a case-insensitive substring so
    # that `BSD`, the PEP 639 expression `BSD-3-Clause` step 6 will add, and the
    # trove classifier `License :: OSI Approved :: BSD License` all count --
    # while `MIT` does not.
    carriers = [("License", field(metadata, "License") or ""),
                ("License-Expression", field(metadata, "License-Expression") or "")]
    carriers += [("Classifier", c) for c in have if c.startswith("License ::")]
    want_licence = (expected["license"] or "").strip()
    matched = [k for k, v in carriers if want_licence
               and want_licence.lower() in v.lower()]
    row("01", "meta.license", bool(matched),
        "%r carried by %r" % (want_licence, matched) if matched
        else "%r in none of %r" % (want_licence, carriers))

    # `bento.info` says `DescriptionFromFile: README.rst`, and the pinned tree's
    # own generator resolved that to the text in `expected["description"]`. The
    # comparison is against that recorded text, not against whatever README the
    # candidate happens to ship, so moving the long description to a file that
    # says something else is a failure rather than a tautology.
    description = metadata.get_payload() or field(metadata, "Description") or ""
    want = (expected["description"] or "").strip()
    row("01", "meta.description", bool(want) and description.strip() == want,
        "wheel METADATA description %d chars, %s says %d chars, equal=%s"
        % (len(description.strip()), expected["description_from_file"], len(want),
           description.strip() == want))

    # The console script the pinned tree declares, spelled the way the entry
    # point specification spells it.
    entry = ""
    member = next((n for n in wheelnames
                   if n.endswith(".dist-info/entry_points.txt")), None)
    if member:
        entry = gatelib.wheel_read(wheel, member).decode("utf-8")
    wanted = ["%s = %s:%s" % (script, module, function)
              for script, (module, function) in sorted(expected["executables"].items())]
    normalised = [re.sub(r"\s*=\s*", " = ", l.strip())
                  for l in entry.split("\n") if "=" in l]
    row("01", "meta.console-script", all(w in normalised for w in wanted),
        "want %r, entry_points.txt has %r" % (wanted, normalised))

    sdist_meta = None
    if sdist:
        try:
            sdist_meta, _names = gatelib.sdist_metadata(sdist)
        except Exception as exc:
            sdist_meta = None
    same = sdist_meta is not None and all(
        field(sdist_meta, k) == field(metadata, k)
        for k in ("Name", "Version", "Summary"))
    row("01", "meta.sdist-agrees", same,
        "sdist PKG-INFO %r" % ([field(sdist_meta, k) for k in
                                ("Name", "Version", "Summary")]
                               if sdist_meta else None))


# --------------------------------------------------------------------------
# what the artifacts contain
# --------------------------------------------------------------------------

# Package data, not modules. `find_packages()` does not find these, and the
# recorded suite reads all five out of the *installed* distribution
# (`api_scripts`, `api_wininst`), so they have to survive every step. One label
# each: the instruction names all five, and a tree that ships none of them
# should not cost what a tree that ships four of them costs.
DATA_MEMBERS = [
    ("wheel.data.cli-exe", "bento/commands/cli.exe"),
    ("wheel.data.wininst-7.1", "bento/commands/wininst/wininst-7.1.exe"),
    ("wheel.data.wininst-8.0", "bento/commands/wininst/wininst-8.0.exe"),
    ("wheel.data.wininst-9.0", "bento/commands/wininst/wininst-9.0.exe"),
    ("wheel.data.wininst-9.0-amd64",
     "bento/commands/wininst/wininst-9.0-amd64.exe"),
]

# The part of the distribution that no step in this migration line removes.
#
# `wheel.declared-packages` below is scored against the *candidate's* own
# `bento.info`, which is what makes it survive the later steps -- but it also
# means shrinking `Library:` shrinks the requirement. Without a floor, a tree
# with almost no Bento left in it, a hand-written `pyproject.toml` copying
# `bento.info`'s fields and a two-package `Library:` passes the declaration,
# the metadata and the contents families outright, and only the behaviour
# family notices. This list is that floor, and it is a floor rather than the
# whole manifest so that it stays passable at every step.
#
# Excluded, each because a later step is allowed to remove it:
# `bento.private.*` (step 3 de-vendors `_ply`, `_simplejson` and `_six`),
# `bento.distutils*` (step 6 removes the distutils integration layer), and
# `bento.backends.waf_tools` and `bento.testing`, which may be reorganised.
CORE_PACKAGES = [
    "bento", "bento.backends", "bento.commands", "bento.compat",
    "bento.convert", "bento.core", "bento.core.platforms", "bento.parser",
    "bento.pypi", "bento.utils", "bentomakerlib",
]

# The candidate's own `bento.info`, read by the *pinned* parser. Run in a
# subprocess because the parser is the pinned tree's code and this process must
# not import it: the gate module has to stay a reader.
#
# `sys.path` has the cwd stripped out and the pristine tree put first, so the
# parser that runs is the pinned one even though the working directory is the
# candidate's -- the instrument is fixed while the input is the candidate's.
# The call is the one `lib/record_meta.py` makes to produce the recording, so
# this is the recording's own reader pointed at the tree in front of it.
DECLARED_LIBRARY_SNIPPET = """
import json, os, sys
pristine = sys.argv[1]
sys.path[:] = [p for p in sys.path if p not in ("", ".", os.getcwd())]
sys.path.insert(0, pristine)
from bento.core import PackageDescription
pkg = PackageDescription.from_file("bento.info")
json.dump({"packages": sorted(pkg.packages),
           "py_modules": sorted(pkg.py_modules)}, sys.stdout)
"""


def declared_library(tree, pristine):
    """(packages, py_modules, failure): what this tree declares it is made of.

    Read from the candidate's own `bento.info` rather than from the recording.
    The recording's list is the *pinned* tree's, and this row is inherited by
    every later step: a frozen copy would be a check that a later step's own
    requirements make unpassable, because moving or removing a package is work
    those steps ask for. Asking the tree what it declares, with a parser the
    candidate cannot touch, scores the property the instruction states -- the
    distribution ships what it declares -- at every step.

    A parse that exits non-zero, times out, or prints something that is not the
    expected JSON is a failed row, not a harness fault: `bento.info` is the
    tree's own manifest, and a candidate who broke it broke something real. The
    behaviour family says so too.
    """
    try:
        proc = subprocess.run(
            [sys.executable, "-P", "-c", DECLARED_LIBRARY_SNIPPET, pristine],
            cwd=tree, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=30,
            env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1",
                     PYTHONHASHSEED="0", PYTHONWARNINGS="ignore"))
    except subprocess.TimeoutExpired:
        return [], [], "reading bento.info with the pinned parser timed out (30s)"
    except Exception as exc:
        return [], [], "could not read bento.info: %s: %s" % (type(exc).__name__, exc)
    tail = proc.stderr.decode("utf-8", "replace").strip().replace("\n", " ")[-200:]
    if proc.returncode != 0:
        return [], [], ("the pinned parser could not read this tree's bento.info "
                        "(exit %d): %s" % (proc.returncode, tail))
    try:
        parsed = json.loads(proc.stdout.decode("utf-8", "replace"))
        packages = [str(p) for p in parsed["packages"]]
        modules = [str(m) for m in parsed["py_modules"]]
    except Exception as exc:
        return [], [], ("the pinned parser printed no usable declaration: %s: %s; %s"
                        % (type(exc).__name__, exc, tail))
    return packages, modules, None


SDIST_SOURCES = ["bento.info", "README.rst", "LICENSE.txt", "bscript"]


# `Library: Packages:` holds dotted importable names; `Library: Modules:` holds
# a *path* -- `bento/private/_six/six` on the pinned tree, the only entry there
# is. The two are read the same way below, so the path needs the one
# normalisation a src layout demands.
#
# Step 2 moves the importable trees under `src/` and says in as many words that
# `src` is not a package and that no `src/` prefix appears in the wheel, in a
# module name, or in an import. A candidate who also repaths this manifest
# entry to `src/bento/private/_six/six` has done nothing wrong -- the field
# names a file in the source tree and that file moved -- but the wheel member
# is still `bento/private/_six/six.py`, so a literal lookup would fail an
# inherited row on a tree that satisfies the instruction it was handed: an
# inherited test the step's own requirements made unpassable. Dropping the one layout prefix the instruction
# itself defines as not-a-package is that instruction applied to the manifest;
# it loosens nothing else, because every remaining component still has to match
# exactly and the member still has to be in the wheel.
#
# `Packages:` gets no such treatment. `src.bento` is not an importable name,
# and a candidate who wrote it would have declared a package the wheel does not
# ship -- the row failing correctly, not the row being too strict.
def wheel_path_for_module(mod):
    """The wheel member a `Library: Modules:` entry names."""
    path = mod.replace(".", "/")
    if path.startswith("src/"):
        path = path[len("src/"):]
    return path + ".py"



def gate_contents(tree, wheel, sdist, pristine):
    if wheel:
        names = set(gatelib.wheel_names(wheel))
    else:
        names = set()

    # What the distribution says it is made of has to be what the wheel ships.
    # On the pinned tree `bento.info`'s `Library:` section lists 22 packages and
    # one module, and four of the package paths reach the importable package
    # through a directory with no `__init__.py`, so a plain `find_packages()`
    # silently drops them and the wheel imports from a checkout but not from
    # itself.
    #
    # The list is read from *this tree's* own manifest by the pinned parser, not
    # copied out of the recording. A frozen copy would be an inherited check a
    # later step could not pass: moving the tree, de-vendoring the packages
    # under `bento/private/` and removing the distutils layer are all work later
    # steps ask for, and each of them changes this list. Fixing the instrument
    # and letting the input move is what makes the property -- the distribution
    # ships what it declares -- the same property at every step.
    packages, modules, failure = declared_library(tree, pristine)
    declared_total = len(packages) + len(modules)
    if failure:
        row("01", "wheel.declared-packages", False, failure)
    elif not declared_total:
        # A tree that declares nothing has not shown that it ships what it
        # declares; it has only made the question unanswerable.
        row("01", "wheel.declared-packages", False,
            "bento.info declares no packages and no modules")
    else:
        absent = [pkg for pkg in packages
                  if pkg.replace(".", "/") + "/__init__.py" not in names]
        absent += [mod for mod in modules
                   if wheel_path_for_module(mod) not in names]
        row("01", "wheel.declared-packages", bool(wheel) and not absent,
            "%d of %d declared packages/modules missing from the wheel: %r"
            % (len(absent), declared_total, absent[:8]) if absent
            else "all %d declared packages and modules present" % declared_total)

    # The floor the row above cannot provide, because that row's input is a
    # file the candidate writes. See CORE_PACKAGES.
    missing_core = [pkg for pkg in CORE_PACKAGES
                    if pkg.replace(".", "/") + "/__init__.py" not in names]
    row("01", "wheel.core-packages", bool(wheel) and not missing_core,
        "%d of %d core packages missing from the wheel: %r"
        % (len(missing_core), len(CORE_PACKAGES), missing_core) if missing_core
        else "all %d core packages present" % len(CORE_PACKAGES))

    for label, member in DATA_MEMBERS:
        if member not in names:
            row("01", label, False, "%s is not in the wheel" % member)
            continue
        want_path = os.path.join(pristine, member)
        try:
            with open(want_path, "rb") as fh:
                want = fh.read()
            same = gatelib.wheel_read(wheel, member) == want
            row("01", label, same,
                "%s present and byte-identical to the pinned tree" % member if same
                else "%s present but differs from the pinned tree" % member)
        except Exception as exc:
            row("01", label, False,
                "%s: %s: %s" % (member, type(exc).__name__, exc))

    licences = [n for n in names
                if ".dist-info/" in n
                and re.match(r"licen[cs]e", os.path.basename(n), re.I)]
    row("01", "wheel.licence-file", bool(licences), repr(licences))

    sdist_names = set(gatelib.sdist_names(sdist)) if sdist else set()
    prefix = ""
    if sdist_names:
        prefix = sorted(sdist_names)[0].split("/", 1)[0] + "/"
    absent = [s for s in SDIST_SOURCES if prefix + s not in sdist_names]
    row("01", "sdist.sources", bool(sdist) and not absent,
        "missing %r" % (absent,) if absent else "%d source files present"
                                                % len(SDIST_SOURCES))


def gate_sdist_rebuild(tree_wheel, sdist_wheel):
    """A wheel built from the sdist has to be the wheel built from the tree.

    This is the check that catches an sdist which omits a file the wheel needs:
    the tree build succeeds because the file is sitting there, and only a build
    from the unpacked archive notices.
    """
    if not tree_wheel or not sdist_wheel:
        row("01", "sdist.rebuilds", False,
            "tree wheel %r, sdist wheel %r"
            % (os.path.basename(tree_wheel or ""), os.path.basename(sdist_wheel or "")))
        return
    same_name = os.path.basename(tree_wheel) == os.path.basename(sdist_wheel)
    a = set(n for n in gatelib.wheel_names(tree_wheel) if not n.endswith("/RECORD"))
    b = set(n for n in gatelib.wheel_names(sdist_wheel) if not n.endswith("/RECORD"))
    row("01", "sdist.rebuilds", same_name and a == b,
        "name equal=%s; only in tree wheel %r; only in sdist wheel %r"
        % (same_name, sorted(a - b)[:6], sorted(b - a)[:6]))


def check_emitted(mode):
    """Refuse to hand `test.sh` a row set of the wrong shape."""
    want = {"precondition": DECLARATION_LABELS + MIGRATED_STRUCT_LABELS
            + MIGRATED_DECLARED_LABELS}.get(
        mode, DIST_LABELS + META_LABELS + CONTENT_LABELS + MIGRATED_BUILD_LABELS)
    got = [label for _family, label, _ok, _detail in ROWS
           if not label.startswith("suppression.")]
    if sorted(got) != sorted(want):
        sys.stderr.write(
            "GATE FAULT (%s): emitted %d scored rows, expected %d; "
            "missing %r; unexpected %r\n"
            % (mode, len(got), len(want), sorted(set(want) - set(got)),
               sorted(set(got) - set(want))))
        raise SystemExit(4)


def main(argv):
    mode = argv[1]
    tree = argv[2]
    with open("/tests/data/meta-expected.json") as fh:
        expected = json.load(fh)

    if mode == "precondition":
        # Both halves of "this tree is a PEP 517 build of bento, not bento's
        # own build with a file next to it". `test.sh` runs the build only when
        # this returns 0: with `setup.py` in place setuptools builds from it,
        # and for this repository that path produces a wheel whose metadata,
        # contents and behaviour are all correct -- so an unmigrated tree would
        # collect four of the five families without doing the work.
        data, note = read_pyproject(tree)
        declared = gate_declaration(tree, data, note)
        migrated = gate_migrated(tree, data, expected)
        check_emitted(mode)
        flush()
        return 0 if (declared and migrated) else 3

    if mode == "post":
        distdir, sdist_distdir, pristine, built_ok = argv[3], argv[4], argv[5], argv[6]
        wheel, sdist = gate_artifacts(distdir, expected)
        gate_metadata(wheel, sdist, expected)
        gate_contents(tree, wheel, sdist, pristine)
        sdist_wheels = sorted(f for f in os.listdir(sdist_distdir)
                              if f.endswith(".whl")) \
            if os.path.isdir(sdist_distdir) else []
        gate_sdist_rebuild(wheel, os.path.join(sdist_distdir, sdist_wheels[0])
                           if len(sdist_wheels) == 1 else None)
        gate_isolated_build(built_ok == "1")
        penalty = gate_escapes(tree)
        check_emitted(mode)
        flush()
        sys.stderr.write("PENALTY %.4f\n" % penalty)
        return 0

    if mode == "skipped":
        # The precondition failed, so nothing downstream can be earned. The
        # three tree-read `migrated.` rows were already emitted by that run.
        for label in DIST_LABELS + META_LABELS + CONTENT_LABELS:
            row("01", label, False, "precondition not met; build not attempted")
        gate_isolated_build(False)
        penalty = gate_escapes(tree)
        check_emitted(mode)
        flush()
        sys.stderr.write("PENALTY %.4f\n" % penalty)
        return 0

    raise SystemExit("unknown mode %r" % mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
