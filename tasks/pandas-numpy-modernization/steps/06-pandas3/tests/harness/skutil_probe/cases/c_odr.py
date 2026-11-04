"""skutil.odr: the LINPACK QR decomposition and its wrapper.

``qr_decomposition`` calls into the Fortran ``dqrsl`` extension, so these cases
double as the behavioral check on the f2py build.
"""
from __future__ import print_function, absolute_import, division

import numpy as np

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data

QTOL = dict(rtol=1e-9, atol=1e-12)

HAPPY = ("square", "tall", "identity")
HOSTILE = ("wide", "rank_deficient", "singular", "zeros", "ones",
           "single_col", "single_row", "huge", "tiny", "mixed_scale",
           "with_nan", "with_inf", "int", "empty_rows")


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _decomposition_cases():
    from skutil.odr import qr_decomposition, QRDecomposition

    for kind in HAPPY + HOSTILE:
        tags = (BEHAVIOR,) if kind in HAPPY else (BEHAVIOR, ADVERSARIAL)
        case("odr.qr_decomposition.%s" % kind, tags=tags, **QTOL)(
            _bind(lambda k=kind: qr_decomposition(data.matrix(k))))
        case("odr.qr_decomposition.nopivot.%s" % kind, tags=tags, **QTOL)(
            _bind(lambda k=kind: qr_decomposition(data.matrix(k), job=0)))
        case("odr.QRDecomposition.R.%s" % kind, tags=tags, **QTOL)(
            _bind(lambda k=kind: QRDecomposition(data.matrix(k)).get_R()))
        case("odr.QRDecomposition.rank.%s" % kind, tags=tags)(
            _bind(lambda k=kind: (QRDecomposition(data.matrix(k)).get_rank(),
                                  QRDecomposition(data.matrix(k)).get_R_rank())))

    case("odr.QRDecomposition.frame_input", tags=(BEHAVIOR,), **QTOL)(
        _bind(lambda: QRDecomposition(data.num_frame(20, 4)).get_R()))
    adversarial("odr.QRDecomposition.list_input", **QTOL)(
        _bind(lambda: QRDecomposition([[1.0, 2.0], [3.0, 4.0]]).get_R()))
    adversarial("odr.QRDecomposition.params")(
        _bind(lambda: QRDecomposition(data.matrix("tall")).get_params()))
    adversarial("odr.QRDecomposition.pivot_flag_zero", **QTOL)(
        _bind(lambda: QRDecomposition(data.matrix("rank_deficient"),
                                      pivot=0).get_R()))


_decomposition_cases()


def _coef_cases():
    """``get_coef`` is the one path that allocates with ``np.int``."""
    from skutil.odr import QRDecomposition

    r = data.rs(59)
    X = r.randn(20, 4)
    y1 = r.randn(20, 1)
    y3 = r.randn(20, 3)

    case("odr.get_coef.single_target", tags=(BEHAVIOR,), **QTOL)(
        _bind(lambda: QRDecomposition(X).get_coef(y1)))
    case("odr.get_coef.multi_target", tags=(BEHAVIOR,), **QTOL)(
        _bind(lambda: QRDecomposition(X).get_coef(y3)))
    adversarial("odr.get_coef.rank_deficient", **QTOL)(
        _bind(lambda: QRDecomposition(data.matrix("rank_deficient"))
              .get_coef(data.rs(3).randn(10, 1))))
    adversarial("odr.get_coef.row_mismatch")(
        _bind(lambda: QRDecomposition(X).get_coef(data.rs(3).randn(19, 1))))
    adversarial("odr.get_coef.frame_target", **QTOL)(
        _bind(lambda: QRDecomposition(X).get_coef(data.num_frame(20, 2))))
    adversarial("odr.get_coef.nan_target")(
        _bind(lambda: QRDecomposition(X).get_coef(
            np.full((20, 1), np.nan))))
    adversarial("odr.get_coef.int_target", **QTOL)(
        _bind(lambda: QRDecomposition(X).get_coef(
            data.rs(3).randint(0, 5, (20, 1)))))


_coef_cases()


def _guard_cases():
    from skutil.odr.dqrutl import _validate_matrix_size, _qr_R

    case("odr._validate_matrix_size.ok", tags=(BEHAVIOR,))(
        _bind(_validate_matrix_size, 1000, 1000))
    adversarial("odr._validate_matrix_size.too_big")(
        _bind(_validate_matrix_size, 100000, 100000))
    adversarial("odr._validate_matrix_size.boundary")(
        _bind(_validate_matrix_size, 2147483647, 1))
    adversarial("odr._qr_R.wide")(
        _bind(_qr_R, data.matrix("wide")))


_guard_cases()
