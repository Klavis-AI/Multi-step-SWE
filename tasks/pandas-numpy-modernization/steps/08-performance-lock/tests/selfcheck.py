"""Assert that this verifier image is the one step 8 was calibrated against.

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

Step 8 moves no scored library at all: matplotlib holds at 3.10.9, pandas at
3.0.5, numpy 2.3.5, scikit-learn 1.7.2, the interpreter 3.12 -- character for
character step 7's stack, all asserted here so an image accidentally rebuilt on
a different pandas or numpy fails the build. Because nothing that drives the
corpus moves, the recording is unchanged and so is the flip set: step 7's same
318 hand-written claims that the pinned libraries changed an answer, carried
verbatim into ``flips-step08.json``. Two ways a flip can be wrong, and both
score green:

  * a flip that *restates* what the entry tree already produced grades nothing
    -- the tree the step was handed would satisfy it untouched. Checked below,
    against the recording itself, for all 318.
  * a flip whose new answer this image does not actually produce would fail the
    honest candidate. That one cannot be settled here -- it is what the oracle
    run settles -- but the pins it depends on are asserted here, so a flip
    cannot quietly start being graded under a different stack.

What step 8 ADDS to the image is the columnar-engine toolchain: ``polars`` and
``duckdb`` are installed into the verifier and asserted here. They are the one
thing that distinguishes this image from step 7's -- an image left on step 7's
stack has neither -- so they stand in for the ``PREVIOUS`` pin-move a normal
step would assert. D-7 leaves the vectorisation engine open: a candidate may
take a hot path off row iteration with numpy or by pushing it into
polars/duckdb, and the verifier imports the candidate's own skutil to record the
corpus, so an engine-based solution must find them installed rather than fail to
import and score zero on family B.

The new work at this step is a performance lock: three interpreter-level hot
paths -- ``filter_collinearity``, ``InteractionTermTransformer.transform`` and
``GainsStatisticalReport.as_data_frame`` -- are vectorised so that no ``for`` or
``while`` loop iterates the frame a row at a time. That shows up as one new AST
rule the scanner gates as a manifest row: ``hot-path-row-iteration`` (seven
loops in those three functions the golden removes -- every ``for``, comprehension
and non-driver ``while`` in them, whatever the iterator, so a respelling cannot
slip past). Three more rules become active at the same step but are carried
guards at zero -- ``pandas-row-iteration`` (``.iterrows``/``.itertuples`` anywhere
in the package), ``pandas-apply-axis-1`` (``.apply(..., axis=1)``) and
``hot-path-engine-udf`` (a polars/duckdb per-row callable), the row-wise escape
hatches a lazy vectorisation would reach for and the golden never does. This
file asserts that split holds in both directions: the scanner knows all four
rules, and the manifest gates the one that is this step's work.

Nothing else in the manifest moves at this step. The two rows step 7 earned
(``pyplot-show-in-library``, ``pyplot-global-state``) become carried guards at
zero here, the ruff target stays py312 (so ``ruff-bugbear`` stays at entry
zero), and the build system is the same meson-python step 5 introduced.

A failure here fails ``docker build``. That is the intent: a verifier image
built against the wrong stack, or without the columnar engines the open engine
choice requires, must not exist, let alone grade anything.
"""
import hashlib
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
# catches. setuptools and wheel are checked by name here rather than in the
# closure block (pip freeze does not list them the same way across
# environments). meson-python is the build backend and a runtime-visible pin;
# meson, ninja and pyproject-metadata are asserted by the closure. polars and
# duckdb are new at step 8 -- the columnar engines D-7 lets a candidate vectorise
# into -- and are checked by name because their presence is what tells step 8's
# image apart from step 7's.
VERSIONS = {
    "numpy": "2.3.5",
    "scipy": "1.16.3",
    "pandas": "3.0.5",
    "pyarrow": "21.0.0",
    "scikit-learn": "1.7.2",
    "matplotlib": "3.10.9",
    "seaborn": "0.13.2",
    "Cython": "3.1.6",
    "pytest": "8.4.2",
    "setuptools": "80.9.0",
    "wheel": "0.45.1",
    "meson-python": "0.18.0",
    "polars": "1.35.1",
    "duckdb": "1.4.4",
    "skops": "0.13.0",
}

# The step-7 stack, and which this image must *not* be. No runtime pin moves
# between step 7 and step 8 -- every scored library is identical -- so there is
# no "must differ" pin to name, and a "must differ" check on a pin that stayed
# put would demand the image differ from itself. What tells step 8's image apart
# from step 7's is instead an ADDITION: polars and duckdb, present here and
# absent from step 7's image, asserted in VERSIONS above. PREVIOUS is therefore
# empty by design.
PREVIOUS = {}

# The stack the behaviour recording was made under, at step 1, and never
# re-recorded since. The recording is *supposed* to disagree with this image's
# libraries; the flips are how that disagreement is declared. Asserted so a
# recording quietly re-made under a newer pandas would fail the build.
RECORDING = {
    "pandas": "0.23.4",
    "numpy": "1.14.6",
}

# What these versions mean, as behaviour rather than as strings. None of them
# move at step 8 -- pandas is still 3.0 (PDEP-14 string dtype, copy-on-write
# always on), numpy still 2.3 (NEP 50 weak, `np.float64(1.5)` repr) and
# scikit-learn 1.7 -- and asserting them here is how an image quietly rebuilt on
# an older pandas or numpy fails the build even though step 8 was meant to move
# no library at all. No version strings drive the flips -- a vendored shim
# faking pandas-2 object columns would keep __version__ and change these.
SEMANTICS = {
    "python": "3.12",
    "numpy_version": "2.3.5",
    "copy_on_write": True,
    "default_string_dtype": "str",
    "default_int_dtype": "int64",
    "nep50_f32_plus_pyfloat": "float32",
    "nep50_i64_plus_f32": "float64",
    "nep50_u8_plus_200": "uint8",
    "nep50_u8_array_plus_200": "uint8",
    "scalar_repr": "np.float64(1.5)",
    "view_tracks_parent": False,
    "sklearn_version": "1.7.2",
    "pandas_version": "3.0.5",
}

# Which members of the 2.0 expiry list this numpy still answers to. At 2.3.5
# `in1d`, `row_stack` and `trapz` are still present (deprecated, not yet
# removed), and nothing else on the list is: `float_`, `unicode_`, `alltrue`,
# `NaN` and the rest are gone. The exact set is the fingerprint of *this* numpy:
# 1.26 would answer to all nineteen, a still-later 2.x to none of these three,
# so pinning the set to these three ties the image to numpy 2.3's line.
EXPIRY_FINGERPRINT = ["in1d", "row_stack", "trapz"]

# The staged ruff selection for this step, in the order `selection_for` returns
# it. Nothing about ruff moves at step 8: no new code family, and the target
# stays py312 (it advanced py39 -> py312 back at step 6). The performance-lock
# work the scanner gates is AST rules, not ruff codes. selection_for(8) is
# therefore identical to selection_for(7), asserted below.
SELECTION = ["B", "NPY", "NPY001", "NPY002", "PD", "PD002", "PD003", "PD007",
             "PD008", "PD009", "PD012", "PD015", "UP"]

# The four AST rules the scanner newly reaches at step 8. One is a manifest row
# that grades -- hot-path-row-iteration, which the entry tree (step 7's golden)
# fails on seven loops in the three hot paths and this step's golden clears by
# vectorising -- and three are carried guards at zero: pandas-row-iteration,
# pandas-apply-axis-1 and hot-path-engine-udf, the row-wise escape hatches the
# golden never reaches for. This name list is the scanner's side of the split;
# the manifest side is asserted under `expectations`.
NEW_AST_RULES = ["hot-path-engine-udf", "hot-path-row-iteration",
                 "pandas-apply-axis-1", "pandas-row-iteration",
                 "pickle-persistence"]
# Review round 2 added `pickle-persistence` (the task line's "pickles replaced
# by skops/ONNX"): a fifth AST rule new at step 8, and the AST half of the
# `persistence-and-examples` manifest row of kind `replaced`, whose other
# halves are the skops.io call census and the example-notebook run. The same round added the
# `environment-lock` row (kind `lock`, the task line's "environment lock"),
# which reads the tree's pylock.toml and is no AST rule.

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
    check("interpreter: cpython build", sys.version.split()[0], "3.12.11")

    print("versions")
    got = probe.installed_versions(sorted(set(VERSIONS) | set(PREVIOUS)))
    for name in sorted(VERSIONS):
        check("versions: %s" % name, got.get(name), VERSIONS[name])
    for name in sorted(PREVIOUS):
        check("versions: %s is not the step-7 pin" % name,
              got.get(name) == PREVIOUS[name], False)

    print("closure")
    check_closure("/tests/stack.txt")

    print("semantics")
    sem = probe.semantics()
    for key in sorted(SEMANTICS):
        check("semantics: %s" % key, sem.get(key), SEMANTICS[key])
    check("semantics: the numpy-2.3 expiry fingerprint",
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
    # meson-python cannot read the same file to drive the build. Under python
    # 3.12 the reader is the stdlib `tomllib`, so no `tomli` pin is needed; the
    # ceiling in manifest.json was measured with a reader present.
    check("tools: a TOML reader is importable",
          bool(suppress.HAVE_TOML), True)

    # The staged selection for this step, so a mis-edit of ruffgate.SELECTIONS
    # shows up here rather than as a mysteriously easy gate. And the ladder
    # invariant: step 7's codes are all still selected at step 8, with no new
    # family added and no target move.
    check("tools: step 8 ruff selection", ruffgate.selection_for(8), SELECTION)
    check("tools: step 8 ruff target is py312", ruffgate.target_for(8), "py312")
    check("tools: step 7's selection is still contained in it",
          [c for c in ruffgate.selection_for(7)
           if c not in ruffgate.selection_for(8)], [])
    check("tools: no new ruff family at step 8",
          sorted(set(ruffgate.selection_for(8)) - set(ruffgate.selection_for(7))),
          [])
    check("tools: and the selection is unchanged from step 7",
          ruffgate.selection_for(8), ruffgate.selection_for(7))
    check("tools: the target does not advance at step 8 (stays py312)",
          (ruffgate.target_for(7), ruffgate.target_for(8)), ("py312", "py312"))

    # The four AST rules this step reaches. One is a manifest row
    # (hot-path-row-iteration); three are carried guards (pandas-row-iteration,
    # pandas-apply-axis-1, hot-path-engine-udf). This half asserts the scanner
    # *knows* all four and that they are new at 8; the other half, under
    # `expectations`, asserts which one is a row and which three are carries.
    # Re-derive both halves for the next step rather than carrying this over.
    active8 = set(r.name for r in ast_rules.by_step(8))
    active7 = set(r.name for r in ast_rules.by_step(7))
    check("rules: the five AST rules are new at step 8",
          sorted(active8 - active7), NEW_AST_RULES)
    check("rules: step 8 retires none of step 7's rules",
          sorted(active7 - active8), [])

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

    with open("/tests/expected/flips-step08.json") as fh:
        flips = json.load(fh)
    check("flips: this step's file", flips.get("step"), 8)
    entries = flips.get("flips") or []
    check("flips: how many", len(entries), 318)
    check("flips: no case is dropped from the corpus at this step",
          flips.get("drops"), [])

    # Warn 10 / R3: the flip *payload* is step 7's, carried forward unchanged --
    # step 8 (performance-lock) changes no behaviour, so its 318 flips are
    # byte-for-byte step 7's array; only the top-level `step` (7 -> 8) and the
    # `note` differ. Machine-checked here, not just asserted in prose: the
    # constant is the sha256 of step 7's flips array (flips-step07.json), and
    # this step's array must hash to the same value. Recompute the constant with
    #   python -c "import json,hashlib; d=json.load(open('flips-step07.json'));\
    #     print(hashlib.sha256(json.dumps(d['flips'], sort_keys=True,\
    #       separators=(',',':')).encode()).hexdigest())"
    FLIPS_STEP7_SHA = \
        "bff9bd0de4024f03b9099d7c0fe2c60cd738c2f5abbff5d5c9008817958da7f4"
    step8_sha = hashlib.sha256(
        json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    check("flips: the 318-flip payload is step 7's, carried verbatim",
          step8_sha, FLIPS_STEP7_SHA)

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
    # and, because `mean(corpus)` is 85% of family A, 318 of them would hand
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

    # The inherited suite, re-captured under this step's stack against the entry
    # tree (step 7's golden). No scored library moves at step 8, so the tree is
    # scored under the very stack step 7 recorded 55/8 under, and the tally holds
    # at 55/8. Step 8's work is a performance rewrite of three hot paths and must
    # leave every recorded answer unchanged within tolerance, so it repairs none
    # of the eight failures and breaks none of the fifty-five: entry and golden
    # share this 55/8 tally. test_yeo_johnson stays among the eight inherited
    # failures the floor does not require this step to fix. Asserted against the
    # tally so a hand-edit of the suite fails the build.
    with open("/tests/expected/inherited-suite.json") as fh:
        inherited = json.load(fh)
    outcomes = inherited["outcomes"]
    check("expectations: recorded test outcomes", len(outcomes), 63)
    check("expectations: their census", inherited["census"],
          {"passed": 55, "failed": 8})
    tally = {"passed": sum(1 for v in outcomes.values() if v == "passed"),
             "failed": sum(1 for v in outcomes.values() if v == "failed")}
    check("expectations: the census is the tally of the outcomes",
          tally, inherited["census"])
    check("expectations: every outcome is passed or failed",
          sorted(set(outcomes.values())), ["failed", "passed"])
    check("expectations: the suite is step 7's, none added or dropped",
          (len(outcomes), "test_yeo_johnson" in
           ".".join(outcomes)), (63, True))

    with open("/tests/manifest.json") as fh:
        manifest = json.load(fh)
    check("expectations: the manifest is this step's", manifest["step"], 8)
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

    # Step 8's one new row, and only that. hot-path-row-iteration (seven loops
    # across the three hot paths) is the performance work; the golden clears it
    # by vectorising each path. Nothing else this step touches is a new row: no
    # library moves so there are no new flips, pandas-row-iteration,
    # pandas-apply-axis-1 and hot-path-engine-udf are carried guards, and no ruff
    # family or target moves.
    check("expectations: exactly three new rows, the ones this step adds",
          sorted(new_names),
          ["environment-lock", "hot-path-row-iteration",
           "persistence-and-examples"])

    # Step 7's two new rows are step 8's carried rows: the ladder's whole shape
    # is that nothing an earlier step earned may come back. pyplot-show-in-library
    # and pyplot-global-state were step 7's `new`; here they must be carried, and
    # at zero (step 7's golden, the tree this step is handed, already returns
    # figures and clears both).
    check("expectations: step 7's new work is carried here",
          [n for n in ("pyplot-show-in-library", "pyplot-global-state")
           if n not in carried_names], [])
    # And step 6's three new rows stay carried at zero.
    check("expectations: step 6's new work is carried here",
          [n for n in ("stdlib-distutils", "pandas-inplace-kwarg",
                       "pandas-object-dtype-blind")
           if n not in carried_names], [])
    # And step 5's new row, numpy-distutils, stays carried at zero.
    check("expectations: step 5's new work is carried here",
          [n for n in ("numpy-distutils",)
           if n not in carried_names], [])
    # Step 4's new rows are carried too.
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

    # The four AST rules the scanner newly reaches at step 8, and the split
    # between them. One is this step's new row; three are carried guards the
    # golden keeps at zero. Assert each is present and placed as its own gateable
    # row, and that all four are gated at limit zero.
    check("expectations: the one new AST row is this step's",
          sorted(n for n in NEW_AST_RULES if n in new_names),
          ["hot-path-row-iteration"])
    check("expectations: the three row-wise guards are carried",
          sorted(n for n in NEW_AST_RULES if n in carried_names),
          ["hot-path-engine-udf", "pandas-apply-axis-1", "pandas-row-iteration"])
    check("expectations: and all four are gated at limit zero",
          sorted(set(rows[n]["limit"] for n in NEW_AST_RULES if n in rows)), [0])

    hotpath = rows["hot-path-row-iteration"]
    check("expectations: hot-path-row-iteration is an AST rule row",
          (hotpath["kind"], hotpath.get("rule")),
          ("ast", "hot-path-row-iteration"))
    check("expectations: seven row-iterating loops in the three hot paths on "
          "the entry tree, floored to zero",
          (hotpath["entry"], hotpath["limit"]), (7, 0))

    # The two reproducibility rows review round 2 added. Their entry counts
    # are measured on the entry tree (step 7's golden): two joblib calls in
    # the pipeline notebook and no skops.io save or load (2 + 2), and no
    # pylock.toml at all (all nineteen pins missing). The weights keep the
    # vectorisation the step is chiefly about at 3/5 of the family.
    pick = rows["persistence-and-examples"]
    check("expectations: persistence-and-examples is a replaced row over "
          "pickle-persistence",
          (pick["kind"], pick.get("rule"), pick["entry"], pick["limit"]),
          ("replaced", "pickle-persistence", 4, 0))
    check("expectations: and it runs the two example notebooks the stack can run",
          sorted(pick.get("executes") or []),
          ["doc/examples/decomposition/SelectivePCA weighting.ipynb",
           "doc/examples/pipeline/skutil grid demo.ipynb"])
    from skutil_gates import run as gates_run                # noqa: E402
    check("expectations: the notebooks stage runs exactly those",
          sorted(gates_run.EXAMPLE_NOTEBOOKS), sorted(pick.get("executes") or []))
    check("expectations: and every one it runs is in the inventory",
          [n for n in pick.get("executes") or []
           if n not in rows["file-inventory"]["expect"]], [])
    check("expectations: and it requires a skops.io save and a skops.io load",
          [(sorted(r["calls"]), r.get("at_least")) for r in pick["require"]],
          [(["skops.io.dump", "skops.io.dumps"], 1),
           (["skops.io.load", "skops.io.loads"], 1)])
    lock = rows["environment-lock"]
    check("expectations: environment-lock is a PEP 751 lock row",
          (lock["kind"], lock.get("lock_version"), lock["entry"], lock["limit"]),
          ("lock", "1.0", 19, 0))
    check("expectations: it pins the nineteen distributions of the environment",
          len(lock["expect"]), 19)
    check("expectations: and agrees with this image on every pin both name",
          sorted(n for n, v in lock["expect"].items()
                 if n in VERSIONS and VERSIONS[n] != v), [])
    check("expectations: the new rows are weighted 3:1:1",
          (hotpath.get("weight"), pick.get("weight"), lock.get("weight")),
          (3, 1, 1))

    check("expectations: and the one is a new row, not a carry",
          sorted(n for n in ("hot-path-row-iteration",)
                 if n in new_names and n not in carried_names),
          ["hot-path-row-iteration"])

    # The carried AST guards at zero. numpy-distutils (step 5), pandas-expired-3
    # and pandas-fillna-downcast (step 6's pandas-3 removals), the three rows
    # step 6 earned (stdlib-distutils, pandas-inplace-kwarg,
    # pandas-object-dtype-blind), step 7's three matplotlib rules the scanner
    # turned on (matplotlib-ambient-rcparams, pyplot-show-in-library,
    # pyplot-global-state), and this step's own non-forcing row-wise guards
    # (pandas-row-iteration, pandas-apply-axis-1, hot-path-engine-udf) -- every
    # one an AST rule the golden keeps at zero on the entry tree it is handed.
    for guard in ("numpy-distutils", "pandas-expired-3",
                  "pandas-fillna-downcast", "stdlib-distutils",
                  "pandas-inplace-kwarg", "pandas-object-dtype-blind",
                  "matplotlib-ambient-rcparams", "pyplot-show-in-library",
                  "pyplot-global-state", "pandas-row-iteration",
                  "pandas-apply-axis-1", "hot-path-engine-udf"):
        row = rows[guard]
        check("expectations: %s is a carried AST guard at zero" % guard,
              (row["kind"], row["entry"], row["limit"], guard in carried_names),
              ("ast", 0, 0, True))

    api = rows["public-api"]
    check("expectations: the public-api row still names 75",
          sum(len(v) for v in api["expect"].values()), 75)
    check("expectations: across ten importing modules",
          len(api["expect"]), 10)

    # The inventory is the row that stops a parse gate being answered with `rm`,
    # so it has to list the files that survive the migration -- and *not* list
    # the ones the earlier steps retired, because a kind=paths row has no allow
    # list and a retired file left in the expect would be counted as missing on
    # the golden. The entry tree here is step 7's golden. Step 7 edited sources
    # in place and neither added nor removed a file, so it hands on the same 97
    # step 5's golden ended on (the meson-python backend and the four retired
    # numpy.distutils setup.py included); step 8 is another in-place rewrite and
    # retires nothing of its own. 97, re-recorded from this step's own entry tree
    # by tools/record_census.py (whose RETIRED table carries a step-5 entry
    # only), not copied down the ladder.
    files = rows["file-inventory"]["expect"]
    check("expectations: the inventory covers the handed-over tree", len(files),
          97)
    # The meson-python build backend step 5 introduced survives this runtime
    # migration untouched, so it is inventoried here: a candidate cannot delete
    # meson.build or pyproject.toml without the paths gate seeing it go.
    check("expectations: the meson-python backend files are inventoried",
          sorted(p for p in ("meson.build", "pyproject.toml") if p in files),
          ["meson.build", "pyproject.toml"])
    check("expectations: including the one nothing imports",
          "doc/sphinxext/numpy_ext/docscrape.py" in files, True)
    # The four numpy.distutils setup.py were deleted at step 5, so they remain
    # absent from the inventory this step inherits.
    check("expectations: the four numpy.distutils setup.py files stay retired",
          sorted(p for p in ("setup.py", "skutil/setup.py",
                             "skutil/metrics/setup.py", "skutil/odr/setup.py")
                 if p in files), [])
    # _build_utils/__init__.py was stubbed (not deleted) at step 5: still in the
    # tree, so still in the inventory, even though it stays out of the content
    # rows below.
    check("expectations: _build_utils/__init__.py stays in the inventory "
          "(stubbed at step 5, not deleted)",
          "skutil/_build_utils/__init__.py" in files, True)
    # A Cython-generated .c that sits beside its .pyx is build output -- any
    # build regenerates and clobbers it -- and is not pinned. The .pyx it comes
    # from is pinned and is load-bearing.
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
    # _build_utils/__init__.py was freshly stubbed at step 5, so step 5 dropped
    # it from its content rows: demanding step 4's line count back would have
    # failed step 5's honest golden. Step 8 inherits it as a stable file with a
    # binding (DEFAULT_ROOT) and stubs nothing further, so it earns a substance
    # and retention row here on its own small line count. It still defines
    # nothing, so it stays out of the definition census.
    check("expectations: the stubbed build file is still not in the census",
          "skutil/_build_utils/__init__.py" in censused, False)
    # 2026-10-06 (DeepSeek trajectory review): nor does it carry a substance
    # or retention row. Step 5's instruction lets the stub keep none of its
    # contents, so a floor taken from the reference's four-line stub zeroed an
    # emptied one; file-inventory above still requires the file.
    check("expectations: nor is it in the substance row (step 5 may empty it)",
          "skutil/_build_utils/__init__.py" in lines, False)
    # Retention records which lines it was handed, because a count can be padded
    # back while the code stays gone. Re-recorded against *this* step's entry
    # tree (step 7's golden), not inherited.
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
    # step-7 golden, which supports set_output on all eighteen, so entry is zero
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

    # ruff-bugbear is carried from step 3. It was the one carried ruff row with a
    # non-zero entry at step 6, when the py39 -> py312 target move lit B905 (`zip`
    # without `strict=`) on twelve sites; step 6's golden cleared them. Neither
    # step 7 nor step 8 moves the target (it stays py312), so the tree step 8 is
    # handed -- step 7's golden -- reads zero under the B family, and the row
    # stays at entry=0 -> limit=0. The vectorisation work must keep it there: a
    # new `zip(...)` over broadcast column pairs without strict= would relight
    # B905, so the golden passes strict=False.
    bugbear = rows["ruff-bugbear"]
    check("expectations: ruff-bugbear counts the B family",
          (bugbear["kind"], bugbear["code"]), ("ruff", "B"))
    check("expectations: ruff-bugbear stays at zero on the entry tree",
          (bugbear["entry"], bugbear["limit"]), (0, 0))

    # The pandas-vet family is carried at zero: step 4's golden rewrote `.values`
    # to `.to_numpy()` throughout, and every step since kept it, so the tree this
    # step is handed measures 0 under the whole PD family.
    pandasvet = rows["ruff-pandas-vet"]
    check("expectations: ruff-pandas-vet counts the PD family",
          (pandasvet["kind"], pandasvet["code"]), ("ruff", "PD"))
    check("expectations: ruff-pandas-vet carried at zero on the entry tree",
          (pandasvet["entry"], pandasvet["limit"]), (0, 0))
    check("expectations: and it is carried at this step, not new",
          ("ruff-pandas-vet" in new_names,
           "ruff-pandas-vet" in carried_names), (False, True))

    # pandas-groupby-implicit-observed is carried at zero, since the step-4
    # golden already names observed= on both groupers.
    groupby = rows["pandas-groupby-implicit-observed"]
    check("expectations: the groupby rule is carried at zero now",
          (groupby["kind"], groupby["entry"], groupby["limit"]), ("ast", 0, 0))
    check("expectations: and it is carried at this step, not new",
          ("pandas-groupby-implicit-observed" in new_names,
           "pandas-groupby-implicit-observed" in carried_names), (False, True))

    # estimator-contract is carried (step 2 earned it). The entry tree here is
    # step 7's golden, which imports under scikit-learn 1.7 and holds the
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
    # inherited suppressions is not this step's job, but the count may not grow.
    supp = rows["suppression-ceiling"]
    check("expectations: the suppression ceiling is the entry count",
          (supp["entry"], supp["limit"]), (13, 13))

    # The launcher that keeps a candidate-writable cwd from shadowing the
    # harness must ship, and every stage that runs a harness module must go
    # through it.
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
