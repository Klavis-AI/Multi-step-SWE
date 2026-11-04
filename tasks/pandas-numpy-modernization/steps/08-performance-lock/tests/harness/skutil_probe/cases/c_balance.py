"""skutil.preprocessing.balance: the three class balancers.

The balancers have no ``random_state``; they draw from the process-global
legacy RNG (``numpy.random.choice`` / ``permutation``), which the runner seeds
per case. Those two draw from the ``RandomState`` stream NumPy guarantees to be
bit-identical across releases, so the sampled rows are recordable exactly -- the
one thing that is *not* stable is the order of equally-populated classes coming
out of ``value_counts().sort_values()``, so the fixtures give every class a
distinct count and the tie cases record invariants only.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL, INVARIANT
from .. import data
from ..est import observe_balance

BTOL = dict(rtol=1e-9, atol=1e-12)


def _reg(cid, tags, fn, **kw):
    case(cid, tags=tags, **kw)(fn)


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _frame(*ratios, **kw):
    """Distinct class counts, so the ascending value_counts order is total."""
    n = kw.pop("n", 200)
    seed = kw.pop("seed", 41)
    tgt = kw.pop("tgt", "target")
    return lambda: data.class_frame(n=n, ratios=ratios, seed=seed, tgt=tgt)


def _object_frame():
    df = data.class_frame(n=200, ratios=(0.7, 0.2, 0.1), seed=17)
    df["target"] = df["target"].map({0: "maj", 1: "mid", 2: "min"})
    return df


def _over_cases():
    from skutil.preprocessing import OversamplingClassBalancer

    _reg("balance.Oversampling.binary", (BEHAVIOR,),
         observe_balance(lambda: OversamplingClassBalancer(y="target",
                                                           shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Oversampling.shuffled", (BEHAVIOR,),
         observe_balance(lambda: OversamplingClassBalancer(y="target",
                                                           shuffle=True),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Oversampling.three_class", (BEHAVIOR,),
         observe_balance(lambda: OversamplingClassBalancer(y="target",
                                                           shuffle=False),
                         _frame(0.7, 0.2, 0.1)), **BTOL)
    _reg("balance.Oversampling.ratio_1", (BEHAVIOR,),
         observe_balance(lambda: OversamplingClassBalancer(y="target", ratio=1.0,
                                                           shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Oversampling.ratio_half", (BEHAVIOR,),
         observe_balance(lambda: OversamplingClassBalancer(y="target", ratio=0.5,
                                                           shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Oversampling.as_array", (BEHAVIOR,),
         observe_balance(lambda: OversamplingClassBalancer(y="target",
                                                           as_df=False,
                                                           shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Oversampling.object_target", (BEHAVIOR,),
         observe_balance(lambda: OversamplingClassBalancer(y="target",
                                                           shuffle=False),
                         _object_frame), **BTOL)

    # already balanced past the ratio: the early-exit path
    _reg("balance.Oversampling.already_balanced", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: OversamplingClassBalancer(y="target", ratio=0.2,
                                                           shuffle=False),
                         _frame(0.6, 0.4)), **BTOL)
    _reg("balance.Oversampling.already_balanced_shuffled", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: OversamplingClassBalancer(y="target", ratio=0.2,
                                                           shuffle=True),
                         _frame(0.6, 0.4)), **BTOL)
    # the mid class clears the ratio while the rare class does not -- the
    # ``continue`` branch inside the loop
    _reg("balance.Oversampling.partial_need", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: OversamplingClassBalancer(y="target", ratio=0.3,
                                                           shuffle=False),
                         _frame(0.6, 0.35, 0.05)), **BTOL)

    def singleton_frame():
        df = data.class_frame(n=100, ratios=(0.99, 0.01), seed=5)
        return df

    # one observation in the minority class: warns, then oversamples it
    _reg("balance.Oversampling.singleton_class", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: OversamplingClassBalancer(y="target",
                                                           shuffle=False),
                         singleton_frame), **BTOL)

    adversarial("balance.Oversampling.one_class")(
        _bind(lambda: OversamplingClassBalancer(y="target").balance(
            data.class_frame(n=50, ratios=(1.0,)))))
    adversarial("balance.Oversampling.missing_target_col")(
        _bind(lambda: OversamplingClassBalancer(y="nope").balance(
            data.class_frame())))
    adversarial("balance.Oversampling.bad_ratio_zero")(
        _bind(lambda: OversamplingClassBalancer(y="target", ratio=0.0).balance(
            data.class_frame())))
    adversarial("balance.Oversampling.bad_ratio_gt_one")(
        _bind(lambda: OversamplingClassBalancer(y="target", ratio=1.5).balance(
            data.class_frame())))
    adversarial("balance.Oversampling.bad_ratio_int")(
        _bind(lambda: OversamplingClassBalancer(y="target", ratio=1).balance(
            data.class_frame())))
    adversarial("balance.Oversampling.target_not_a_string")(
        _bind(lambda: OversamplingClassBalancer(y=0).balance(
            data.class_frame())))
    adversarial("balance.Oversampling.not_a_frame")(
        _bind(lambda: OversamplingClassBalancer(y="target").balance(
            np.arange(20.0))))

    def too_many_classes():
        r = data.rs(63)
        df = pd.DataFrame(r.randn(120, 2), columns=["a", "b"])
        df["target"] = np.arange(120) % 25
        return df

    adversarial("balance.Oversampling.too_many_classes")(
        _bind(lambda: OversamplingClassBalancer(y="target").balance(
            too_many_classes())))

    # equal class counts: value_counts tie-ordering is a pandas implementation
    # detail, so only the version-independent invariants are contracted.
    _reg("balance.Oversampling.tied_counts", (BEHAVIOR, INVARIANT, ADVERSARIAL),
         observe_balance(lambda: OversamplingClassBalancer(y="target",
                                                           shuffle=False),
                         _frame(0.5, 0.5), invariants_only=True), **BTOL)


_over_cases()


def _under_cases():
    from skutil.preprocessing import UndersamplingClassBalancer

    _reg("balance.Undersampling.binary", (BEHAVIOR,),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Undersampling.shuffled", (BEHAVIOR,),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            shuffle=True),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Undersampling.three_class", (BEHAVIOR,),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            shuffle=False),
                         _frame(0.7, 0.2, 0.1)), **BTOL)
    _reg("balance.Undersampling.ratio_1", (BEHAVIOR,),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            ratio=1.0,
                                                            shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Undersampling.as_array", (BEHAVIOR,),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            as_df=False,
                                                            shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.Undersampling.object_target", (BEHAVIOR,),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            shuffle=False),
                         _object_frame), **BTOL)
    _reg("balance.Undersampling.already_balanced", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            ratio=0.2,
                                                            shuffle=False),
                         _frame(0.6, 0.4)), **BTOL)
    # needs_balancing is True, but the majority already sits under n_required:
    # the second early exit, which returns *sorted* indices even when shuffling
    _reg("balance.Undersampling.exit_on_n_required", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            ratio=0.9,
                                                            shuffle=True),
                         _frame(0.55, 0.3, 0.15)), **BTOL)
    _reg("balance.Undersampling.singleton_class", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            shuffle=False),
                         lambda: data.class_frame(n=100, ratios=(0.99, 0.01),
                                                  seed=5)), **BTOL)

    adversarial("balance.Undersampling.one_class")(
        _bind(lambda: UndersamplingClassBalancer(y="target").balance(
            data.class_frame(n=50, ratios=(1.0,)))))
    adversarial("balance.Undersampling.bad_ratio")(
        _bind(lambda: UndersamplingClassBalancer(y="target", ratio=-0.5).balance(
            data.class_frame())))
    adversarial("balance.Undersampling.empty_target_name")(
        _bind(lambda: UndersamplingClassBalancer(y="").balance(
            data.class_frame())))

    _reg("balance.Undersampling.tied_counts", (BEHAVIOR, INVARIANT, ADVERSARIAL),
         observe_balance(lambda: UndersamplingClassBalancer(y="target",
                                                            shuffle=False),
                         _frame(0.5, 0.5), invariants_only=True), **BTOL)


_under_cases()


def _smote_cases():
    from skutil.preprocessing import SMOTEClassBalancer

    _reg("balance.SMOTE.binary", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.SMOTE.shuffled", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", shuffle=True),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.SMOTE.three_class", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", shuffle=False),
                         _frame(0.7, 0.2, 0.1)), **BTOL)
    _reg("balance.SMOTE.k_5", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", k=5, ratio=0.6,
                                                    shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    # k larger than the number of synthetic parents drawn: NearestNeighbors
    # refuses before any interpolation happens
    _reg("balance.SMOTE.k_exceeds_sample", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: SMOTEClassBalancer(y="target", k=5,
                                                    shuffle=False),
                         _frame(0.85, 0.15)))
    _reg("balance.SMOTE.k_1", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", k=1,
                                                    shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.SMOTE.ratio_1", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", ratio=1.0,
                                                    shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.SMOTE.as_array", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", as_df=False,
                                                    shuffle=False),
                         _frame(0.85, 0.15)), **BTOL)
    _reg("balance.SMOTE.object_target", (BEHAVIOR,),
         observe_balance(lambda: SMOTEClassBalancer(y="target", shuffle=False),
                         _object_frame), **BTOL)
    _reg("balance.SMOTE.already_balanced", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: SMOTEClassBalancer(y="target", ratio=0.2,
                                                    shuffle=False),
                         _frame(0.6, 0.4)), **BTOL)
    _reg("balance.SMOTE.already_balanced_shuffled", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: SMOTEClassBalancer(y="target", ratio=0.2,
                                                    shuffle=True),
                         _frame(0.6, 0.4)), **BTOL)
    _reg("balance.SMOTE.partial_need", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: SMOTEClassBalancer(y="target", ratio=0.3,
                                                    shuffle=False),
                         _frame(0.6, 0.35, 0.05)), **BTOL)
    # n_samples exceeds the minority population, so ``choice`` has to replace
    _reg("balance.SMOTE.oversample_with_replacement", (BEHAVIOR, ADVERSARIAL),
         observe_balance(lambda: SMOTEClassBalancer(y="target", ratio=1.0, k=2,
                                                    shuffle=False),
                         _frame(0.93, 0.07, n=100, seed=29)), **BTOL)

    adversarial("balance.SMOTE.singleton_class")(
        _bind(lambda: SMOTEClassBalancer(y="target").balance(
            data.class_frame(n=100, ratios=(0.99, 0.01), seed=5))))
    def nan_class_frame():
        df = data.class_frame(n=120, ratios=(0.85, 0.15), seed=31)
        df.loc[[3, 11, 40], "a"] = np.nan
        return df

    # ``assert_all_finite=True`` is the one place a balancer screens its input
    adversarial("balance.SMOTE.with_nan")(
        _bind(lambda: SMOTEClassBalancer(y="target").balance(nan_class_frame())))
    adversarial("balance.SMOTE.one_class")(
        _bind(lambda: SMOTEClassBalancer(y="target").balance(
            data.class_frame(n=50, ratios=(1.0,)))))
    adversarial("balance.SMOTE.bad_ratio")(
        _bind(lambda: SMOTEClassBalancer(y="target", ratio="0.5").balance(
            data.class_frame())))

    _reg("balance.SMOTE.tied_counts", (BEHAVIOR, INVARIANT, ADVERSARIAL),
         observe_balance(lambda: SMOTEClassBalancer(y="target", shuffle=False),
                         _frame(0.5, 0.5), invariants_only=True), **BTOL)


_smote_cases()


def _helper_cases():
    from skutil.preprocessing.balance import (_validate_ratio, _validate_target,
                                              _validate_num_classes,
                                              _validate_x_y_ratio, BalancerMixin,
                                              _BaseBalancePartitioner)

    case("balance._validate_ratio.mid", tags=(BEHAVIOR,))(
        _bind(_validate_ratio, 0.5))
    case("balance._validate_ratio.one", tags=(BEHAVIOR,))(
        _bind(_validate_ratio, 1.0))
    adversarial("balance._validate_ratio.zero")(_bind(_validate_ratio, 0.0))
    adversarial("balance._validate_ratio.above_one")(_bind(_validate_ratio, 1.1))
    adversarial("balance._validate_ratio.negative")(_bind(_validate_ratio, -0.1))
    adversarial("balance._validate_ratio.int")(_bind(_validate_ratio, 1))
    adversarial("balance._validate_ratio.string")(_bind(_validate_ratio, "0.5"))
    adversarial("balance._validate_ratio.none")(_bind(_validate_ratio, None))
    adversarial("balance._validate_ratio.numpy_float")(
        _bind(_validate_ratio, np.float64(0.5)))
    adversarial("balance._validate_ratio.numpy_float32")(
        _bind(_validate_ratio, np.float32(0.5)))

    case("balance._validate_target.string", tags=(BEHAVIOR,))(
        _bind(_validate_target, "target"))
    adversarial("balance._validate_target.empty")(_bind(_validate_target, ""))
    adversarial("balance._validate_target.none")(_bind(_validate_target, None))
    adversarial("balance._validate_target.int")(_bind(_validate_target, 3))

    case("balance._validate_num_classes.two", tags=(BEHAVIOR,))(
        _bind(_validate_num_classes, pd.Series([10, 20])))
    adversarial("balance._validate_num_classes.one")(
        _bind(_validate_num_classes, pd.Series([10])))
    adversarial("balance._validate_num_classes.too_many")(
        _bind(_validate_num_classes, pd.Series(np.arange(25))))
    case("balance._validate_num_classes.at_max", tags=(BEHAVIOR,))(
        _bind(_validate_num_classes, pd.Series(np.arange(20))))

    case("balance._validate_x_y_ratio.numeric", tags=(BEHAVIOR,), **BTOL)(
        _bind(_validate_x_y_ratio, data.class_frame(n=200,
                                                    ratios=(0.85, 0.15)),
              "target", 0.2))
    case("balance._validate_x_y_ratio.object", tags=(BEHAVIOR,), **BTOL)(
        lambda: _validate_x_y_ratio(_object_frame(), "target", 0.2))

    # The private `_default_indices` helper is exercised only through the public
    # balancer path (the `shuffle=False` / `shuffle=True` cases above), not called
    # directly here. Its arity is not part of the contract: property 2 requires
    # `random_state` be threaded into the balance path, and both the golden's
    # resolve-in-the-helper design (`_default_indices(length, shuffle,
    # random_state=None)`) and a resolve-at-the-edge one (a required third
    # parameter) are correct. A direct two-argument probe froze one of those two
    # spellings and failed the other on a TypeError while every stated requirement
    # held; the public ordered/shuffled cases cover the same behaviour without
    # pinning a private signature.

    case("balance.BalancerMixin.constants", tags=(BEHAVIOR,))(
        lambda: {"max_classes": BalancerMixin._max_classes,
                 "def_ratio": BalancerMixin._def_ratio})
    adversarial("balance.BalancerMixin.balance_not_implemented")(
        lambda: BalancerMixin().balance(data.class_frame()))
    adversarial("balance._BaseBalancePartitioner.abstract")(
        lambda: _BaseBalancePartitioner(data.class_frame(), "target", 0.2))


_helper_cases()
