"""Step 3: scikit-learn private internals and the 0.17-era search attribute."""

from sklearn.datasets import load_boston                  # sklearn-load-boston
from sklearn.model_selection._search import BaseSearchCV  # sklearn-private-api


class Search(BaseSearchCV):
    def __init__(self):
        self.n_folds = 3
        self.n_iter = 10

    def report(self):
        return self.grid_scores_           # sklearn-grid-scores

    def near_misses(self):
        """n_folds and n_iter are this package's own attributes, not sklearn's."""
        return self.n_folds, self.n_iter


def data():
    return load_boston()                   # sklearn-load-boston, call arm
