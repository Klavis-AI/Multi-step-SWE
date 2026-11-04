"""The tree builds, with plain `make`, on this decade's toolchain.

The pinned baseline does not. It predates glibc 2.32 removing <sys/sysctl.h>
and gcc 10 defaulting to -fno-common, so `make` stops before it has produced a
single binary. Every other scenario in the suite depends on this one having
succeeded, which is why it is the first gate and why it is scored from step 1
onward: a tree that stopped building has not stayed migrated.

Two builds are read, not one: the one the gates share, which passes
`-save-temps=obj` on the `make` command line, and a second built with plain
`make` and nothing after it. The step asks for both and neither implies the
other -- see `gate.plain_dir`.

Both are builds of a *cleaned* copy. A candidate hands over the tree it was
working in, objects and all, and `make` on a tree whose objects are newer than
its sources exits 0 without compiling anything -- so "it builds" would be true
of a tree that no longer compiles. `lib/build.py` runs the project's own
`distclean` on the copy first and records what survived it.

What is recorded is what the build produced, not what it printed. `make -j`
interleaves the output of however many compilers were running, so the text is
in a different order every time; the set of binaries, the set of errors and the
set of implicit declarations are not.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import build  # noqa: E402
import gate  # noqa: E402

PORTS = 0
FAMILY = "gate"

# The one warning class that says the migration itself is wrong rather than
# merely untidy: a call to `getaddrinfo` or `inet_ntop` whose header was never
# included compiles, links and then truncates a pointer at runtime.
IMPLICIT = "-Wimplicit-function-declaration"


def run(ctx):
    d = gate.build_dir(ctx)
    r = build.load_report(d)

    ctx.emit("gate/build/rc", r["rc"])
    ctx.emit("gate/build/binaries", r["binaries"])
    # `CFLAGS` given on the `make` command line has to reach every compilation,
    # in `src/` and in `deps/`. The build the gates do passes `-save-temps=obj`
    # that way and nothing else, so the directories that came back holding
    # intermediates are the directories the flag arrived in. A tree that hard-
    # codes its own flags, or stops passing them down to `deps/`, loses one.
    ctx.emit("gate/build/savetemps-dirs", r.get("savetemps-dirs", []))
    # Both builds start by asking the tree to clean itself, because `make` on a
    # tree that was handed over already built has nothing to do and reports rc 0
    # for it. What survived that clean is scored, so a tree whose `distclean`
    # stopped working cannot quietly turn both builds into no-ops.
    ctx.emit("gate/build/clean-residue", r.get("residue", ["<no report>"]))

    diags = r["diagnostics"]
    errors = sorted({(x["file"], x["text"]) for x in diags
                     if x["severity"] == "error"})
    ctx.emit("gate/build/errors", gate.sites(errors))
    ctx.emit("gate/build/error-count", len(errors))

    implicit = sorted({(x["file"], x["text"]) for x in diags
                       if x["tag"] == IMPLICIT})
    ctx.emit("gate/build/implicit-declarations", gate.sites(implicit), adv=True)

    # The same tree, built the way its own README says to build it: `make`,
    # with nothing after it. The build above is the one the gates read, and it
    # names a flag on the command line, so on its own it never asks whether
    # plain `make` works -- which is the first sentence of the step.
    q = build.load_report(gate.plain_dir(ctx))
    ctx.emit("gate/build/plain/rc", q["rc"])
    ctx.emit("gate/build/plain/binaries", q["binaries"])
    ctx.emit("gate/build/plain/error-count",
             len({(x["file"], x["text"]) for x in q["diagnostics"]
                  if x["severity"] == "error"}))

    # Per binary, so that a tree which builds five of the six does not score as
    # though the sixth were a rounding error. `redis-check-rdb` and
    # `redis-check-aof` are the two that are easiest to forget, being hard links
    # to the server rather than programs of their own.
    for b in build.BINARIES:
        ctx.emit("gate/build/have/%s" % b, b in r["binaries"])
