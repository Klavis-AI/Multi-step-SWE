"""Shared setup for the migrated-ness gates.

A gate measures a property of the tree rather than of a running server, so the
gates need compiler output where the behaviour scenarios need a listening
socket. `build_dir` is where that output comes from: a build the verifier does
once, before any scenario starts, or -- when a gate is run on its own -- one the
gate does for itself.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build  # noqa: E402
import norm  # noqa: E402

# Set by the verifier to a tree already built with `-save-temps=obj`, so six
# gates do not do six identical builds.
BUILD_ENV = "HARBOR_GATE_BUILD"

# Set by the verifier to a second copy built with plain `make` -- nothing on the
# command line. See `plain_dir`.
PLAIN_ENV = "HARBOR_PLAIN_BUILD"

# The sources that carry an address. Named rather than discovered so that the
# observation names are the same for every tree: a per-file count that only
# exists when the file has a finding would let a candidate score by deleting
# the file.
FOCUS_FILES = (
    "src/anet.c",
    "src/anet.h",
    "src/cluster.c",
    "src/config.c",
    "src/networking.c",
    "src/redis.c",
    "src/redis-benchmark.c",
    "src/redis-cli.c",
    "src/replication.c",
    "src/sentinel.c",
    "deps/hiredis/hiredis.c",
    "deps/hiredis/net.c",
)

# Where a raw string comparison of two addresses is a defect. `anet.c` and its
# header are missing on purpose: they are where a shared comparison belongs, so
# a call left there is the implementation rather than a site still needing one.
# Naming the exempt file rather than the exempt function is what keeps the cell
# from also dictating what a solution calls its helper.
COMPARE_FILES = tuple(
    f for f in FOCUS_FILES if f not in ("src/anet.c", "src/anet.h"))

# Where `.i` files are looked for when the build did not say. Two directories
# was a claim about which part of the tree the migration touches, and that is
# the candidate's decision rather than this file's: the instruction puts every
# file the repository ships in scope, vendored trees included and nothing
# carved out, so a socket opened in `deps/lua` is in scope under the
# instruction and out of scope under a literal. Kept only as the fallback
# below.
SCAN_DIRS = ("src", "deps/hiredis")


def scan_dirs(dest):
    """The directories this build left preprocessed output in.

    `build.savetemps_dirs` already walked the build and recorded them, so the
    scan follows the build instead of a list somebody has to remember to widen.

    A missing or empty report falls back to `SCAN_DIRS`. That is not
    decoration: reading no directories at all reads no units, and a gate that
    read nothing would report a tree with no findings in it. Scanning the two
    directories the migration is known to touch is the smaller of the two
    mistakes, and `gate/static/units-scanned` and `gate/static/scanner-faults`
    are what say which one happened.
    """
    try:
        dirs = build.load_report(dest).get("savetemps-dirs") or ()
    except Exception:  # noqa: BLE001 - any unreadable report is the fallback
        dirs = ()
    return tuple(dirs) or SCAN_DIRS


# Families it is legitimate to name in source. AF_UNSPEC leaves the choice to
# the resolver, AF_UNIX is not an internet family at all, and AF_INET6 is the
# family the tree is being moved to.
ALLOWED_FAMILIES = ("AF_UNSPEC", "AF_UNIX", "AF_INET6")


def build_dir(ctx, cflags=()):
    """A tree built with its intermediates kept, ready to be read.

    Registered for masking on the way out: the findings are repo-relative by
    construction, but a compiler message is whatever the compiler felt like
    saying, and where the build happened is the runner's choice rather than the
    tree's property.
    """
    shared = os.environ.get(BUILD_ENV)
    if shared and os.path.exists(os.path.join(shared, build.REPORT)):
        dest = shared
    else:
        dest = os.path.join(ctx.workspace.base, "gatebuild")
        build.build(ctx.tree.root, dest, cflags=cflags)
    norm.register_path(dest, "<GATEBUILD>")
    return dest


def plain_dir(ctx):
    """A copy of the tree built the way the project documents building it.

    `build_dir` passes `-save-temps=obj` through `CFLAGS` on the `make` command
    line, because the gates need preprocessed sources. That makes it an
    instrument for one of the things the step asks -- a flag given that way
    still reaches every compilation -- and not for the other one, which is that
    the tree builds at all. The two come apart in both directions: a tree that
    hard-codes its own `CFLAGS` builds plainly and loses the flag the scorer
    passed, and a tree that only compiles because of something the scorer
    passed does the opposite. So the plain build is a second build, and it is
    the one whose `make` line is the project's own.
    """
    shared = os.environ.get(PLAIN_ENV)
    if shared and os.path.exists(os.path.join(shared, build.REPORT)):
        dest = shared
    else:
        dest = os.path.join(ctx.workspace.base, "plainbuild")
        build.build(ctx.tree.root, dest, savetemps=False)
    norm.register_path(dest, "<PLAINBUILD>")
    return dest


def sites(pairs):
    """A finding set rendered as a sorted list of lists, for comparison."""
    return sorted([list(p) for p in pairs])
