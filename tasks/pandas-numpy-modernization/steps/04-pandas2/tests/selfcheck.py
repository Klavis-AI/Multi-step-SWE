"""Assert that this verifier image is the one step 4 was calibrated against.

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

Step 4 keeps step 2's second audience -- the flips -- and the third: the AST
rules the scanner turns on at this step. ``flips-step04.json`` is 160
hand-written claims of the form "the pinned libraries changed this answer, and
here is the new one". Two ways that can be wrong, and both of them score green:

  * a flip that *restates* what the entry tree already produced grades nothing
    -- the tree the step was handed would satisfy it untouched. Checked below,
    against the recording itself, for all 160.
  * a flip whose new answer this image does not actually produce would fail the
    honest candidate. That one cannot be settled here -- it is what the oracle
    run settles -- but the pins it depends on are asserted here, so a flip
    cannot quietly start being graded under a different pandas.

And the new rules invert step 3's decision. At step 3 the three sklearn scanners
(``sklearn-load-boston`` and its two siblings) were *not* manifest rows: the
golden retained the constructs they flag, so a limit-0 row could never reach
zero and the removals were enforced behaviourally instead. The three pandas AST
rules this step turns on -- ``pandas-groupby-implicit-observed``,
``pandas-chained-assignment`` and ``pandas-silences-chained-assignment`` -- are
the opposite: the golden drives each to zero (it names every ``observed=`` and
writes no chained assignment), so each is an honest limit-0 row. Of the three,
the groupby rule is a new manifest row and the other two are carried guards.
(The step's other new row is a ruff row, not an AST rule: the pandas-vet family
widens to the whole of PD here. Two new rows in all.) This file asserts that
decision holds in both directions: the scanner knows the three rules, and the
manifest gates all three.

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
    "pandas": "2.2.3",
    "pyarrow": "17.0.0",
    "scikit-learn": "1.6.1",
    "matplotlib": "3.8.4",
    "seaborn": "0.13.2",
    "Cython": "3.0.11",
    "pytest": "8.3.5",
    "setuptools": "59.8.0",
    "wheel": "0.37.1",
}

# The step-3 stack, and which this image must *not* be. Only one pin moves at
# step 4 -- pandas 1.5.3 -> 2.2.3 (pyarrow 17.0.0 arrives with it) -- so pandas
# is the only version this image must have left behind. numpy, scipy,
# scikit-learn, matplotlib and seaborn are unchanged from step 3 and so are NOT
# asserted here: a "must differ" check on a pin that did not move would demand
# the image differ from itself.
PREVIOUS = {
    "pandas": "1.5.3",
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
# scikit-learn stays at 1.6. What moves at step 4 is pandas 1.5 -> 2.2, where
# `copy_on_write` is a real option still defaulting off (2.2 warns rather than
# switching), the unset-`observed=` grouper starts raising a FutureWarning, and
# `pandas_version` reads 2.2.3.
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
    "pandas_version": "2.2.3",
}

# The staged ruff selection for this step, in the order `selection_for` returns
# it. PD (the whole pandas-vet family) is the one code this step adds; it widens
# the individual pandas smells (PD002/003/007/008/009/012/015) already selected
# into their family, and B (flake8-bugbear) arrived at step 3.
SELECTION = ["B", "NPY001", "NPY002", "PD", "PD002", "PD003", "PD007", "PD008",
             "PD009", "PD012", "PD015", "UP"]

# The three pandas AST rules the scanner turns on at step 4. Unlike step 3's
# sklearn scanners, all three ARE manifest rows here: the golden reaches zero on
# each. Of these three, the groupby rule is a new manifest row and the other two
# are carried guards; the step's other new row is the widened PD family, which
# is a ruff row and not an AST rule at all.
NEW_AST_RULES = ["pandas-chained-assignment", "pandas-groupby-implicit-observed",
                 "pandas-silences-chained-assignment"]

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
        check("versions: %s is not the step-3 pin" % name,
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
    # invariant: step 3's codes are all still selected at step 4.
    check("tools: step 4 ruff selection", ruffgate.selection_for(4), SELECTION)
    check("tools: step 4 ruff target", ruffgate.target_for(4), "py39")
    check("tools: step 3's selection is still contained in it",
          [c for c in ruffgate.selection_for(3)
           if c not in ruffgate.selection_for(4)], [])
    check("tools: PD is new at step 4",
          "PD" in ruffgate.selection_for(4)
          and "PD" not in ruffgate.selection_for(3), True)

    # The three AST rules this step adds, and they invert the step-3 decision:
    # here the manifest gates all three. The golden reaches zero on each of them
    # -- it names `observed=` on every groupby and writes no chained assignment,
    # so a limit-0 row is honest -- where at step 3 the newly-active sklearn
    # scanners were deliberately left out of the manifest because that step's
    # golden still kept the constructs they flag. So this half asserts the
    # scanner *knows* the three rules and that they are new at 4; the other
    # half, under `expectations`, asserts each one is a row and where it sits.
    # Re-derive both halves for the next step rather than carrying this over:
    # which rules are new, and whether its golden can reach zero on them, is a
    # question about that step's golden, not a property of this paragraph.
    active4 = set(r.name for r in ast_rules.by_step(4))
    active3 = set(r.name for r in ast_rules.by_step(3))
    check("rules: the three pandas rules are new at step 4",
          sorted(active4 - active3), NEW_AST_RULES)
    check("rules: step 4 retires none of step 3's rules",
          sorted(active3 - active4), [])

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

    with open("/tests/expected/flips-step04.json") as fh:
        flips = json.load(fh)
    check("flips: this step's file", flips.get("step"), 4)
    entries = flips.get("flips") or []
    check("flips: how many", len(entries), 160)
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
    # and, because `mean(corpus)` is 85% of family A, 160 of them would hand
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
    check("expectations: the manifest is this step's", manifest["step"], 4)
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

    # Step 4's two new rows, and only those. pandas-groupby-implicit-observed is
    # the unset-observed= grouper pandas 2 turns from silent into a
    # FutureWarning; ruff-pandas-vet is the PD family, which widens from step 2's
    # individual smells to the whole of pandas-vet here and newly catches the
    # tree's `.values` reads. A newly-selected or newly-widened ruff family is a
    # new row in the step that turns it on and a carried row at zero afterwards
    # -- the same shape ruff-pyupgrade and the PD00x subset had at step 2 and
    # ruff-bugbear had at step 3. Everything else this step touches is a carry.
    check("expectations: exactly two new rows, the ones this step adds",
          sorted(new_names),
          ["pandas-groupby-implicit-observed", "ruff-pandas-vet"])

    # Step 3's new rows are step 4's carried rows: the ladder's whole shape is
    # that nothing an earlier step earned may come back. set-output and
    # ruff-bugbear were step 3's `new`, seaborn-pyplot-reexport and
    # estimator-contract were step 2's; here they must all be carried.
    check("expectations: step 3's new work is carried here",
          [n for n in ("set-output", "ruff-bugbear",
                       "seaborn-pyplot-reexport", "estimator-contract")
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

    # The three pandas AST rules the scanner newly reaches at step 4 are the
    # gate this step is about -- the inverse of step 3's sklearn scanners, which
    # fed the ast-sum rows and were never gated on their own. Here each is a real
    # limit-0 row: pandas-groupby-implicit-observed is this step's one new AST
    # row (the golden drives its two sites to zero), and pandas-chained-assignment
    # / pandas-silences-chained-assignment are carried guards the golden keeps at
    # zero. The step's other new row, ruff-pandas-vet, is a ruff row and so is
    # absent from NEW_AST_RULES by construction -- which is what makes the
    # equality below a statement about the AST rules and not about the manifest.
    # Assert each is present and placed as its own gateable row.
    check("expectations: the groupby rule is this step's one new AST row",
          [n for n in NEW_AST_RULES if n in new_names],
          ["pandas-groupby-implicit-observed"])
    check("expectations: the two chained-assignment guards are carried",
          sorted(n for n in NEW_AST_RULES if n in carried_names),
          ["pandas-chained-assignment", "pandas-silences-chained-assignment"])
    check("expectations: and all three are gated at limit zero",
          sorted(set(rows[n]["limit"] for n in NEW_AST_RULES if n in rows)), [0])

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
    # 2026-10-06 (DeepSeek trajectory review): the census pins what the agent
    # is handed, and the reference's class-by-class get_feature_names_out are
    # not that -- a shared base-class implementation meets step 3's set_output
    # requirement without them. They are allowed, not deleted from `expect`,
    # so the recorded entry tree stays visible; `set-output` holds the surface.
    allowed = rows["source-census"].get("allow", [])
    check("expectations: the census allows exactly the eleven "
          "reference-authored get_feature_names_out",
          (len(allowed), sorted(set(a.split("::")[1].split(".")[-1]
                                    for a in allowed))),
          (11, ["get_feature_names_out"]))
    check("expectations: and each allowed name is one the census records",
          [a for a in allowed if a.split("::")[1]
           not in censused.get(a.split("::")[0], [])], [])
    check("expectations: the census covers every defining file",
          len(censused), 38)
    check("expectations: the substance row covers every file with a binding",
          len(lines), 52)
    check("expectations: and every censused file carries a line floor",
          set(censused) <= set(lines), True)
    # Retention records which lines it was handed, because a count can be padded
    # back while the code stays gone. Re-recorded against *this* step's entry
    # tree (step 3's golden -- the scikit-learn-1.6 tree, which carries the
    # get_feature_names_out grafts), not inherited.
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

    # set-output is carried from step 3, still measured on the entry tree with
    # the same code that will observe the submission: the entry tree here is the
    # step-3 golden, which supports set_output on all eighteen, so entry is zero
    # (carried convention) while the row keeps naming every one of the eighteen.
    setout = rows["set-output"]
    check("expectations: set-output is a repro row measured in the image",
          (setout["kind"], setout.get("measured_in_image")), ("repro", True))
    check("expectations: it still names all eighteen transformers, carried at zero",
          (len(setout["expect"]), setout["entry"], setout["limit"]),
          (18, 0, 0))
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
    # Carried from step 3: the step-3 golden already cleared the B family, so the
    # entry tree this step inherits carries zero, floored to zero.
    check("expectations: ruff-bugbear carried at zero on the entry tree",
          (bugbear["entry"], bugbear["limit"]), (0, 0))

    # The pandas-vet family is step 4's other new row: the house rule widens from
    # the individual pandas smells to the whole PD family here, and the construct
    # it newly catches on the entry tree is `.values` (PD011), which the golden
    # rewrites to `.to_numpy()`. New rather than carried, because the entry tree
    # does not pass it -- a carried row asserts the handed-over tree is already
    # at the limit, and this one enters above it.
    pandasvet = rows["ruff-pandas-vet"]
    check("expectations: ruff-pandas-vet counts the PD family",
          (pandasvet["kind"], pandasvet["code"]), ("ruff", "PD"))
    check("expectations: 37 on the entry tree, floored to zero",
          (pandasvet["entry"], pandasvet["limit"]), (37, 0))
    check("expectations: and it is a new row at this step, not a carry",
          ("ruff-pandas-vet" in new_names,
           "ruff-pandas-vet" in carried_names), (True, False))

    # estimator-contract is carried (step 2 earned it). The entry tree here is
    # step 3's golden, which imports under scikit-learn 1.6 and holds the
    # contract, so it measures zero on the entry tree -- carried at zero, scored
    # on the candidate.
    contract = rows["estimator-contract"]
    check("expectations: the contract row names every exported estimator",
          len(contract["expect"]), 26)
    check("expectations: and asks all of them to hold",
          sorted(set(contract["expect"].values())), [True])
    check("expectations: keyed by the check names repro.run emits",
          [k for k in contract["expect"]
           if not k.startswith("estimator_contract.")], [])
    check("expectations: carried at zero on the entry tree",
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
