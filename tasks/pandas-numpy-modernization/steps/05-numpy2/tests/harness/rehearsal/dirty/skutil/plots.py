"""Steps 7 and 8: global figure state, and the hot path's interpreter loop."""

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns


def heat(df):
    matplotlib.rcParams["figure.dpi"] = 120   # matplotlib-ambient-rcparams
    sns.set_style("whitegrid")                # matplotlib-ambient-rcparams
    plt.title("correlations")                 # pyplot-global-state
    sns.plt.legend()                          # seaborn-plt, counted once
    sns.heatmap(df.corr())
    plt.show()                                # pyplot-show-in-library
    plt.close("all")


def filter_collinearity(corr, threshold):
    """A nested pair of loops, so the rule counts two iteration expressions."""
    drop = []
    for i in range(corr.shape[0]):                  # hot-path-row-iteration
        for j in range(i + 1, corr.shape[0]):       # hot-path-row-iteration
            if np.abs(corr.iloc[i, j]) > threshold:
                drop.append(i)
    return drop


def elsewhere(corr, threshold):
    """The same loop outside a hot path: the hot-path rule must not fire."""
    return [i for i in range(corr.shape[0]) if threshold > 0]
