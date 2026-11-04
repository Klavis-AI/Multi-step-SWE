"""Assert that this verifier image is the one the step was calibrated against.

Runs at image build time, next to ``rehearsal.rehearse``. The two answer
different questions: the rehearsal asks whether the gates can tell a clean tree
from a dirty one, this asks whether the *image* the gates will run in is the one
whose numbers are written into ``manifest.json`` and ``expected/``.

Every constant checked here is a constant of the image, not of the submission.
That is why they are checked once at build time and are **not** manifest rows:
in ``environment_mode = "separate"`` the verifier's own interpreter and library
versions cannot be influenced by the candidate, so a carried check reading them
could never fail. It would still be averaged into ``carried_mean``, where a row
that is always 1.0 softens the penalty for the rows that can move. Constants
belong in the build, not in the score.

Step 2 adds a second audience for this file: the flips. ``flips-step02.json``
is 119 hand-written claims of the form "the pinned libraries changed this
answer, and here is the new one". Two ways that can be wrong, and both of them
score green:

  * a flip that *restates* what the entry tree already produced grades nothing
    -- the tree the step was handed would satisfy it untouched. Checked below,
    against the recording itself, for all 119.
  * a flip whose new answer this image does not actually produce would fail
    the honest candidate. That one cannot be settled here -- it is what the
    oracle run settles -- but the pins it depends on are asserted here, so a
    flip cannot quietly start being graded under a different pandas.

A failure here fails ``docker build``. That is the intent: a verifier image
built against the wrong pandas must not exist, let alone grade anything.
"""
import io
import json
import re
import os
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, "/tests/harness")

from skutil_gates import probe, ruffgate, suppress          # noqa: E402
from skutil_probe import canon                              # noqa: E402
from skutil_probe.run import comparable                     # noqa: E402

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
    "numpy": "1.21.6",
    "scipy": "1.7.3",
    "pandas": "1.3.5",
    "scikit-learn": "0.24.2",
    "matplotlib": "3.5.3",
    "seaborn": "0.11.2",
    "six": "1.16.0",
    "Cython": "0.29.36",
    "pytest": "7.4.4",
    "setuptools": "59.8.0",
}

# The stack step 1 recorded under, and which this image must *not* be. Every
# one of the 119 flips exists because one of these moved.
PREVIOUS = {
    "numpy": "1.14.6",
    "scipy": "1.1.0",
    "pandas": "0.23.4",
    "scikit-learn": "0.20.4",
    "matplotlib": "2.2.5",
    "seaborn": "0.7.1",
}

# What those versions mean, as behaviour rather than as strings. A wheel can be
# repaired in place under an unchanged version number; these are the specific
# semantics this step is scored against, and the ones the later steps are
# defined as moving away from. pandas 1.3 has no Copy-on-Write option at all,
# NumPy 1.21 promotes by the old value-based rules, and every name NumPy 2
# expired is still here.
SEMANTICS = {
    "python": "3.9",
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

# The staged selection for this step, in the order `selection_for` returns it.
SELECTION = ["NPY001", "NPY002", "PD002", "PD003", "PD007", "PD008", "PD009",
             "PD012", "PD015", "UP"]

# `compare`'s defaults, since test.sh does not override them. The flip
# discriminator below has to use the same tolerances the scoring run will, or
# it would be answering a question nobody asks.
RTOL, ATOL = 1e-7, 1e-10


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
    print("interpreter")
    # The patch level, not just 3.9: the agent runs python-build-standalone
    # 3.9.23 and the corpus carries CPython's own exception messages, which are
    # reworded between patch releases.
    check("interpreter: cpython build", sys.version.split()[0], "3.9.23")

    print("versions")
    got = probe.installed_versions(sorted(set(VERSIONS) | set(PREVIOUS)))
    for name in sorted(VERSIONS):
        check("versions: %s" % name, got.get(name), VERSIONS[name])
    for name in sorted(PREVIOUS):
        check("versions: %s is not the step-1 pin" % name,
              got.get(name) == PREVIOUS[name], False)

    print("closure")
    check_closure("/tests/stack.txt")

    print("semantics")
    sem = probe.semantics()
    for key in sorted(SEMANTICS):
        check("semantics: %s" % key, sem.get(key), SEMANTICS[key])
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
    check("tools: step 2 ruff selection", ruffgate.selection_for(2), SELECTION)
    check("tools: step 2 ruff target", ruffgate.target_for(2), "py39")
    check("tools: step 1's selection is still contained in it",
          [c for c in ruffgate.selection_for(1)
           if c not in ruffgate.selection_for(2)], [])

    print("expectations")
    with open("/tests/expected/baseline-recording.json") as fh:
        recording = json.load(fh)
    records = dict((r["id"], r) for r in recording["records"])
    check("expectations: recorded cases", len(recording["records"]), 1236)
    check("expectations: no duplicate case ids", len(records), 1236)
    adversarial = sum(1 for r in recording["records"]
                      if "adversarial" in (r.get("tags") or []))
    check("expectations: at least a third of them adversarial",
          adversarial * 3 >= len(recording["records"]), True)
    # The recording is step 1's, recorded once and never re-recorded. It is
    # *supposed* to disagree with this image's libraries; the flips are how
    # that disagreement is declared.
    check("expectations: the recording is still step 1's pandas",
          recording["versions"].get("pandas"), PREVIOUS["pandas"])
    check("expectations: the recording is still step 1's numpy",
          recording["versions"].get("numpy"), PREVIOUS["numpy"])

    with open("/tests/expected/flips-step02.json") as fh:
        flips = json.load(fh)
    check("flips: this step's file", flips.get("step"), 2)
    entries = flips.get("flips") or []
    check("flips: how many", len(entries), 119)
    check("flips: no case is dropped from the corpus at this step",
          flips.get("drops"), [])

    cases = [e.get("case") for e in entries]
    check("flips: every flip names a distinct case",
          len(set(cases)), len(cases))
    check("flips: every flip names a recorded case",
          [c for c in cases if c not in records], [])
    check("flips: every flip has a reason",
          [e.get("case") for e in entries if not (e.get("reason") or "").strip()],
          [])
    check("flips: every flip has a citation",
          [e.get("case") for e in entries
           if not (e.get("citation") or "").strip()], [])
    check("flips: every flip supplies a whole comparable record",
          [e.get("case") for e in entries
           if sorted((e.get("expect") or {}).keys())
           not in (["status", "value"], ["exc", "status"])], [])

    # The check this file exists for. A flip that restates the recorded answer
    # is satisfied by the tree the step was handed, so it grades nothing --
    # and, because `mean(corpus)` is 85% of family A, 119 of them would hand
    # out most of the step for free.
    restated = []
    for entry in entries:
        cid = entry["case"]
        rec = records[cid]
        rtol = rec.get("rtol")
        atol = rec.get("atol")
        if not canon.compare(entry["expect"], comparable(rec),
                             rtol=RTOL if rtol is None else rtol,
                             atol=ATOL if atol is None else atol):
            restated.append(cid)
    check("flips: every flip actually changes the expectation", restated, [])

    # One flip named on its own, because it is the one that can be *half* right
    # and still look generated. `QRDecomposition` has two parameters and the
    # entry tree loses both -- `X` never stored, `pivot` overwritten by
    # `_decompose` with the pivot vector LINPACK returned -- and a solution that
    # stores `X` and leaves `pivot` alone passes every structural reading of
    # the tree while `clone()` still raises. So the flip has to record the flag
    # that was passed in, not the vector.
    qr = [e for e in entries if e["case"] == "odr.QRDecomposition.params"]
    check("flips: the QR case is flipped", len(qr), 1)
    if qr:
        got = dict((k["v"], v) for k, v in qr[0]["expect"]["value"]["v"])
        check("flips: and its pivot is the flag the caller passed",
              got.get("pivot"), {"t": "int", "v": 1})
        check("flips: while X is the matrix it was handed",
              (got.get("X") or {}).get("shape"), [20, 4])

    with open("/tests/expected/inherited-suite.json") as fh:
        inherited = json.load(fh)
    check("expectations: recorded test outcomes", len(inherited["outcomes"]), 65)
    check("expectations: their census", inherited["census"],
          {"passed": 59, "failed": 6})

    with open("/tests/manifest.json") as fh:
        manifest = json.load(fh)
    check("expectations: the manifest is this step's", manifest["step"], 2)
    # test.sh passes manifest["flips_expected"] to the grader as --min-flips,
    # the floor below which the must-flip corpus is scored 0. It is the count
    # the grader trusts, so it has to be the count of flips that actually
    # exist: tie it to the flips file here, where both are loaded, so the
    # manifest can neither drift above the file (a floor no honest submission
    # clears) nor below it (a floor a stripped flips file would slip under).
    check("expectations: the manifest's flip floor is the flips file's count",
          manifest.get("flips_expected"), len(entries))
    check("expectations: every new check has somewhere to fall from",
          [c["name"] for c in manifest["new"] if c["entry"] <= 0], [])
    rows = dict((c["name"], c) for c in manifest["new"] + manifest["carried"])
    # Step 1's `new` rows are step 2's `carried` rows: the ladder's whole shape
    # is that nothing an earlier step retired may come back.
    check("expectations: step 1's work is carried here",
          [n for n in ("global-rng-use", "ruff-legacy-random",
                       "warning-raising-constructs", "unparsable",
                       "self-warnings", "reproducibility")
           if n not in [c["name"] for c in manifest["carried"]]], [])
    # And so are the rows that are not about the migration at all, but about
    # the tree still being there to migrate. `source-substance` and
    # `source-retention` in particular have to travel together: a line-count
    # floor on its own is clearable by gutting a file and appending filler,
    # which is measured at 1.000000 against step 1 before the retention row
    # existed.
    check("expectations: the source-shape rows are carried too",
          [n for n in ("file-inventory", "source-census", "source-substance",
                       "source-retention", "test-assertions",
                       "no-dynamic-legacy-access", "no-dynamic-retired-name")
           if n not in [c["name"] for c in manifest["carried"]]], [])
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

    # `source-census` pins definitions only, so a dead import or a collapsed
    # version flag the migration drops is not read as a gutting; the fuller
    # coverage (every file with a binding) is what the substance and retention
    # rows carry, and it is those that stop the inventory being satisfied by a
    # hundred empty files.
    censused = rows["source-census"]["expect"]
    lines = rows["source-substance"]["expect"]
    check("expectations: the substance row covers every file with a binding",
          len(lines), 52)
    check("expectations: and every censused file carries a line floor",
          set(censused) <= set(lines), True)
    # And records which lines it was handed, because a count can be padded back
    # while the code stays gone. Re-recorded against *this* step's entry tree,
    # not inherited: step 1's oracle already rewrote many of these lines, and
    # digests of the previous step's source would floor a file at text it was
    # never given.
    kept = rows["source-retention"]["expect"]
    width = rows["source-retention"]["width"]
    check("expectations: retention covers exactly the substance files",
          sorted(kept) == sorted(lines), True)
    check("expectations: recorded as fixed-width digests, nothing else",
          sorted(set(len(v) % width for v in kept.values())), [0])
    check("expectations: and they are step 2's lines, not step 1's",
          len(kept["skutil/utils/fixes.py"]) // width, 596)
    check("expectations: and the floor is a fraction, not a count",
          rows["source-retention"]["floor"], 0.6)
    check("expectations: the two that did not parse on the baseline are covered",
          [p for p in ("doc/conf.py",
                       "doc/examples/pipeline/skutil grid demo.ipynb")
           if not lines.get(p)], [])

    floors = rows["test-assertions"]["expect"]
    check("expectations: the assertion floor covers every asserting module",
          len(floors), 11)

    # The two rows this step adds beyond the construct gates. Both were
    # measured on the entry tree with the same code that will observe the
    # submission, so what is asserted here is that the measurement did not get
    # copied in wrong.
    seaborn = rows["seaborn-pyplot-reexport"]
    check("expectations: the seaborn row counts the rule it names",
          (seaborn["kind"], seaborn["rule"]), ("ast", "seaborn-plt"))
    check("expectations: both sns.plt sites have to go",
          (seaborn["entry"], seaborn["limit"]), (2, 0))

    contract = rows["estimator-contract"]
    check("expectations: the contract row names every exported estimator",
          len(contract["expect"]), 26)
    check("expectations: and asks all of them to hold",
          sorted(set(contract["expect"].values())), [True])
    check("expectations: keyed by the check names repro.run emits",
          [k for k in contract["expect"]
           if not k.startswith("estimator_contract.")], [])
    check("expectations: four of the twenty-six fail on the entry tree",
          (contract["entry"], contract["limit"]), (4, 0))
    check("expectations: including the one this step's flip is about",
          "estimator_contract.QRDecomposition" in contract["expect"], True)

    # The launcher that keeps a candidate-writable cwd from shadowing the
    # harness must ship, and every stage that runs a harness module must go
    # through it. A future editor who reintroduces `python -m skutil_...` here
    # -- or drops _run.py -- reopens a root-level scorer compromise (item 5,
    # round 4), so the build refuses it rather than trusting review to catch it.
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
