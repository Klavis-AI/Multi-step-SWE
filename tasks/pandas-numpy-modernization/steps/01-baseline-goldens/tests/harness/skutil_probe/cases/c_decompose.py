"""skutil.decomposition: the selective decomposers."""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data
from ..est import observe

# SVD sign is basis-dependent; sklearn fixes it with svd_flip, but the last
# digits still belong to LAPACK.
DTOL = dict(rtol=1e-8, atol=1e-11)


def _reg(cid, tags, fn, **kw):
    case(cid, tags=tags, **kw)(fn)


def _pca_cases():
    from skutil.decomposition import SelectivePCA

    for n in (None, 1, 2, 3, 0.95):
        lbl = str(n).replace(".", "_")
        tags = (BEHAVIOR,) if n in (None, 2) else (BEHAVIOR, ADVERSARIAL)
        _reg("decompose.SelectivePCA.n_%s" % lbl, tags,
             observe(lambda k=n: SelectivePCA(n_components=k),
                     lambda: data.num_frame(60, 5)), **DTOL)

    _reg("decompose.SelectivePCA.whiten", (BEHAVIOR,),
         observe(lambda: SelectivePCA(whiten=True),
                 lambda: data.num_frame(60, 5)), **DTOL)
    _reg("decompose.SelectivePCA.weight", (BEHAVIOR,),
         observe(lambda: SelectivePCA(weight=True),
                 lambda: data.num_frame(60, 5)), **DTOL)
    _reg("decompose.SelectivePCA.weight_and_whiten", (BEHAVIOR,),
         observe(lambda: SelectivePCA(weight=True, whiten=True),
                 lambda: data.num_frame(60, 5)), **DTOL)
    _reg("decompose.SelectivePCA.subset", (BEHAVIOR,),
         observe(lambda: SelectivePCA(cols=["a", "b", "c"]),
                 lambda: data.num_frame(60, 5)), **DTOL)
    _reg("decompose.SelectivePCA.as_array", (BEHAVIOR,),
         observe(lambda: SelectivePCA(as_df=False),
                 lambda: data.num_frame(40, 4)), **DTOL)
    _reg("decompose.SelectivePCA.collinear", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectivePCA(), lambda: data.collinear_frame()), **DTOL)
    _reg("decompose.SelectivePCA.wide", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectivePCA(), lambda: data.num_frame(4, 12)), **DTOL)
    _reg("decompose.SelectivePCA.single_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectivePCA(), lambda: data.num_frame(30, 1)), **DTOL)
    _reg("decompose.SelectivePCA.with_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectivePCA(), lambda: data.nan_frame()))
    _reg("decompose.SelectivePCA.too_many_components", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectivePCA(n_components=20),
                 lambda: data.num_frame(30, 4)))
    _reg("decompose.SelectivePCA.refit_transform", (BEHAVIOR,),
         observe(lambda: SelectivePCA(n_components=3),
                 lambda: data.num_frame(60, 5), call="fit_then_transform"),
         **DTOL)

    def roundtrip():
        X = data.num_frame(60, 5)
        pca = SelectivePCA(n_components=3).fit(X)
        Xt = pca.transform(X)
        return {"score": pca.score(X),
                "inverse": pca.inverse_transform(Xt),
                "decomposition": type(pca.get_decomposition()).__name__}

    case("decompose.SelectivePCA.roundtrip", tags=(BEHAVIOR,), **DTOL)(roundtrip)

    def unfitted_decomposition():
        return SelectivePCA().get_decomposition()

    adversarial("decompose.SelectivePCA.unfitted_decomposition")(
        unfitted_decomposition)


_pca_cases()


def _svd_cases():
    from skutil.decomposition import SelectiveTruncatedSVD

    for n in (1, 2, 4):
        _reg("decompose.SelectiveTruncatedSVD.n_%d" % n, (BEHAVIOR,),
             observe(lambda k=n: SelectiveTruncatedSVD(
                 n_components=k, algorithm="arpack"),
                 lambda: data.num_frame(60, 5)), **DTOL)

    _reg("decompose.SelectiveTruncatedSVD.subset", (BEHAVIOR,),
         observe(lambda: SelectiveTruncatedSVD(cols=["a", "b", "c"],
                                               algorithm="arpack"),
                 lambda: data.num_frame(60, 5)), **DTOL)
    _reg("decompose.SelectiveTruncatedSVD.as_array", (BEHAVIOR,),
         observe(lambda: SelectiveTruncatedSVD(as_df=False, algorithm="arpack"),
                 lambda: data.num_frame(40, 4)), **DTOL)
    _reg("decompose.SelectiveTruncatedSVD.collinear", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveTruncatedSVD(algorithm="arpack"),
                 lambda: data.collinear_frame()), **DTOL)
    _reg("decompose.SelectiveTruncatedSVD.single_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveTruncatedSVD(algorithm="arpack"),
                 lambda: data.num_frame(30, 1)))
    _reg("decompose.SelectiveTruncatedSVD.too_many_components",
         (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveTruncatedSVD(n_components=9,
                                               algorithm="arpack"),
                 lambda: data.num_frame(30, 4)))
    _reg("decompose.SelectiveTruncatedSVD.with_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveTruncatedSVD(algorithm="arpack"),
                 lambda: data.nan_frame()))
    _reg("decompose.SelectiveTruncatedSVD.refit_transform", (BEHAVIOR,),
         observe(lambda: SelectiveTruncatedSVD(n_components=3,
                                               algorithm="arpack"),
                 lambda: data.num_frame(60, 5), call="fit_then_transform"),
         **DTOL)

    def roundtrip():
        X = data.num_frame(60, 5)
        svd = SelectiveTruncatedSVD(n_components=3, algorithm="arpack").fit(X)
        Xt = svd.transform(X)
        return {"inverse": svd.inverse_transform(Xt),
                "decomposition": type(svd.get_decomposition()).__name__}

    case("decompose.SelectiveTruncatedSVD.roundtrip", tags=(BEHAVIOR,), **DTOL)(
        roundtrip)


_svd_cases()
