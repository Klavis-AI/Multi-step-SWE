"""Step 7: global figure state. The hot-path loops live in hot_paths.py."""

import matplotlib
import matplotlib.pyplot as plt
import seaborn as sns


def heat(df):
    matplotlib.rcParams["figure.dpi"] = 120   # matplotlib-ambient-rcparams
    sns.set_style("whitegrid")                # matplotlib-ambient-rcparams
    plt.title("correlations")                 # pyplot-global-state
    sns.plt.legend()                          # seaborn-plt, counted once
    sns.heatmap(df.corr())
    plt.show()                                # pyplot-show-in-library
    plt.close("all")
