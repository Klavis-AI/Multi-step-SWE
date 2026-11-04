# Third-Party Notices

## Task

`pandas-numpy-modernization` — migrate the skutil scientific-Python library from
pandas 0.23 / NumPy 1.14 / scikit-learn 0.20 on Python 3.7 to pandas 3.0 /
NumPy 2.3 / scikit-learn 1.7 on Python 3.12, one toolchain at a time.

## Upstream project

| Field | Value |
| --- | --- |
| Project | skutil |
| URL | <https://github.com/tgsmith61591/skutil> |
| Pin | tag `v0.1.6` (`skutil.__version__ == '0.1.6'`) |
| Bundled path | `environment/baseline/` — the v0.1.6 package tree, docs and build files |
| Upstream license | BSD-3-Clause; copyright line `Copyright (c) 2016, Taylor G. Smith` (`environment/baseline/LICENSE`) |

## License files

| SPDX ID | File under `LICENSES/` | Covers |
| --- | --- | --- |
| BSD-3-Clause | `BSD-3-Clause.txt` | skutil v0.1.6. Exact copy of `environment/baseline/LICENSE`. |

## Modifications by the benchmark

The baseline is skutil v0.1.6 with the following changes, made so the library
runs without its retired H2O integration and against the task's toolchains:

- Removed the `skutil/h2o/` subpackage and `doc/examples/h2o/`.
- Removed the `from h2o.frame import H2OFrame` import from
  `skutil/preprocessing/balance.py`, and removed that import plus the H2OFrame
  branches from `skutil/metrics/_act.py`; removed `h2o` from
  `skutil/__init__.py`, `skutil/setup.py`, `requirements.txt` and `setup.py`.
  Remaining H2O references are docstring text only.
- Added to `README.md` the deprecation note that appears in upstream master.
- Inlined a local `_check_numpy_unicode_bug` in
  `skutil/preprocessing/encode.py` (matching scikit-learn's former
  `sklearn.preprocessing.label._check_numpy_unicode_bug`) so the module imports
  on newer scikit-learn.
- Replaced `X[[1]]` with `X.iloc[:, 1]` in
  `skutil/utils/tests/test_util.py` for pandas compatibility.
- All remaining source files are the v0.1.6 tree.

Migration steps under `steps/` add benchmark-authored instructions, tests,
workdirs and solutions.

## Third-party runtime dependencies

The scientific-Python stack is installed from PyPI into `/opt/venv` at image
build time and is not vendored or redistributed in the task tree. Its versions
are pinned in `environment/stack.txt` and `environment/stacks/01.txt` through
`08.txt` (for example NumPy 1.14.6 through 2.3.5, pandas 0.23.4 through 3.0.5,
scikit-learn 0.20.4 through 1.7.2, SciPy, matplotlib, seaborn, pytest and
coverage). Each package's own distribution license applies. No third-party
source is shipped in the task tree.

## Required notices

- skutil: `Copyright (c) 2016, Taylor G. Smith`, BSD-3-Clause.
- The pinned tree contains no upstream `NOTICE` file.

Task-authored benchmark content is licensed separately by the benchmark maintainers.
