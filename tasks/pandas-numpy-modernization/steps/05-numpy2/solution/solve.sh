#!/usr/bin/env bash
#
# Step 5 golden solution: 04-pandas2's tree -> 05-numpy2.
#
# The entry tree is step 4's golden, already clean under pandas 2.2.3 and the
# widened pandas-vet select. The whole stack moves up here -- numpy 1.26.4 ->
# 2.0.2, and with it the build system: numpy.distutils was removed at NumPy
# 2.0, and skutil's four setup.py files and skutil/_build_utils are written
# against it. Under numpy 2.0.2 `import numpy.distutils` raises, so the tree
# handed to this step cannot build its C, Cython and Fortran extensions and
# cannot be installed at all -- B is forced to 0 before any runtime family runs.
#
# The 166 behaviour differences numpy 2.0 brings (NEP 50 weak-scalar promotion,
# NEP 51 scalar repr, the numpy.bool rename, the 2.0 linalg tie-reorder) are the
# STACK's doing, not the code's: this step declares them as flips and the golden
# reproduces every one simply by building and running under numpy 2.0.2. The
# runtime sources already survive the move -- the expired-member and copy=False
# guards carry at zero on the entry tree. So the work here is exactly one thing:
# replace the numpy.distutils build with a meson-python backend, so the tree
# builds again and the corpus can replay.
#
# One gate arrives at step 5, and the golden answers it:
#
#   numpy-distutils. The AST instrument counts every numpy.distutils import and
#   attribute access. The entry tree has 9 -- two in the top-level setup.py, two
#   each in skutil/setup.py, skutil/metrics/setup.py and skutil/odr/setup.py,
#   and one lazy import in skutil/_build_utils/__init__.py. This is the one gate
#   the behaviour corpus cannot stand in for: a tree that fails it does not
#   import, and every downstream credit is already zero.
#
# Three moves, in the order the tree needs them:
#
#   Layer 1 (add).  A pyproject.toml naming the mesonpy build-backend and a
#   meson.build that compiles _kernel_fast through Cython and dqrsl through
#   f2py's meson custom-target path (generate the C, let meson compile it -- the
#   numpy-2 way, since `f2py -c`'s default backend is the same dead distutils).
#
#   Layer 2 (delete).  The four numpy.distutils setup.py files, dead under the
#   meson backend.
#
#   Layer 3 (reduce).  skutil/_build_utils/__init__.py drops its lazy
#   numpy.distutils get_blas_info -- unused at run time, no importers -- and
#   becomes its DEFAULT_ROOT constant.
#
# Then the checks: every surviving module compiles, the three notebooks still
# hold every cell, and an independent scan (its own tokenizer, strings and
# comments blanked) confirms no numpy.distutils reference survives, the four
# setup.py are gone, and the meson-python backend is in place. The verifier
# performs the actual build from this source; these checks are static so the
# oracle stays fast and needs no toolchain of its own.
set -euo pipefail

ROOT="${1:-/app}"
cd "$ROOT"

echo "== step 5 oracle: $ROOT"

# ---------------------------------------------------------------------------
# Entry assertion. Prove we are transforming the tree this step expects: the
# four numpy.distutils setup.py present, _build_utils still reaching for
# numpy.distutils, and no meson backend yet.
# ---------------------------------------------------------------------------
python - "$ROOT" <<'PYEOF'
import io, re, sys, pathlib, tokenize
root = sys.argv[1]

def die(msg):
    sys.stderr.write("solve.sh entry: " + msg + "\n")
    sys.exit(1)

def code_only(text):
    """Blank every string and comment in place so prose that names
    numpy.distutils is not mistaken for code importing it."""
    buf = list(text)
    lines = text.splitlines(keepends=True)
    offs = [0]
    for ln in lines:
        offs.append(offs[-1] + len(ln))
    def pos(row, col):
        return offs[row - 1] + col
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except tokenize.TokenError:
        return text
    for tok in toks:
        if tok.type in (tokenize.STRING, tokenize.COMMENT):
            for k in range(pos(*tok.start), pos(*tok.end)):
                if buf[k] != "\n":
                    buf[k] = " "
    return "".join(buf)

ND = re.compile(r"numpy\s*\.\s*distutils|from\s+numpy\s+import\s+distutils")

def nd_count(root):
    total = 0
    for p in sorted(pathlib.Path(root).rglob("*.py")):
        total += len(ND.findall(code_only(io.open(p, encoding="utf-8").read())))
    return total

SETUPS = ["setup.py", "skutil/setup.py",
          "skutil/metrics/setup.py", "skutil/odr/setup.py"]
for rel in SETUPS:
    if not pathlib.Path(root, rel).is_file():
        die("expected build file missing at entry: %s" % rel)

bu = pathlib.Path(root, "skutil/_build_utils/__init__.py")
if not bu.is_file():
    die("skutil/_build_utils/__init__.py missing at entry")
if "numpy.distutils" not in code_only(bu.read_text(encoding="utf-8")):
    die("_build_utils no longer reaches numpy.distutils -- unexpected entry tree")

for rel in ("pyproject.toml", "meson.build"):
    if pathlib.Path(root, rel).exists():
        die("%s already present -- entry tree is not the pandas-2 golden" % rel)

n = nd_count(root)
if n <= 0:
    die("expected numpy.distutils references at entry, found %d" % n)
print("entry tree: %d numpy.distutils reference(s), four setup.py present, no meson backend" % n)
PYEOF

# ---------------------------------------------------------------------------
# Layer 1 -- add the meson-python backend.
# ---------------------------------------------------------------------------
cat > "$ROOT/pyproject.toml" <<'TOML'
[build-system]
build-backend = "mesonpy"
requires = ["meson-python>=0.15", "Cython>=3", "numpy>=2"]

[project]
name = "skutil"
version = "0.1.6"
description = "A set of sklearn-esque extension modules"
readme = "README.md"
requires-python = ">=3.9"
license = {text = "new BSD"}
maintainers = [{name = "Taylor G. Smith", email = "tgsmith61591@gmail.com"}]
dependencies = ["numpy", "scipy", "pandas", "scikit-learn"]

[tool.meson-python.args]
setup = ["-Dbuildtype=release"]
TOML

cat > "$ROOT/meson.build" <<'MESON'
project(
  'skutil',
  'c', 'cython', 'fortran',
  version: '0.1.6',
  meson_version: '>=0.64.0',
  default_options: ['warning_level=1', 'buildtype=release'],
)

py_mod = import('python')
py = py_mod.find_installation(pure: false)
py_dep = py.dependency()

# NumPy C headers
incdir_numpy = run_command(py,
  ['-c', 'import numpy; print(numpy.get_include())'],
  check: true).stdout().strip()
inc_np = include_directories(incdir_numpy)

# f2py runtime source (fortranobject.c / .h)
incdir_f2py = run_command(py,
  ['-c', 'import numpy.f2py, os; print(os.path.join(os.path.dirname(numpy.f2py.__file__), "src"))'],
  check: true).stdout().strip()
inc_f2py = include_directories(incdir_f2py)
fortranobject_c = incdir_f2py / 'fortranobject.c'

# Install the pure-python package tree
install_subdir('skutil', install_dir: py.get_install_dir())

# Cython extension: skutil.metrics._kernel_fast
py.extension_module('_kernel_fast',
  'skutil/metrics/_kernel_fast.pyx',
  include_directories: inc_np,
  dependencies: py_dep,
  install: true,
  subdir: 'skutil/metrics',
)

# f2py Fortran extension: skutil.odr.dqrsl
dqrsl_c = custom_target('dqrsl_c',
  input: 'skutil/odr/dqrsl.pyf',
  output: ['dqrslmodule.c', 'dqrsl-f2pywrappers.f'],
  command: [py, '-m', 'numpy.f2py', '@INPUT@', '--build-dir', '@OUTDIR@'],
)
py.extension_module('dqrsl',
  ['skutil/odr/dqrsl.f', dqrsl_c, fortranobject_c],
  include_directories: [inc_np, inc_f2py],
  dependencies: py_dep,
  install: true,
  subdir: 'skutil/odr',
)
MESON

echo "Layer 1: wrote pyproject.toml (mesonpy backend) and meson.build"

# ---------------------------------------------------------------------------
# Layer 2 -- delete the four numpy.distutils setup.py files.
# ---------------------------------------------------------------------------
for rel in setup.py skutil/setup.py skutil/metrics/setup.py skutil/odr/setup.py; do
    if [ ! -f "$ROOT/$rel" ]; then
        echo "solve.sh Layer 2: $rel already gone" >&2
        exit 1
    fi
    rm -f "$ROOT/$rel"
    echo "Layer 2: deleted $rel"
done

# ---------------------------------------------------------------------------
# Layer 3 -- reduce _build_utils to its DEFAULT_ROOT constant.
# ---------------------------------------------------------------------------
cat > "$ROOT/skutil/_build_utils/__init__.py" <<'PYBU'
"""
Utilities useful during the build -- adapted from sklearn.
"""
# author: Andy Mueller, Gael Varoquaux
# license: BSD

DEFAULT_ROOT = "skutil"
PYBU
echo "Layer 3: reduced skutil/_build_utils/__init__.py to DEFAULT_ROOT"

# ---------------------------------------------------------------------------
# Checks. Independent of the gates: the tree still compiles, the notebooks still
# hold their cells, and the numpy.distutils build is gone with meson in place.
# ---------------------------------------------------------------------------
echo "== checks"

# 1. every surviving module compiles. The four setup.py are gone, so the .py
#    count drops by four from step 4's 58 to 54.
python - "$ROOT" <<'PYEOF'
import compileall, sys, pathlib
root = sys.argv[1]
files = sorted(pathlib.Path(root).rglob("*.py"))
if len(files) != 54:
    sys.stderr.write("checks: expected 54 .py files, found %d\n" % len(files))
    sys.exit(1)
ok = compileall.compile_dir(root, quiet=1, force=True)
if not ok:
    sys.stderr.write("checks: a module failed to compile\n")
    sys.exit(1)
print("compile: %d modules OK" % len(files))
PYEOF

# 2. the three notebooks parse and still hold every cell.
python - "$ROOT" <<'PYEOF'
import json, sys
root = sys.argv[1]
EXPECTED = {
    "doc/examples/decomposition/SelectivePCA weighting.ipynb": (13, 6),
    "doc/examples/machine_learning_demo/ml demo.ipynb": (47, 24),
    "doc/examples/pipeline/skutil grid demo.ipynb": (15, 10),
}
for rel, (ncells, ncode) in EXPECTED.items():
    nb = json.load(open(root + "/" + rel, encoding="utf-8"))
    cells = nb["cells"]
    code = [c for c in cells if c["cell_type"] == "code"]
    if len(cells) != ncells or len(code) != ncode:
        sys.stderr.write("checks: %s has %d cells / %d code, expected %d / %d\n"
                         % (rel, len(cells), len(code), ncells, ncode))
        sys.exit(1)
print("notebooks: 3 parse with expected cell counts")
PYEOF

# 3. independent scan -- no numpy.distutils reference survives anywhere, the four
#    setup.py are gone, _build_utils is the DEFAULT_ROOT stub, and the
#    meson-python backend names both extension targets.
python - "$ROOT" <<'PYEOF'
import io, re, sys, pathlib, tokenize
root = sys.argv[1]

def code_only(text):
    buf = list(text)
    lines = text.splitlines(keepends=True)
    offs = [0]
    for ln in lines:
        offs.append(offs[-1] + len(ln))
    def pos(row, col):
        return offs[row - 1] + col
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except tokenize.TokenError:
        return text
    for tok in toks:
        if tok.type in (tokenize.STRING, tokenize.COMMENT):
            for k in range(pos(*tok.start), pos(*tok.end)):
                if buf[k] != "\n":
                    buf[k] = " "
    return "".join(buf)

ND = re.compile(r"numpy\s*\.\s*distutils|from\s+numpy\s+import\s+distutils")

bad = False
def report(label, got, want):
    global bad
    tag = "ok" if got == want else "BAD"
    if got != want:
        bad = True
    print("  %-3s %-40s %r (want %r)" % (tag, label, got, want))

nd = 0
for p in sorted(pathlib.Path(root).rglob("*.py")):
    nd += len(ND.findall(code_only(io.open(p, encoding="utf-8").read())))
report("numpy.distutils references", nd, 0)

for rel in ("setup.py", "skutil/setup.py",
            "skutil/metrics/setup.py", "skutil/odr/setup.py"):
    report("%s removed" % rel, pathlib.Path(root, rel).exists(), False)

bu = pathlib.Path(root, "skutil/_build_utils/__init__.py").read_text(encoding="utf-8")
report("_build_utils is the stub", 'DEFAULT_ROOT = "skutil"' in bu and "get_blas_info" not in bu, True)

pp = pathlib.Path(root, "pyproject.toml").read_text(encoding="utf-8")
report("pyproject names mesonpy", 'build-backend = "mesonpy"' in pp, True)

mb = pathlib.Path(root, "meson.build").read_text(encoding="utf-8")
report("meson builds _kernel_fast", "'_kernel_fast'" in mb, True)
report("meson builds dqrsl", "'dqrsl'" in mb, True)

if bad:
    sys.stderr.write("checks: independent scan mismatch\n")
    sys.exit(1)
print("independent scan: numpy.distutils gone, meson-python backend in place")
PYEOF

echo "--- step 5 golden produced"
