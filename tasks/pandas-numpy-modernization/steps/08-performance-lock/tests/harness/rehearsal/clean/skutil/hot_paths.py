"""Step 8 near misses: the vectorised hot paths and the batch-wise engine call.

None of these may fire ``hot-path-row-iteration`` or ``hot-path-engine-udf``.
The three functions carry the scoped names on purpose -- the rule is scoped by
name, so the proof that it does not over-fire has to sit inside those very names.
filter_collinearity keeps its top-level greedy ``while`` (the exempt driver) with
a vectorised body; transform and as_data_frame carry no loop at all; the engine
call is ``map_batches``, which hands the engine a whole column.
"""

import numpy as np


def filter_collinearity(corr, threshold):
    finished = False
    survivors = corr.columns.tolist()

    def _colmax(m):                              # helper nested in a hot path:
        np.fill_diagonal(m, np.nan)              # vectorised, no row loop, so the
        return np.nanmax(m, axis=0)              # scoping rule must NOT fire here

    while not finished:                          # top-level greedy driver: exempt
        mask = _colmax(np.abs(corr.to_numpy())) > threshold
        finished = not mask.any()
        survivors = corr.columns[~mask].tolist()
    return survivors


class InteractionTermTransformer:
    def transform(self, X):
        cols = np.asarray(X.columns).astype(str)
        n = len(cols)
        ii, jj = np.triu_indices(n, 1)
        names = np.char.add(np.char.add(cols[ii], '_'), cols[jj]).tolist()
        arr = X.to_numpy()
        prod = arr[:, ii] * arr[:, jj]
        return names, prod


class GainsStatisticalReport:
    def as_data_frame(self):
        scores = np.asarray(self.stats['lift']).reshape(self.n_iter, self.n_folds)
        return scores.mean(axis=1), scores.std(axis=1)


def engine_paths(frame, fn):
    """The batch-wise engine call hands a whole column at once: vectorised."""
    return frame.map_batches(fn)
