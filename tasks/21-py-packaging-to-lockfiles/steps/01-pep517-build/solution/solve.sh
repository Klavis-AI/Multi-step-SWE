#!/usr/bin/env bash
# Step 01 reference solution: declare the build in pyproject.toml and delete
# setup.py.
#
# Every field below is a restatement of something bento.info already says. The
# distribution's own metadata generators were asked what the pinned tree
# declares (`PackageDescription.from_file("bento.info")`), and this file says
# the same things in PEP 621 spelling. Nothing is invented: there is no
# `requires-python` because bento.info does not state one.
set -euo pipefail
cd /app

cat > pyproject.toml <<'EOF'
[build-system]
requires = ["setuptools>=77"]
build-backend = "setuptools.build_meta"

[project]
name = "bento"
version = "0.2.0"
description = "A tool to nicely packages your python software"
readme = { file = "README.rst", content-type = "text/x-rst" }
license = { text = "BSD" }
keywords = ["packaging"]
authors = [{ name = "David Cournapeau", email = "cournape@gmail.com" }]
maintainers = [{ name = "David Cournapeau", email = "cournape@gmail.com" }]
classifiers = [
    "Development Status :: 1 - Planning",
    "Intended Audience :: Developers",
    "License :: OSI Approved",
    "Programming Language :: Python",
    "Topic :: Software Development",
    "Operating System :: Microsoft :: Windows",
    "Operating System :: POSIX",
    "Operating System :: Unix",
    "Operating System :: MacOS",
]

[project.urls]
Homepage = "http://github.com/cournape/Bento"

[project.scripts]
bentomaker = "bentomakerlib.bentomaker:noexc_main"

[tool.setuptools]
license-files = ["LICENSE.txt"]
include-package-data = true

# `namespaces = true`, and not the default, because four of the directories
# this distribution has to ship have no __init__.py of their own:
# bento/commands/wininst (the Windows launcher stubs), and the vendored
# bento/private/_ply, _simplejson and _yaku trees whose importable packages sit
# one level below. Plain find_packages() drops all four, which imports cleanly
# from a checkout and fails the moment anything runs from the installed wheel.
[tool.setuptools.packages.find]
where = ["."]
include = ["bento*", "bentomakerlib*"]
exclude = ["bento.private._yaku.examples*", "bento.private._yaku.tools*"]
namespaces = true

[tool.setuptools.package-data]
"bento" = ["__package_info.py.in"]
"bento.commands" = ["cli.exe"]
"bento.commands.wininst" = ["*.exe"]
"bento.parser.tests" = ["functionals/*.info"]
"bento.testing" = ["bentos/*.info", "bentos/*.py"]
EOF

# The source files bento.info lists under ExtraSourceFiles that are not inside
# a package, plus the two the build itself needs to be reproducible from.
cat > MANIFEST.in <<'EOF'
include bento.info bscript
include INSTALL LICENSE.txt PACKAGERS.txt README.rst TESTING.txt THANKS
include bento/__package_info.py.in
recursive-include bento/commands *.exe
recursive-include bento/parser/tests/functionals *.info
recursive-include bento/testing/bentos *.py *.info
EOF

# The whole point of the step: this tree no longer builds itself with itself.
rm -f setup.py
