"""The two charting entry points: ``corr_plot`` and ``report_grid_score_detail``.

Pixels are not the contract -- matplotlib restyles its defaults every major
release and seaborn restyles on top of that. What *is* the contract is the data
that reaches the artists: the correlation matrix and its upper-triangle mask,
the scatter offsets, the axis labels and titles, and the result frame that
``report_grid_score_detail`` hands back. ``fig_digest`` below reads exactly
those off the figure and nothing that a theme could move.

Both functions render through the Agg backend (the runner pins ``MPLBACKEND``),
so ``show()`` is a no-op and nothing tries to reach a display.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL, FIGURE
from .. import data

PTOL = dict(rtol=1e-9, atol=1e-12)


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _arr(a):
    """Flatten an artist's data array, masked cells becoming NaN."""
    if a is None:
        return None
    a = np.ma.filled(np.ma.asarray(a, dtype=np.float64), np.nan)
    return [float(v) for v in np.asarray(a).ravel()]


def fig_digest():
    """Describe every open figure by the data its artists carry."""
    import matplotlib.pyplot as plt

    figs = []
    for num in plt.get_fignums():
        fig = plt.figure(num)
        axes = []
        for ax in fig.axes:
            axes.append({
                "title": ax.get_title(),
                "xlabel": ax.get_xlabel(),
                "ylabel": ax.get_ylabel(),
                "n_collections": len(ax.collections),
                "n_lines": len(ax.lines),
                "n_patches": len(ax.patches),
                "collection_data": [_arr(getattr(c, "get_array", lambda: None)())
                                    for c in ax.collections],
                "collection_offsets": [
                    _arr(c.get_offsets()) if hasattr(c, "get_offsets") else None
                    for c in ax.collections],
                "patch_extents": [[float(p.get_x()), float(p.get_y()),
                                   float(p.get_width()), float(p.get_height())]
                                  for p in ax.patches
                                  if hasattr(p, "get_width")],
            })
        figs.append({"n_axes": len(fig.axes), "axes": axes})
    return {"n_figures": len(figs), "figures": figs}


def _scatter_colors():
    """The per-point RGBA of every collection drawn, rounded.

    ``report_grid_score_detail`` paints each point ``def_color`` and repaints
    the first one ``highlight_col`` when the frame is sorted and highlighting
    is on. Those two arguments have no other effect anywhere in the function,
    so without reading the facecolours back, ``highlight_best`` and
    ``def_color`` are unobservable and the cases that vary them record the same
    bytes as the default one. This is deliberately *not* part of
    ``fig_digest``: a named colour resolves to the same RGBA in every
    matplotlib the ladder installs, but a colormap lookup -- which is what
    fills ``corr_plot``'s heatmap -- does not, and seaborn restyles those
    between the versions the steps move through.
    """
    import matplotlib.pyplot as plt

    out = []
    for num in plt.get_fignums():
        fig = plt.figure(num)
        for ax in fig.axes:
            for coll in ax.collections:
                getter = getattr(coll, "get_facecolor", None)
                if getter is None:
                    continue
                rgba = np.atleast_2d(np.asarray(getter(), dtype=np.float64))
                out.append([[round(float(v), 6) for v in row] for row in rgba])
    return out


def _charted(fn, colors=False):
    """Run a charting call on a clean slate and digest what it drew."""
    def run():
        import matplotlib.pyplot as plt
        plt.close("all")
        try:
            out = fn()
            drawn = fig_digest()
            if colors:
                drawn["scatter_colors"] = _scatter_colors()
            return {"returned": out, "figures": drawn}
        finally:
            plt.close("all")
    return run


def _corr_cases():
    from skutil.utils import corr_plot

    def call(**kw):
        def inner():
            return corr_plot(kw.pop("X", None) or data.num_frame(40, 5), **kw)
        return _charted(inner)

    case("plot.corr_plot.default", tags=(BEHAVIOR, FIGURE), **PTOL)(call())
    case("plot.corr_plot.spearman", tags=(BEHAVIOR, FIGURE), **PTOL)(
        call(method="spearman"))
    case("plot.corr_plot.kendall", tags=(BEHAVIOR, FIGURE), **PTOL)(
        call(method="kendall"))
    case("plot.corr_plot.styling", tags=(BEHAVIOR, FIGURE), **PTOL)(
        call(figsize=(4, 3), cmap_a=200, cmap_b=20, vmax=0.8,
             xticklabels=2, yticklabels=2, linewidths=0.1,
             cbar_kws={"shrink": 0.8}))
    case("plot.corr_plot.ticklabels_bool", tags=(BEHAVIOR, FIGURE), **PTOL)(
        call(xticklabels=True, yticklabels=False))
    # a tick spacing of exactly 1 is an int that is not > 1, so seaborn's
    # heatmap falls through to the "iterable of labels" branch and slices it
    adversarial("plot.corr_plot.ticklabels_one")(
        call(xticklabels=1, yticklabels=1))
    case("plot.corr_plot.collinear", tags=(BEHAVIOR, FIGURE, ADVERSARIAL), **PTOL)(
        _charted(lambda: corr_plot(data.collinear_frame())))
    case("plot.corr_plot.single_column", tags=(BEHAVIOR, FIGURE, ADVERSARIAL),
         **PTOL)(_charted(lambda: corr_plot(data.num_frame(30, 1))))

    def precomputed():
        X = data.num_frame(40, 5)
        return corr_plot(X.corr(), corr="precomputed")

    case("plot.corr_plot.precomputed", tags=(BEHAVIOR, FIGURE), **PTOL)(
        _charted(precomputed))

    def kde():
        return corr_plot(data.num_frame(25, 3), plot_type="kde", n_levels=3)

    case("plot.corr_plot.kde", tags=(BEHAVIOR, FIGURE, ADVERSARIAL), **PTOL)(
        _charted(kde))

    def pair():
        return corr_plot(data.num_frame(25, 3), plot_type="pair")

    case("plot.corr_plot.pair", tags=(BEHAVIOR, FIGURE, ADVERSARIAL), **PTOL)(
        _charted(pair))

    adversarial("plot.corr_plot.bad_plot_type")(
        _charted(lambda: corr_plot(data.num_frame(10, 3), plot_type="hist")))
    adversarial("plot.corr_plot.with_nan")(
        _charted(lambda: corr_plot(data.nan_frame())))
    adversarial("plot.corr_plot.object_cols")(
        _charted(lambda: corr_plot(data.mixed_frame())))
    adversarial("plot.corr_plot.not_a_frame")(
        _charted(lambda: corr_plot(np.arange(12.0).reshape(4, 3))))

    def mask_shape():
        """The upper-triangle mask is built with ``np.zeros_like(X, dtype=...)``
        over a DataFrame -- shape and fill are the whole of its contract."""
        X = data.num_frame(40, 5).corr()
        mask = np.zeros_like(X, dtype=bool)
        mask[np.triu_indices_from(mask)] = True
        return {"corr": X, "mask": mask.tolist(), "n_masked": int(mask.sum())}

    case("plot.corr_plot.mask_shape", tags=(BEHAVIOR,), **PTOL)(mask_shape)


_corr_cases()


_TIMING = ("mean_fit_time", "std_fit_time", "mean_score_time", "std_score_time")


def _detail(out):
    """The grid-detail frame with its wall-clock columns held back.

    ``_grid_detail`` copies sklearn's four timing columns straight through from
    ``cv_results_``. That they are present, named and ordered as they are is
    contract -- ``report_grid_score_detail`` skips them by name, so a migration
    that renamed one would change what gets charted. Their *values* are how
    fast this machine happened to fit a ridge regression, so the names are
    recorded and the numbers are not.
    """
    if isinstance(out, tuple):
        return tuple(_detail(o) for o in out)
    if isinstance(out, pd.DataFrame):
        return {"columns": list(out.columns),
                "dtypes": [str(d) for d in out.dtypes],
                "timed_columns": [c for c in out.columns if c in _TIMING],
                "frame": out[[c for c in out.columns if c not in _TIMING]]}
    return out


def _grid_report_cases():
    from skutil.utils import report_grid_score_detail
    from skutil.grid_search import GridSearchCV
    from sklearn.linear_model import Ridge
    from sklearn.model_selection import KFold

    def fitted(param_grid=None):
        r = data.rs(211)
        X = pd.DataFrame(r.randn(90, 3), columns=data.cols(3))
        y = pd.Series(X.values.dot([1.0, 2.0, 3.0]) + 0.1 * r.randn(90))
        pg = param_grid if param_grid is not None else {"alpha": [0.1, 1.0, 10.0]}
        return GridSearchCV(estimator=Ridge(solver="cholesky"), param_grid=pg,
                            cv=KFold(n_splits=3)).fit(X, y)

    def report(**kw):
        def inner():
            return _detail(report_grid_score_detail(fitted(), **kw))
        return _charted(inner)

    case("plot.report_grid.default", tags=(BEHAVIOR, FIGURE), **PTOL)(report())
    case("plot.report_grid.no_charts", tags=(BEHAVIOR,), **PTOL)(
        report(charts=False))
    case("plot.report_grid.return_drops", tags=(BEHAVIOR,), **PTOL)(
        report(charts=False, return_drops=True))
    case("plot.report_grid.descending", tags=(BEHAVIOR, FIGURE), **PTOL)(
        report(ascending=False))
    case("plot.report_grid.unsorted", tags=(BEHAVIOR, FIGURE), **PTOL)(
        report(sort_results=False))
    case("plot.report_grid.std_axis", tags=(BEHAVIOR, FIGURE), **PTOL)(
        report(y_axis="std_test_score"))
    case("plot.report_grid.no_highlight", tags=(BEHAVIOR, FIGURE), **PTOL)(
        report(highlight_best=False))
    case("plot.report_grid.custom_colors", tags=(BEHAVIOR, FIGURE), **PTOL)(
        report(highlight_col="green", def_color="black"))
    case("plot.report_grid.percentile", tags=(BEHAVIOR, FIGURE), **PTOL)(
        report(percentile=0.5))

    def string_param():
        """An object-dtype parameter column takes the ``groupby``/``barh``
        branch rather than the scatter branch."""
        return _detail(report_grid_score_detail(
            fitted({"alpha": [0.1, 1.0], "solver": ["cholesky", "lsqr"]})))

    case("plot.report_grid.object_param_column",
         tags=(BEHAVIOR, FIGURE, ADVERSARIAL), **PTOL)(_charted(string_param))

    def nested_param():
        """A ``__``-separated pipeline parameter gets its label split."""
        from sklearn.pipeline import Pipeline
        from skutil.preprocessing import SelectiveScaler

        r = data.rs(213)
        X = pd.DataFrame(r.randn(90, 3), columns=data.cols(3))
        y = pd.Series(X.values.dot([1.0, 2.0, 3.0]) + 0.1 * r.randn(90))
        pipe = Pipeline([("scale", SelectiveScaler()),
                         ("model", Ridge(solver="cholesky"))])
        g = GridSearchCV(estimator=pipe,
                         param_grid={"model__alpha": [0.1, 1.0, 10.0]},
                         cv=KFold(n_splits=3)).fit(X, y)
        return _detail(report_grid_score_detail(g))

    case("plot.report_grid.nested_param_label", tags=(BEHAVIOR, FIGURE), **PTOL)(
        _charted(nested_param))

    def _numeric_param(search, name="param_alpha"):
        """Retype one parameter column so it lands in the scatter branch.

        sklearn hands every ``param_*`` column over as an object-dtype masked
        array whatever the values in it are, so ``get_numeric`` never returns a
        parameter column and the ``elif col not in valid_axes`` arm of
        ``report_grid_score_detail`` -- the scatter, the title, the two axis
        labels and the ``show()`` -- never runs against a stock
        ``GridSearchCV``. The arm is not dead code: the 0.17/H2O path of
        ``_grid_detail`` builds its frame out of parameter dicts, where a float
        hyper-parameter stays a float. Coercing the column reaches the same
        state through the ``cv_results_`` path that this stack actually takes,
        and leaves every other column exactly as sklearn produced it.

        It matters that these lines are pinned by something: they carry three
        of the module's global-pyplot calls and its only in-library ``show()``,
        which a later step has to rewrite. Without a case through here the
        rewrite could put anything at all in their place.
        """
        search.cv_results_ = dict(search.cv_results_)
        search.cv_results_[name] = np.asarray(
            [float(v) for v in search.cv_results_[name]], dtype=np.float64)
        return search

    def numeric(**kw):
        def inner():
            return _detail(report_grid_score_detail(_numeric_param(fitted()), **kw))
        return _charted(inner, colors=True)

    case("plot.report_grid.numeric_param", tags=(BEHAVIOR, FIGURE), **PTOL)(
        numeric())
    case("plot.report_grid.numeric_param_no_highlight",
         tags=(BEHAVIOR, FIGURE), **PTOL)(numeric(highlight_best=False))
    case("plot.report_grid.numeric_param_custom_colors",
         tags=(BEHAVIOR, FIGURE), **PTOL)(
        numeric(highlight_col="green", def_color="black"))
    case("plot.report_grid.numeric_param_unsorted", tags=(BEHAVIOR, FIGURE),
         **PTOL)(numeric(sort_results=False))
    case("plot.report_grid.numeric_param_std_axis", tags=(BEHAVIOR, FIGURE),
         **PTOL)(numeric(y_axis="std_test_score"))

    def numeric_mixed():
        """A numeric parameter beside an object one: the scatter loop and the
        ``groupby``/``barh`` loop both run, over the same frame."""
        search = _numeric_param(
            fitted({"alpha": [0.1, 1.0], "solver": ["cholesky", "lsqr"]}))
        return _detail(report_grid_score_detail(search))

    case("plot.report_grid.numeric_and_object_params",
         tags=(BEHAVIOR, FIGURE, ADVERSARIAL), **PTOL)(
        _charted(numeric_mixed, colors=True))

    def numeric_nested():
        """``x_lab = col.split('__')[-1]``: the other arm of the label split,
        which only a numeric *pipeline* parameter reaches."""
        from sklearn.pipeline import Pipeline
        from skutil.preprocessing import SelectiveScaler

        r = data.rs(217)
        X = pd.DataFrame(r.randn(90, 3), columns=data.cols(3))
        y = pd.Series(X.values.dot([1.0, 2.0, 3.0]) + 0.1 * r.randn(90))
        pipe = Pipeline([("scale", SelectiveScaler()),
                         ("model", Ridge(solver="cholesky"))])
        g = GridSearchCV(estimator=pipe,
                         param_grid={"model__alpha": [0.1, 1.0, 10.0]},
                         cv=KFold(n_splits=3)).fit(X, y)
        return _detail(report_grid_score_detail(
            _numeric_param(g, name="param_model__alpha")))

    case("plot.report_grid.numeric_nested_param_label",
         tags=(BEHAVIOR, FIGURE), **PTOL)(_charted(numeric_nested, colors=True))

    adversarial("plot.report_grid.bad_y_axis")(
        _charted(lambda: report_grid_score_detail(fitted(), y_axis="score")))
    adversarial("plot.report_grid.percentile_zero")(
        _charted(lambda: report_grid_score_detail(fitted(), percentile=0.0)))
    adversarial("plot.report_grid.percentile_one")(
        _charted(lambda: report_grid_score_detail(fitted(), percentile=1.0)))
    adversarial("plot.report_grid.percentile_negative")(
        _charted(lambda: report_grid_score_detail(fitted(), percentile=-0.5)))
    adversarial("plot.report_grid.unfitted")(
        _charted(lambda: report_grid_score_detail(
            GridSearchCV(estimator=Ridge(), param_grid={"alpha": [1.0]}))))


_grid_report_cases()


def _chart_flag_cases():
    """The two module-level flags that gate every plotting path."""
    import skutil.utils.util as u

    case("plot.can_chart_flags", tags=(BEHAVIOR,))(
        lambda: {"mpl": bool(u.CAN_CHART_MPL), "sns": bool(u.CAN_CHART_SNS)})


_chart_flag_cases()
