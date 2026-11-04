"""skutil.metrics.kernel: the eleven kernel functions.

Every kernel routes through the compiled ``_kernel_fast`` extension, so these
cases are also the behavioral check on the Cython build.
"""
from __future__ import print_function, absolute_import, division

import numpy as np

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data

KERNELS = (
    ("exponential_kernel", {}),
    ("gaussian_kernel", {}),
    ("inverse_multiquadric_kernel", {}),
    ("laplace_kernel", {}),
    ("linear_kernel", {}),
    ("multiquadric_kernel", {}),
    ("polynomial_kernel", {}),
    ("power_kernel", {}),
    ("rbf_kernel", {}),
    ("tanh_kernel", {}),
)

# The kernels accumulate over the feature axis in Cython, so they carry a
# slightly looser relative tolerance than the default: a BLAS or compiler
# change can reorder the summation without changing the mathematics.
KTOL = dict(rtol=1e-9, atol=1e-12)


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _kernel_cases():
    import skutil.metrics as M

    happy = ("square", "tall", "wide")
    hostile = ("rank_deficient", "singular", "zeros", "ones", "single_col",
               "single_row", "identity", "huge", "tiny", "mixed_scale",
               "with_nan", "with_inf", "int", "empty_rows")

    for name, _ in KERNELS:
        fn = getattr(M, name)
        for kind in happy:
            case("kernel.%s.%s" % (name, kind), tags=(BEHAVIOR,), **KTOL)(
                _bind(lambda f=fn, k=kind: f(data.matrix(k))))
        for kind in hostile:
            case("kernel.%s.%s" % (name, kind),
                 tags=(BEHAVIOR, ADVERSARIAL), **KTOL)(
                _bind(lambda f=fn, k=kind: f(data.matrix(k))))

        # X against a separate Y of compatible width
        case("kernel.%s.xy" % name, tags=(BEHAVIOR,), **KTOL)(
            _bind(lambda f=fn: f(data.matrix("tall"), data.rs(5).randn(7, 4))))
        adversarial("kernel.%s.xy_mismatched" % name, **KTOL)(
            _bind(lambda f=fn: f(data.matrix("tall"), data.rs(5).randn(7, 9))))
        adversarial("kernel.%s.dataframe_input" % name, **KTOL)(
            _bind(lambda f=fn: f(data.num_frame(12, 4))))
        adversarial("kernel.%s.list_input" % name, **KTOL)(
            _bind(lambda f=fn: f([[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]])))
        adversarial("kernel.%s.one_dim" % name, **KTOL)(
            _bind(lambda f=fn: f(np.arange(6.0))))


_kernel_cases()


def _spline_cases():
    """``spline_kernel`` is recorded on square inputs only, and here is why.

    ``_prep_X_Y_for_cython`` hands the Cython routine a *transposed* ``Y`` of
    shape ``(n_features, n_samples_Y)``, and ``_spline_kernel_fast`` then reads
    ``Y[j, k]`` with ``j`` running over ``n_samples_Y`` and ``k`` over
    ``n_features`` -- the two subscripts the wrong way round. Every other
    kernel in the module reads ``Y[k, j]``. The module is compiled with
    ``boundscheck=False`` and ``wraparound=False``, so for any input where
    ``n_samples_Y != n_features`` the routine reads past the end of the buffer
    instead of raising: the observed output is whatever happened to be in
    memory, and a same-machine double recording of a tall matrix disagrees with
    itself. Out-of-bounds reads are not behavior, so they are not recorded --
    only the square shapes, where every subscript is in range, are.
    """
    import skutil.metrics as M

    r = data.rs(53)
    squares = {
        "square": data.matrix("square"),
        "identity": data.matrix("identity"),
        "zeros": np.zeros((5, 5)),
        "ones": np.ones((5, 5)),
        "int": r.randint(-5, 5, size=(5, 5)),
        "huge": r.randn(5, 5) * 1e150,
        "tiny": r.randn(5, 5) * 1e-150,
        "single_cell": r.randn(1, 1),
        "empty_rows": np.zeros((0, 0)),
    }
    hostile = ("zeros", "ones", "int", "huge", "tiny", "single_cell",
               "empty_rows")

    for kind in sorted(squares):
        tags = (BEHAVIOR, ADVERSARIAL) if kind in hostile else (BEHAVIOR,)
        case("kernel.spline_kernel.%s" % kind, tags=tags, **KTOL)(
            _bind(lambda m=squares[kind]: M.spline_kernel(m)))

    nan = data.matrix("square").copy()
    nan[2, 1] = np.nan
    case("kernel.spline_kernel.with_nan", tags=(BEHAVIOR, ADVERSARIAL), **KTOL)(
        _bind(lambda m=nan: M.spline_kernel(m)))

    inf = data.matrix("square").copy()
    inf[1, 0] = np.inf
    inf[3, 2] = -np.inf
    case("kernel.spline_kernel.with_inf", tags=(BEHAVIOR, ADVERSARIAL), **KTOL)(
        _bind(lambda m=inf: M.spline_kernel(m)))

    # X is (6, 6) and Y is (6, 6): n_samples_Y equals n_features, so the
    # transposed subscripts stay in range
    case("kernel.spline_kernel.xy", tags=(BEHAVIOR,), **KTOL)(
        _bind(lambda: M.spline_kernel(data.matrix("square"),
                                      data.rs(59).randn(6, 6))))
    # both of these raise in ``check_pairwise_arrays``, before any indexing
    adversarial("kernel.spline_kernel.xy_mismatched", **KTOL)(
        _bind(lambda: M.spline_kernel(data.matrix("square"),
                                      data.rs(59).randn(6, 9))))
    adversarial("kernel.spline_kernel.one_dim", **KTOL)(
        _bind(lambda: M.spline_kernel(np.arange(6.0))))
    adversarial("kernel.spline_kernel.dataframe_input", **KTOL)(
        _bind(lambda: M.spline_kernel(data.num_frame(6, 6))))


_spline_cases()


def _kernel_param_cases():
    """The parameterized kernels, swept over their own arguments."""
    import skutil.metrics as M

    X = data.matrix("tall")

    for sigma in (0.25, 1.0, 10.0, 0.0, -1.0, 1e-12, 1e12):
        lbl = repr(sigma).replace(".", "_").replace("-", "neg")
        tags = (BEHAVIOR,) if sigma in (0.25, 1.0, 10.0) \
            else (BEHAVIOR, ADVERSARIAL)
        for name in ("exponential_kernel", "gaussian_kernel", "laplace_kernel",
                     "rbf_kernel"):
            fn = getattr(M, name)
            case("kernel.%s.sigma_%s" % (name, lbl), tags=tags, **KTOL)(
                _bind(lambda f=fn, s=sigma: f(X, sigma=s)))

    for constant in (0.0, 1.0, -3.5, 1e9):
        lbl = repr(constant).replace(".", "_").replace("-", "neg")
        tags = (BEHAVIOR,) if constant in (0.0, 1.0) else (BEHAVIOR, ADVERSARIAL)
        for name in ("inverse_multiquadric_kernel", "linear_kernel",
                     "multiquadric_kernel"):
            fn = getattr(M, name)
            case("kernel.%s.constant_%s" % (name, lbl), tags=tags, **KTOL)(
                _bind(lambda f=fn, c=constant: f(X, constant=c)))

    for degree in (1.0, 2.0, 3.0, 0.0, -1.0, 0.5):
        lbl = repr(degree).replace(".", "_").replace("-", "neg")
        tags = (BEHAVIOR,) if degree in (1.0, 2.0) else (BEHAVIOR, ADVERSARIAL)
        case("kernel.power_kernel.degree_%s" % lbl, tags=tags, **KTOL)(
            _bind(lambda d=degree: M.power_kernel(X, degree=d)))
        case("kernel.polynomial_kernel.degree_%s" % lbl, tags=tags, **KTOL)(
            _bind(lambda d=degree: M.polynomial_kernel(X, degree=d)))

    for alpha in (1.0, 0.0, -2.0, 1e6):
        lbl = repr(alpha).replace(".", "_").replace("-", "neg")
        tags = (BEHAVIOR,) if alpha == 1.0 else (BEHAVIOR, ADVERSARIAL)
        case("kernel.tanh_kernel.alpha_%s" % lbl, tags=tags, **KTOL)(
            _bind(lambda a=alpha: M.tanh_kernel(X, alpha=a)))
        case("kernel.polynomial_kernel.alpha_%s" % lbl, tags=tags, **KTOL)(
            _bind(lambda a=alpha: M.polynomial_kernel(X, alpha=a)))


_kernel_param_cases()


def _pairwise_reexport_cases():
    """skutil.metrics re-exports sklearn's validators; they are public here."""
    from skutil.metrics import check_array, check_X_y

    case("metrics.check_array.frame", tags=(BEHAVIOR,))(
        _bind(lambda: check_array(data.num_frame(6, 3))))
    adversarial("metrics.check_array.with_nan")(
        _bind(lambda: check_array(data.matrix("with_nan"))))
    adversarial("metrics.check_array.one_dim")(
        _bind(lambda: check_array(np.arange(4.0))))
    case("metrics.check_X_y.pair", tags=(BEHAVIOR,))(
        _bind(lambda: check_X_y(data.matrix("tall"), np.arange(20))))
    adversarial("metrics.check_X_y.length_mismatch")(
        _bind(lambda: check_X_y(data.matrix("tall"), np.arange(19))))


_pairwise_reexport_cases()
