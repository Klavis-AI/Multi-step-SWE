"""Step 8: the three hot paths' interpreter loops, and the engine-UDF hole.

Positive fixtures for ``hot-path-row-iteration`` and ``hot-path-engine-udf``.
The three functions carry the exact names the row-iteration rule is scoped to
(``functions=HOT_PATHS``): the module-level ``filter_collinearity``,
``InteractionTermTransformer.transform`` and
``GainsStatisticalReport.as_data_frame``. Inside them every ``for`` and every
comprehension counts whatever its iterator -- a bare sequence, an ``enumerate``,
a ``range`` -- and every ``while`` counts too, with the single exemption of
filter_collinearity's top-level greedy driver (structural control flow, not row
iteration). ``np.vectorize`` and ``np.frompyfunc`` count as well: they are a
per-element Python loop wearing a numpy name. Twelve in all: three in
filter_collinearity -- one of them in a helper nested inside it, since a row loop
must not launder out of the gate by moving to a local ``def`` -- five in
transform (the comprehension, the i/j range pair, and the np.vectorize /
np.frompyfunc pair), and four in as_data_frame (the bare/iter/fold triple plus a
``while`` in a nested ``def filter_collinearity`` -- a hot-path NAME nested in
another hot path does not inherit the top-level greedy exemption, which is
matched on the exact owner, not the tail name). The engine-UDF calls follow:
map_elements / map_rows / apply_rows, the qualified and bare-name duckdb
``create_function`` and a getattr-spelled ``map_elements`` -- six charged, with
the batch-wise ``map_batches`` the near miss the rule must not charge.
"""

import numpy as np


def filter_collinearity(corr, threshold):
    finished = False
    cols = list(corr.columns)

    def _scan(seq):                             # helper nested inside a hot path
        acc = []
        for x in seq:                           # hot-path-row-iteration [3] nested-def owner
            acc.append(x)
        return acc

    while not finished:                         # exempt: top-level greedy driver
        for i, nm in enumerate(cols):           # hot-path-row-iteration [1]
            j = 0
            while j < i:                         # hot-path-row-iteration [2] nested while
                j += 1
        finished = True
    return _scan(cols)


class InteractionTermTransformer(object):
    def transform(self, X):
        cols = list(X.columns)
        n = len(cols)
        names = [c for c in cols]               # hot-path-row-iteration [4] comprehension
        vec = np.vectorize(lambda z: z)          # hot-path-row-iteration [10] np.vectorize is
        vec2 = np.frompyfunc(lambda z: z, 1, 1)  # hot-path-row-iteration [11] a per-element loop
        out = []
        for i in range(n):                       # hot-path-row-iteration [5]
            for j in range(i + 1, n):            # hot-path-row-iteration [6]
                out.append(X[cols[i]] * X[cols[j]])
        return names, out, vec, vec2


class GainsStatisticalReport(object):
    def as_data_frame(self):
        keys = ['lift', 'gini']
        rows = []
        for metric in keys:                      # hot-path-row-iteration [7] bare sequence
            idx = 0
            for _ in range(self.n_iter):         # hot-path-row-iteration [8]
                for _fold in range(self.n_folds):  # hot-path-row-iteration [9]
                    rows.append(self.stats[metric][idx])
                    idx += 1

        def filter_collinearity():               # a hot-path NAME nested inside another hot
            while False:                         # hot-path-row-iteration [12] path does NOT
                pass                             # inherit the while-exemption: the owner is
        filter_collinearity()                    # as_data_frame.filter_collinearity, matched
        return rows                              # exactly, not by tail name


def engine_paths(frame, con, fn):
    """polars/duckdb per-row callables are a row loop wearing a method name."""
    from duckdb import create_function            # the same call, imported unqualified
    a = frame.map_elements(fn)                   # hot-path-engine-udf [1]
    b = frame.map_rows(fn)                        # hot-path-engine-udf [2]
    c = frame.apply_rows(fn)                      # hot-path-engine-udf [3]
    d = con.create_function("f", fn)             # hot-path-engine-udf [6]
    g = create_function("g", fn)                 # hot-path-engine-udf [7] bare-name spelling
    h = getattr(frame, "map_elements")(fn)       # hot-path-engine-udf [8] getattr spelling
    e = frame.map_batches(fn)                     # near miss: batch-wise, not charged
    return a, b, c, d, e, g, h


def elsewhere(corr, threshold):
    """The same loops outside a hot path: neither rule may fire here."""
    _v = np.vectorize(lambda z: z)               # np.vectorize outside a hot path: not charged
    _g = getattr(corr, "to_numpy")()             # getattr of a non-engine name: not charged
    return [i for i in range(len(corr)) if threshold > 0], _v, _g
