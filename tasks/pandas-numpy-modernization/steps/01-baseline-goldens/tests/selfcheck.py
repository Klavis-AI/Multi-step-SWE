"""Assert that this verifier image is the one the step was calibrated against.

Runs at image build time, next to ``rehearsal.rehearse``. The two answer
different questions: the rehearsal asks whether the gates can tell a clean tree
from a dirty one, this asks whether the *image* the gates will run in is the one
whose numbers are written into ``manifest.json``.

Every constant checked here is a constant of the image, not of the submission.
That is why they are checked once at build time and are **not** manifest rows:
in ``environment_mode = "separate"`` the verifier's own interpreter and library
versions cannot be influenced by the candidate, so a carried check reading them
could never fail. It would still be averaged into ``carried_mean``, where a row
that is always 1.0 softens the penalty for the rows that can move. Constants
belong in the build, not in the score.

A failure here fails ``docker build``. That is the intent: a verifier image
built against the wrong pandas must not exist, let alone grade anything.
"""
from __future__ import print_function

import io
import json
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, "/tests/harness")

from skutil_gates import probe, ruffgate, suppress          # noqa: E402

FAILURES = []


def check(label, got, want):
    if got == want:
        print("  ok    %s" % label)
        return
    print("  FAIL  %s" % label)
    print("        expected: %r" % (want,))
    print("        observed: %r" % (got,))
    FAILURES.append(label)


# The pins. `stack.txt` states them for pip; this states them for the scorer,
# and the two disagreeing is the failure this catches.
VERSIONS = {
    "numpy": "1.14.6",
    "scipy": "1.1.0",
    "pandas": "0.23.4",
    "scikit-learn": "0.20.4",
    "matplotlib": "2.2.5",
    "seaborn": "0.7.1",
    "six": "1.16.0",
    "Cython": "0.29.36",
    "pytest": "6.2.5",
    "setuptools": "59.8.0",
}

# What those versions mean, as behaviour rather than as strings. A wheel can be
# repaired in place under an unchanged version number; these are the specific
# semantics the corpus was recorded against and the ladder's later steps are
# defined as moving away from.
SEMANTICS = {
    "python": "3.7",
    "copy_on_write": "raised:OptionError",
    "default_string_dtype": "object",
    "default_int_dtype": "int64",
    "nep50_f32_plus_pyfloat": "float64",
    "nep50_i64_plus_f32": "float64",
    "nep50_u8_plus_200": "int64",
    "nep50_u8_array_plus_200": "uint8",
    "scalar_repr": "1.5",
    "view_tracks_parent": True,
}


# Everything pip put in the image, not just the pins this step is about.
# `stack.txt` carries the full transitive closure -- pytz, packaging, joblib
# and the rest -- because those move on their own schedule: pytz publishes a
# release whenever a government changes a timezone rule, and the behaviour
# corpus was recorded against one specific pytz. Pinning them is half of it;
# this is the other half, asserting that what the image *has* is what the file
# says, in both directions. A transitive dependency that appeared, vanished or
# shifted version between the recording and this build fails `docker build`
# rather than quietly grading against a stack nobody wrote down.
#
# pip, setuptools and wheel are dropped from both sides because `pip freeze`
# does not list them; the two that matter are pinned in `stack.txt` anyway and
# the interpreter build fixes the third.
NOT_IN_FREEZE = ("pip", "setuptools", "wheel")


def _norm(name):
    return re.sub(r"[-_.]+", "-", name).lower()


def _pins(text):
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "==" in line:
            name, _, _version = line.partition("==")
            out[_norm(name)] = line
    return out


def check_closure(path):
    declared = _pins(io.open(path, encoding="utf-8").read())
    frozen = _pins(subprocess.check_output(
        [sys.executable, "-m", "pip", "freeze"]).decode("utf-8", "replace"))
    for name in NOT_IN_FREEZE:
        declared.pop(_norm(name), None)
        frozen.pop(_norm(name), None)
    check("closure: nothing installed that stack.txt does not pin",
          sorted(n for n in frozen if n not in declared), [])
    check("closure: nothing pinned that is not installed",
          sorted(n for n in declared if n not in frozen), [])
    check("closure: every pin at the version stack.txt names",
          sorted(frozen[n] for n in frozen
                 if n in declared and frozen[n] != declared[n]), [])


def main():
    print("versions")
    got = probe.installed_versions(sorted(VERSIONS))
    for name in sorted(VERSIONS):
        check("versions: %s" % name, got.get(name), VERSIONS[name])

    print("closure")
    check_closure("/tests/stack.txt")

    print("semantics")
    sem = probe.semantics()
    for key in sorted(SEMANTICS):
        check("semantics: %s" % key, sem.get(key), SEMANTICS[key])
    # NumPy 2 expired eighteen of these names; every one must still be here, or
    # this is not a 1.14 image.
    check("semantics: np.in1d still exists",
          "in1d" in (sem.get("surviving_expired_names") or []), True)

    print("tools")
    try:
        out = subprocess.check_output(["ruff", "--version"],
                                      stderr=subprocess.STDOUT)
        version = out.decode("utf-8", "replace").strip()
    except Exception as exc:                              # noqa: BLE001
        version = "not runnable: %s" % exc
    check("tools: ruff version", version, "ruff 0.14.0")

    # Without a TOML reader the suppression scan cannot read pyproject.toml and
    # reports one `toml-unreadable` finding instead of what is in the file. The
    # ceiling in manifest.json was measured with a reader present.
    check("tools: a TOML reader is importable",
          bool(suppress.HAVE_TOML), True)

    # The staged selection for this step, so a mis-edit of ruffgate.SELECTIONS
    # shows up here rather than as a mysteriously easy gate.
    check("tools: step 1 ruff selection", ruffgate.selection_for(1),
          ["NPY002"])
    check("tools: step 1 ruff target", ruffgate.target_for(1), "py37")

    print("expectations")
    with open("/tests/expected/baseline-recording.json") as fh:
        recording = json.load(fh)
    check("expectations: recorded cases", len(recording["records"]), 1236)
    adversarial = sum(1 for r in recording["records"]
                      if "adversarial" in (r.get("tags") or []))
    check("expectations: at least a third of them adversarial",
          adversarial * 3 >= len(recording["records"]), True)
    check("expectations: the recording's pandas is this image's pandas",
          recording["versions"].get("pandas"), VERSIONS["pandas"])
    check("expectations: the recording's numpy is this image's numpy",
          recording["versions"].get("numpy"), VERSIONS["numpy"])

    with open("/tests/expected/inherited-suite.json") as fh:
        inherited = json.load(fh)
    check("expectations: recorded test outcomes", len(inherited["outcomes"]), 65)
    check("expectations: their census", inherited["census"],
          {"passed": 59, "failed": 6})

    with open("/tests/manifest.json") as fh:
        manifest = json.load(fh)
    check("expectations: the manifest is this step's", manifest["step"], 1)
    check("expectations: every new check has somewhere to fall from",
          [c["name"] for c in manifest["new"] if c["entry"] <= 0], [])
    rows = dict((c["name"], c) for c in manifest["new"] + manifest["carried"])
    api = rows["public-api"]
    check("expectations: the public-api row still names 75",
          sum(len(v) for v in api["expect"].values()), 75)

    # The inventory is the row that stops a parse gate being answered with
    # `rm`, so it has to actually list the file that would be deleted first.
    files = rows["file-inventory"]["expect"]
    check("expectations: the inventory covers the whole handed-over tree",
          len(files), 99)
    check("expectations: including the one nothing imports",
          "doc/sphinxext/numpy_ext/docscrape.py" in files, True)
    # 99, not the tree's 100: a Cython-generated .c that sits beside its .pyx is
    # build output -- any `pip install` regenerates and clobbers it, so its
    # presence tracks the last build, not a source edit -- and is not pinned.
    # The .pyx it comes from is pinned and is load-bearing (the build needs it),
    # so nothing real can be gutted through the gap.
    check("expectations: the cythonized .c is not pinned (it is build output)",
          "skutil/metrics/_kernel_fast.c" in files, False)
    check("expectations: but the .pyx it is generated from is",
          "skutil/metrics/_kernel_fast.pyx" in files, True)

    # `source-census` pins definitions only -- the names a file `def`s or
    # `class`es. A dead import or a collapsed version flag the migration
    # legitimately drops is not a gutting, so it is not counted here; the
    # fuller coverage (every file with any binding at all) is carried by the
    # substance and retention rows below, and it is those that stop the
    # inventory being satisfied by a hundred empty files.
    censused = rows["source-census"]["expect"]
    check("expectations: the census pins the definitions of the defining files",
          (len(censused), sum(len(v) for v in censused.values())), (39, 480))
    check("expectations: the vendored parser is censused down to its methods",
          len(censused.get("doc/sphinxext/numpy_ext/docscrape.py", [])), 51)
    check("expectations: three bindings are written down as removable",
          sorted(rows["source-census"]["allow"]),
          ["skutil/__init__.py::setup_module",
           "skutil/preprocessing/balance.py::choice",
           "skutil/preprocessing/tests/test_impute.py::choice"])
    # The substance and retention rows carry the fuller coverage: every file
    # with a binding, 52 of them -- the 39 that define, plus the 13 that only
    # assign or import, conf.py and the two notebooks among them. A file keeps
    # every name it binds while its methods are emptied, so each also carries a
    # floor on how much code is left in it.
    lines = rows["source-substance"]["expect"]
    check("expectations: the substance row covers every file with a binding",
          len(lines), 52)
    check("expectations: and every censused file is among them",
          set(censused) <= set(lines), True)
    check("expectations: including the two that do not parse on entry",
          [p for p in ("doc/conf.py",
                       "doc/examples/pipeline/skutil grid demo.ipynb")
           if not lines.get(p)], [])
    check("expectations: the floor is under the handed-over size, not at it",
          lines["doc/sphinxext/numpy_ext/docscrape.py"], 249)
    # Nor was a line *count* enough: it can be padded back with junk while the
    # code stays gone, so each file also records which lines it was handed.
    kept = rows["source-retention"]["expect"]
    width = rows["source-retention"]["width"]
    check("expectations: retention covers exactly the substance files",
          sorted(kept) == sorted(lines), True)
    check("expectations: recorded as fixed-width digests, nothing else",
          sorted(set(len(v) % width for v in kept.values())), [0])
    check("expectations: the vendored parser records its distinct code lines",
          len(kept["doc/sphinxext/numpy_ext/docscrape.py"]) // width, 347)
    check("expectations: and the floor is a fraction, not a count",
          rows["source-retention"]["floor"], 0.6)

    floors = rows["test-assertions"]["expect"]
    check("expectations: the assertion floor covers every asserting module",
          (len(floors), sum(floors.values())), (11, 266))
    check("expectations: including the ones the RNG sites live in",
          [p for p in ("skutil/preprocessing/tests/test_impute.py",
                       "skutil/preprocessing/tests/test_balance.py")
           if not floors.get(p)], [])

    # The launcher that keeps a candidate-writable cwd from shadowing the
    # harness must ship, and every stage that runs a harness module must go
    # through it. A future editor who reintroduces `python -m skutil_...` here
    # -- or drops _run.py -- reopens a root-level scorer compromise (item 5,
    # round 3), so the build refuses it rather than trusting review to catch it.
    check("expectations: the cwd-shadow launcher ships",
          os.path.exists("/tests/harness/_run.py"), True)
    with open("/tests/test.sh") as fh:
        testsh = fh.read()
    check("expectations: no stage imports the harness with -m",
          re.findall(r"python\b[^\n]*-m\s+skutil_", testsh), [])
    stages = sorted(set(re.findall(r"\b(skutil_\w+\.\w+)\b(?=\s)", testsh)))
    routed = sorted(set(re.findall(r"_run\.py\s+(skutil_\w+\.\w+)", testsh)))
    check("expectations: every harness stage runs through the launcher",
          stages, routed)

    # And a subtler reopening of the same compromise: a *root* context that
    # imports the candidate's package at all. `import skutil` runs the
    # candidate's skutil/__init__.py, so any such import outside run_stage is
    # candidate code executing as root, free to overwrite this harness --
    # grade.py included -- before the launcher ever gets a say (item 5, round 4;
    # the `installed=` diagnostic did exactly this). Join backslash
    # continuations, then require every line that imports the candidate package
    # to have dropped to the unprivileged uid first.
    logical, buf = [], ""
    for _ln in testsh.splitlines():
        buf += _ln
        if _ln.rstrip().endswith("\\"):
            buf = buf.rstrip()[:-1] + " "
            continue
        logical.append(buf); buf = ""
    if buf:
        logical.append(buf)
    _imp = re.compile(r"import\s+[^\n]*\bskutil\b(?!_)|from\s+skutil\b(?!_)")
    check("expectations: every candidate-package import runs unprivileged",
          [ln.strip() for ln in logical
           if _imp.search(ln) and "run_stage" not in ln
           and not ln.lstrip().startswith("#")], [])

    # Nothing in the payload may carry bytecode: a .pyc compiled on the author's
    # machine shadows the source it was compiled from, and the gate that runs is
    # then not the gate that was reviewed (trap 8).
    stale = []
    for dirpath, dirnames, filenames in os.walk("/tests"):
        for name in list(dirnames):
            if name == "__pycache__":
                stale.append(os.path.join(dirpath, name))
                dirnames.remove(name)
        for name in filenames:
            if name.endswith((".pyc", ".pyo")):
                stale.append(os.path.join(dirpath, name))
    check("expectations: no bytecode in the payload", stale, [])

    print("")
    if FAILURES:
        print("SELFCHECK FAILED: %d checks" % len(FAILURES))
        for name in FAILURES:
            print("  - %s" % name)
        return 1
    print("selfcheck passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
