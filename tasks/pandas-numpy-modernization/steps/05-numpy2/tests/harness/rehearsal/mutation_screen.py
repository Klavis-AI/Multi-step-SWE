"""Does the rehearsal catch anything?

``rehearse.py`` passing tells you the gates agree with ``expected.json``. It
does not tell you that the rehearsal would have noticed if they had not -- a
suite of checks that cannot fail reads exactly like a suite of checks that
passed. So: break one thing at a time, on purpose, and require the rehearsal to
go red. Each mutation below is a real failure mode of the corresponding module,
not an arbitrary edit:

* a rule that matches one construct too many (the migrated spelling as well as
  the retired one), which is how a gate starts penalising the fix;
* a rule that matches one too few, which is how a gate starts passing a tree
  that did nothing;
* a rule staged at the wrong step, which is how a step gets asked for work its
  own toolchain cannot do;
* a rule that reads only the first spelling of a construct the tree writes two
  ways, which is how a gate becomes clearable by rewriting rather than fixing;
* a rule written from the one evasion a fixture happened to use, which is how a
  gate closes a hole for the spelling that found it and no other;
* a suppression counter that charges for promoting a warning, which is how a
  step gets charged for doing its job;
* a scoring rule that gives full credit for partial work, which is how the
  whole family stops discriminating.

Exit status is 0 only if every mutation was caught.
"""
from __future__ import print_function, absolute_import, division

import ast
import io
import sys

sys.dont_write_bytecode = True

import rehearse                                  # noqa: E402
from skutil_gates import rules, score, suppress  # noqa: E402


def run_rehearsal():
    """Run the whole rehearsal quietly; return the list of failed labels."""
    del rehearse.FAILURES[:]
    rehearse.CHECKS[0] = 0
    saved = sys.stdout
    sys.stdout = io.StringIO()
    try:
        rehearse.main(["--skip-ruff", "--skip-probe"])
    finally:
        sys.stdout = saved
    return list(rehearse.FAILURES)


# --------------------------------------------------------------------------
# the mutations


def widen_pyplot():
    original = rules._PYPLOT_GLOBAL
    rules._PYPLOT_GLOBAL = frozenset(set(original) | {"subplots"})
    return lambda: setattr(rules, "_PYPLOT_GLOBAL", original)


def narrow_draws():
    original = rules._GLOBAL_DRAWS
    rules._GLOBAL_DRAWS = frozenset(set(original) - {"randn"})
    return lambda: setattr(rules, "_GLOBAL_DRAWS", original)


def misstage_rule():
    target = rules.by_name("pandas-inplace-kwarg")
    original = target.since
    target.since = 2
    return lambda: setattr(target, "since", original)


def direct_version_branches_only():
    """`py2-version-branch` as it read before the aliased spelling was added.

    Six of the real tree's nine sites bind the interpreter's major version to a
    module-level local and branch on the local, so a rule that only matches
    `sys.version_info` at the comparison sees three -- and can be *cleared* by
    hoisting the subscript out of the branch it guards, which leaves the dead
    code exactly where it was. The dirty fixture carries both spellings so this
    narrowing has something to miss.
    """
    target = rules.by_name("py2-version-branch")
    original = target.match

    def direct_only(node, ctx):
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            return None
        if isinstance(node.left, ast.Name):
            return None
        return original(node, ctx)

    target.match = direct_only
    return lambda: setattr(target, "match", original)


def getattr_fetches_only():
    """`dynamic-retired-name` as a rule written from its own fixture would read.

    The evasion this rule exists for was found by a fixture that spelled it
    `getattr(np, "flo" + "at")`, and a rule written from that fixture alone
    reads one of five spellings. `np.__dict__["int"]`, `vars(np)["object"]`,
    `sys.modules["sklearn.externals"]` and
    `importlib.import_module("sklearn.grid_search")` fetch exactly the same
    retired names with no getattr anywhere in them, so the narrow rule would
    report a tree carrying all four as clean. The dirty fixture carries all
    five so this narrowing has something to miss.
    """
    target = rules.by_name("dynamic-retired-name")
    original = target.match

    def getattr_only(node, ctx):
        if not isinstance(node, ast.Call):
            return None
        if not isinstance(node.func, ast.Name) or node.func.id != "getattr":
            return None
        return original(node, ctx)

    target.match = getattr_only
    return lambda: setattr(target, "match", original)


def count_promotions():
    original = suppress._IGNORE_ACTIONS
    suppress._IGNORE_ACTIONS = ("ignore", "error")
    return lambda: setattr(suppress, "_IGNORE_ACTIONS", original)


def credit_partial_work():
    original = score.Check.credit

    def generous(self, graded):
        return 1.0
    score.Check.credit = generous
    return lambda: setattr(score.Check, "credit", original)


def drop_notebooks():
    original = rules.by_name("global-rng-seed").match

    def never(node, ctx):
        return None
    rules.by_name("global-rng-seed").match = never
    return lambda: setattr(rules.by_name("global-rng-seed"), "match", original)


MUTATIONS = [
    ("a rule that also matches the migrated spelling (plt.subplots)",
     widen_pyplot, "clean"),
    ("a rule that stops matching its own construct (np.random.randn)",
     narrow_draws, "dirty"),
    ("a rule staged four steps too early (pandas-inplace-kwarg)",
     misstage_rule, "staging"),
    ("a rule that only reads one of two spellings (py2-version-branch)",
     direct_version_branches_only, "dirty"),
    ("a rule that reads only the spelling its own fixture used "
     "(dynamic-retired-name)", getattr_fetches_only, "dirty"),
    ("a suppression counter that charges for error:: entries",
     count_promotions, "clean"),
    ("a scorer that gives full credit for partial work",
     credit_partial_work, "score"),
    ("a rule silenced entirely (global-rng-seed)", drop_notebooks, "dirty"),
]


def main():
    baseline = run_rehearsal()
    if baseline:
        print("the unmutated rehearsal is already failing; fix that first:")
        for name in baseline:
            print("  - %s" % name)
        return 2

    missed = []
    for label, mutate, expect_area in MUTATIONS:
        restore = mutate()
        try:
            failures = run_rehearsal()
        finally:
            restore()
        hit = [f for f in failures if expect_area in f]
        if failures and hit:
            print("  caught  %-62s (%d checks failed, first: %s)"
                  % (label, len(failures), failures[0]))
        else:
            print("  MISSED  %-62s (failures: %r)" % (label, failures))
            missed.append(label)

    after = run_rehearsal()
    if after:
        print("\nmutations did not restore cleanly: %r" % after)
        return 2

    print("")
    if missed:
        print("MUTATION SCREEN FAILED: %d of %d mutations went unnoticed"
              % (len(missed), len(MUTATIONS)))
        return 1
    print("mutation screen passed: %d of %d mutations caught"
          % (len(MUTATIONS), len(MUTATIONS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
