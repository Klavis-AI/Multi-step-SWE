"""skutil.preprocessing.encode: the safe label encoder and one-hot encoder."""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data
from ..est import observe


def _reg(cid, tags, fn, **kw):
    case(cid, tags=tags, **kw)(fn)


def _safe_label_cases():
    from skutil.preprocessing import SafeLabelEncoder
    from skutil.preprocessing.encode import _get_unseen, _check_numpy_unicode_bug

    def fit_then(labels, test):
        def run():
            le = SafeLabelEncoder()
            le.fit(labels)
            return {"classes": le.classes_, "encoded": le.transform(test)}
        return run

    letters = np.array(["a", "b", "c", "a", "b"])
    case("encode.SafeLabelEncoder.seen", tags=(BEHAVIOR,))(
        fit_then(letters, np.array(["a", "c"])))
    case("encode.SafeLabelEncoder.unseen", tags=(BEHAVIOR,))(
        fit_then(letters, np.array(["a", "z", "c"])))
    adversarial("encode.SafeLabelEncoder.all_unseen")(
        fit_then(letters, np.array(["x", "y"])))
    adversarial("encode.SafeLabelEncoder.empty_transform")(
        fit_then(letters, np.array([], dtype=object)))
    adversarial("encode.SafeLabelEncoder.numeric_labels")(
        fit_then(np.array([3, 1, 2, 1]), np.array([1, 5])))
    adversarial("encode.SafeLabelEncoder.mixed_labels")(
        fit_then(np.array(["1", "b", "2"], dtype=object),
                 np.array(["1", "q"], dtype=object)))
    adversarial("encode.SafeLabelEncoder.nan_label")(
        fit_then(np.array([1.0, 2.0, np.nan]), np.array([1.0, np.nan, 9.0])))
    adversarial("encode.SafeLabelEncoder.single_class")(
        fit_then(np.array(["a", "a"]), np.array(["a", "b"])))

    def inverse_roundtrip():
        le = SafeLabelEncoder()
        enc = le.fit_transform(letters)
        return {"encoded": enc, "inverse": le.inverse_transform(enc)}

    case("encode.SafeLabelEncoder.inverse_roundtrip", tags=(BEHAVIOR,))(
        inverse_roundtrip)

    case("encode._get_unseen.value", tags=(BEHAVIOR,))(_get_unseen)

    def too_many_levels():
        """``len(classes) >= unseen`` is a documented limit, and the whole of
        its contract lives at the boundary.

        99999 is also the sentinel the encoder returns for a level it did not
        see at fit time, so a transform input with that many distinct levels
        cannot be encoded without a code colliding with the sentinel -- which
        is why the comparison is ``>=`` and not ``>``. One off-by-one there
        turns the error into a silently ambiguous encoding, and nothing below
        the boundary can tell the two spellings apart.
        """
        le = SafeLabelEncoder()
        le.fit(np.arange(4))
        return le.transform(np.arange(_get_unseen()))

    adversarial("encode.SafeLabelEncoder.at_unseen_limit")(too_many_levels)

    def just_under_limit():
        """One level below the limit: the count is what the check reads, and
        the encoder has to still be willing to work here."""
        le = SafeLabelEncoder()
        le.fit(np.arange(4))
        out = le.transform(np.arange(_get_unseen() - 1))
        return {"n": int(out.shape[0]), "head": out[:6],
                "n_sentinel": int((out == _get_unseen()).sum())}

    adversarial("encode.SafeLabelEncoder.just_under_unseen_limit")(
        just_under_limit)
    adversarial("encode._check_numpy_unicode_bug.ascii")(
        lambda: _check_numpy_unicode_bug(np.array(["a", "b"])))
    adversarial("encode._check_numpy_unicode_bug.numeric")(
        lambda: _check_numpy_unicode_bug(np.array([1, 2])))


_safe_label_cases()


def _one_hot_cases():
    from skutil.preprocessing import OneHotCategoricalEncoder

    _reg("encode.OneHot.default", (BEHAVIOR,),
         observe(lambda: OneHotCategoricalEncoder(),
                 lambda: data.category_frame()))
    _reg("encode.OneHot.custom_fill", (BEHAVIOR,),
         observe(lambda: OneHotCategoricalEncoder(fill="UNK"),
                 lambda: data.category_frame()))
    _reg("encode.OneHot.as_array", (BEHAVIOR,),
         observe(lambda: OneHotCategoricalEncoder(as_df=False),
                 lambda: data.category_frame()))
    _reg("encode.OneHot.mixed_frame", (BEHAVIOR,),
         observe(lambda: OneHotCategoricalEncoder(),
                 lambda: data.mixed_frame()))
    _reg("encode.OneHot.refit_transform", (BEHAVIOR,),
         observe(lambda: OneHotCategoricalEncoder(),
                 lambda: data.category_frame(), call="fit_then_transform"))
    _reg("encode.OneHot.no_object_cols", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: OneHotCategoricalEncoder(),
                 lambda: data.num_frame(20, 3)))
    _reg("encode.OneHot.all_object_cols", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: OneHotCategoricalEncoder(),
                 lambda: pd.DataFrame({"a": list("xyzxy"),
                                       "b": list("pqpqp")})))

    def missing_frame():
        df = data.category_frame(40)
        df.loc[[0, 5, 9], "a"] = np.nan
        return df

    _reg("encode.OneHot.with_missing", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: OneHotCategoricalEncoder(), missing_frame))
    _reg("encode.OneHot.numeric_fill", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: OneHotCategoricalEncoder(fill=-1), missing_frame))
    _reg("encode.OneHot.single_level", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: OneHotCategoricalEncoder(),
                 lambda: pd.DataFrame({"a": ["only"] * 10,
                                       "b": np.arange(10.0)})))
    _reg("encode.OneHot.high_cardinality", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: OneHotCategoricalEncoder(),
                 lambda: pd.DataFrame({"a": ["v%d" % i for i in range(50)],
                                       "b": np.arange(50.0)})))

    def unseen_at_transform():
        enc = OneHotCategoricalEncoder()
        enc.fit(pd.DataFrame({"a": ["x", "y", "x", "y"]}))
        return enc.transform(pd.DataFrame({"a": ["x", "z", "y", "x"]}))

    adversarial("encode.OneHot.unseen_level_at_transform")(unseen_at_transform)


_one_hot_cases()
