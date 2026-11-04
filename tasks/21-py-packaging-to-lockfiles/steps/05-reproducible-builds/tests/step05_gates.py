#!/usr/bin/env python3
"""Step 05 gates: builds that produce the same bytes twice.

Emits one tab-separated `family<TAB>label<TAB>ok<TAB>detail` line per check,
the same contract the earlier steps' gate scripts have with `test.sh`. Every
row here is recorded against family `05`; the earlier steps' rows are re-run
from their own gate scripts and keep families `01`..`04`.

Nothing here builds anything and nothing here executes candidate code. The
builds have already been made by `test.sh`, unprivileged, into one output
directory each from one copy of the tree each, in different parent directories,
and are read here as archives: two of them at one epoch, which are the two the
byte-identity rows compare, and a third at a second epoch, which no row
compares with anything because its bytes are supposed to differ -- it exists so
that `repro.epoch` is asked its question at more than one value.

Two groups, weighed by `test.sh` on the label prefix:

    repro.  3 rows   the wheels and the sdists' tars are byte-identical across
                     the two builds made at one epoch, and every timestamp in
                     every artifact -- those two builds' and a third build's,
                     made at a second epoch -- is the `SOURCE_DATE_EPOCH` that
                     build was given
    pin.    2 rows   every requirement of every `[build-system]` table in the
                     tree names an exact version, and the version each wheel
                     reports as its generator is the version that distribution
                     pins

Why the sdist comparison is of the tar and not of the `.tar.gz`: a gzip stream
carries an mtime of its own in its header, ahead of the tar it wraps, and
whether a front end zeroes it is a property of the front end rather than of the
tree the candidate is editing. `instruction.md` says so in as many words -- "the
archive inside the sdist's gzip wrapper", and again under *Not part of this
milestone* -- so the gate compares what the instruction promises and nothing
wider.
"""
import gzip
import os
import re
import sys

from packaging.requirements import Requirement

sys.path.insert(0, "/tests/lib")
import gatelib  # noqa: E402

# Step 02's, imported rather than copied, for the same reason step 03 and step
# 04 import them: `walk` is the definition of "the tree" that every migrated-ness
# row in this task already scans, `load_toml` is the one TOML reader, and
# `canonical` is PEP 503 name comparison. A second copy here would let this step
# disagree with the steps whose tree it inherits.
sys.path.insert(0, "/tests")
import step02_gates  # noqa: E402

walk = step02_gates.walk
load_toml = step02_gates.load_toml
canonical = step02_gates.canonical

ROWS = []


def row(label, ok, detail=""):
    ROWS.append(("05", label, "1" if ok else "0",
                 str(detail).replace("\t", " ")[:300]))


def flush():
    for family, label, ok, detail in ROWS:
        sys.stdout.write("\t".join((family, label, ok, detail)) + "\n")


# --------------------------------------------------------------------------
# the tree's build-system tables
# --------------------------------------------------------------------------

def pyproject_paths(tree):
    """Every `pyproject.toml` in the tree, sorted, wherever it is.

    Whole-repo: the instruction says "each `[build-system]`
    requires in the tree", not "the three that are there today", so a candidate
    who adds a fourth distribution is held to the same requirement and a
    candidate who moves one is not let off.
    """
    return [p for p in walk(tree)
            if os.path.basename(p) == "pyproject.toml"]


def build_system(path):
    """(requires, backend, note) for one pyproject.toml.

    `requires` is None when the file has no `[build-system]` table to read --
    which is not this step's business; step 01 already scores the root
    distribution's table existing, and a directory with a `pyproject.toml` and
    no build system is not a distribution this step has anything to say about.
    """
    data, note = load_toml(path)
    if data is None:
        return None, None, note
    table = data.get("build-system")
    if not isinstance(table, dict):
        return None, None, "no [build-system] table"
    requires = table.get("requires")
    if not isinstance(requires, list):
        return None, table.get("build-backend"), "no requires list"
    return ([r for r in requires if isinstance(r, str)],
            table.get("build-backend"), "%d requirements" % len(requires))


def pin_of(req):
    """(name, version, fault). `version` is None when the requirement is not
    pinned to one exact version.

    Parsed with `packaging`, which the verifier image installs (26.3) and which
    step 01 already reads `Requires-Python` through: PEP 508 is not a grammar
    worth re-implementing in a scorer, and a hand-rolled version of it would
    disagree with the one the packaging tools themselves use at exactly the
    inputs a candidate is most likely to write.

    Exact means one clause, `==` or `===`, with no wildcard: `==1.2.*` admits a
    range and `>=1.2` admits everything above it, which is the shape the tree
    arrives with. An environment marker is allowed and ignored -- it narrows
    *when* a requirement applies, not *which version* -- and so is an extras
    list.
    """
    try:
        parsed = Requirement(req)
    except Exception as exc:
        return None, None, "%r is not a requirement: %s" % (req, exc)
    name = parsed.name
    if parsed.url:
        return name, None, "%s is a direct URL reference, not a version" % name
    clauses = list(parsed.specifier)
    if not clauses:
        return name, None, "%s names no version at all" % name
    if len(clauses) > 1:
        return name, None, "%s has more than one version clause: %s" % (
            name, parsed.specifier)
    clause = clauses[0]
    if clause.operator not in ("==", "==="):
        return name, None, "%s is %r, which is not an exact pin" % (
            name, str(parsed.specifier))
    if "*" in clause.version:
        return name, None, "%s is pinned to %s, which is a range" % (
            name, clause.version)
    return name, clause.version, None


def gate_pin_requires(tree):
    """Every requirement of every build-system table names one version."""
    label = "pin.requires"
    paths = pyproject_paths(tree)
    if not paths:
        row(label, False, "no pyproject.toml anywhere in the tree")
        return
    faults = []
    seen = 0
    for path in paths:
        rel = gatelib.relpath(path, tree)
        requires, _backend, note = build_system(path)
        if requires is None:
            continue
        if not requires:
            faults.append("%s: requires is empty" % rel)
            continue
        for req in requires:
            seen += 1
            _name, version, fault = pin_of(req)
            if version is None:
                faults.append("%s: %s" % (rel, fault))
    if not seen and not faults:
        row(label, False,
            "%d pyproject.toml files and not one [build-system] requires "
            "among them" % len(paths))
        return
    row(label, not faults,
        "; ".join(faults)[:290] if faults
        else "%d requirements across %d build-system tables, every one pinned "
             "to an exact version" % (seen, len(
                 [p for p in paths if build_system(p)[0] is not None])))


# --------------------------------------------------------------------------
# what actually generated each wheel
# --------------------------------------------------------------------------

_GENERATOR = re.compile(r"^\s*([A-Za-z0-9._-]+)\s*\(?\s*([^)\s]+)?\s*\)?\s*$")


def generator_of(wheel):
    """(name, version) from the wheel's `Generator:` line, or (None, None).

    Both spellings the ecosystem uses are read: `setuptools (84.0.0)` and
    `hatchling 1.32.0`.
    """
    try:
        _meta, wheel_file, _names = gatelib.wheel_metadata(wheel)
    except Exception:
        return None, None
    values = (wheel_file or {}).get("Generator") or []
    if not values:
        return None, None
    m = _GENERATOR.match(values[0])
    if not m:
        return None, None
    return m.group(1), m.group(2)


def distributions(tree):
    """canonical project name -> its pyproject.toml path.

    A wheel is matched to the table that has to pin its generator by the name
    the distribution declares, not by where the wheel landed: `uv build
    --package` and `python -m build <dir>` put artifacts wherever they are
    told, and the distribution's identity is its `[project] name`.
    """
    out = {}
    for path in pyproject_paths(tree):
        data, _note = load_toml(path)
        name = ((data or {}).get("project") or {}).get("name")
        if isinstance(name, str) and name:
            out[canonical(name)] = path
    return out


def wheel_distribution(wheel):
    """The distribution name from a wheel filename, per the wheel spec."""
    base = os.path.basename(wheel)
    return canonical(base.split("-")[0]) if base.endswith(".whl") else None


# The two backends whose reported name genuinely differs from the distribution
# name a `requires` list has to spell: `flit_core` is the distribution on the
# index and `flit` is what its wheels say generated them, `meson-python` is the
# distribution and `meson` is what its wheels say. Each entry is an equivalence
# class rather than a direction, so the row does not care which of the two names
# is on which side of the comparison.
#
# The other back ends this task builds with -- hatchling, pdm-backend,
# scikit-build-core -- report their own distribution name exactly (for example
# `pin.effective` reads `hatchling=1.32.0, scikit-build-core=1.0.3`), so none of
# them needs an entry here. Stripping a trailing role word (`-core`, `-backend`,
# `-build`, `-python`) from each side instead would also equate `pdm` with
# `pdm-backend` and `scikit-build` with `scikit-build-core` -- two different
# distributions on the index, either of which could satisfy this row at a
# coincidental version without being the requirement that the table pinned.
_NAME_ALIASES = (
    frozenset(("flit-core", "flit")),
    frozenset(("meson-python", "meson")),
)


def _aliases(name):
    """Every canonical name this one is the same back end as, itself included."""
    canon = canonical(name)
    out = {canon}
    for group in _NAME_ALIASES:
        if canon in group:
            out |= set(group)
    return out


def names_agree(generator, pinned):
    """Is the generator's own name the name the requirement pins?"""
    return bool(_aliases(generator) & _aliases(pinned))


def versions_agree(reported, pinned):
    """PEP 440 equality, so `1.32` and `1.32.0` are one release written twice.

    The same instrument step 06's lock row uses, and inside a `try` for the same
    reason: a `Generator:` line is whatever the back end chose to write there,
    the pin beside it is whatever the candidate typed, and a row that raised on
    a string neither `packaging` nor anyone else can read as a version would
    fail a tree for the shape of a string rather than for its meaning. When
    either side will not parse they are compared as the strings they are.

    A missing side is the one place the two rows disagree:
    step 06's lock row calls a `None` agreement, because a dependency with no
    declared version is not a dependency the lock contradicts. Here a `None` is
    a wheel whose `Generator:` line carries no version at all, or a pin this row
    could not read one out of, and neither of those is evidence that the version
    that ran is the version that was pinned -- which is the whole claim.
    """
    if reported is None or pinned is None:
        return False
    try:
        from packaging.version import Version
        return Version(reported) == Version(pinned)
    except Exception:
        return str(reported) == str(pinned)


def gate_pin_effective(tree, wheels):
    """The version a wheel says produced it is the version its table pins."""
    label = "pin.effective"
    if not wheels:
        row(label, False, "no wheels were built, so nothing reports a generator")
        return
    by_name = distributions(tree)
    faults = []
    agreed = []
    for wheel in wheels:
        base = os.path.basename(wheel)
        dist = wheel_distribution(wheel)
        gen_name, gen_version = generator_of(wheel)
        if gen_name is None:
            faults.append("%s declares no readable Generator" % base)
            continue
        path = by_name.get(dist)
        if path is None:
            faults.append("%s: no pyproject.toml in the tree declares the "
                          "name %r" % (base, dist))
            continue
        requires, _backend, _note = build_system(path)
        pins = []
        for req in requires or []:
            name, version, _fault = pin_of(req)
            if version is not None:
                pins.append((name, version))
        match = [(n, v) for n, v in pins if names_agree(gen_name, n)]
        if not match:
            # A distribution with no `[build-system]` table at all reaches here
            # with an empty `pins`: the front end built it with the PEP 517
            # default, which is an unpinned setuptools, and that is the exact
            # situation this row is about. `pin.requires` says nothing about
            # such a table -- it scores the tables that exist -- so this row is
            # the only place a candidate hears about it, and it says so in
            # those words rather than reading "pins only nothing".
            faults.append("%s was generated by %s but %s pins %s"
                          % (base, gen_name, gatelib.relpath(path, tree),
                             ("only " + ", ".join(n for n, _ in pins)) if pins
                             else "no build requirement to a version"))
            continue
        if not any(versions_agree(gen_version, v) for _n, v in match):
            faults.append("%s reports %s %s; %s pins %s"
                          % (base, gen_name, gen_version,
                             gatelib.relpath(path, tree),
                             ", ".join("%s==%s" % (n, v) for n, v in match)))
            continue
        agreed.append("%s=%s" % (gen_name, gen_version))
    row(label, not faults,
        "; ".join(faults)[:290] if faults
        else "%d wheels, each generated by the version its own table pins: %s"
             % (len(wheels), ", ".join(sorted(set(agreed)))))


# --------------------------------------------------------------------------
# two builds, the same bytes
# --------------------------------------------------------------------------

def artifacts_in(dirpath, suffix):
    """{filename: absolute path} for one kind of artifact in one directory."""
    if not os.path.isdir(dirpath):
        return {}
    return dict((n, os.path.join(dirpath, n))
                for n in sorted(os.listdir(dirpath)) if n.endswith(suffix))


def read_bytes(path):
    with open(path, "rb") as fh:
        return fh.read()


def tar_of(path):
    """The tar inside a `.tar.gz`, with the gzip wrapper taken off."""
    return gzip.decompress(read_bytes(path))


def first_difference(a_path, b_path):
    """The name of the first member whose bytes differ, for the detail string.

    Diagnostic only: the row has already failed by the time this is called. A
    wheel that is not byte-identical is usually not byte-identical in one
    member, and saying which one is the difference between a reward a candidate
    can act on and a number.
    """
    import tarfile
    import zipfile
    try:
        if a_path.endswith(".whl"):
            with zipfile.ZipFile(a_path) as za, zipfile.ZipFile(b_path) as zb:
                na, nb = za.namelist(), zb.namelist()
                for name in sorted(set(na) | set(nb)):
                    if name not in na or name not in nb:
                        return "%s is in only one of them" % name
                    if za.read(name) != zb.read(name):
                        return "%s differs in content" % name
                    ia, ib = za.getinfo(name), zb.getinfo(name)
                    if ia.date_time != ib.date_time:
                        return "%s differs in timestamp, %s vs %s" % (
                            name, ia.date_time, ib.date_time)
            return "no member differs; the archive's own framing does"
        with tarfile.open(a_path) as ta, tarfile.open(b_path) as tb:
            ma = dict((m.name, m) for m in ta.getmembers())
            mb = dict((m.name, m) for m in tb.getmembers())
            for name in sorted(set(ma) | set(mb)):
                if name not in ma or name not in mb:
                    return "%s is in only one of them" % name
                if ma[name].mtime != mb[name].mtime:
                    return "%s differs in mtime, %s vs %s" % (
                        name, ma[name].mtime, mb[name].mtime)
                ea, eb = ta.extractfile(ma[name]), tb.extractfile(mb[name])
                if (ea.read() if ea else b"") != (eb.read() if eb else b""):
                    return "%s differs in content" % name
        return "no member differs; the archive's own framing does"
    except Exception as exc:
        return "%s: %s" % (type(exc).__name__, exc)


def compare_builds(label, dir_a, dir_b, suffix, reader, what):
    """One reproducibility row over one kind of artifact."""
    a = artifacts_in(dir_a, suffix)
    b = artifacts_in(dir_b, suffix)
    if not a and not b:
        row(label, False, "neither build produced a %s" % what)
        return []
    faults = []
    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    if only_a or only_b:
        faults.append("the two builds produced different files: %s"
                      % ", ".join(only_a + only_b)[:120])
    same = []
    for name in sorted(set(a) & set(b)):
        try:
            if reader(a[name]) == reader(b[name]):
                same.append(name)
            else:
                faults.append("%s: %s" % (name, first_difference(a[name], b[name])))
        except Exception as exc:
            faults.append("%s: %s: %s" % (name, type(exc).__name__, exc))
    row(label, not faults,
        "; ".join(faults)[:290] if faults
        else "%d %ss, byte-identical across two builds from two directories: %s"
             % (len(same), what, ", ".join(same)[:170]))
    return sorted(a.values())


def zip_stamps(path):
    """Every distinct entry timestamp in a zip, as seconds since the epoch."""
    import calendar
    import zipfile
    with zipfile.ZipFile(path) as zf:
        return sorted({calendar.timegm(tuple(i.date_time) + (0, 0, 0))
                       for i in zf.infolist()})


def tar_stamps(path):
    """Every distinct member mtime in a tar, `.tar.gz` or not."""
    import tarfile
    with tarfile.open(path, "r:*") as tf:
        return sorted({int(m.mtime) for m in tf.getmembers()})


def gate_epoch(pairs):
    """Every timestamp is the epoch the build was GIVEN, at more than one
    epoch, so a constant of the tool's own cannot pass.

    `pairs` is (output directory, the epoch the build that filled it was handed)
    and there is more than one of them, which is the whole point of the row and
    not a generalisation for its own sake. Asked at one epoch this row cannot
    tell a back end that read `SOURCE_DATE_EPOCH` from a back end that writes a
    constant of its own choosing which happens to be that number -- and the
    number it would be handed by accident is the worst one available, since the
    default this task builds at is also the example in the `SOURCE_DATE_EPOCH`
    specification the instruction links and the git date the environment image
    stamps onto `/app`. Asked at two, only a back end that read the variable can
    answer both.

    One row over all of the pairs, not one row each: "the artifacts carry the
    epoch the build was given" is one property, and `test.sh` weighs it as one
    row of one check. So a directory that produced no artifact at all is a fault
    of this row too -- a third build that silently built nothing would otherwise
    let the second epoch go unasked while the row still passed on the first.

    Zip stores a DOS timestamp, which has two-second granularity and cannot
    represent an odd second; an epoch of 1409491949 therefore appears in a wheel
    as 1409491948 and that is the value this row requires. A tar header holds
    the seconds as they are.
    """
    label = "repro.epoch"
    faults = []
    counts = []
    for dirpath, epoch in pairs:
        # Named by the last component of the output directory, because "carries
        # timestamps that are not N" is ambiguous once there is more than one N:
        # a candidate reading the detail has to be able to tell which build --
        # and therefore which epoch -- produced the artifact that failed.
        where = os.path.basename(dirpath.rstrip("/")) or dirpath
        even = epoch - (epoch % 2)
        checked = 0
        for name, path in sorted(artifacts_in(dirpath, ".whl").items()):
            checked += 1
            bad = [v for v in zip_stamps(path) if v != even]
            if bad:
                faults.append("%s/%s (built at %d) carries %d timestamps that "
                              "are not %d: %s"
                              % (where, name, epoch, len(bad), even, bad[:4]))
        for name, path in sorted(artifacts_in(dirpath, ".tar.gz").items()):
            checked += 1
            bad = [v for v in tar_stamps(path) if v != epoch]
            if bad:
                faults.append("%s/%s (built at %d) carries %d timestamps that "
                              "are not %d: %s"
                              % (where, name, epoch, len(bad), epoch, bad[:4]))
        counts.append((checked, epoch, where))
    if not sum(c for c, _e, _w in counts):
        # Every build produced nothing, not one of them: said in the words
        # `test.sh`'s comment above the call quotes, because a tree that built
        # nothing has already been told so by every build row above this one.
        row(label, False, "no artifacts were built, so nothing carries a timestamp")
        return
    for checked, epoch, where in counts:
        if not checked:
            faults.append("%s holds no artifact at all, so the build at %d "
                          "carries no timestamp to read" % (where, epoch))
    row(label, not faults,
        "; ".join(faults)[:290] if faults
        else "every timestamp is the epoch its own build was given -- %s (a "
             "wheel carries the even second below it, which is all a DOS "
             "timestamp can hold)"
             % ", ".join("%d artifacts at %d in %s" % (checked, epoch, where)
                         for checked, epoch, where in counts))


# --------------------------------------------------------------------------

SOURCE_LABELS = ["pin.requires"]
ARTIFACT_LABELS = ["repro.wheel", "repro.sdist", "repro.epoch", "pin.effective"]


def check_emitted(mode, want):
    got = [label for _family, label, _ok, _detail in ROWS]
    if sorted(got) != sorted(want):
        sys.stderr.write(
            "GATE FAULT (%s): emitted %d rows, expected %d; missing %r; "
            "unexpected %r\n"
            % (mode, len(got), len(want), sorted(set(want) - set(got)),
               sorted(set(got) - set(want))))
        raise SystemExit(4)


def main(argv):
    mode = argv[1]

    if mode == "source":
        tree = argv[2]
        gate_pin_requires(tree)
        # Where the distributions are, for the caller that has to build them.
        # Derived here from the same whole-repo scan `pin.requires` uses rather
        # than written down in `test.sh`, so "every distribution in the
        # workspace" means one thing in this step and not two: a candidate who
        # adds a fourth distribution gets it built and compared, and a
        # candidate who moves one is still built from where it now is.
        # Scored by nothing; stderr, like every other handoff in this suite.
        for name, path in sorted(distributions(tree).items()):
            sys.stderr.write("DIST %s %s\n" % (
                name, gatelib.relpath(os.path.dirname(path), tree)))
        check_emitted(mode, SOURCE_LABELS)
        flush()
        return 0

    if mode == "artifacts":
        # artifacts <tree> <dir_a> <dir_b> <epoch_a> [<dir_c> <epoch_c> ...]
        #
        # `dir_a` and `dir_b` are the two builds made at the same epoch, and
        # they are the only two anything is compared against: a third directory
        # is there because it was built at a DIFFERENT epoch, so its bytes are
        # supposed to differ from the first two's and comparing them would score
        # the opposite of the property. Every (directory, epoch) pair, `dir_a`'s
        # included, goes to `repro.epoch`.
        #
        # Any number of trailing pairs, rather than exactly one more, so that
        # adding a fourth epoch later is a change to `test.sh` and not to the
        # argument grammar -- and so that a pair dropped by a shell quoting
        # mistake is a hard error here rather than an epoch that silently stops
        # being asked about.
        tree, dir_a, dir_b = argv[2], argv[3], argv[4]
        rest = argv[6:]
        if len(rest) % 2:
            raise SystemExit(
                "artifacts: the arguments after <epoch_a> are (directory, "
                "epoch) pairs; got an odd %d of them: %r" % (len(rest), rest))
        pairs = [(dir_a, int(argv[5]))]
        pairs += [(rest[i], int(rest[i + 1])) for i in range(0, len(rest), 2)]
        wheels = compare_builds("repro.wheel", dir_a, dir_b, ".whl",
                                read_bytes, "wheel")
        compare_builds("repro.sdist", dir_a, dir_b, ".tar.gz",
                       tar_of, "sdist")
        gate_epoch(pairs)
        gate_pin_effective(tree, wheels)
        check_emitted(mode, ARTIFACT_LABELS)
        flush()
        return 0

    raise SystemExit("unknown mode %r" % mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
