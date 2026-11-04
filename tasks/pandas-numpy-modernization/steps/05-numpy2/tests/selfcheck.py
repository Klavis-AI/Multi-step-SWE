"""Assert that this verifier image is the one step 5 was calibrated against.

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

Step 5 keeps step 2's second audience -- the flips -- and the third: the AST
rules the scanner turns on at this step. ``flips-step05.json`` is 166
hand-written claims of the form "the pinned libraries changed this answer, and
here is the new one". Two ways that can be wrong, and both of them score green:

  * a flip that *restates* what the entry tree already produced grades nothing
    -- the tree the step was handed would satisfy it untouched. Checked below,
    against the recording itself, for all 166.
  * a flip whose new answer this image does not actually produce would fail the
    honest candidate. That one cannot be settled here -- it is what the oracle
    run settles -- but the pins it depends on are asserted here, so a flip
    cannot quietly start being graded under a different numpy.

The one new rule at this step is the leap itself. NumPy 2.0 removes
``numpy.distutils``, the build backend skutil's four ``setup.py`` files were
written against, so the tree the step is handed does not build at all under this
image. ``numpy-distutils`` is the one new manifest row: the entry tree names it
nine times and the golden -- which moves the build to meson-python -- names it
zero. ``numpy-expired-member`` and ``numpy-copy-false`` become active at the
same step but are carried guards at zero, because the source was already
numpy-2-clean by step 2's alias sweep. This file asserts that split holds in
both directions: the scanner knows all three rules, and the manifest gates the
one that is this step's work.

The build files the golden retires need their own assertions, because a
``kind=paths`` row has no allow list. The four ``setup.py`` are deleted, so they
must be *absent* from the inventory rather than allowed in it; ``_build_utils``
is stubbed, so it must stay in the inventory but drop out of the content rows.
Both are checked below.

A failure here fails ``docker build``. That is the intent: a verifier image
built against the wrong numpy must not exist, let alone grade anything.
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
# catches. setuptools and wheel move here (59.8.0/0.37.1 -> 75.3.2/0.45.1): the
# old pins existed only to keep numpy.distutils importable, which no longer
# matters, and they are checked by name here rather than in the closure block
# (pip freeze does not list them the same way across environments). meson-python
# is the new build backend and is a runtime-visible pin; meson, ninja and
# pyproject-metadata are its own dependencies and are asserted by the closure.
VERSIONS = {
    "numpy": "2.0.2",
    "scipy": "1.13.1",
    "pandas": "2.2.3",
    "pyarrow": "17.0.0",
    "scikit-learn": "1.6.1",
    "matplotlib": "3.8.4",
    "seaborn": "0.13.2",
    "Cython": "3.0.11",
    "pytest": "8.3.5",
    "setuptools": "75.3.2",
    "wheel": "0.45.1",
    "meson-python": "0.17.1",
}

# The step-4 stack, and which this image must *not* be. Only one runtime pin
# moves at step 5 -- numpy 1.26.4 -> 2.0.2 -- so numpy is the version this image
# must have left behind. scipy, pandas, pyarrow, scikit-learn, matplotlib and
# seaborn are unchanged from step 4 and so are NOT asserted here: a "must differ"
# check on a pin that did not move would demand the image differ from itself.
PREVIOUS = {
    "numpy": "1.26.4",
}

# The stack the behaviour recording was made under, at step 1, and never
# re-recorded since. The recording is *supposed* to disagree with this image's
# libraries; the flips are how that disagreement is declared. Asserted so a
# recording quietly re-made under a newer numpy would fail the build.
RECORDING = {
    "pandas": "0.23.4",
    "numpy": "1.14.6",
}

# What these versions mean, as behaviour rather than as strings. numpy moves to
# 2.0 here and it is the whole point of the step: NEP 50 makes a python scalar
# weak, so `float32 + 1.0` stays float32 and `uint8(1) + 200` stays uint8 where
# 1.26 promoted both to 64-bit; NEP 51 changes the scalar repr to
# `np.float64(1.5)`. pandas stays at 2.2 (copy_on_write still off, view still
# tracks parent) and scikit-learn at 1.6, so those read exactly as at step 4. No
# version strings drive the flips -- a vendored shim restoring NumPy 1 promotion
# would keep __version__ and change these.
SEMANTICS = {
    "python": "3.9",
    "numpy_version": "2.0.2",
    "copy_on_write": False,
    "default_string_dtype": "object",
    "default_int_dtype": "int64",
    "nep50_f32_plus_pyfloat": "float32",
    "nep50_i64_plus_f32": "float64",
    "nep50_u8_plus_200": "uint8",
    "nep50_u8_array_plus_200": "uint8",
    "scalar_repr": "np.float64(1.5)",
    "view_tracks_parent": True,
    "sklearn_version": "1.6.1",
    "pandas_version": "2.2.3",
}

# Which members of the 2.0 expiry list this numpy still answers to. At 2.0.2
# exactly three survive -- `in1d`, `row_stack` and `trapz` are deprecated but
# not yet removed (that lands in a later 2.x) -- and nothing else does:
# `float_`, `unicode_`, `alltrue`, `NaN` and the rest are gone. The exact set is
# the fingerprint of *this* numpy: 1.26 would answer to all nineteen, a later
# 2.x to none of these three, so pinning the set to these three ties the image
# to numpy 2.0.2 specifically.
EXPIRY_FINGERPRINT = ["in1d", "row_stack", "trapz"]

# The staged ruff selection for this step, in the order `selection_for` returns
# it. NPY (the rest of the NumPy rules, including the 2.0 deprecations) is the
# one code this step adds; NPY001 and NPY002 were already selected at steps 2
# and 1, and the PD family and B arrived at steps 4 and 3.
SELECTION = ["B", "NPY", "NPY001", "NPY002", "PD", "PD002", "PD003", "PD007",
             "PD008", "PD009", "PD012", "PD015", "UP"]

# The three numpy AST rules the scanner turns on at step 5. Only one is a
# manifest row that grades: numpy-distutils, which the entry tree fails (nine
# references) and the golden clears. numpy-expired-member and numpy-copy-false
# become active at the same step but are carried guards -- the golden source is
# already numpy-2-clean, so each is an honest limit-0 carry rather than this
# step's work.
NEW_AST_RULES = ["numpy-copy-false", "numpy-distutils", "numpy-expired-member"]

# `compare`'s defaults, since test.sh does not override them. The flip
# discriminator below has to use the same tolerances the scoring run will.
RTOL, ATOL = 1e-7, 1e-10

# pip, setuptools and wheel are dropped from both sides of the closure check
# because `pip freeze` does not list them the same way across environments; the
# two that matter are pinned in `stack.txt` and asserted by name in VERSIONS,
# and the interpreter build fixes the third.
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
        check("versions: %s is not the step-4 pin" % name,
              got.get(name) == PREVIOUS[name], False)

    print("closure")
    check_closure("/tests/stack.txt")

    print("semantics")
    sem = probe.semantics()
    for key in sorted(SEMANTICS):
        check("semantics: %s" % key, sem.get(key), SEMANTICS[key])
    check("semantics: the numpy-2.0.2 expiry fingerprint",
          sem.get("surviving_expired_names"), EXPIRY_FINGERPRINT)

    print("tools")
    try:
        out = subprocess.check_output(["ruff", "--version"],
                                      stderr=subprocess.STDOUT)
        version = out.decode("utf-8", "replace").strip()
    except Exception as exc:                              # noqa: BLE001
        version = "not runnable: %s" % exc
    check("tools: ruff version", version, "ruff 0.14.0")

    # Without a TOML reader the suppression scan cannot read pyproject.toml and
    # reports one `toml-unreadable` finding instead of what is in the file, and
    # meson-python cannot read the same file to drive the build. The ceiling in
    # manifest.json was measured with a reader present.
    check("tools: a TOML reader is importable",
          bool(suppress.HAVE_TOML), True)

    # The staged selection for this step, so a mis-edit of ruffgate.SELECTIONS
    # shows up here rather than as a mysteriously easy gate. And the ladder
    # invariant: step 4's codes are all still selected at step 5.
    check("tools: step 5 ruff selection", ruffgate.selection_for(5), SELECTION)
    check("tools: step 5 ruff target", ruffgate.target_for(5), "py39")
    check("tools: step 4's selection is still contained in it",
          [c for c in ruffgate.selection_for(4)
           if c not in ruffgate.selection_for(5)], [])
    check("tools: NPY is new at step 5",
          "NPY" in ruffgate.selection_for(5)
          and "NPY" not in ruffgate.selection_for(4), True)

    # The three numpy AST rules this step adds. Unlike the pandas rules at step
    # 4, only one of the three is a manifest row: numpy-distutils, which the
    # golden takes to zero by moving the build off numpy.distutils. The other
    # two are carried guards -- the source was numpy-2-clean since step 2, so a
    # limit-0 row on them is honest. This half asserts the scanner *knows* all
    # three and that they are new at 5; the other half, under `expectations`,
    # asserts which one is a row and which two are carries. Re-derive both halves
    # for the next step rather than carrying this over.
    active5 = set(r.name for r in ast_rules.by_step(5))
    active4 = set(r.name for r in ast_rules.by_step(4))
    check("rules: the three numpy rules are new at step 5",
          sorted(active5 - active4), NEW_AST_RULES)
    check("rules: step 5 retires none of step 4's rules",
          sorted(active4 - active5), [])

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

    with open("/tests/expected/flips-step05.json") as fh:
        flips = json.load(fh)
    check("flips: this step's file", flips.get("step"), 5)
    entries = flips.get("flips") or []
    check("flips: how many", len(entries), 166)
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
    # and, because `mean(corpus)` is 85% of family A, 166 of them would hand
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
    # vector LINPACK returned). Every step below inherits the earlier golden, so
    # the flip is already satisfied by the tree it is handed -- but the recording
    # is still step 1's, so the flip is still declared, and it still has to
    # record the flag that was passed in rather than the vector.
    qr = [e for e in entries if e["case"] == "odr.QRDecomposition.params"]
    check("flips: the QR case is still flipped", len(qr), 1)
    if qr:
        got = dict((k["v"], v) for k, v in qr[0]["expect"]["value"]["v"])
        check("flips: and its pivot is the flag the caller passed",
              got.get("pivot"), {"t": "int", "v": 1})
        check("flips: while X is the matrix it was handed",
              (got.get("X") or {}).get("shape"), [20, 4])

    # The inherited suite, re-captured under numpy 2.0.2 against the golden.
    # numpy 2.0 breaks no additional skutil test -- the six failures are the
    # matplotlib-3.8 and scipy-1.13 breakages inherited from step 4 -- so the
    # 63 outcomes and their 57/6 census are step 4's, unchanged. Asserted
    # against the tally so a hand-edit of one without the other fails the build.
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
    check("expectations: the suite is step 4's, none added or dropped",
          (len(outcomes), "test_yeo_johnson" in
           ".".join(outcomes)), (63, True))

    with open("/tests/manifest.json") as fh:
        manifest = json.load(fh)
    check("expectations: the manifest is this step's", manifest["step"], 5)
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

    # Step 5's one new row, and only that. numpy-distutils is the build-system
    # migration numpy 2.0 forces: the tree the step is handed reaches for
    # numpy.distutils nine times and does not build, the golden moves to
    # meson-python and reaches for it zero times. Everything else this step
    # touches is a carry -- there is no widened ruff family this step, because
    # NPY is added to a selection whose golden is already clean under it.
    check("expectations: exactly one new row, the one this step adds",
          sorted(new_names), ["numpy-distutils"])

    # Step 4's new rows are step 5's carried rows: the ladder's whole shape is
    # that nothing an earlier step earned may come back. pandas-groupby-
    # implicit-observed and ruff-pandas-vet were step 4's `new`; here they must
    # be carried.
    check("expectations: step 4's new work is carried here",
          [n for n in ("pandas-groupby-implicit-observed", "ruff-pandas-vet")
           if n not in carried_names], [])
    check("expectations: step 3's new work is carried here",
          [n for n in ("set-output", "ruff-bugbear")
           if n not in carried_names], [])
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

    # The three numpy AST rules the scanner newly reaches at step 5, and the
    # split between them. numpy-distutils is this step's one new AST row (the
    # golden drives its nine sites to zero); numpy-expired-member and
    # numpy-copy-false are carried guards the golden keeps at zero. Assert each
    # is present and placed as its own gateable row.
    check("expectations: numpy-distutils is this step's one new AST row",
          [n for n in NEW_AST_RULES if n in new_names], ["numpy-distutils"])
    check("expectations: the two numpy guards are carried",
          sorted(n for n in NEW_AST_RULES if n in carried_names),
          ["numpy-copy-false", "numpy-expired-member"])
    check("expectations: and all three are gated at limit zero",
          sorted(set(rows[n]["limit"] for n in NEW_AST_RULES if n in rows)), [0])

    distutils = rows["numpy-distutils"]
    check("expectations: numpy-distutils is an AST rule row",
          (distutils["kind"], distutils.get("rule")), ("ast", "numpy-distutils"))
    check("expectations: nine numpy.distutils references on the entry tree, "
          "floored to zero", (distutils["entry"], distutils["limit"]), (9, 0))
    check("expectations: and it is a new row at this step, not a carry",
          ("numpy-distutils" in new_names,
           "numpy-distutils" in carried_names), (True, False))

    api = rows["public-api"]
    check("expectations: the public-api row still names 75",
          sum(len(v) for v in api["expect"].values()), 75)
    check("expectations: across ten importing modules",
          len(api["expect"]), 10)

    # The inventory is the row that stops a parse gate being answered with `rm`,
    # so it has to list the files that survive the migration -- and *not* list
    # the ones it retires, because a kind=paths row has no allow list and a
    # retired file left in the expect would be counted as missing on the golden.
    files = rows["file-inventory"]["expect"]
    check("expectations: the inventory covers the handed-over tree minus the "
          "retired build files", len(files), 95)
    check("expectations: including the one nothing imports",
          "doc/sphinxext/numpy_ext/docscrape.py" in files, True)
    # NumPy 2.0 removes numpy.distutils, so the golden deletes the four setup.py
    # it configured. Deleted, not stubbed: they must be absent from the inventory.
    check("expectations: the four numpy.distutils setup.py files are retired",
          sorted(p for p in ("setup.py", "skutil/setup.py",
                             "skutil/metrics/setup.py", "skutil/odr/setup.py")
                 if p in files), [])
    # _build_utils/__init__.py is stubbed to its DEFAULT_ROOT constant, not
    # deleted: the file must still be present in the tree, so it stays in the
    # inventory even though it drops out of the content rows below.
    check("expectations: but _build_utils/__init__.py stays in the inventory "
          "(stubbed, not deleted)",
          "skutil/_build_utils/__init__.py" in files, True)
    # 95, not the tree's 96 shipped files: a Cython-generated .c that sits beside
    # its .pyx is build output -- any build regenerates and clobbers it -- and is
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
          len(censused), 33)
    check("expectations: the substance row covers every file with a binding",
          len(lines), 47)
    check("expectations: and every censused file carries a line floor",
          set(censused) <= set(lines), True)
    # The stubbed build file drops out of the content rows even though it stays
    # in the inventory: a stub has neither the definitions nor the line count the
    # entry tree carried, so demanding either back would fail the honest golden.
    check("expectations: the stubbed build file is not in the census",
          "skutil/_build_utils/__init__.py" in censused, False)
    check("expectations: nor in the substance row",
          "skutil/_build_utils/__init__.py" in lines, False)
    # Retention records which lines it was handed, because a count can be padded
    # back while the code stays gone. Re-recorded against *this* step's entry
    # tree (step 4's golden), not inherited.
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
    # step-4 golden, which supports set_output on all eighteen, so entry is zero
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

    # The pandas-vet family was step 4's new row; here it is carried at zero. The
    # step-4 golden reads `.to_numpy()` throughout, so the tree this step is
    # handed measures 0 under the whole PD family -- a carried guard that a
    # numpy-2 edit does not reach back for `.values`.
    pandasvet = rows["ruff-pandas-vet"]
    check("expectations: ruff-pandas-vet counts the PD family",
          (pandasvet["kind"], pandasvet["code"]), ("ruff", "PD"))
    check("expectations: ruff-pandas-vet carried at zero on the entry tree",
          (pandasvet["entry"], pandasvet["limit"]), (0, 0))
    check("expectations: and it is carried at this step, not new",
          ("ruff-pandas-vet" in new_names,
           "ruff-pandas-vet" in carried_names), (False, True))

    # pandas-groupby-implicit-observed was step 4's one new AST row; here it is
    # a carried guard at zero, since the step-4 golden already names observed=
    # on both groupers.
    groupby = rows["pandas-groupby-implicit-observed"]
    check("expectations: the groupby rule is carried at zero now",
          (groupby["kind"], groupby["entry"], groupby["limit"]), ("ast", 0, 0))
    check("expectations: and it is carried at this step, not new",
          ("pandas-groupby-implicit-observed" in new_names,
           "pandas-groupby-implicit-observed" in carried_names), (False, True))

    # estimator-contract is carried (step 2 earned it). The entry tree here is
    # step 4's golden, which imports under scikit-learn 1.6 and holds the
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
    # inherited suppressions is not this step's job, but the count may not grow,
    # and numpy 2.0 raises its own DeprecationWarnings from the tree's lines, so
    # a filter is the cheapest way to make them stop being counted.
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
