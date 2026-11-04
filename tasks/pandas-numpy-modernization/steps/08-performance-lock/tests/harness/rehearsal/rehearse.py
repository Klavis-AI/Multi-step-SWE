"""Check the gates against a known answer before any of them scores anything.

Trap 6 of the authoring prompt, stated as a program: *verify the instrument
before the finding; pair every clean control with a synthetic failure fixture.*
A gate that never fires and a gate that fires on everything both produce a
plausible-looking number, and neither is visible from the number alone. So:

``clean/``         every near miss -- the spelling a migrated tree uses, sitting
                   where the retired one used to be. Must produce zero findings
                   at step 8 and zero suppressions. A hit here is a rule
                   matching something broader than it claims.
``dirty/``         one positive occurrence of every rule and every suppression
                   kind, counted by hand in ``expected.json``. A miss here is a
                   rule that has never been shown to fire.
``dirty_syntax/``  a file that will not parse and a notebook that is not JSON,
                   so that "it does not compile" is never read as "it contains
                   none of the constructs".

Run with no arguments to check everything, ``--skip-ruff`` where no ruff binary
is available. Exit status is 0 only if every check passed.

This runs at image build time, not at scoring time: a broken instrument has to
fail the verifier image that carries it, never a submission that is measured
by it.
"""
from __future__ import print_function, absolute_import, division

import argparse
import json
import os
import sys

sys.dont_write_bytecode = True    # never leave __pycache__ in a build context

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

# In the verifier image the two packages sit side by side (`/tests/harness`),
# so the line above is enough. In the authoring tree the gates live one level
# deeper than the probe (`harness/gates/skutil_gates` next to
# `harness/skutil_probe`), and without this the casebook fixture skips itself
# exactly where it is being developed.
_up = os.path.dirname(os.path.dirname(HERE))
if os.path.isdir(os.path.join(_up, "skutil_probe")):
    sys.path.insert(1, _up)

from skutil_gates import probe, ruffgate, rules, scan, score as score_mod  # noqa: E402
from skutil_gates import census, grade, suppress, suite                    # noqa: E402

FAILURES = []
CHECKS = [0]


def check(label, got, want):
    CHECKS[0] += 1
    if got == want:
        print("  ok    %s" % label)
        return True
    print("  FAIL  %s" % label)
    print("        expected: %r" % (want,))
    print("        observed: %r" % (got,))
    FAILURES.append(label)
    return False


def load_expected():
    with open(os.path.join(HERE, "expected.json")) as fh:
        return json.load(fh)


# --------------------------------------------------------------------------


def check_tree(name, spec):
    print("tree %s" % name)
    root = os.path.join(HERE, name)
    findings, files = scan.scan_tree(root, 8)
    counts = scan.counts(findings)
    check("%s: files walked" % name, files, spec["files"])
    check("%s: ast counts at step 8" % name, counts, spec["ast_counts"])

    supp = suppress.scan_tree(root)
    check("%s: suppression total" % name, supp["total"],
          spec["suppressions"]["total"])
    check("%s: suppressions by kind" % name, supp["by_kind"],
          spec["suppressions"]["by_kind"])

    # Byte-for-byte determinism: two scans of one tree must be the same report,
    # or a diff of two reports is not a diff of the tree.
    again, _ = scan.scan_tree(root, 8)
    check("%s: scan is deterministic" % name,
          json.dumps(again, sort_keys=True), json.dumps(findings, sort_keys=True))


def check_catalogue(expected):
    """Every rule is staged where the design says, and every rule is exercised."""
    print("catalogue")
    want_since = expected["since"]
    got_since = dict((r.name, r.since) for r in rules.RULES)
    check("catalogue: rule set", sorted(got_since), sorted(want_since))
    check("catalogue: since table", got_since, want_since)

    dirty_counts = expected["trees"]["dirty"]["ast_counts"]
    untested = sorted(r.name for r in rules.RULES
                      if dirty_counts.get(r.name, 0) < 1)
    check("catalogue: every rule has a positive fixture", untested, [])

    # Unique names, or `by_name` silently resolves to whichever came first.
    names = [r.name for r in rules.RULES]
    check("catalogue: names are unique", len(set(names)), len(names))


def check_staging(expected):
    """A rule fires at its own step and is silent at the step before it."""
    print("staging")
    root = os.path.join(HERE, "dirty")
    per_step = {}
    for step in range(0, 9):
        findings, _ = scan.scan_tree(root, step)
        per_step[step] = scan.counts(findings)

    full = expected["trees"]["dirty"]["ast_counts"]
    bad_on = []
    bad_before = []
    for name, since in sorted(expected["since"].items()):
        if per_step[since].get(name, 0) != full.get(name, 0):
            bad_on.append(name)
        if per_step[since - 1].get(name, 0) != 0:
            bad_before.append(name)
    check("staging: each rule is at full count from its own step", bad_on, [])
    check("staging: each rule is silent one step earlier", bad_before, [])

    # And the ladder never goes backwards.
    shrank = []
    for step in range(1, 9):
        for name, count in per_step[step - 1].items():
            if per_step[step].get(name, 0) < count:
                shrank.append((step, name))
    check("staging: no count shrinks as the ladder climbs", shrank, [])


def check_ruff(expected, ruff_bin):
    print("ruff")
    clean = ruffgate.run(os.path.join(HERE, "clean"), 8, ruff=ruff_bin)
    if clean.get("error"):
        check("ruff: ran at all", clean["error"], None)
        return
    check("ruff: clean tree total", clean["total"],
          expected["ruff"]["clean_total"])

    dirty = ruffgate.run(os.path.join(HERE, "dirty"), 8, ruff=ruff_bin)
    missing = [c for c in expected["ruff"]["dirty_codes_at_least"]
               if dirty.get("by_code", {}).get(c, 0) < 1]
    check("ruff: dirty tree produces every expected code", missing, [])
    check("ruff: dirty tree is not empty", dirty["total"] > 0, True)

    # The staged selection has to actually narrow what is measured, or the
    # per-step ruff gates are all the same gate.
    step1 = ruffgate.selection_for(1)
    step8 = ruffgate.selection_for(8)
    check("ruff: step 1 selection is narrower than step 8",
          len(step1) < len(step8), True)


def check_score(expected):
    """The four corners of the scoring contract, computed by hand."""
    print("score")
    manifest = {
        "step": 0,
        "new": [
            {"name": "seeds", "kind": "ast", "rule": "global-rng-seed",
             "entry": 10},
            {"name": "draws", "kind": "ast", "rule": "global-rng-draw",
             "entry": 4},
        ],
        # Two of them, deliberately: with one carried row a mean and an
        # all-or-nothing family produce the same number, so a single row cannot
        # tell the contract from a plausible mis-implementation of it.
        "carried": [
            {"name": "py2", "kind": "ast", "rule": "py2-builtin", "entry": 0},
            {"name": "abc", "kind": "ast", "rule": "collections-abc-alias",
             "entry": 0},
        ],
    }

    def obs(seeds, draws, py2, abc=0):
        return {"ast_counts": {"global-rng-seed": seeds,
                               "global-rng-draw": draws,
                               "py2-builtin": py2,
                               "collections-abc-alias": abc}}

    want = expected["score"]
    check("score: untouched tree scores zero",
          score_mod.score(manifest, obs(10, 4, 0))["score"], want["untouched"])
    check("score: finished tree scores one",
          score_mod.score(manifest, obs(0, 0, 0))["score"], want["fixed"])
    check("score: half the work scores a quarter",
          score_mod.score(manifest, obs(5, 2, 0))["score"],
          want["half_removed"])
    check("score: one carried regression costs the family",
          score_mod.score(manifest, obs(0, 0, 1))["score"],
          want["carried_regression"])
    # The same regression with the other row intact. A mean over two rows would
    # answer 0.5 here; the contract says a regression is not something you can
    # be half of, and says it no matter how many carried rows a later step adds.
    check("score: the surviving carried row does not dilute it",
          score_mod.score(manifest, obs(0, 0, 0, 1))["score"],
          want["carried_regression"])
    check("score: carried_mean is the family, not an average",
          score_mod.score(manifest, obs(0, 0, 1))["carried_mean"], 0.0)

    # A count above the entry count is worse than doing nothing, and must not
    # come out as credit through a negative-room division.
    check("score: adding findings scores zero",
          score_mod.score(manifest, obs(20, 8, 0))["score"], 0.0)

    # A grouped check counts its rules together and nothing else.
    grouped = {"name": "rng", "kind": "ast-sum",
               "rules": ["global-rng-seed", "global-rng-draw"]}
    check("score: a grouped check sums exactly its own rules",
          score_mod.observe(grouped, obs(10, 4, 7)), 14)
    check("score: a grouped check counts a rule nobody reported as zero",
          score_mod.observe({"name": "x", "kind": "ast-sum",
                             "rules": ["global-rng-seed", "no-such-rule"]},
                            obs(10, 4, 0)), 10)

    # A tree that answers a parse gate with `rm` has to be visible, so the
    # control and the break here differ by exactly one deleted file.
    inv = {"name": "files", "kind": "paths", "expect": ["a.py", "b/c.py"]}
    check("score: a tree that kept its files is missing none",
          score_mod.observe(inv,
                            {"tree_paths": ["a.py", "b/c.py", "new.py"]}), 0)
    check("score: a deleted file is counted",
          score_mod.observe(inv, {"tree_paths": ["a.py"]}), 1)
    check("score: an observation with no inventory is not a complete tree",
          score_mod.observe(inv, {}), 2)

    # ruff failing to start must fail the check that reads it, never pass it.
    broken = {"ruff": {"error": "ruff did not run", "by_code": {}}}
    check("score: a ruff that did not run counts as dirty",
          score_mod.observe({"name": "npy", "kind": "ruff", "code": "NPY"},
                            broken) > 0, True)


def check_replacement_and_lock():
    """The two row kinds review round 2 added at step 8, against hand-computed
    answers: `replaced` (a construct swapped for another, so that deleting the
    old one is not the same as replacing it) and `lock` (a PEP 751 lock of the
    environment), plus the readers that feed them."""
    print("replacement and lock")
    import shutil
    import tempfile
    from skutil_gates import run as run_mod

    rep = {"name": "p", "kind": "replaced", "rule": "pickle-persistence",
           "require": [{"calls": ["skops.io.dump", "skops.io.dumps"],
                        "at_least": 1},
                       {"calls": ["skops.io.load", "skops.io.loads"],
                        "at_least": 1}],
           "entry": 4, "limit": 0}

    def calls(dump, load):
        return {"skops.io.dump": dump, "skops.io.dumps": 0,
                "skops.io.load": load, "skops.io.loads": 0}

    check("replaced: the untouched tree reads its entry count",
          score_mod.observe(rep, {"ast_counts": {"pickle-persistence": 2},
                                  "calls": calls(0, 0)}), 4)
    check("replaced: deleting the pickle calls clears only half of it",
          score_mod.observe(rep, {"ast_counts": {}, "calls": calls(0, 0)}), 2)
    check("replaced: a save and a load through skops clears it",
          score_mod.observe(rep, {"ast_counts": {}, "calls": calls(1, 1)}), 0)
    check("replaced: adding skops beside the pickle is not a replacement",
          score_mod.observe(rep, {"ast_counts": {"pickle-persistence": 2},
                                  "calls": calls(1, 1)}), 2)
    check("replaced: an observation with no call census misses both",
          score_mod.observe(rep, {"ast_counts": {}}), 2)
    ex = dict(rep, executes=["a.ipynb", "b.ipynb"])
    check("replaced: a notebook that ran costs nothing",
          score_mod.observe(ex, {"ast_counts": {}, "calls": calls(1, 1),
                                 "notebooks": {"a.ipynb": {"ok": True},
                                               "b.ipynb": {"ok": True}}}), 0)
    check("replaced: a notebook that failed costs one",
          score_mod.observe(ex, {"ast_counts": {}, "calls": calls(1, 1),
                                 "notebooks": {"a.ipynb": {"ok": True},
                                               "b.ipynb": {"ok": False}}}), 1)
    check("replaced: a notebook run that never reported fails every notebook",
          score_mod.observe(ex, {"ast_counts": {}, "calls": calls(1, 1)}), 2)

    lock = {"name": "l", "kind": "lock", "lock_version": "1.0",
            "expect": {"numpy": "2.3.5", "scikit-learn": "1.7.2",
                       "meson-python": "0.18.0"},
            "entry": 3, "limit": 0}

    def lk(packages, version="1.0", present=True, error=None):
        return {"lock": {"present": present, "error": error,
                         "lock_version": version, "packages": packages}}

    full = {"numpy": ["2.3.5"], "scikit-learn": ["1.7.2"],
            "meson-python": ["0.18.0"]}
    check("lock: no file pins nothing",
          score_mod.observe(lock, lk({}, None, False)), 3)
    check("lock: every pin at its version",
          score_mod.observe(lock, lk(full)), 0)
    check("lock: a wrong version is a missing pin",
          score_mod.observe(lock, lk(dict(full, numpy=["2.3.4"]))), 1)
    check("lock: a lock of another format version pins nothing",
          score_mod.observe(lock, lk(full, "2.0")), 3)
    check("lock: an unreadable file pins nothing",
          score_mod.observe(lock, lk(full, error="unreadable")), 3)

    tmp = tempfile.mkdtemp(prefix="rehearsal-lock-")
    try:
        with open(os.path.join(tmp, "pylock.toml"), "w") as fh:
            fh.write('lock-version = "1.0"\ncreated-by = "rehearsal"\n\n'
                     '[[packages]]\nname = "NumPy"\nversion = "2.3.5"\n\n'
                     '[[packages]]\nname = "scikit_learn"\nversion = "1.7.2"\n\n'
                     '[[packages]]\nname = "meson.python"\nversion = "0.18.0"\n')
        read = run_mod.read_lock(tmp)
        check("lock: the reader normalises names the way PEP 503 does",
              sorted(read["packages"]), ["meson-python", "numpy", "scikit-learn"])
        check("lock: and a lock read off disk scores clean",
              score_mod.observe(lock, {"lock": read}), 0)
        with open(os.path.join(tmp, "pylock.toml"), "w") as fh:
            fh.write("lock-version = [\n")
        check("lock: a file that is not TOML is an error, not a pass",
              bool(run_mod.read_lock(tmp)["error"]), True)
        os.remove(os.path.join(tmp, "pylock.toml"))
        os.symlink("/etc/hostname", os.path.join(tmp, "pylock.toml"))
        check("lock: a symlink is not followed",
              bool(run_mod.read_lock(tmp)["error"]), True)
        os.remove(os.path.join(tmp, "pylock.toml"))
        os.makedirs(os.path.join(tmp, "doc"))
        with open(os.path.join(tmp, "doc", "persist.py"), "w") as fh:
            fh.write("import skops.io as sio\nfrom skops.io import load\n"
                     "sio.dump(m, 'm.skops')\nm = load('m.skops', trusted=[])\n"
                     "import joblib\njoblib.dump(m, 'm.pkl')\n")
        got = scan.call_census(tmp, run_mod.SKOPS_CALLS)
        check("call census: aliases and from-imports resolve to skops.io",
              (got["skops.io.dump"], got["skops.io.load"]), (1, 1))

        def notebook(cells):
            return json.dumps({"cells": [
                {"cell_type": "code", "metadata": {}, "outputs": [],
                 "execution_count": None, "source": c} for c in cells],
                "metadata": {}, "nbformat": 4, "nbformat_minor": 0})
        with open(os.path.join(tmp, "doc", "good.ipynb"), "w") as fh:
            fh.write(notebook([["x = 1\n", "%matplotlib inline\n"],
                               ["open('saved.txt', 'w').write(str(x))\n",
                                "!rm saved.txt"]]))
        with open(os.path.join(tmp, "doc", "bad.ipynb"), "w") as fh:
            fh.write(notebook([["x = 1\n"], ["raise ValueError('boom')\n"]]))
        # IPython's own spelling of an escape and a magic, and the kernel's
        # display(): a notebook saved that way runs under a kernel, so it runs
        # here; and an error after them still fails it.
        with open(os.path.join(tmp, "doc", "ipy.ipynb"), "w") as fh:
            fh.write(notebook([["x = 2\n", "display(x)\n"],
                               ["get_ipython().system('rm saved.txt')\n",
                                "get_ipython().run_line_magic('matplotlib', "
                                "'inline')\n"]]))
        with open(os.path.join(tmp, "doc", "ipybad.ipynb"), "w") as fh:
            fh.write(notebook([["get_ipython().system('true')\n"],
                               ["raise ValueError('after escape')\n"]]))
        ran = run_mod.execute_notebooks(
            tmp, ("doc/good.ipynb", "doc/bad.ipynb", "doc/gone.ipynb",
                  "doc/ipy.ipynb", "doc/ipybad.ipynb"),
            timeout=60)
        check("notebooks: IPython's get_ipython()/display spelling runs as "
              "under a kernel", ran["doc/ipy.ipynb"]["ok"], True)
        check("notebooks: and does not hide an error after it",
              (ran["doc/ipybad.ipynb"]["ok"],
               "after escape" in (ran["doc/ipybad.ipynb"]["error"] or "")),
              (False, True))
        check("notebooks: one whose cells run passes, magics and all",
              ran["doc/good.ipynb"]["ok"], True)
        check("notebooks: one whose cell raises fails, and says why",
              (ran["doc/bad.ipynb"]["ok"],
               "boom" in (ran["doc/bad.ipynb"]["error"] or "")), (False, True))
        check("notebooks: one that is not there fails",
              ran["doc/gone.ipynb"]["ok"], False)
        check("notebooks: nothing a run saves lands in the tree",
              os.path.exists(os.path.join(tmp, "doc", "saved.txt")), False)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def check_census():
    """The census, and the two ways a tree can look intact and not be."""
    print("census")
    lib = ("import numpy as np\n"
           "COLUMNS = ['a', 'b']\n"
           "def draw(n):\n"
           "    return np.random.rand(n)\n"
           "class Thing(object):\n"
           "    pass\n"
           "try:\n"
           "    import simplejson as json\n"
           "except ImportError:\n"
           "    import json\n")
    got = census.census_source(lib, False)
    check("census: every module-level binding, including inside a try",
          [n for n in got["names"] if "." not in n],
          ["COLUMNS", "Thing", "draw", "json", "np"])
    check("census: a function-local name is not a module-level one",
          "n" in got["names"], False)

    # Members, because a class that keeps its name and loses its methods binds
    # exactly what it bound before -- measured at reward 1.000000 before this.
    members = ("class Reader(object):\n"
               "    def read(self):\n"
               "        def helper():\n"
               "            pass\n"
               "        return 1\n"
               "    def reset(self):\n"
               "        return 2\n")
    check("census: methods and nested defs are censused dotted",
          census.census_source(members, False)["names"],
          ["Reader", "Reader.read", "Reader.read.helper", "Reader.reset"])
    stubbed = "class Reader(object):\n    pass\n"
    check("census: stubbing the class keeps its name and loses its members",
          census.census_source(stubbed, False)["names"], ["Reader"])

    tests = ("def test_a():\n"
             "    assert 1 == 1\n"
             "    assert_array_equal([1], [1])\n"
             "def test_b():\n"
             "    with raises(ValueError):\n"
             "        pass\n")
    check("census: assertions are counted", census.census_source(tests, True)["asserts"], 3)
    gutted = "def test_a():\n    pass\ndef test_b():\n    pass\n"
    check("census: a body of `pass` keeps the names",
          census.census_source(gutted, True)["names"], ["test_a", "test_b"])
    check("census: and loses the assertions",
          census.census_source(gutted, True)["asserts"], 0)

    # And the line floor, which is what catches a class that keeps every
    # method and empties each one.
    kept = ("class Reader(object):\n"
            "    def read(self):\n"
            "        pass\n"
            "    def reset(self):\n"
            "        pass\n")
    check("census: names survive emptying every body",
          census.census_source(kept, False)["names"],
          ["Reader", "Reader.read", "Reader.reset"])
    check("census: the line count does not",
          (census.census_source(members, False)["lines"],
           census.census_source(kept, False)["lines"]), (7, 5))
    check("census: comments and blank lines are not code",
          census.census_source("# c\n\nx = 1\n", False)["lines"], 1)

    py2 = "project = u'skutil'\nextensions = ['a']\nprint 'built'\n"
    flat = census.census_source(py2, False)
    check("census: a file that will not parse is still censused",
          (flat["parsed"], flat["names"]), (False, ["extensions", "project"]))
    check("census: and still counted, in the same unit as one that does",
          flat["lines"], 3)

    # And the scorer's reading of both, which is what the manifest rows use.
    # Stripped before hashing, so the same line at two indents is one line.
    check("census: re-indenting a line is not losing it",
          set(census.census_source("def f():\n    return 1\n", False)["digests"])
          <= set(census.census_source(
              "class C:\n    def f():\n        return 1\n", False)["digests"]),
          True)
    check("census: a line repeated is not two lines kept",
          len(census.census_source("x = 1\nx = 1\n", False)["digests"]), 1)

    obs = {"census": {"a.py": {"def_names": ["x", "y"], "names": ["x", "y"],
                               "asserts": 0, "lines": 40},
                      "t.py": {"def_names": ["test_a"], "names": ["test_a"],
                               "asserts": 1, "lines": 9}}}
    spec = {"kind": "names", "expect": {"a.py": ["x", "y", "z"],
                                        "t.py": ["test_a"]},
            "allow": ["a.py::z"]}
    check("census: an allowed removal is not a loss",
          score_mod.observe(spec, obs), 0)
    spec = dict(spec, allow=[])
    check("census: an unallowed one is", score_mod.observe(spec, obs), 1)
    check("census: a file the tree no longer has loses every name it owed",
          score_mod.observe({"kind": "names", "expect": {"gone.py": ["a", "b"]}},
                            obs), 2)

    # The narrowing that fixes step 2's reward-killer: `source-census` scores
    # `def_names`, so retiring a dead import or a collapsed version flag is not
    # read as gutting, while dropping a definition still is.
    trimmed = census.census_source(
        "import sys\nSK18 = True\ndef f():\n    return 1\n", False)
    kept_def = census.census_source("def f():\n    return 1\n", False)
    check("census: def_names is definitions only, not imports or constants",
          (trimmed["def_names"], kept_def["def_names"]), (["f"], ["f"]))
    check("census: names still records the full binding set",
          "sys" in trimmed["names"] and "SK18" in trimmed["names"], True)
    check("census: dropping a dead import is not a source-census loss",
          score_mod.observe({"kind": "names", "expect": {"m.py": ["f"]}},
                            {"census": {"m.py": kept_def}}), 0)
    check("census: dropping a definition still is",
          score_mod.observe({"kind": "names", "expect": {"m.py": ["f", "g"]}},
                            {"census": {"m.py": kept_def}}), 1)
    floor = {"kind": "asserts", "expect": {"t.py": 1}}
    check("census: a test module at its floor passes",
          score_mod.observe(floor, obs), 0)
    check("census: one below it does not",
          score_mod.observe({"kind": "asserts", "expect": {"t.py": 2}}, obs), 1)
    check("census: a test module the census never saw does not",
          score_mod.observe({"kind": "asserts", "expect": {"nope.py": 1}}, obs), 1)
    check("census: a file above its line floor passes",
          score_mod.observe({"kind": "substance", "expect": {"a.py": 24}}, obs), 0)
    check("census: a gutted one does not",
          score_mod.observe({"kind": "substance", "expect": {"a.py": 41}}, obs), 1)
    check("census: and so does a file the census never saw",
          score_mod.observe({"kind": "substance", "expect": {"gone.py": 1}}, obs), 1)

    # Retention. The pairing that matters is the last one: the same file
    # passes the line floor and fails this, which is the whole reason it is
    # here -- a gutted file can pad its line count back and cannot pad this.
    handed = "aaaaaa" + "bbbbbb" + "cccccc" + "dddddd" + "eeeeee"
    ret = {"kind": "retention", "expect": {"a.py": handed}, "floor": 0.6}
    obs_all = {"census": {"a.py": {"lines": 40, "digests":
                                   ["aaaaaa", "bbbbbb", "cccccc",
                                    "dddddd", "eeeeee"]}}}
    check("census: a file that kept every line passes the retention floor",
          score_mod.observe(ret, obs_all), 0)
    obs_some = {"census": {"a.py": {"lines": 40, "digests":
                                    ["aaaaaa", "bbbbbb", "cccccc"]}}}
    check("census: three of five is at the floor and passes",
          score_mod.observe(ret, obs_some), 0)
    obs_few = {"census": {"a.py": {"lines": 40, "digests":
                                   ["aaaaaa", "bbbbbb"]}}}
    check("census: two of five is under it and does not",
          score_mod.observe(ret, obs_few), 1)
    obs_pad = {"census": {"a.py": {"lines": 99, "digests":
                                   ["aaaaaa"] + ["%06d" % i for i in range(60)]}}}
    check("census: padding clears the line floor",
          score_mod.observe({"kind": "substance", "expect": {"a.py": 24}},
                            obs_pad), 0)
    check("census: ... and does not clear the retention floor",
          score_mod.observe(ret, obs_pad), 1)
    check("census: a file the census never saw keeps nothing",
          score_mod.observe({"kind": "retention", "expect": {"gone.py": handed},
                             "floor": 0.6}, obs_all), 1)


def check_exec_and_dynamic():
    """Source in a string, and a name built at runtime, are still the thing."""
    print("indirection")
    active = rules.by_step(1)

    def counts(src):
        return scan.counts(scan.scan_source(src, "x.py", "library", active, "/"))

    # Each of these is two findings, not one, and deliberately so: reaching a
    # retired stream function through a runtime-built name breaks the property
    # the step is *new* work for (the draw itself) and the property the tree
    # already had (no indirection), so it is counted once against each.
    check("indirection: getattr on the global RNG counts once",
          counts('import numpy as np\ngetattr(np.random, "rand")(5)\n'),
          {"global-rng-draw": 1, "dynamic-legacy-access": 1})
    check("indirection: so does a dynamically imported one",
          counts('c = getattr(__import__("numpy.random", fromlist=["choice"]),'
                 ' "choice")\n'),
          {"global-rng-draw": 1, "dynamic-legacy-access": 2})
    check("indirection: and an unreadable attribute name",
          counts('import numpy as np\nn = "seed"\ngetattr(np.random, n)()\n'),
          {"global-rng-draw": 1, "dynamic-legacy-access": 1})
    check("indirection: a legitimate getattr is not a finding",
          counts('import numpy as np\ngetattr(np.random, "RandomState")(0)\n'
                 'getattr(self, "_x")()\n'),
          {})
    # The line above is the one that has to hold: `RandomState` is the
    # replacement the instruction names, the carried family is all-or-nothing,
    # and so a rule that fired on every reach into numpy.random would take an
    # honest submission to zero for spelling one oddly.
    check("indirection: fetching the replacement is not an indirection",
          counts('import numpy as np\nRS = getattr(np.random, "RandomState")\n'),
          {})
    check("indirection: fetching a retired stdlib stream function is",
          counts('import random\nSH = getattr(random, "shuffle")\n'),
          {"stdlib-global-rng": 1, "dynamic-legacy-access": 1})
    check("indirection: importing a global RNG module at runtime is too",
          counts('import importlib\n'
                 'm = importlib.import_module("numpy.random")\n'),
          {"dynamic-legacy-access": 1})
    check("indirection: importing anything else at runtime is not",
          counts('import importlib\n'
                 'm = importlib.import_module("numpy.linalg")\n'), {})
    check("indirection: source inside exec is scanned",
          counts('import numpy as np\nexec("np.random.seed(0)")\n'),
          {"global-rng-seed": 1, "dynamic-legacy-access": 1})
    check("indirection: with the enclosing file's imports in scope",
          counts('import numpy as onp\neval("onp.random.rand(2)")\n'),
          {"global-rng-draw": 1, "dynamic-legacy-access": 1})
    # Both lines above count twice for the reason the getattr checks do: the
    # exec'd source is scanned, so the draw itself is seen, and putting it in a
    # string is separately an indirection. The alias matters -- matching only
    # the canonical spelling would hand `np.` back for free in a file that
    # says `import numpy as np`, which is every file in this tree.
    check("indirection: an exec'd fragment that will not parse is unparsable",
          counts('exec("print 1")\n'), {"unparsable": 1})
    check("indirection: a non-literal exec argument is left alone",
          counts('exec(open("x").read())\n'), {})

    # The four spellings that reach the same function without a getattr and
    # without an import. Each was invisible to the first version of this rule,
    # and each is worth the same +0.135 the getattr disguise was.
    check("indirection: sys.modules is a fetch",
          counts('import sys\nsys.modules["numpy.random"].seed(0)\n'),
          {"dynamic-legacy-access": 1})
    check("indirection: vars() on the module is a fetch",
          counts('import numpy as np\nvars(np.random)["rand"](5)\n'),
          {"dynamic-legacy-access": 1})
    check("indirection: so is the module's __dict__",
          counts('import numpy as np\nnp.random.__dict__["rand"](5)\n'),
          {"dynamic-legacy-access": 1})
    check("indirection: and so is attrgetter applied to it",
          counts('import numpy as np\nimport operator\n'
                 'operator.attrgetter("seed")(np.random)(0)\n'),
          {"dynamic-legacy-access": 1})
    check("indirection: methodcaller has the same shape",
          counts('import random\nimport operator\n'
                 'operator.methodcaller("shuffle", v)(random)\n'),
          {"dynamic-legacy-access": 1})
    check("indirection: a __dict__ key built at runtime counts",
          counts('import numpy as np\nnp.random.__dict__[picked](0)\n'),
          {"dynamic-legacy-access": 1})

    # And the near-misses. Every one of these is something an honest
    # submission may legitimately contain, and the carried family is
    # all-or-nothing, so a false positive here costs a real agent everything.
    check("indirection: sys.modules on any other module is not",
          counts('import sys\nm = sys.modules["skutil.utils"]\n'
                 'n = sys.modules[__name__]\n'), {})
    check("indirection: vars() and __dict__ elsewhere are not",
          counts('import numpy as np\nvars(np)["pi"]\n'
                 'np.linalg.__dict__["svd"](x)\n'), {})
    check("indirection: attrgetter for anything else is not",
          counts('import operator\noperator.attrgetter("shape")(arr)\n'),
          {})
    check("indirection: fetching RandomState through __dict__ is not",
          counts('import numpy as np\n'
                 'np.random.__dict__["RandomState"](0)\n'
                 'vars(np.random)["RandomState"](0)\n'), {})
    check("indirection: an eval that names nothing retired is not",
          counts('import numpy as np\neval("np.linalg.svd")(m)\n'
                 'eval("1 + 1")\n'), {})
    check("indirection: a seeded RandomState the caller owns is not",
          counts('import numpy as np\nrs = np.random.RandomState(0)\n'
                 'rs.seed(4)\nrs.shuffle(v)\n'), {})


def check_suite():
    """The inherited-suite floor, against a four-test table computed by hand.

    The two rows that matter most are the ones that are easy to get backwards:
    repairing an inherited failure must be free, and deleting a test must cost
    exactly what that test was worth.
    """
    print("suite")
    expected = {"outcomes": {"m::a": "passed", "m::b": "passed",
                             "m::c": "failed", "m::d": "passed"}}

    def sc(outcomes, error=None):
        return suite.score(expected, {"error": error, "outcomes": outcomes})

    passing = {"m::a": "passed", "m::b": "passed", "m::c": "failed",
               "m::d": "passed"}
    check("suite: the recorded table scores one", sc(passing)["score"], 1.0)

    repaired = dict(passing, **{"m::c": "passed"})
    check("suite: repairing an inherited failure is free",
          sc(repaired)["score"], 1.0)
    check("suite: and is reported as a repair", sc(repaired)["repaired"],
          ["m::c"])

    check("suite: one regression costs one test",
          sc(dict(passing, **{"m::a": "failed"}))["score"], 0.75)
    check("suite: a skip is a regression too",
          sc(dict(passing, **{"m::a": "skipped"}))["score"], 0.75)

    deleted = dict(passing)
    del deleted["m::d"]
    check("suite: deleting a test costs the same as failing it",
          sc(deleted)["score"], 0.75)
    check("suite: deleting every test scores zero", sc({})["score"], 0.0)

    added = dict(passing, **{"m::new": "passed"})
    check("suite: a test the candidate added earns nothing",
          sc(added)["score"], 1.0)

    check("suite: a suite that did not run scores zero",
          sc({}, error="timeout")["score"], 0.0)
    check("suite: an empty expectation is not a pass",
          suite.score({"outcomes": {}},
                      {"error": None, "outcomes": {"x": "passed"}})["score"],
          0.0)

    # The junit reader is the half of this that touches pytest's output format,
    # so it gets a fixture rather than trust.
    import tempfile
    handle, path = tempfile.mkstemp(suffix=".xml")
    os.close(handle)
    try:
        with open(path, "w") as fh:
            fh.write('<?xml version="1.0" encoding="utf-8"?>\n'
                     '<testsuites><testsuite name="pytest" tests="4">\n'
                     '<testcase classname="pkg.tests.test_m" name="test_ok"/>\n'
                     '<testcase classname="pkg.tests.test_m" name="test_bad">'
                     '<failure message="boom">tb</failure></testcase>\n'
                     '<testcase classname="pkg.tests.test_m" name="test_err">'
                     '<error message="boom">tb</error></testcase>\n'
                     '<testcase classname="pkg.tests.test_m" name="test_skip">'
                     '<skipped message="why"/></testcase>\n'
                     '</testsuite></testsuites>\n')
        got = suite._parse_junit(path)
    finally:
        os.remove(path)
    check("suite: junit outcomes are read from classname::name", got, {
        "pkg.tests.test_m::test_ok": "passed",
        "pkg.tests.test_m::test_bad": "failed",
        "pkg.tests.test_m::test_err": "error",
        "pkg.tests.test_m::test_skip": "skipped",
    })
    check("suite: an unreadable report is None, not an empty pass",
          suite._parse_junit(os.path.join(HERE, "does-not-exist.xml")), None)

    # Both spellings pytest can emit for the same test have to land on one id,
    # or a step that changed the package's install layout would report all 65
    # recorded tests missing and score zero for it.
    check("suite: the two rootdir spellings are the same node id",
          [suite._normalize_classname("utils.tests.test_util"),
           suite._normalize_classname("skutil.utils.tests.test_util"),
           suite._normalize_classname("site-packages.skutil.tests.test_big")],
          ["utils.tests.test_util", "utils.tests.test_util", "tests.test_big"])


def check_grade():
    """The reward arithmetic, at the corners the run contract depends on."""
    print("grade")

    def behavior(total, passed, adversarial_failures=0):
        results = []
        for i in range(total):
            ok = i < passed
            tags = ["adversarial"] if (not ok and i - passed
                                       < adversarial_failures) else []
            results.append({"case": "c%03d" % i, "ok": ok, "tags": tags})
        return {"total": total, "passed": passed, "results": results}

    def gates(value, new_mean=None, carried_mean=None):
        return {"score": value, "new": [], "carried": [],
                "new_mean": value if new_mean is None else new_mean,
                "carried_mean": 1.0 if carried_mean is None else carried_mean}

    full_suite = {"score": 1.0, "total": 65}
    check("grade: the golden solution scores one",
          grade.grade(behavior(100, 100), full_suite, gates(1.0))["reward"],
          1.0)

    # The two corners the run contract is written in terms of. The untouched
    # tree is what `harbor run --agent nop` submits, and rule 5 says it passes
    # family A completely and fails family B completely; the contract says it
    # reports 0.0. Those two sentences are the reason the families multiply,
    # and this is where that is asserted rather than argued.
    check("grade: the untouched tree -- all behavior, no gates -- scores zero",
          grade.grade(behavior(100, 100), full_suite,
                      gates(0.0, new_mean=0.0))["reward"],
          0.0)
    check("grade: a tree that migrated by breaking everything scores zero",
          grade.grade(behavior(100, 0), {"score": 0.0, "total": 65},
                      gates(1.0))["reward"],
          0.0)
    check("grade: nothing at all scores zero",
          grade.grade(None, None, None)["reward"], 0.0)

    # Trap 4 in the small: a family whose denominator is empty is a probe that
    # did not run, and reading it as a pass would hand out most of a reward for
    # a crash. Family A collapses to the suite's 0.15, and the product carries
    # that straight through to the reward.
    empty = grade.grade({"total": 0, "passed": 0, "results": []}, full_suite,
                        gates(1.0))
    check("grade: an empty corpus is not a pass", empty["reward"], 0.15)
    check("grade: and says why",
          bool(empty["families"]["behavior"]["corpus"]["detail"]["error"]),
          True)

    # Rule 5 wants neither family below ~30% of the reward. Under a product the
    # question is how far the reward moves when a family does, so ask it that
    # way: ten points off either family costs the same ten percent, and zeroing
    # either costs everything. Nothing in between can be reached by doing one
    # family twice as well instead of the other at all.
    ten_off_a = grade.grade(behavior(100, 90), {"score": 0.9, "total": 65},
                            gates(1.0))["reward"]
    ten_off_b = grade.grade(behavior(100, 100), full_suite,
                            gates(0.9, new_mean=0.9))["reward"]
    check("grade: ten points off either family costs the same",
          (round(ten_off_a, 6), round(ten_off_b, 6)), (0.9, 0.9))
    check("grade: neither family can carry the reward alone",
          [grade.grade(behavior(100, 100), full_suite,
                       gates(0.0, new_mean=0.0))["reward"],
           grade.grade(behavior(100, 0), {"score": 0.0, "total": 65},
                       gates(1.0))["reward"]],
          [0.0, 0.0])
    check("grade: and the report names the combination",
          grade.grade(behavior(100, 100), full_suite,
                      gates(1.0))["combination"], "A * B")

    # The recorded corpus has to outweigh the repository's own suite, or the
    # 65 tests the migration is allowed to rewrite would outvote the 1236
    # values it is not.
    corpus_loss = 1.0 - grade.grade(behavior(100, 90), full_suite,
                                    gates(1.0))["reward"]
    suite_loss = 1.0 - grade.grade(behavior(100, 100),
                                   {"score": 0.9, "total": 65},
                                   gates(1.0))["reward"]
    check("grade: losing 10% of the corpus costs more than 10% of the suite",
          corpus_loss > suite_loss, True)

    # The recorded corpus is two populations -- behaviours the migration must
    # keep and the must-flip inputs it must change -- multiplied, not summed.
    # A tree that keeps everything and un-migrates a tenth of the flips loses a
    # tenth of the corpus, where a single fraction over the union would lose a
    # tenth of the flip count out of the whole 1236.
    def with_flips(total, n_flips, flip_fail):
        results, applied = [], []
        for i in range(total):
            cid = "c%04d" % i
            if i < n_flips:
                applied.append({"case": cid})
                ok = i >= flip_fail
            else:
                ok = True
            results.append({"case": cid, "ok": ok, "tags": []})
        passed = sum(1 for r in results if r["ok"])
        return {"total": total, "passed": passed, "results": results,
                "flips_applied": applied}

    check("grade: the oracle passes both corpus populations and scores one",
          grade.grade(with_flips(1236, 119, 0), full_suite,
                      gates(1.0), min_flips=119)["reward"], 1.0)

    shim = grade.grade(with_flips(1236, 119, 17), full_suite,
                       gates(1.0), min_flips=119)
    shim_corpus = shim["families"]["behavior"]["corpus"]["fraction"]
    check("grade: un-migrating 17 of 119 flips costs the flip mean, not 17/1236",
          round(shim_corpus, 6), round(102 / 119.0, 6))
    check("grade: which is more than eight times the single-fraction penalty",
          (1.0 - shim_corpus) > 8 * (1.0 - 1219 / 1236.0), True)

    # Trap 4 for the flip factor: a mean of no flips is 1, which would undo the
    # split. A step that declares flips and applies none scores its corpus 0.
    lost = grade.grade(with_flips(1236, 0, 0), full_suite,
                       gates(1.0), min_flips=119)
    check("grade: a step that declares flips but applies none fails the corpus",
          lost["families"]["behavior"]["corpus"]["fraction"], 0.0)
    check("grade: and the report says the flips file did not load",
          "flips" in (lost["families"]["behavior"]["corpus"]["detail"]
                      .get("error") or ""), True)

    # Step 1 declares no flips: min_flips is 0, the factor is inert, and the
    # corpus is the plain fraction it was before the split existed.
    check("grade: a step with no declared flips uses the plain fraction",
          grade.grade(behavior(100, 90), full_suite, gates(1.0),
                      min_flips=0)["families"]["behavior"]["corpus"]["fraction"],
          0.9)


def check_importable():
    """Family B's precondition, both ways round.

    Every gate counts occurrences of something old and rewards fewer of them,
    so an empty tree is a perfect tree as far as the counts go. The clean
    control here and the stub next to it report *the same counts*; the only
    difference between them is whether the package imports, which is exactly
    the thing the check is supposed to be able to see.
    """
    print("importable")
    manifest = {"step": 1,
                "new": [{"name": "seeds", "kind": "ast",
                         "rule": "global-rng-seed", "entry": 10}],
                "carried": []}

    def obs(repro):
        return {"ast_counts": {"global-rng-seed": 0}, "repro": repro}

    # The real shape: `skutil` itself exports nothing but submodules, which the
    # probe drops, so the root's own list is empty on a perfectly good tree.
    live = obs({"public_api": {"skutil": {"names": []},
                               "skutil.base": {"names": ["BaseSkutil"]}}})
    stub = obs({"public_api": {"skutil":
                               {"error": "ImportError: No module named skutil"}}})
    hollow = obs({"public_api": {"skutil": {"names": []},
                                 "skutil.base": {"names": []}}})
    crashed = obs({"public_api": {"error": "probe exited 1"}})
    skipped = obs({"checks": {}, "detail": {"skipped": True}})

    check("importable: a root package that only re-exports submodules is fine",
          score_mod.score(manifest, live)["score"], 1.0)
    check("importable: the same counts from a tree that does not import score 0",
          score_mod.score(manifest, stub)["score"], 0.0)
    check("importable: and the report carries the interpreter's own reason",
          "No module named skutil"
          in score_mod.score(manifest, stub)["import_detail"]["why"], True)
    check("importable: a package whose every module is empty scores 0",
          score_mod.score(manifest, hollow)["score"], 0.0)
    check("importable: a probe that crashed is not a clean result",
          score_mod.score(manifest, crashed)["score"], 0.0)

    # The third state. `observe --no-repro` is an authoring aid that never
    # asked the question, and a tree nobody looked at must not read as a tree
    # that failed -- otherwise every count taken while building a manifest
    # would come back zero and the entry numbers would be nonsense.
    skipped_row = score_mod.score(manifest, skipped)
    check("importable: --no-repro is not a failed import",
          skipped_row["score"], 1.0)
    check("importable: ... and says it was never checked",
          skipped_row["import_detail"]["checked"], False)

    # End to end: the stub tree the review found, which scored about a tenth
    # of the step when the families were added together.
    stub_reward = grade.grade({"total": 100, "passed": 0, "results": []},
                              {"score": 0.0, "total": 65},
                              score_mod.score(manifest, stub))["reward"]
    check("importable: a stub tree scores nothing end to end", stub_reward, 0.0)


def check_casebooks():
    """One casebook that will not import must cost its own cases, not all 1236.

    The casebooks import the names they exercise at module scope, and the
    migrator calibration found what that costs: ruff's F401 fix deleted two
    unused re-exports from ``skutil/metrics/__init__.py``, one casebook's
    import raised, and the whole behaviour corpus -- 85% of family A -- read
    0.0 with no recording written at all. The gates charged two credits for the
    dropped names, which is the right price; the corpus charged everything,
    which is a cliff rather than a measurement.

    So the loader takes the casebooks one at a time, and this is the fixture
    that says so: two that import, one that raises the exact error the
    calibration produced, and a `compare` over the result to confirm the cases
    that never registered are scored as failures rather than quietly left out
    of the denominator.
    """
    print("casebooks")
    try:
        from skutil_probe import registry
        from skutil_probe import run as probe_run
    except Exception as exc:                          # noqa: BLE001
        print("  skip  (skutil_probe is not on this path: %s)" % exc)
        return

    import importlib
    import shutil
    import tempfile

    tmp = tempfile.mkdtemp(prefix="rehearsal-casebooks-")
    pkg = os.path.join(tmp, "rehearsal_casebooks")
    os.mkdir(pkg)

    def write(name, text):
        with open(os.path.join(pkg, name), "w") as fh:
            fh.write(text)

    write("__init__.py", "MODULES = ('c_ok_one', 'c_boom', 'c_ok_two')\n")
    case_src = ("from skutil_probe.registry import case\n"
                "\n"
                "@case('rehearsal.%s')\n"
                "def _c():\n"
                "    return %d\n")
    write("c_ok_one.py", case_src % ("one", 1))
    write("c_boom.py",
          "raise ImportError(\"cannot import name 'check_array' "
          "from 'skutil.metrics'\")\n")
    write("c_ok_two.py", case_src % ("two", 2))

    saved_cases = list(registry.CASES)
    saved_seen = set(registry._SEEN)
    saved_module = sys.modules.get("skutil_probe.cases")
    import skutil_probe
    saved_attr = getattr(skutil_probe, "cases", None)
    sys.path.insert(0, tmp)
    try:
        del registry.CASES[:]
        registry._SEEN.clear()
        fake = importlib.import_module("rehearsal_casebooks")
        sys.modules["skutil_probe.cases"] = fake
        skutil_probe.cases = fake

        cases = registry.load_all()
        check("casebooks: the casebooks that do import still register",
              [c["id"] for c in cases], ["rehearsal.one", "rehearsal.two"])
        check("casebooks: the one that does not is named, not swallowed",
              [e["module"] for e in registry.LOAD_ERRORS], ["c_boom"])
        check("casebooks: with the interpreter's own reason",
              "check_array" in registry.LOAD_ERRORS[0]["error"], True)
        check("casebooks: loading twice does not double the corpus",
              len(registry.load_all()), 2)

        # The other half: a case that never registered has to be a failure, not
        # a smaller denominator. `record` writes what it has, so the comparison
        # is where the absence gets charged.
        rec = os.path.join(tmp, "actual.json")
        probe_run.main(["record", "--out", rec])
        exp = os.path.join(tmp, "expected.json")
        with open(rec) as fh:
            payload = json.load(fh)
        check("casebooks: the recording carries the failure into the bundle",
              [e["module"] for e in payload["load_errors"]], ["c_boom"])
        # The baseline recorded three; the candidate tree can only produce two.
        third = dict(payload["records"][0])
        third["id"] = "rehearsal.three"
        with open(exp, "w") as fh:
            json.dump({"versions": payload["versions"],
                       "records": payload["records"] + [third]}, fh)
        out = os.path.join(tmp, "behavior.json")
        probe_run.main(["compare", "--expected", exp, "--actual", rec,
                        "--out", out])
        with open(out) as fh:
            report = json.load(fh)
        check("casebooks: an unregistered case is a failure",
              (report["total"], report["passed"], report["failed"]),
              (3, 2, 1))
        why = [r.get("why") for r in report["results"] if not r["ok"]]
        check("casebooks: and says why", why, ["missing"])
        check("casebooks: the grader can see it in the saved bundle",
              grade.corpus_fraction(report)[1]["casebooks_unimported"],
              ["c_boom"])
    finally:
        sys.path.remove(tmp)
        del registry.CASES[:]
        registry.CASES.extend(saved_cases)
        registry._SEEN.clear()
        registry._SEEN.update(saved_seen)
        for name in list(sys.modules):
            if name.startswith("rehearsal_casebooks"):
                del sys.modules[name]
        if saved_module is None:
            sys.modules.pop("skutil_probe.cases", None)
        else:
            sys.modules["skutil_probe.cases"] = saved_module
        if saved_attr is None:
            if hasattr(skutil_probe, "cases"):
                del skutil_probe.cases
        else:
            skutil_probe.cases = saved_attr
        shutil.rmtree(tmp, ignore_errors=True)


def check_probe():
    """The runtime probe answers, whatever is installed under it."""
    print("probe")
    sem = probe.semantics()
    wanted = ["copy_on_write", "default_string_dtype", "nep50_f32_plus_pyfloat",
              "nep50_u8_plus_200", "surviving_expired_names"]
    missing = [k for k in wanted if k not in sem]
    check("probe: semantics reports every key", missing, [])
    check("probe: versions of an absent distribution are None",
          probe.installed_versions(["definitely-not-installed"]),
          {"definitely-not-installed": None})


# --------------------------------------------------------------------------


def main(argv=None):
    p = argparse.ArgumentParser(prog="rehearse")
    p.add_argument("--ruff", default="ruff")
    p.add_argument("--skip-ruff", action="store_true")
    p.add_argument("--skip-probe", action="store_true")
    args = p.parse_args(argv)

    expected = load_expected()
    check_catalogue(expected)
    for name in ("clean", "dirty", "dirty_syntax"):
        check_tree(name, expected["trees"][name])
    check_staging(expected)
    if args.skip_ruff:
        print("ruff\n  skip  (--skip-ruff)")
    else:
        check_ruff(expected, args.ruff)
    check_score(expected)
    check_replacement_and_lock()
    check_census()
    check_exec_and_dynamic()
    check_suite()
    check_grade()
    check_importable()
    check_casebooks()
    if args.skip_probe:
        print("probe\n  skip  (--skip-probe)")
    else:
        check_probe()

    print("")
    if FAILURES:
        print("REHEARSAL FAILED: %d of %d checks" % (len(FAILURES), CHECKS[0]))
        for name in FAILURES:
            print("  - %s" % name)
        return 1
    print("rehearsal passed: %d checks" % CHECKS[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
