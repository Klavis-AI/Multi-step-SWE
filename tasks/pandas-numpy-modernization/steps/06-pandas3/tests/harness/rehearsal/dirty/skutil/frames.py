"""Steps 2, 4, 6 and 8: the pandas constructs the ladder retires."""

import pandas as pd


def frame_ops(df, fn):
    arr = df.as_matrix()                      # pandas-removed-method
    df.dropna(inplace=True)                   # pandas-inplace-true + -kwarg
    df.reset_index(inplace=False)             # pandas-inplace-kwarg only
    df.values[0] = 1                          # pandas-chained-assignment
    grouped = df.groupby("g").sum()           # pandas-groupby-implicit-observed
    filled = df.fillna(0, downcast="infer")   # pandas-fillna-downcast
    mapped = df.applymap(fn)                  # pandas-expired-3
    rows = [r for _i, r in df.iterrows()]     # pandas-row-iteration
    wide = df.apply(fn, axis=1)               # pandas-apply-axis-1
    factors = df.dtypes == 'object'           # pandas-object-dtype-blind (compare)
    text_cols = df.select_dtypes(include=['object'])  # pandas-object-dtype-blind (select)
    return arr, grouped, filled, mapped, rows, wide, factors, text_cols


def near_misses(df, fn):
    """Spellings one character away from the ones above; none may fire."""
    pd.set_option("display.width", 100)
    return (df.to_numpy(), df.fillna(0), df.groupby("g", observed=True),
            df.map(fn), df.apply(fn, axis=0), df.items(), df.columns.append,
            (df.dtypes == 'object') | (df.dtypes == 'str'),
            df.select_dtypes(include=['object', 'str']))
