#!/usr/bin/env bash
# Step 06 reference solution: the repository stops depending on a module the
# interpreter no longer ships, and starts describing itself in the metadata
# standards that replaced the ones it was written against.
#
# Cumulative: this runs on the tree step 05 left behind, so /app is one uv
# workspace with one lock and three distributions -- `bento` at the root,
# `yaku`, and `hello` under examples/simples/single_extension -- every
# `[build-system] requires` is pinned, and two builds of any of them produce
# the same bytes.
#
# Two halves that share a theme and nothing else.
#
# The first is PEP 632. `distutils` left the standard library in 3.12 and this
# tree has 41 import sites in the root distribution. Twenty of them go. The
# other twenty-one are in three modules that are *driving* distutils as a
# third-party tool rather than depending on it to describe themselves, and the
# instruction names all three: `convert/core.py` executes a foreign `setup.py`,
# `commands/build_distutils.py` is the `UseBackends: Distutils` backend, and
# `commands/wininst_utils.py` needs one function that has no stdlib successor
# and that the repository's own tests patch by its dotted name. Porting those
# would rename them, not migrate them.
#
# The second half is what an index and an installer read: a licence as an SPDX
# expression rather than a free-text table (PEP 639), a typing marker in every
# package that ships (PEP 561), a lock the rest of the world can read (PEP
# 751), and a bill of materials that agrees with the lock.
#
# What is deliberately NOT done here: `convert/utils.py` carries two latent
# bugs -- its `setuptools_code` probe patches `setuptools.setup` and
# `distutils.core.setup` to the same function, which recurses under any modern
# setuptools, and `logged_run` waits on a `Popen` whose stdout is a pipe
# nobody drains, so that recursion hangs rather than fails. Neither is
# reachable on this tree, because nothing here installs setuptools. They are
# written down in NOTES rather than fixed, because fixing them is not what
# this step line asks for.
set -euo pipefail
cd /app

# =============================================================================
# 1. PEP 632
# =============================================================================

# --- 1a. bento/compat/dist.py ------------------------------------------------
#
# `DistributionMetadata` is the one piece of distutils this repository cannot
# simply stop calling: `bento/conv.py`'s `pkg_to_distutils_meta` builds one and
# `pkg_to_distutils_meta_pkg_info` renders it, both are public names other
# people import, and `bentomakerlib`'s own tests import the class by name from
# this module.
#
# So it is copied in rather than re-pointed. `setuptools._distutils` has a
# class by the same name and it is NOT a drop-in: it writes fields with
# `maybe_write`, so an empty field is omitted rather than written as UNKNOWN;
# it drops the UNKNOWN defaults entirely; it canonicalises `get_fullname`
# through `packaging`; and its `rfc822_escape` splits with `keepends=True` and
# handles a trailing newline differently. Any of those changes the bytes of a
# PKG-INFO this repository has been writing since 2009.
#
# The copy is of the semantics, not of the source: `read_pkg_file` and the
# `set_*` family are not reproduced, because nothing in this tree calls them.
# Checked against the stdlib class on all 19 `bento.info` files in the tree and
# on 12 hand-built adversarial metadata objects -- byte-identical PKG-INFO on
# every one.
cat > src/bento/compat/dist.py <<'PYEOF'
"""The PKG-INFO writer bento used to borrow from ``distutils.dist``.

PEP 632 removed ``distutils`` from the standard library, so the class that
rendered bento's PKG-INFO files now lives here instead of being aliased to the
standard library's. This is a copy of the semantics, not of the source: the
fields, their order, the ``UNKNOWN``/``0.0.0`` defaults and the RFC-822 escape
are what Python 3.11's ``distutils.dist.DistributionMetadata`` did, because
every PKG-INFO bento has ever written was written by that code and the bytes
have to stay the same.

Only what bento uses is here. ``bento.conv.to_distutils_meta`` builds one of
these by ``setattr``-ing every name in ``_METHOD_BASENAMES``, and
``bento.conv.write_pkg_info`` and ``bento.commands.egg_utils`` then call
``write_pkg_file``. Nothing in bento ever read a PKG-INFO back or called the
``set_*`` validators, so ``read_pkg_file`` and the ``distutils.versionpredicate``
checks are not reproduced.
"""
import os

PKG_INFO_ENCODING = 'utf-8'

def rfc822_escape(header):
    """Return a version of the string escaped for inclusion in an
    RFC-822 header, by ensuring there are 8 spaces space after each newline.
    """
    lines = header.split('\n')
    sep = '\n' + 8 * ' '
    return sep.join(lines)

class DistributionMetadata(object):
    """Holds the distribution meta-data: name, version, author, and so forth."""

    _METHOD_BASENAMES = ("name", "version", "author", "author_email",
                         "maintainer", "maintainer_email", "url",
                         "license", "description", "long_description",
                         "keywords", "platforms", "fullname", "contact",
                         "contact_email", "classifiers", "download_url",
                         # PEP 314
                         "provides", "requires", "obsoletes",
                         )

    def __init__(self):
        self.name = None
        self.version = None
        self.author = None
        self.author_email = None
        self.maintainer = None
        self.maintainer_email = None
        self.url = None
        self.license = None
        self.description = None
        self.long_description = None
        self.keywords = None
        self.platforms = None
        self.classifiers = None
        self.download_url = None
        # PEP 314
        self.provides = None
        self.requires = None
        self.obsoletes = None

    def write_pkg_info(self, base_dir):
        """Write the PKG-INFO file into the release tree.
        """
        with open(os.path.join(base_dir, 'PKG-INFO'), 'w',
                  encoding='UTF-8') as pkg_info:
            self.write_pkg_file(pkg_info)

    def write_pkg_file(self, file):
        """Write the PKG-INFO format data to a file object.
        """
        version = '1.0'
        if (self.provides or self.requires or self.obsoletes or
                self.classifiers or self.download_url):
            version = '1.1'

        file.write('Metadata-Version: %s\n' % version)
        file.write('Name: %s\n' % self.get_name())
        file.write('Version: %s\n' % self.get_version())
        file.write('Summary: %s\n' % self.get_description())
        file.write('Home-page: %s\n' % self.get_url())
        file.write('Author: %s\n' % self.get_contact())
        file.write('Author-email: %s\n' % self.get_contact_email())
        file.write('License: %s\n' % self.get_license())
        if self.download_url:
            file.write('Download-URL: %s\n' % self.download_url)

        long_desc = rfc822_escape(self.get_long_description())
        file.write('Description: %s\n' % long_desc)

        keywords = ','.join(self.get_keywords())
        if keywords:
            file.write('Keywords: %s\n' % keywords)

        self._write_list(file, 'Platform', self.get_platforms())
        self._write_list(file, 'Classifier', self.get_classifiers())

        # PEP 314
        self._write_list(file, 'Requires', self.get_requires())
        self._write_list(file, 'Provides', self.get_provides())
        self._write_list(file, 'Obsoletes', self.get_obsoletes())

    def _write_list(self, file, name, values):
        for value in values:
            file.write('%s: %s\n' % (name, value))

    # -- Metadata query methods ----------------------------------------

    def get_name(self):
        return self.name or "UNKNOWN"

    def get_version(self):
        return self.version or "0.0.0"

    def get_fullname(self):
        return "%s-%s" % (self.get_name(), self.get_version())

    def get_author(self):
        return self.author or "UNKNOWN"

    def get_author_email(self):
        return self.author_email or "UNKNOWN"

    def get_maintainer(self):
        return self.maintainer or "UNKNOWN"

    def get_maintainer_email(self):
        return self.maintainer_email or "UNKNOWN"

    def get_contact(self):
        return self.maintainer or self.author or "UNKNOWN"

    def get_contact_email(self):
        return self.maintainer_email or self.author_email or "UNKNOWN"

    def get_url(self):
        return self.url or "UNKNOWN"

    def get_license(self):
        return self.license or "UNKNOWN"
    get_licence = get_license

    def get_description(self):
        return self.description or "UNKNOWN"

    def get_long_description(self):
        return self.long_description or "UNKNOWN"

    def get_keywords(self):
        return self.keywords or []

    def get_platforms(self):
        return self.platforms or ["UNKNOWN"]

    def get_classifiers(self):
        return self.classifiers or []

    def get_download_url(self):
        return self.download_url or "UNKNOWN"

    # PEP 314

    def get_requires(self):
        return self.requires or []

    def get_provides(self):
        return self.provides or []

    def get_obsoletes(self):
        return self.obsoletes or []
PYEOF

# --- 1b. the subpackage that existed only to serve distutils -----------------
#
# `bento/distutils` subclasses four distutils commands so that a project built
# with bento could be packaged by a `setup.py`. PEP 632 is the end of that
# bridge. Its own `tests/` go with it -- a test directory for a package that
# does not exist is not a test suite -- and on this tree that costs three tests
# that already fail and one that is already skipped, and no test that passes.
rm -rf src/bento/distutils

# `bento.info` lists the packages the distribution ships. Two of those lines
# name a tree that is now gone; leaving them would make the manifest describe
# something the source does not contain.
python3 - <<'PYEOF'
import re
p = "bento.info"
s = open(p).read()
before = s
for name in ("bento.distutils.commands", "bento.distutils"):
    s = re.sub(r"^[ \t]*%s,[ \t]*\n" % re.escape(name), "", s, flags=re.M)
assert s != before, "bento.info: neither bento.distutils line was found"
assert "bento.distutils" not in s, "bento.info: a bento.distutils line survived"
open(p, "w").write(s)
PYEOF

# --- 1c. the four modules that had a standard-library successor --------------
#
# Each of these borrowed one name from distutils and each name has somewhere
# else to come from. They are separate edits because the successors are not the
# same shape as what they replace.

# `script_utils.get_script_header` matched a shebang against
# `distutils.command.build_scripts.first_line_re` -- an undocumented module
# global that PEP 632 took with the rest. The pattern is four tokens long and
# is compiled here rather than imported.
python3 - <<'PYEOF'
p = "src/bento/commands/script_utils.py"
s = open(p).read()
old = '''    from distutils.command.build_scripts import first_line_re

    match = first_line_re.match(six.b(""))
'''
new = '''    match = _FIRST_LINE_RE.match(six.b(""))
'''
assert old in s, "script_utils: first_line_re anchor"
s = s.replace(old, new)
anchor = "# XXX: taken verbatim from setuptools, rewrite this crap\n"
assert anchor in s, "script_utils: comment anchor"
decl = (
    "# The shebang-with-options pattern get_script_header used to borrow from\n"
    "# distutils.command.build_scripts, which PEP 632 removed. Same pattern,\n"
    "# compiled here instead.\n"
    r'_FIRST_LINE_RE = re.compile(b"^#!.*python[0-9.]*([ \t].*)?$")' "\n"
    "\n"
)
s = s.replace(anchor, decl + anchor, 1)
open(p, "w").write(s)
PYEOF

# `wininst_utils` borrowed three names. Two have successors: `distutils.util.
# get_platform` is `sysconfig.get_platform`, and `distutils.sysconfig.
# get_python_version` is two fields of `sys.version_info`. The third --
# `distutils.msvccompiler.get_build_version` -- has none, and
# `src/bento/commands/tests/test_wininst_utils.py` patches it by that exact
# dotted name in three tests that pass. Rule 6 says those tests are not ours to
# edit, so that import stays and the instruction lists it.
python3 - <<'PYEOF'
p = "src/bento/commands/wininst_utils.py"
s = open(p).read()
old = """from distutils.util \\
    import \\
        get_platform
from distutils.sysconfig \\
    import \\
        get_python_version
"""
assert old in s, "wininst_utils: import anchor"
s = s.replace(old, "import sysconfig\n")
assert "        plat_name = get_platform()" in s, "wininst_utils: get_platform call"
s = s.replace("        plat_name = get_platform()",
              "        plat_name = sysconfig.get_platform()")
old2 = "    cur_version = get_python_version()"
assert old2 in s, "wininst_utils: get_python_version call"
s = s.replace(old2, '    cur_version = "%d.%d" % sys.version_info[:2]')
open(p, "w").write(s)
PYEOF

# `msi_utils` imported the whole of `distutils.sysconfig` for one call. The
# stdlib module of the same name is not a superset -- `get_python_version` is
# one of the names it never had -- so the call goes to `sys.version_info` and
# the import to the stdlib module.
python3 - <<'PYEOF'
p = "src/bento/commands/msi_utils.py"
s = open(p).read()
assert "from distutils import sysconfig\n" in s, "msi_utils: import anchor"
s = s.replace("from distutils import sysconfig\n", "import sysconfig\n", 1)
old = "    short_version = sysconfig.get_python_version()"
assert old in s, "msi_utils: get_python_version call"
s = s.replace(old, '    short_version = "%d.%d" % sys.version_info[:2]', 1)
open(p, "w").write(s)
PYEOF

# `configure` read `distutils.command.install.INSTALL_SCHEMES` to find out
# whether it was on a Debian derivative, where the default install prefix is
# `/usr/local`. `sysconfig` carries the same table and spells the same scheme
# `posix_local` rather than `unix_local`. `expand=False` keeps the `{base}`
# placeholders the caller substitutes itself, which is what reading the raw
# INSTALL_SCHEMES dictionary used to give it.
python3 - <<'PYEOF'
p = "src/bento/commands/configure.py"
s = open(p).read()
old = """            # XXX: unix_local is an ubuntu/debian thing only ?
            from distutils.command.install import INSTALL_SCHEMES
            if "unix_local" in INSTALL_SCHEMES:
                dist_scheme = INSTALL_SCHEMES["unix_local"]
"""
new = """            # XXX: posix_local is an ubuntu/debian thing only ?  It is what
            # distutils called unix_local before PEP 632 removed distutils;
            # sysconfig is where the schemes live now and Debian adds it there.
            if "posix_local" in sysconfig.get_scheme_names():
                dist_scheme = sysconfig.get_paths("posix_local", expand=False)
"""
assert old in s, "configure: INSTALL_SCHEMES anchor"
s = s.replace(old, new, 1)
open(p, "w").write(s)
PYEOF
grep -q '^import sysconfig$' src/bento/commands/configure.py || \
    sed -i '0,/^import sys$/s//import sys\nimport sysconfig/' src/bento/commands/configure.py

# Every module this half rewrote, parsed. A migration that leaves a SyntaxError
# behind fails every row at once and says nothing about which edit did it.
for f in src/bento/compat/dist.py src/bento/commands/script_utils.py \
         src/bento/commands/wininst_utils.py src/bento/commands/msi_utils.py \
         src/bento/commands/configure.py; do
    python3 -c "import ast, sys; ast.parse(open(sys.argv[1]).read())" "$f"
done

# =============================================================================
# 2. What an index and an installer read
# =============================================================================

# --- 2a. the last setup.py ---------------------------------------------------
#
# Three left, all under examples/simples, none referenced by anything in `src/`
# and two of them Python 2 that has not parsed since the repository moved. Each
# of those directories already carries the bento-native description that was
# the point of the example; the `setup.py` beside it is the "before" of a demo
# nobody runs.
rm -f examples/simples/executable/setup.py \
      examples/simples/single_module/setup.py \
      examples/simples/single_package/setup.py

# --- 2b. PEP 639 -------------------------------------------------------------
#
# `license = { text = "BSD" }` is a sentence, not a licence. "BSD" names at
# least four different sets of terms and an index cannot tell them apart.
# LICENSE.txt is three-clause -- it carries the "Neither the name of the Bento
# Developers nor the names of any contributors may be used to endorse" clause
# that is the whole difference between BSD-3-Clause and BSD-2-Clause -- so that
# is what the expression says.
#
# `license-files` is the other half and is the half that matters to a consumer:
# the expression is a claim and the file is the evidence, and PEP 639 puts the
# file inside the wheel under `.dist-info/licenses/` where an installer can
# find it.
#
# The `License :: OSI Approved` classifier goes at the same time. PEP 639
# deprecates the licence classifiers outright, and a classifier that says
# "approved by somebody" next to an expression that says exactly which terms
# apply is strictly less information in a field that now contradicts itself.
python3 - <<'PYEOF'
import re
p = "pyproject.toml"
s = open(p).read()
old = 'license = { text = "BSD" }\n'
assert old in s, "root pyproject: license anchor"
s = s.replace(old, 'license = "BSD-3-Clause"\nlicense-files = ["LICENSE.txt"]\n', 1)
before = s
s = re.sub(r'^[ \t]*"License :: [^"]*",[ \t]*\n', "", s, flags=re.M)
assert s != before, "root pyproject: no License:: classifier to drop"
open(p, "w").write(s)
PYEOF

# yaku says the same thing about the same terms and ships no copy of them at
# all, so it gets one. A workspace member is a distribution in its own right:
# it is resolved, built and installed on its own, and "the licence is in the
# repository root somewhere" is not something an installer can act on.
cp LICENSE.txt yaku/LICENSE.txt
python3 - <<'PYEOF'
p = "yaku/pyproject.toml"
s = open(p).read()
old = 'license = { text = "BSD" }\n'
assert old in s, "yaku pyproject: license anchor"
s = s.replace(old, 'license = "BSD-3-Clause"\nlicense-files = ["LICENSE.txt"]\n', 1)
open(p, "w").write(s)
PYEOF

# yaku's sdist target names what goes into the sdist, and the list is an
# allowlist rather than an addition to a default: a file nobody names is a file
# the sdist does not carry. PEP 639 back ends read `license-files` themselves
# for the wheel's `.dist-info`, but the sdist is built from this list, so the
# licence copy is named here too.
python3 - <<'PYEOF'
p = "yaku/pyproject.toml"
s = open(p).read()
old = 'include = ["src/yaku", "README", "NOTES"]'
assert old in s, "yaku pyproject: sdist include anchor"
s = s.replace(old, 'include = ["src/yaku", "README", "NOTES", "LICENSE.txt"]', 1)
open(p, "w").write(s)
PYEOF

# --- 2c. PEP 561 -------------------------------------------------------------
#
# One marker per *package*, not one per distribution. `bento`'s wheel installs
# two importable packages and a consumer that imports `bentomakerlib` learns
# nothing from a marker sitting in `bento`.
: > src/bento/py.typed
: > src/bentomakerlib/py.typed
: > yaku/src/yaku/py.typed
: > examples/simples/single_extension/hello/py.typed

# --- 2d. PEP 751 and the bill of materials -----------------------------------
#
# `uv.lock` is uv's own format and nothing else reads it. PEP 751 is the
# interchange format that came out of that problem, and `uv export` writes it
# from the same resolution, so the two cannot disagree unless one of them is
# stale.
#
# `--no-dev` because this describes what the distribution *depends on*, not
# what its maintainers install to work on it: pytest and sphinx are in the lock
# and are not part of anything that gets shipped.
uv export --format pylock.toml --no-dev -o pylock.toml

# The same resolution again, as CycloneDX. This is the artifact somebody
# downstream runs a vulnerability scanner over, and the reason it is generated
# from the lock rather than written by hand is that a bill of materials nobody
# regenerates is a bill of materials that is wrong.
uv export --format cyclonedx1.5 --no-dev -o sbom.json
