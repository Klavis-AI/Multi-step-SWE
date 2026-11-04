#!/usr/bin/env bash
# Step 05 reference solution: two builds of this workspace produce the same
# bytes, every timestamp in every artifact is the epoch the build was handed,
# every build requirement names one version, and the version that is named is
# the version that runs.
#
# Cumulative: this runs on the tree step 04 left behind, so /app is one uv
# workspace with one lock and three distributions -- `bento` at the root,
# `yaku`, and `hello` under examples/simples/single_extension -- and `hello`
# already builds an abi3 wheel and a free-threaded one.
#
# There are two independent changes here and it is worth saying which is which,
# because only one of them is a choice:
#
#   * pinning every `[build-system] requires` is forced. Nothing else makes the
#     build environment a fixed input, and an unpinned `requires` is why the
#     wheels this tree builds today say `Generator: setuptools (84.0.0)` while
#     the image has 79.0.1 installed -- PEP 517 build isolation resolved the
#     back end from the index;
#   * changing the back end of the two pure-Python distributions is a choice,
#     and it is made because setuptools' sdist writer cannot be made
#     deterministic from the tree. It stamps the files it *generates* --
#     PKG-INFO, setup.cfg, the egg-info directory -- with the wall clock, and
#     stamps only the files it *copies* with SOURCE_DATE_EPOCH. Two builds a
#     second apart differ in about forty members, every one of them by mtime
#     and none by content. There is no key, no flag and no environment variable
#     that turns that off, so the only route to `two builds, the same bytes` is
#     a back end whose archive writer is deterministic.
#
# `hello` keeps scikit-build-core, which already normalises every timestamp in
# both archives to whatever SOURCE_DATE_EPOCH it is handed. It needed a pin and
# nothing else.
set -euo pipefail
cd /app

# --- 1. the root distribution ------------------------------------------------
#
# The [build-system] table is replaced and the three [tool.setuptools*] tables
# go with it: they configured a back end this distribution no longer uses, and
# leaving them would leave a second, silent description of what the wheel
# contains.
#
# hatchling needs less said to it because it is file-based rather than
# module-based. `packages` names two directories and everything inside them
# ships -- which is what `include-package-data = true` plus four
# [tool.setuptools.package-data] entries were arranging: bento/cli.exe, the
# wininst launcher stubs, __package_info.py.in, the parser's functional .info
# fixtures and the testing bentos. The wheel comes out with the same members as
# before except `top_level.txt`, which is a setuptools legacy file that no
# specification describes and nothing in this repository reads.
#
# The sdist list is explicit rather than left to the default. Without a .git to
# ask, hatchling's default is "everything under the project root", and the
# project root here is a monorepo: it would ship yaku, the examples, the
# documentation, the lock, and any .venv a previous command happened to leave
# behind. The list below is the root distribution's own sources plus the files
# bento.info has always named under ExtraSourceFiles.
PYTHONPYCACHEPREFIX=/tmp/pyc python3 - <<'PY'
import pathlib
import re
import tomllib

p = pathlib.Path("pyproject.toml")
text = p.read_text()

build_system = '''[build-system]
requires = ["hatchling==1.32.0"]
build-backend = "hatchling.build"
'''
text, n = re.subn(r"(?s)\A\[build-system\].*?\n(?=\[project\])",
                  build_system + "\n", text, count=1)
assert n == 1, "the [build-system] table was not where it was expected"

# The setuptools tables, from the first of them to the next table that is not
# one. They are contiguous and they end the file's tool section.
text, n = re.subn(r"(?s)\n# `namespaces = true`.*?\n(?=\n# One workspace)",
                  "\n", text, count=1)
assert n == 1, "the [tool.setuptools.packages.find] block was not matched"
text, n = re.subn(r"(?s)\n\[tool\.setuptools\]\n.*?\n(?=\n)", "\n", text, count=1)
assert n == 1, "the [tool.setuptools] table was not matched"

text = text.rstrip("\n") + '''

# hatchling is file-based: naming the two package directories ships everything
# inside them, which is what include-package-data and four package-data entries
# were arranging for setuptools.
[tool.hatch.build.targets.wheel]
packages = ["src/bento", "src/bentomakerlib"]

# Explicit, because hatchling's default without a VCS to ask is the whole
# project root, and the project root of a monorepo is not one distribution's
# sources. These are bento's own, plus the files bento.info lists under
# ExtraSourceFiles.
[tool.hatch.build.targets.sdist]
include = [
    "src/bento",
    "src/bentomakerlib",
    "bento.info",
    "bscript",
    "INSTALL",
    "LICENSE.txt",
    "PACKAGERS.txt",
    "README.rst",
    "TESTING.txt",
    "THANKS",
]
'''
p.write_text(text)

data = tomllib.loads(p.read_text())
assert data["build-system"]["requires"] == ["hatchling==1.32.0"]
assert data["build-system"]["build-backend"] == "hatchling.build"
assert "setuptools" not in data.get("tool", {}), "a setuptools table survived"
hatch = data["tool"]["hatch"]["build"]["targets"]
assert hatch["wheel"]["packages"] == ["src/bento", "src/bentomakerlib"]
assert "src/bento" in hatch["sdist"]["include"]
# Everything step 03 put here is still here.
assert data["project"]["dependencies"][0] == "ply==3.4"
assert data["tool"]["uv"]["sources"]["yaku"] == {"workspace": True}
assert data["project"]["scripts"]["bentomaker"] == \
    "bentomakerlib.bentomaker:noexc_main"
print("pyproject.toml: hatchling, pinned; setuptools configuration removed")
PY

# --- 2. yaku, the same change for the same reason ----------------------------
PYTHONPYCACHEPREFIX=/tmp/pyc python3 - <<'PY'
import pathlib
import re
import tomllib

p = pathlib.Path("yaku/pyproject.toml")
text = p.read_text()

text, n = re.subn(r"(?s)\A\[build-system\]\nrequires = .*?\nbuild-backend = .*?\n",
                  '[build-system]\nrequires = ["hatchling==1.32.0"]\n'
                  'build-backend = "hatchling.build"\n', text, count=1)
assert n == 1, "yaku's [build-system] table was not where it was expected"

text, n = re.subn(r"(?s)\n\[tool\.setuptools\.packages\.find\]\n.*?\Z", "\n",
                  text, count=1)
assert n == 1, "yaku's [tool.setuptools.packages.find] table was not matched"

text = text.rstrip("\n") + '''

[tool.hatch.build.targets.wheel]
packages = ["src/yaku"]

[tool.hatch.build.targets.sdist]
include = ["src/yaku", "README", "NOTES"]
'''
p.write_text(text)

data = tomllib.loads(p.read_text())
assert data["build-system"]["requires"] == ["hatchling==1.32.0"]
assert "setuptools" not in data.get("tool", {})
assert data["tool"]["hatch"]["build"]["targets"]["wheel"]["packages"] == \
    ["src/yaku"]
assert data["project"]["name"] == "yaku"
assert data["project"]["version"] == "0.1.0"
print("yaku/pyproject.toml: hatchling, pinned")
PY

# --- 3. hello keeps its back end and gains a version -------------------------
#
# scikit-build-core already writes every timestamp in both archives as the
# epoch it is handed, and it is the only back end here that can compile C at
# all. All it was missing was the pin: `requires = ["scikit-build-core"]` has no
# bound of any kind, so the version that builds this extension is whatever the
# index is serving at the moment the build runs.
#
# cmake and ninja are not listed. scikit-build-core asks for them itself
# through the PEP 517 get_requires_for_build_wheel hook, after inspecting what
# is already on the machine; putting them in `requires` would be declaring a
# dependency this distribution does not have.
PYTHONPYCACHEPREFIX=/tmp/pyc python3 - <<'PY'
import pathlib
import tomllib

p = pathlib.Path("examples/simples/single_extension/pyproject.toml")
text = p.read_text()
before = 'requires = ["scikit-build-core"]'
assert text.count(before) == 1, "hello's requires line was not where it was expected"
p.write_text(text.replace(before, 'requires = ["scikit-build-core==1.0.3"]'))

data = tomllib.loads(p.read_text())
assert data["build-system"]["requires"] == ["scikit-build-core==1.0.3"]
assert data["build-system"]["build-backend"] == "scikit_build_core.build"
# Step 04's declarations are untouched.
assert data["tool"]["scikit-build"]["wheel"]["py-api"] == "cp311"
assert len(data["tool"]["scikit-build"]["overrides"]) == 1
print("hello: scikit-build-core pinned to the version the image carries")
PY

# MANIFEST.in stays, at the root and under hello, for the reason step 04 gave
# for hello's: hatchling does not read it, and removing a manifest is a
# different act from the one this milestone asked for.

# --- 4. every requirement in the tree is pinned, and nothing else is ---------
PYTHONPYCACHEPREFIX=/tmp/pyc python3 - <<'PY'
import pathlib
import tomllib

seen = []
for p in sorted(pathlib.Path(".").rglob("pyproject.toml")):
    if any(part in {".git", "build", "dist", "__pycache__", ".venv"}
           for part in p.parts):
        continue
    table = tomllib.loads(p.read_text()).get("build-system")
    if not table:
        continue
    for req in table["requires"]:
        assert "==" in req, "%s: %r is not pinned" % (p, req)
        seen.append("%s -> %s" % (p, req))
assert len(seen) == 3, seen
print("\n".join("  " + s for s in seen))
PY

# --- 5. the lock follows the declarations ------------------------------------
uv lock
uv lock --check

# --- 6. two builds, from two directories, the same bytes --------------------
#
# The check that everything above is true, made the way the requirement is
# stated: two copies of the tree in different directories, the same epoch, and
# a comparison of the artifacts. Built into scratch directories and thrown
# away; this is a measurement, not a release.
#
# The sdists are compared after the gzip wrapper is taken off. A `.tar.gz` is
# an archive inside a container, the container's header carries an mtime by
# construction, and which front end wrote it is not a property of this tree.
EPOCH=1409491949
A=$(mktemp -d); B=$(mktemp -d); SRC_B=$(mktemp -d)
cp -a /app/. "$SRC_B/"
SOURCE_DATE_EPOCH=$EPOCH uv build --all-packages --out-dir "$A" >/dev/null
( cd "$SRC_B" && SOURCE_DATE_EPOCH=$EPOCH uv build --all-packages \
    --out-dir "$B" >/dev/null )

PYTHONPYCACHEPREFIX=/tmp/pyc python3 - "$A" "$B" "$EPOCH" <<'PY'
import calendar
import gzip
import hashlib
import pathlib
import sys
import tarfile
import zipfile

a, b, epoch = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), int(sys.argv[3])


def payload(p):
    """What the requirement compares: the file, or the tar inside the wrapper."""
    raw = p.read_bytes()
    return gzip.decompress(raw) if p.name.endswith(".tar.gz") else raw


def stamps(p):
    if p.name.endswith(".whl"):
        with zipfile.ZipFile(p) as z:
            return sorted({calendar.timegm(i.date_time + (0, 0, 0))
                           for i in z.infolist()})
    with tarfile.open(p, "r:*") as t:
        return sorted({int(m.mtime) for m in t.getmembers()})


# `uv build` writes a `.gitignore` into its output directory; the artifacts are
# what this compares.
DIST = (".whl", ".tar.gz")


def built(d):
    return sorted(p.name for p in d.iterdir() if p.name.endswith(DIST))


names = built(a)
assert names == built(b), "the two builds differ in what they produced"
assert len(names) == 6, names

even = epoch - (epoch % 2)
for name in names:
    pa, pb = a / name, b / name
    assert payload(pa) == payload(pb), "%s is not the same twice" % name
    want = even if name.endswith(".whl") else epoch
    bad = [v for v in stamps(pa) if v != want]
    assert not bad, "%s carries timestamps that are not %d: %s" % (name, want, bad[:4])
    print("  %-44s %s" % (name, hashlib.sha256(payload(pa)).hexdigest()[:16]))

# And the version that is pinned is the version that ran.
want = {"bento": "hatchling 1.32.0", "yaku": "hatchling 1.32.0",
        "hello": "scikit-build-core 1.0.3"}
for p in sorted(a.glob("*.whl")):
    dist = p.name.split("-")[0]
    with zipfile.ZipFile(p) as z:
        wheel = [n for n in z.namelist() if n.endswith(".dist-info/WHEEL")][0]
        gen = [l.split(": ", 1)[1] for l in z.read(wheel).decode().splitlines()
               if l.startswith("Generator: ")][0]
    assert gen == want[dist], "%s says %r, not %r" % (p.name, gen, want[dist])
print("  generators: " + ", ".join(sorted(set(want.values()))))
PY
rm -rf "$A" "$B" "$SRC_B"

# --- 7. handoff --------------------------------------------------------------
cat >> WORKSPACE_HANDOFF.md <<'EOF'

## Step 5 — builds that produce the same bytes twice

* `pyproject.toml` and `yaku/pyproject.toml` build with `hatchling==1.32.0`
  instead of `setuptools>=77`. setuptools' sdist writer stamps the files it
  generates (`PKG-INFO`, `setup.cfg`, `*.egg-info/*`) with the wall clock and
  only the files it copies with `SOURCE_DATE_EPOCH`, so two builds of the same
  sources a second apart differ in about forty members. Nothing in the tree
  turns that off. The wheel is unchanged except for `top_level.txt`, a
  setuptools legacy file no specification describes.
* The `[tool.setuptools]`, `[tool.setuptools.packages.find]` and
  `[tool.setuptools.package-data]` tables are gone, replaced by
  `[tool.hatch.build.targets.wheel] packages`. hatchling is file-based, so
  naming the package directories ships the data files that four `package-data`
  entries used to list.
* `[tool.hatch.build.targets.sdist] include` is explicit in both. Without a
  `.git` to ask, hatchling's default is the whole project root, and this
  project root is a monorepo.
* `examples/simples/single_extension/pyproject.toml` keeps
  `scikit_build_core.build` and pins it: `scikit-build-core==1.0.3`. It already
  normalised every timestamp in both archives to the epoch it was handed.
* All three `[build-system] requires` now name exact versions. Before this,
  the root wheel reported `Generator: setuptools (84.0.0)` against an image
  carrying 79.0.1 -- PEP 517 build isolation had resolved the back end from the
  index, and the tree had no way to say which build produced which artifact.
* `MANIFEST.in` is left where it is at the root and under `hello`. Neither back
  end reads it.
EOF

echo "step 05 complete"
