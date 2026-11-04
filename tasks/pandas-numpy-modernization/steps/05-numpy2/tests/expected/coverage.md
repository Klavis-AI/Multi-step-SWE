# Coverage of the behaviour corpus over `skutil/`

Rule 7 / R12 asks for line coverage over the migration-relevant files. This
file is that measurement, shipped inside the step so it can be read without
leaving the task package. **Nothing in `harness/` reads this file**; it is
documentation, and no check scores against it.

## What was measured, and where

The 1236-case behaviour corpus in `expected/baseline-recording.json` is the
same 1236 cases at every step of this task -- a step changes the *tree*, not
the case list -- so the corpus's reach over the library is established once,
on the tree the corpus was recorded against, and inherited by every step that
replays it. That tree is the untouched `skutil` 0.1.6 baseline and the image
is `skutil-rec:step1` (CPython 3.7.17, NumPy 1.14.6, pandas 0.23.4,
scikit-learn 0.20.4, SciPy 1.1.0, Matplotlib 2.2.5, seaborn 0.7.1).

It is measured there and not on step 5's own tree for the reason it is measured
once for the whole task: the corpus is a black-box recording made against the
baseline tree, and its reach over the library is a property of that recording,
not of any later tree. A coverage run on step 5's entry tree (the step-4 golden,
under NumPy 2.0) would report coverage of the *migrated* sources -- built through
meson-python instead of numpy.distutils, the four `setup.py` deleted and
`_build_utils` reduced to its `DEFAULT_ROOT` constant -- which is a different
question from where the 1236 recorded cases reach. (Those build-file changes are
outside `coverage`'s omit set only in that the files are gone; the corpus never
executed them either way.)

Command (`evidence/item3/regen-item3-screens.sh`, screen 3 -- unchanged; the
corpus and the tree it is measured on are the same at every step):

    PYTHONHASHSEED=0 coverage run --branch --rcfile=/app/.coveragerc \
        -m skutil_probe.run record
    coverage report -m --rcfile=/app/.coveragerc \
        --omit='*/tests/*,*/setup.py,*/_build_utils/*'

The same run's recording was compared against the shipped reference and
matched 1236/1236, so this is coverage of the values the corpus actually
scores, not of a separate run.

## Scope

`*/tests/*` is omitted on top of the upstream `.coveragerc`'s `setup.py` and
`_build_utils` omits. The corpus is a black-box recording of skutil's public
API and never executes the package's own pytest files under `skutil/*/tests/`
-- those are a different scored family (the inherited suite, recorded in
`expected/inherited-suite.json`). The upstream `.coveragerc` is left untouched
because it is a file under migration. The full-tree number, which does include
the un-exercised test files, is reported alongside so the scope is legible
rather than hidden:

    TOTAL                                            2811   1026    592     31    68%

## Result

**96% line coverage over 1854 statements** (69 missed), with 556 branches and
31 partial. Rule 7's floor is 75%.

Per file, every module is at or above 94% except two, and both are version
forks this stack never takes:

| file | cover | what is not covered |
| --- | --- | --- |
| `__init__.py` | 61% | the `if __SKUTIL_SETUP__:` partial-import branch (20), reached only mid-build, and `setup_module`'s global-seed hook (36-41), which deviation D-19 deliberately keeps out of the corpus -- recording its unseeded draw would contradict the `reproducibility` gate |
| `grid_search.py` | 73% | lines 386-606, the `else:` half of an `if SK18:` -- the scikit-learn 0.17 `GridSearchCV`/`RandomizedSearchCV` definitions, unreachable on 0.20.4 |
| `utils/fixes.py` | 87% | the matching 0.17 halves: the `SK18 = False` import block (72-86), the `grid_scores_` arm of `_grid_detail` (240-251), and the `_SK17BaseSearchCV` delegated methods (614-615, 629-630, 644-645, 694-696) |
| `utils/util.py` | 94% | the two `except ImportError:` arms that set `CAN_CHART_MPL`/`CAN_CHART_SNS` false and the three "cannot plot" warning arms behind them (29-30, 38-39, 278-279, 284-285, 1064); the Python-2 half of `is_integer` and its `except AssertionError` diagnostic (783, 788-796) |

Everything deliberately uncovered is one of four kinds: build-time-only code,
the scikit-learn 0.17 half of a version fork, an import-failure fallback that
would need matplotlib uninstalled to reach, or the global-seed hook D-19 keeps
out. The version forks matter twice over: the migration *deletes* them, and
because no case records them, deleting them costs the candidate nothing --
which is the intended outcome rather than an accident.

## Full report

    Name                                   Stmts   Miss Branch BrPart  Cover   Missing
    ----------------------------------------------------------------------------------
    skutil/__init__.py                        16      6      2      1    61%   20, 36-41
    skutil/base.py                            31      0      2      0   100%
    skutil/decomposition/__init__.py           2      0      2      0   100%
    skutil/decomposition/decompose.py         76      1     10      0    99%   62
    skutil/feature_selection/__init__.py       3      0      2      0   100%
    skutil/feature_selection/base.py          23      0      6      0   100%
    skutil/feature_selection/combos.py        53      0     14      1    99%   178->194
    skutil/feature_selection/select.py       127      3     32      3    96%   312, 373->377, 393, 395
    skutil/grid_search.py                     20      5      2      1    73%   386-606
    skutil/linear_model/__init__.py            1      0      2      0   100%
    skutil/metrics/__init__.py                 3      0      2      0   100%
    skutil/metrics/_act.py                   128      0     46      1    99%   326->330
    skutil/metrics/kernel.py                  61      2      0      0    97%   42-43
    skutil/metrics/pairwise.py                 3      0      0      0   100%
    skutil/model_selection/__init__.py         2      0      0      0   100%
    skutil/odr/__init__.py                     2      0      2      0   100%
    skutil/odr/dqrutl.py                      54      0      6      0   100%
    skutil/preprocessing/__init__.py           5      0      2      0   100%
    skutil/preprocessing/balance.py          182      3     42      4    97%   176, 205->227, 220, 513->559, 528
    skutil/preprocessing/encode.py            72      1     26      3    96%   28, 171->177, 255->259
    skutil/preprocessing/impute.py           147      1     55      2    99%   263, 507->489
    skutil/preprocessing/transform.py        207      0     52      0   100%
    skutil/testing/__init__.py                 2      0      2      0   100%
    skutil/testing/utils.py                   20      0      2      0   100%
    skutil/utils/__init__.py                   3      0      2      0   100%
    skutil/utils/fixes.py                    235     29     86      7    87%   37->41, 72-86, 195, 240-251, 360->362, 387, 541, 585, 614-615, 629-630, 644-645, 694-696
    skutil/utils/metaestimators.py            56      0     26      1    99%   114->131
    skutil/utils/util.py                     320     18    131      7    94%   29-30, 38-39, 278-279, 284-285, 783, 788-796, 900->903, 933->936, 1064
    ----------------------------------------------------------------------------------
    TOTAL                                   1854     69    556     31    96%
