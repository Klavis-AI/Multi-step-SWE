"""Assert that this verifier image is the one step 3 was calibrated against.

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

Step 3 keeps step 2's second audience -- the flips -- and adds a third: the
three AST rules the scanner turns on at this step that are *deliberately not*
manifest rows. ``flips-step03.json`` is 156 hand-written claims of the form "the
pinned libraries changed this answer, and here is the new one". Two ways that
can be wrong, and both of them score green:

  * a flip that *restates* what the entry tree already produced grades nothing
    -- the tree the step was handed would satisfy it untouched. Checked below,
    against the recording itself, for all 156.
  * a flip whose new answer this image does not actually produce would fail the
    honest candidate. That one cannot be settled here -- it is what the oracle
    run settles -- but the pins it depends on are asserted here, so a flip
    cannot quietly start being graded under a different scikit-learn.

And the new rules: ``sklearn-load-boston``, ``sklearn-private-api`` and
``sklearn-grid-scores`` are the three the scanner adds at step 3, and the golden
retains all three (the load_boston fix moves an import rather than deleting a
helper; the private-api and grid_scores references stay live). A limit-0 row is
only honest when the golden reaches zero, so none of the three is a row -- the
scikit-learn 1.6 removals are enforced behaviourally instead, by the flips, the
estimator-contract and set-output gates, and the inherited suite. This file
asserts that decision holds in both directions: the scanner knows the three
rules, and the manifest gates none of them.

A failure here fails ``docker build``. That is the intent: a verifier image
built against the wrong scikit-learn must not exist, let alone grade anything.
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
from skutil_gates import rules as ast_rules                  # noqa: E402
from skutil_probe import canon                               # noqa: E402
from skutil_probe.run import comparable                      # noqa: E402

FAILURES = []


def check(label, got, want):
    if got == want:
        print("  ok    %s" % label)
        return
    print("  FAIL  %s" % label)
    print("        expected: %r" % (want,))
    print("        observed: %r" % (got,))
    FAILURES.append(label)


# The pins, as behaviour the scorer must see. `stack.txt` states them for pip;
# this states them for the scorer, and the two disagreeing is the failure this
# catches. `six` is no longer a direct pin at step 3 -- step 2 retired the
# vendored `sklearn.externals.six` shim -- so it is not here; the closure block
# still asserts the transitive six==1.17.0 that python-dateutil pulls in.
VERSIONS = {
    "numpy": "1.26.4",
    "scipy": "1.13.1",
    "pandas": "1.5.3",
    "scikit-learn": "1.6.1",
    "matplotlib": "3.8.4",
    "seaborn": "0.13.2",
    "Cython": "3.0.11",
    "pytest": "8.3.5",
    "setuptools": "59.8.0",
    "wheel": "0.37.1",
}

# The step-2 stack, and which this image must *not* be. The 37 new flips this
# step adds exist because one of these moved -- scikit-learn 0.24 -> 1.6 above
# all -- so an image still carrying them would grade the wrong tree.
PREVIOUS = {
    "numpy": "1.21.6",
    "scipy": "1.7.3",
    "pandas": "1.3.5",
    "scikit-learn": "0.24.2",
    "matplotlib": "3.5.3",
    "seaborn": "0.11.2",
}

# The stack the behaviour recording was made under, at step 1, and never
# re-recorded since. The recording is *supposed* to disagree with this image's
# libraries; the flips are how that disagreement is declared. Asserted so a
# recording quietly re-made under a newer pandas would fail the build.
RECORDING = {
    "pandas": "0.23.4",
    "numpy": "1.14.6",
}

# What these versions mean, as behaviour rather than as strings. numpy stays at
# 1.26, so promotion is still value-based and every NumPy-2 name is still here;
# what moves at step 3 is pandas 1.3 -> 1.5, where `copy_on_write` stops being
# an OptionError and becomes a real option that defaults off (step 2 measured it
# *raising*), and scikit-learn 0.24 -> 1.6.
SEMANTICS = {
    "python": "3.9",
    "copy_on_write": False,
    "default_string_dtype": "object",
    "default_int_dtype": "int64",
    "nep50_f32_plus_pyfloat": "float64",
    "nep50_i64_plus_f32": "float64",
    "nep50_u8_plus_200": "int64",
    "nep50_u8_array_plus_200": "uint8",
    "scalar_repr": "1.5",
    "view_tracks_parent": True,
    "sklearn_version": "1.6.1",
    "pandas_version": "1.5.3",
}

# The staged ruff selection for this step, in the order `selection_for` returns
# it. B (flake8-bugbear) is the one code this step adds; the full PD selection
# arrives at step 4.
SELECTION = ["B", "NPY001", "NPY002", "PD002", "PD003", "PD007", "PD008",
             "PD009", "PD012", "PD015", "UP"]

# The three AST rules the scanner turns on at step 3 that are deliberately NOT
# manifest rows (the golden retains all three).
NEW_AST_RULES = ["sklearn-grid-scores", "sklearn-load-boston",
                 "sklearn-private-api"]

# `compare`'s defaults, since test.sh does not override them. The flip
# discriminator below has to use the same tolerances the scoring run will.
RTOL, ATOL = 1e-7, 1e-10

# pip, setuptools and wheel are dropped from both sides of the closure check
# because `pip freeze` does not list them the same way across environments; the
# two that matter are pinned in `stack.txt` anyway and the interpreter build
# fixes the third.
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
    check("interpreter: cpython build", sys.version.split()[0], "3.9.23")

    print("versions")
    got = probe.installed_versions(sorted(set(VERSIONS) | set(PREVIOUS)))
    for name in sorted(VERSIONS):
        check("versions: %s" % name, got.get(name), VERSIONS[name])
    for name in sorted(PREVIOUS):
        check("versions: %s is not the step-2 pin" % name,
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
    # shows up here rather than as a mysteriously easy gate. And the ladder
    # invariant: step 2's codes are all still selected at step 3.
    check("tools: step 3 ruff selection", ruffgate.selection_for(3), SELECTION)
    check("tools: step 3 ruff target", ruffgate.target_for(3), "py39")
    check("tools: step 2's selection is still contained in it",
          [c for c in ruffgate.selection_for(2)
           if c not in ruffgate.selection_for(3)], [])
    check("tools: B is new at step 3",
          "B" in ruffgate.selection_for(3)
          and "B" not in ruffgate.selection_for(2), True)

    # The three AST rules this step adds. They are asserted here precisely
    # because they are NOT manifest rows: the scanner has to *know* them (so the
    # 156 flips and the behavioural gates that enforce the 1.6 removals are
    # backed by a scanner that could name a regression), while the manifest
    # gates none of them (the golden legitimately keeps all three). Both halves
    # below; the "not a row" half is under `expectations`.
    active3 = set(r.name for r in ast_rules.by_step(3))
    active2 = set(r.name for r in ast_rules.by_step(2))
    check("rules: the three sklearn rules are new at step 3",
          sorted(active3 - active2), NEW_AST_RULES)
    check("rules: step 3 retires none of step 2's rules",
          sorted(active2 - active3), [])

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
    # The recording is step 1's, recorded once and never re-recorded.
    check("expectations: the recording is still step 1's pandas",
          recording["versions"].get("pandas"), RECORDING["pandas"])
    check("expectations: the recording is still step 1's numpy",
          recording["versions"].get("numpy"), RECORDING["numpy"])

    with open("/tests/expected/flips-step03.json") as fh:
        flips = json.load(fh)
    check("flips: this step's file", flips.get("step"), 3)
    entries = flips.get("flips") or []
    check("flips: how many", len(entries), 156)
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
    # and, because `mean(corpus)` is 85% of family A, 156 of them would hand
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

    # QRDecomposition is carried from step 2, where it was the estimator whose
    # flip can be *half* right and still look generated: two parameters, and the
    # entry tree loses both (`X` never stored, `pivot` overwritten by the pivot
    # vector LINPACK returned). Step 3 inherits step 2's golden, so the flip is
    # already satisfied by the tree it is handed -- but the recording is still
    # step 1's, so the flip is still declared, and it still has to record the
    # flag that was passed in rather than the vector.
    qr = [e for e in entries if e["case"] == "odr.QRDecomposition.params"]
    check("flips: the QR case is still flipped", len(qr), 1)
    if qr:
        got = dict((k["v"], v) for k, v in qr[0]["expect"]["value"]["v"])
        check("flips: and its pivot is the flag the caller passed",
              got.get("pivot"), {"t": "int", "v": 1})
        check("flips: while X is the matrix it was handed",
              (got.get("X") or {}).get("shape"), [20, 4])

    # The inherited suite is the first to break copy-forward: matplotlib 3.8
    # removed `matplotlib.testing.decorators.cleanup`, dropping test_corr and
    # test_large_grid from collection (CAN_CHART_MPL=False), and scipy 1.13's
    # `optimize.brent` raises BracketError where 1.7 converged, flipping
    # test_yeo_johnson. So step 2's 65 outcomes become 63, and the census moves
    # with them. Asserted against the tally so a hand-edit of one without the
    # other fails the build.
    with open("/tests/expected/inherited-suite.json") as fh:
        inherited = json.load(fh)
    outcomes = inherited["outcomes"]
    check("expectations: recorded test outcomes", len(outcomes), 63)
    check("expectations: their census", inherited["census"],
          {"passed": 57, "failed": 6})
    tally = {"passed": sum(1 for v in outcomes.values() if v == "passed"),
             "failed": sum(1 for v in outcomes.values() if v == "failed")}
    check("expectations: the census is the tally of the outcomes",
          tally, inherited["census"])
    check("expectations: every outcome is passed or failed",
          sorted(set(outcomes.values())), ["failed", "passed"])
    check("expectations: two of step 2's nodes are gone, none added",
          (len(outcomes), "test_yeo_johnson" in
           ".".join(outcomes)), (63, True))

    with open("/tests/manifest.json") as fh:
        manifest = json.load(fh)
    check("expectations: the manifest is this step's", manifest["step"], 3)
    # test.sh passes manifest["flips_expected"] to the grader as --min-flips,
    # the floor below which the must-flip corpus is scored 0. Tie it to the
    # flips file here, where both are loaded.
    check("expectations: the manifest's flip floor is the flips file's count",
          manifest.get("flips_expected"), len(entries))
    check("expectations: every new check has somewhere to fall from",
          [c["name"] for c in manifest["new"] if c["entry"] <= 0], [])

    new_names = [c["name"] for c in manifest["new"]]
    carried_names = [c["name"] for c in manifest["carried"]]
    rows = dict((c["name"], c) for c in manifest["new"] + manifest["carried"])

    # Step 3's two new rows, and only those two. set-output is the scikit-learn
    # 1.x container API the 0.24 tree has none of; ruff-bugbear is the B family.
    check("expectations: exactly two new rows, the ones this step adds",
          new_names, ["set-output", "ruff-bugbear"])

    # Step 2's new rows are step 3's carried rows: the ladder's whole shape is
    # that nothing an earlier step earned may come back. seaborn-pyplot-reexport
    # and estimator-contract were step 2's `new`; here they must be carried.
    check("expectations: step 2's new work is carried here",
          [n for n in ("seaborn-pyplot-reexport", "estimator-contract")
           if n not in carried_names], [])
    # As is step 1's, and the source-shape rows that are not about the migration
    # at all but about the tree still being there to migrate. `source-substance`
    # and `source-retention` in particular travel together: a line-count floor
    # on its own is clearable by gutting a file and appending filler.
    check("expectations: step 1's work is carried here",
          [n for n in ("global-rng-use", "ruff-legacy-random",
                       "warning-raising-constructs", "unparsable",
                       "self-warnings", "reproducibility", "repro-invariants")
           if n not in carried_names], [])
    check("expectations: the source-shape rows are carried too",
          [n for n in ("file-inventory", "source-census", "source-substance",
                       "source-retention", "test-assertions",
                       "no-dynamic-legacy-access", "no-dynamic-retired-name")
           if n not in carried_names], [])

    # The other half of the not-gated decision: the three AST rules the scanner
    # adds at step 3 are absent from the manifest. If a later editor turns one
    # into a limit-0 row, the golden -- which keeps all three -- would score it
    # 0, and the build should stop them here.
    check("expectations: none of the three sklearn AST rules is gated",
          [n for n in NEW_AST_RULES if n in rows], [])

    api = rows["public-api"]
    check("expectations: the public-api row still names 75",
          sum(len(v) for v in api["expect"].values()), 75)
    check("expectations: across ten importing modules",
          len(api["expect"]), 10)

    # The inventory is the row that stops a parse gate being answered with `rm`,
    # so it has to list the file that would be deleted first.
    files = rows["file-inventory"]["expect"]
    check("expectations: the inventory covers the whole handed-over tree",
          len(files), 99)
    check("expectations: including the one nothing imports",
          "doc/sphinxext/numpy_ext/docscrape.py" in files, True)
    # 99, not the tree's 100: a Cython-generated .c that sits beside its .pyx is
    # build output -- any `pip install` regenerates and clobbers it -- and is
    # not pinned. The .pyx it comes from is pinned and is load-bearing.
    check("expectations: the cythonized .c is not pinned (it is build output)",
          "skutil/metrics/_kernel_fast.c" in files, False)
    check("expectations: but the .pyx it is generated from is",
          "skutil/metrics/_kernel_fast.pyx" in files, True)

    # `source-census` pins definitions only; the fuller coverage (every file
    # with a binding) is what substance and retention carry, and it is those
    # that stop the inventory being satisfied by a hundred empty files.
    censused = rows["source-census"]["expect"]
    lines = rows["source-substance"]["expect"]
    check("expectations: the census covers every defining file",
          len(censused), 38)
    check("expectations: the substance row covers every file with a binding",
          len(lines), 52)
    check("expectations: and every censused file carries a line floor",
          set(censused) <= set(lines), True)
    # Retention records which lines it was handed, because a count can be padded
    # back while the code stays gone. Re-recorded against *this* step's entry
    # tree (step 2's golden), not inherited.
    kept = rows["source-retention"]["expect"]
    width = rows["source-retention"]["width"]
    check("expectations: retention covers exactly the substance files",
          sorted(kept) == sorted(lines), True)
    check("expectations: recorded as fixed-width digests, nothing else",
          sorted(set(len(v) % width for v in kept.values())), [0])
    check("expectations: and the floor is a fraction, not a count",
          rows["source-retention"]["floor"], 0.6)

    floors = rows["test-assertions"]["expect"]
    check("expectations: the assertion floor covers every asserting module",
          len(floors), 11)

    # The two new rows, measured on the entry tree with the same code that will
    # observe the submission: what is asserted here is that the measurement did
    # not get copied in wrong.
    setout = rows["set-output"]
    check("expectations: set-output is a repro row measured in the image",
          (setout["kind"], setout.get("measured_in_image")), ("repro", True))
    check("expectations: it asks all eighteen transformers to support it",
          (len(setout["expect"]), setout["entry"], setout["limit"]),
          (18, 18, 0))
    check("expectations: all eighteen true, keyed as repro.run emits",
          (sorted(set(setout["expect"].values())),
           [k for k in setout["expect"]
            if not k.startswith("set_output_supported.")]), ([True], []))

    # Eighteen rows are only worth eighteen if the probe behind them executes
    # the contract the instruction states. A presence test -- is
    # `get_feature_names_out` callable, does `set_output` round trip without
    # raising -- is satisfied by a method that returns positional labels or
    # raises, because `_wrap_in_pandas_container` swallows both. So pin the four
    # things that make this row a measurement rather than a shape: a fit recipe
    # for every class the row names, an executed fit, the default output's own
    # labels taken as the ground truth the names are compared against, and the
    # re-check of the default path after the container is turned back off.
    with open("/tests/harness/skutil_gates/repro.py") as fh:
        gates = fh.read()
    probe_src = gates.split("SET_OUTPUT = '''", 1)[1].split("\n'''", 1)[0]
    recipes = sorted(re.findall(r'^    "(\w+)": \(', probe_src, re.M))
    check("expectations: the probe has a fit recipe for each class the row names",
          recipes,
          sorted(k.split(".", 1)[1] for k in setout["expect"]))
    check("expectations: the probe fits and transforms rather than inspecting",
          ("est.fit(fixture(kind))" in probe_src,
           probe_src.count("est.transform(fixture(kind))")), (True, 3))
    check("expectations: the names are checked against the default output",
          ("want = _labels(base)" in probe_src, "names != want" in probe_src),
          (True, True))
    check("expectations: and the default path is re-checked for additivity",
          (probe_src.count('so(transform="pandas")'),
           probe_src.count('so(transform="default")'),
           "same_values(after, base)" in probe_src), (1, 1, True))
    check("expectations: a row passes on its ok field, not on being truthy",
          'checks["set_output_supported." + name] = '
          'bool((got[name] or {}).get("ok"))' in gates, True)

    bugbear = rows["ruff-bugbear"]
    check("expectations: ruff-bugbear counts the B family",
          (bugbear["kind"], bugbear["code"]), ("ruff", "B"))
    check("expectations: 36 on the entry tree, floored to zero",
          (bugbear["entry"], bugbear["limit"]), (36, 0))

    # estimator-contract is carried at step 3 (step 2 earned it). The entry tree
    # is step 2's golden, which does not import under scikit-learn 1.6
    # (module-scope load_boston), so this step's own verifier sees every check
    # fail on it -- carried at zero, scored on the candidate.
    contract = rows["estimator-contract"]
    check("expectations: the contract row names every exported estimator",
          len(contract["expect"]), 26)
    check("expectations: and asks all of them to hold",
          sorted(set(contract["expect"].values())), [True])
    check("expectations: keyed by the check names repro.run emits",
          [k for k in contract["expect"]
           if not k.startswith("estimator_contract.")], [])
    check("expectations: carried at zero with the import-break convention",
          (contract["entry"], contract["limit"]), (0, 0))
    check("expectations: including the one the QR flip is about",
          "estimator_contract.QRDecomposition" in contract["expect"], True)

    # The suppression ceiling is the entry count, not zero: clearing the
    # inherited suppressions is not step 3's job, but the count may not grow.
    supp = rows["suppression-ceiling"]
    check("expectations: the suppression ceiling is the entry count",
          (supp["entry"], supp["limit"]), (13, 13))

    # The launcher that keeps a candidate-writable cwd from shadowing the
    # harness must ship, and every stage that runs a harness module must go
    # through it (item 5, round 4).
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
    # A root context that imports the candidate's package at all runs candidate
    # code as root, free to overwrite this harness before the launcher gets a
    # say. Join backslash continuations, then require every line that imports the
    # candidate package to have dropped to the unprivileged uid first.
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
