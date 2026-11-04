"""skutil.preprocessing.transform: the selective transformers."""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data
from ..est import observe

# Box-Cox and Yeo-Johnson estimate their lambdas by numerical optimization, so
# the last couple of digits are the optimizer's, not the library's.
OTOL = dict(rtol=1e-6, atol=1e-9)


def _reg(cid, tags, fn, **kw):
    case(cid, tags=tags, **kw)(fn)


# --------------------------------------------------------------------------
# FunctionMapper

def _function_mapper_cases():
    from skutil.preprocessing import FunctionMapper

    def doubler(x):
        return x * 2

    def add_kw(x, amount=0):
        return x + amount

    _reg("transform.FunctionMapper.all_cols", (BEHAVIOR,),
         observe(lambda: FunctionMapper(fun=doubler),
                 lambda: data.num_frame(20, 4)))
    _reg("transform.FunctionMapper.subset", (BEHAVIOR,),
         observe(lambda: FunctionMapper(cols=["a", "c"], fun=doubler),
                 lambda: data.num_frame(20, 4)))
    _reg("transform.FunctionMapper.kwargs", (BEHAVIOR,),
         observe(lambda: FunctionMapper(fun=add_kw, amount=3.5),
                 lambda: data.num_frame(20, 4)))
    # FunctionMapper has no as_df parameter; **kwargs forwards it to ``fun``.
    _reg("transform.FunctionMapper.kwargs_collision", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FunctionMapper(fun=doubler, as_df=False),
                 lambda: data.num_frame(20, 4)))
    _reg("transform.FunctionMapper.no_fun", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FunctionMapper(), lambda: data.num_frame(6, 3)))
    _reg("transform.FunctionMapper.not_callable", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FunctionMapper(fun="nope"),
                 lambda: data.num_frame(6, 3)))
    _reg("transform.FunctionMapper.missing_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FunctionMapper(cols=["zzz"], fun=doubler),
                 lambda: data.num_frame(6, 3)))
    _reg("transform.FunctionMapper.on_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FunctionMapper(fun=doubler), lambda: data.nan_frame()))
    _reg("transform.FunctionMapper.on_object", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: FunctionMapper(fun=doubler), lambda: data.mixed_frame()))


_function_mapper_cases()


# --------------------------------------------------------------------------
# InteractionTermTransformer

def _interaction_cases():
    from skutil.preprocessing import InteractionTermTransformer

    def custom(a, b):
        return a - b

    _reg("transform.Interaction.default", (BEHAVIOR,),
         observe(lambda: InteractionTermTransformer(),
                 lambda: data.num_frame(20, 4)))
    _reg("transform.Interaction.subset", (BEHAVIOR,),
         observe(lambda: InteractionTermTransformer(cols=["a", "b", "c"]),
                 lambda: data.num_frame(20, 5)))
    _reg("transform.Interaction.only_interactions", (BEHAVIOR,),
         observe(lambda: InteractionTermTransformer(only_return_interactions=True),
                 lambda: data.num_frame(20, 4)))
    _reg("transform.Interaction.custom_function", (BEHAVIOR,),
         observe(lambda: InteractionTermTransformer(interaction_function=custom),
                 lambda: data.num_frame(20, 3)))
    _reg("transform.Interaction.suffix", (BEHAVIOR,),
         observe(lambda: InteractionTermTransformer(name_suffix="X"),
                 lambda: data.num_frame(10, 3)))
    _reg("transform.Interaction.as_array", (BEHAVIOR,),
         observe(lambda: InteractionTermTransformer(as_df=False),
                 lambda: data.num_frame(10, 3)))
    _reg("transform.Interaction.one_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: InteractionTermTransformer(),
                 lambda: data.num_frame(10, 1)))
    _reg("transform.Interaction.not_callable", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: InteractionTermTransformer(interaction_function=3),
                 lambda: data.num_frame(10, 3)))
    _reg("transform.Interaction.with_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: InteractionTermTransformer(), lambda: data.nan_frame()))
    _reg("transform.Interaction.int_frame", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: InteractionTermTransformer(), lambda: data.int_frame()))
    _reg("transform.Interaction.collinear", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: InteractionTermTransformer(),
                 lambda: data.collinear_frame()))


_interaction_cases()


# --------------------------------------------------------------------------
# SelectiveScaler

def _scaler_cases():
    from skutil.preprocessing import SelectiveScaler
    from sklearn.preprocessing import (StandardScaler, MinMaxScaler,
                                       RobustScaler, MaxAbsScaler)

    for label, mk in (("standard", StandardScaler), ("minmax", MinMaxScaler),
                      ("robust", RobustScaler), ("maxabs", MaxAbsScaler)):
        _reg("transform.SelectiveScaler.%s" % label, (BEHAVIOR,),
             observe(lambda m=mk: SelectiveScaler(scaler=m()),
                     lambda: data.num_frame(40, 5)))

    _reg("transform.SelectiveScaler.subset", (BEHAVIOR,),
         observe(lambda: SelectiveScaler(cols=["a", "b"]),
                 lambda: data.num_frame(40, 5)))
    _reg("transform.SelectiveScaler.default_scaler_shared", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveScaler(), lambda: data.num_frame(40, 5)))
    _reg("transform.SelectiveScaler.as_array", (BEHAVIOR,),
         observe(lambda: SelectiveScaler(as_df=False),
                 lambda: data.num_frame(20, 3)))
    _reg("transform.SelectiveScaler.constant_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveScaler(),
                 lambda: data.near_zero_variance_frame()))
    _reg("transform.SelectiveScaler.with_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveScaler(), lambda: data.nan_frame()))
    _reg("transform.SelectiveScaler.mixed_types", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveScaler(cols=["a", "b"]),
                 lambda: data.mixed_frame()))
    _reg("transform.SelectiveScaler.mixed_scale", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SelectiveScaler(),
                 lambda: pd.DataFrame(data.matrix("mixed_scale"),
                                      columns=data.cols(4))))
    _reg("transform.SelectiveScaler.refit_transform", (BEHAVIOR,),
         observe(lambda: SelectiveScaler(), lambda: data.num_frame(40, 5),
                 call="fit_then_transform"))


_scaler_cases()


# --------------------------------------------------------------------------
# BoxCoxTransformer / YeoJohnsonTransformer

def _power_cases():
    from skutil.preprocessing import BoxCoxTransformer, YeoJohnsonTransformer

    _reg("transform.BoxCox.positive", (BEHAVIOR,),
         observe(lambda: BoxCoxTransformer(), lambda: data.positive_frame()),
         **OTOL)
    _reg("transform.BoxCox.subset", (BEHAVIOR,),
         observe(lambda: BoxCoxTransformer(cols=["a", "b"]),
                 lambda: data.positive_frame()), **OTOL)
    _reg("transform.BoxCox.shift_amt", (BEHAVIOR,),
         observe(lambda: BoxCoxTransformer(shift_amt=1e-3),
                 lambda: data.positive_frame()), **OTOL)
    _reg("transform.BoxCox.as_array", (BEHAVIOR,),
         observe(lambda: BoxCoxTransformer(as_df=False),
                 lambda: data.positive_frame(30, 3)), **OTOL)
    _reg("transform.BoxCox.n_jobs_2", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: BoxCoxTransformer(n_jobs=2),
                 lambda: data.positive_frame(30, 3)), **OTOL)
    _reg("transform.BoxCox.with_negatives", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: BoxCoxTransformer(), lambda: data.num_frame(30, 3)),
         **OTOL)
    _reg("transform.BoxCox.with_zeros", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: BoxCoxTransformer(),
                 lambda: pd.DataFrame(np.zeros((20, 3)), columns=data.cols(3))),
         **OTOL)
    _reg("transform.BoxCox.single_row", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: BoxCoxTransformer(), lambda: data.positive_frame(1, 3)))
    _reg("transform.BoxCox.constant_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: BoxCoxTransformer(),
                 lambda: pd.DataFrame({"a": np.ones(30), "b": np.arange(1.0, 31.0)})),
         **OTOL)
    _reg("transform.BoxCox.refit_transform", (BEHAVIOR,),
         observe(lambda: BoxCoxTransformer(), lambda: data.positive_frame(),
                 call="fit_then_transform"), **OTOL)

    _reg("transform.YeoJohnson.signed", (BEHAVIOR,),
         observe(lambda: YeoJohnsonTransformer(), lambda: data.num_frame(40, 4)),
         **OTOL)
    _reg("transform.YeoJohnson.positive", (BEHAVIOR,),
         observe(lambda: YeoJohnsonTransformer(), lambda: data.positive_frame()),
         **OTOL)
    _reg("transform.YeoJohnson.subset", (BEHAVIOR,),
         observe(lambda: YeoJohnsonTransformer(cols=["a", "c"]),
                 lambda: data.num_frame(40, 4)), **OTOL)
    _reg("transform.YeoJohnson.as_array", (BEHAVIOR,),
         observe(lambda: YeoJohnsonTransformer(as_df=False),
                 lambda: data.num_frame(30, 3)), **OTOL)
    _reg("transform.YeoJohnson.n_jobs_2", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: YeoJohnsonTransformer(n_jobs=2),
                 lambda: data.num_frame(30, 3)), **OTOL)
    _reg("transform.YeoJohnson.with_zeros", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: YeoJohnsonTransformer(),
                 lambda: pd.DataFrame(np.zeros((20, 3)), columns=data.cols(3))),
         **OTOL)
    _reg("transform.YeoJohnson.single_row", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: YeoJohnsonTransformer(),
                 lambda: data.num_frame(1, 3)))
    _reg("transform.YeoJohnson.huge_scale", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: YeoJohnsonTransformer(),
                 lambda: data.num_frame(30, 3, scale=1e6)), **OTOL)
    _reg("transform.YeoJohnson.refit_transform", (BEHAVIOR,),
         observe(lambda: YeoJohnsonTransformer(), lambda: data.num_frame(40, 4),
                 call="fit_then_transform"), **OTOL)


_power_cases()


# --------------------------------------------------------------------------
# SpatialSignTransformer

def _spatial_sign_cases():
    from skutil.preprocessing import SpatialSignTransformer

    _reg("transform.SpatialSign.default", (BEHAVIOR,),
         observe(lambda: SpatialSignTransformer(), lambda: data.num_frame(40, 4)))
    _reg("transform.SpatialSign.subset", (BEHAVIOR,),
         observe(lambda: SpatialSignTransformer(cols=["a", "b"]),
                 lambda: data.num_frame(40, 4)))
    _reg("transform.SpatialSign.as_array", (BEHAVIOR,),
         observe(lambda: SpatialSignTransformer(as_df=False),
                 lambda: data.num_frame(20, 3)))
    _reg("transform.SpatialSign.n_jobs_2", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SpatialSignTransformer(n_jobs=2),
                 lambda: data.num_frame(20, 3)))
    _reg("transform.SpatialSign.all_zero_col", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SpatialSignTransformer(),
                 lambda: pd.DataFrame({"a": np.zeros(20),
                                       "b": np.arange(20.0)})))
    _reg("transform.SpatialSign.with_nan", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SpatialSignTransformer(), lambda: data.nan_frame()))
    _reg("transform.SpatialSign.tiny_values", (BEHAVIOR, ADVERSARIAL),
         observe(lambda: SpatialSignTransformer(),
                 lambda: data.num_frame(20, 3, scale=1e-160)))
    _reg("transform.SpatialSign.refit_transform", (BEHAVIOR,),
         observe(lambda: SpatialSignTransformer(),
                 lambda: data.num_frame(40, 4), call="fit_then_transform"))


_spatial_sign_cases()


# --------------------------------------------------------------------------
# module-private helpers that the public paths cannot reach on their own

def _helper_cases():
    from skutil.preprocessing.transform import (_eqls, _validate_rows, _mul,
                                                _transform_y, _yj_trans_single_x,
                                                _sq_norm_single)

    case("transform._eqls.within_eps", tags=(BEHAVIOR,))(
        lambda: _eqls(1.0, 1.0 + 1e-14))
    case("transform._eqls.outside_eps", tags=(BEHAVIOR,))(
        lambda: _eqls(1.0, 1.5))
    adversarial("transform._eqls.nan")(lambda: _eqls(np.nan, 1.0))
    adversarial("transform._validate_rows.one_row")(
        lambda: _validate_rows(data.num_frame(1, 3)))
    case("transform._validate_rows.two_rows", tags=(BEHAVIOR,))(
        lambda: _validate_rows(data.num_frame(2, 3)))
    case("transform._mul.pair", tags=(BEHAVIOR,))(
        lambda: _mul(pd.Series(np.arange(5.0)), pd.Series(np.arange(5.0) + 1)))
    adversarial("transform._mul.misaligned_index")(
        lambda: _mul(pd.Series([1.0, 2.0], index=[0, 1]),
                     pd.Series([3.0, 4.0], index=[1, 2])))
    adversarial("transform._transform_y.lambda_zero")(
        lambda: _transform_y(np.arange(1.0, 6.0), 0.0))
    adversarial("transform._transform_y.lambda_two")(
        lambda: _transform_y(np.arange(1.0, 6.0), 2.0))
    adversarial("transform._yj_trans_single_x.negative")(
        lambda: _yj_trans_single_x(-2.0, 0.5))
    adversarial("transform._yj_trans_single_x.zero_lambda")(
        lambda: _yj_trans_single_x(3.0, 0.0))
    adversarial("transform._yj_trans_single_x.lambda_two")(
        lambda: _yj_trans_single_x(-3.0, 2.0))
    adversarial("transform._sq_norm_single.zero_vector")(
        lambda: _sq_norm_single(np.zeros(4)))
    case("transform._sq_norm_single.unit", tags=(BEHAVIOR,))(
        lambda: _sq_norm_single(np.array([3.0, 4.0])))


_helper_cases()
