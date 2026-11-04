"""No address family is decided at compile time, and no IPv4-only type is used.

This is the gate the whole migration turns on, and it is the one that is
easiest to write wrongly. `AF_INET` is a macro: after preprocessing it is the
integer 2, and an AST scan for the name finds nothing whatsoever -- on the
baseline as readily as on a finished tree. So the family half of this gate
reads the preprocessed text the build kept, where the value survives even
though the name did not, and where `socket(2, ...)` written out by hand is
caught by exactly the same rule.

Three instruments, in order of how much they can be trusted:

  * the preprocessed AST -- what family reaches `socket()`, what family is
    assigned to a `sockaddr`, which IPv4-only structs and libc entry points are
    referenced, all attributed back to the `.c` line they came from;
  * the linked binaries -- which of those libc entry points actually made it
    into the import table, which no amount of dead code can fake;
  * a scrubbed-text backstop -- the spellings, over every source in the tree
    including the ones this platform does not compile, so that a construct
    parked behind `#ifdef __FreeBSD__` is still counted.

Two self-checks are scored alongside the findings, because a scanner that read
less than it thought it did would report a clean tree: the list of sources no
preprocessed output covers, and the list of scanner faults. Both are part of
the expectation, so a tree that stops emitting intermediates fails here rather
than passing quietly.
"""

import collections
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import build  # noqa: E402
import cscan  # noqa: E402
import gate  # noqa: E402
import textscan  # noqa: E402

PORTS = 0
FAMILY = "gate"

# The configuration files the pin ships at its root, kept in `tests/fixtures`
# the same way `pin-tests` is for the suite.
PIN_CONF = os.path.join(HERE, os.pardir, os.pardir, "fixtures", "pin-conf")


def _conf_files(root):
    """{name: bytes} for the `.conf` files at a tree's root."""
    out = {}
    try:
        names = os.listdir(root)
    except OSError:
        return out
    for name in names:
        if not name.endswith(".conf"):
            continue
        path = os.path.join(root, name)
        if os.path.islink(path) or not os.path.isfile(path):
            continue
        try:
            with open(path, "rb") as f:
                out[name] = f.read()
        except OSError:
            out[name] = None
    return out


# What a configuration file says, as opposed to how it spells it. A line whose
# first non-blank character is `#` is off; everything else is a directive, and
# the directive is its first word -- except in `sentinel.conf`, where the first
# word is the file's namespace and the second is the setting, so `sentinel
# monitor` and `sentinel failover-timeout` stay two directives rather than one.
NAMESPACES = ("sentinel",)


def _directives(data):
    """The directive names a config file has switched on, as a set."""
    out = set()
    if not data:
        return out
    for line in data.decode("utf-8", "replace").splitlines():
        words = line.split()
        if not words or words[0].startswith("#"):
            continue
        name = words[0].lower()
        if name in NAMESPACES and len(words) > 1:
            name = "%s %s" % (name, words[1].lower())
        out.add(name)
    return out


def conf_missing(pin, mine):
    """Shipped configuration files the tree under test no longer ships.

    Only files the pin has: a `.conf` of the candidate's own added beside them
    is not a change to the project's, while one deleted or renamed is. A rename
    shows up as the old name gone.
    """
    if not pin:
        return ["<pin-conf fixture missing>"]
    return sorted(k for k in pin if k not in mine)


def conf_directives_lost(pin, mine):
    """Settings the shipped configuration switched on and the tree's does not.

    The value of a setting is not read here. Which addresses `redis.conf` and
    `sentinel.conf` name is the subject of this migration and is supposed to
    move; whether they still name them at all is not, and a default that works
    on an IPv6 host because the line that constrains it was deleted is the one
    way of passing this step that is not doing it.
    """
    if not pin:
        return ["<pin-conf fixture missing>"]
    out = []
    for name in sorted(pin):
        lost = _directives(pin[name]) - _directives(mine.get(name))
        out.extend("%s:%s" % (name, d) for d in sorted(lost))
    return out


def _imports(path):
    """The undefined dynamic symbols of an ELF file, or None if unreadable."""
    try:
        p = subprocess.run(["nm", "-D", "--undefined-only", path],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    if p.returncode != 0:
        return None
    names = set()
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if parts and parts[-1]:
            names.add(parts[-1].split("@")[0])
    return names


def run(ctx):
    d = gate.build_dir(ctx)
    report = build.load_report(d)

    # -- the preprocessed AST ------------------------------------------------
    # Which directories to read comes from the build, not from a list here:
    # whatever `-save-temps=obj` reached is what there is to scan, so widening
    # the build widens the gate with it and `deps/` is in scope exactly as the
    # instruction says it is.
    f, units, missing = cscan.scan(d, gate.scan_dirs(d))
    ctx.emit("gate/static/scanner-faults", sorted(f.scanner_faults))
    ctx.emit("gate/static/unscanned-sources", sorted(f.unscanned))
    ctx.emit("gate/static/parse-errors",
             gate.sites((a, c) for a, _, c in f.parse_errors))
    ctx.emit("gate/static/units-scanned", len(units) > 0)

    # A family literal that is not AF_UNSPEC, AF_UNIX or AF_INET6 is a family
    # chosen when the code was written rather than when the name was resolved.
    bad = [x for x in f.family_literals if x[3] not in gate.ALLOWED_FAMILIES]
    ctx.emit("gate/static/family-literals", gate.sites(bad))
    ctx.emit("gate/static/ipv4-type-refs", gate.sites(f.ipv4_type_refs))
    ctx.emit("gate/static/ipv4-func-refs", gate.sites(f.ipv4_func_refs))
    # The other half of the same fact: how many family decisions are made from
    # a value rather than from a constant. A tree with no literals and no
    # dynamic sites either has no sockets or was not scanned.
    ctx.emit("gate/static/family-dynamic-count", len(f.family_dynamic) > 0,
             adv=True)

    # Per file as well as in total: a tree that cleaned up `anet.c` and left
    # `cluster.c` alone has done part of the work, and a single all-or-nothing
    # observation would score that the same as having done none of it.
    for rel in gate.FOCUS_FILES:
        ctx.emit("gate/static/by-file/%s/family-literals" % rel,
                 sum(1 for x in bad if x[0] == rel))
        ctx.emit("gate/static/by-file/%s/ipv4-type-refs" % rel,
                 sum(1 for x in f.ipv4_type_refs if x[0] == rel))
        ctx.emit("gate/static/by-file/%s/ipv4-func-refs" % rel,
                 sum(1 for x in f.ipv4_func_refs if x[0] == rel))

    # -- the linked binaries -------------------------------------------------
    for b in build.BINARIES:
        path = os.path.join(d, "src", b)
        imps = _imports(path)
        ctx.emit("gate/artifact/%s/readable" % b, imps is not None)
        if imps is None:
            continue
        # The forbidden names are pinned as an exact set: the instruction says
        # none of them is left in any of the six programs' dynamic imports, so
        # the set that is there is the finding.
        ctx.emit("gate/artifact/%s/ipv4-imports" % b,
                 sorted(imps & set(cscan.IPV4_FUNCS)))
        # The permitted names are not. What is demanded of the resolver side is
        # that a family-independent way of looking a host up is linked in, and
        # that is a presence, not a set: pinning the exact set would also
        # require whichever of `getnameinfo`, `inet_ntop` and `inet_pton` the
        # golden happened to end up with, and the instruction requires none of
        # them -- a conversion that takes the family from the address it was
        # handed satisfies it whichever of those it is spelled with. A tree
        # that rendered addresses with `inet_ntop` rather than `getnameinfo`
        # has done the work, and an observation it fails is not an observation
        # about the work.
        ctx.emit("gate/artifact/%s/resolves-without-family" % b,
                 bool(imps & set(cscan.RESOLVER_ENTRY)), adv=True)

    # -- the text backstop ---------------------------------------------------
    # Read from the tree itself rather than from the build copy: the build copy
    # has the intermediates in it, and a `#pragma` glibc wrote is not a finding
    # about this tree.
    root = ctx.tree.root
    # Every file the repository ships, vendored trees included. Excluding them
    # was measured rather than assumed to be free: at the pin they hold no
    # IPv4-only identifier at all, and the two `#if 0` blocks they do hold are
    # in the pin list below. Scanning them costs nothing and means the scope of
    # this gate is "the repository", with no carve-out to keep in step with the
    # instruction.
    files = textscan.walk(root)
    hits = textscan.identifier_hits(root, files)
    # By file and name rather than by line: which file still spells an IPv4-only
    # name is the finding, and a line number would make an unrelated edit above
    # it look like one.
    ctx.emit("gate/backstop/ipv4-identifiers",
             gate.sites({(a, c) for a, _, c in hits}))
    for rel in gate.FOCUS_FILES:
        ctx.emit("gate/backstop/by-file/%s" % rel,
                 sorted({c for a, _, c in hits if a == rel}))

    # A finding that was silenced is still a finding. What is scored
    # is what this tree *added*, not the total. The pin already parks dead code
    # behind `#if 0` in eight places, and a candidate that deleted one of them
    # while fixing the compile error above it has suppressed nothing; pinning
    # the total would fail it for tidying up. Adding one anywhere -- a
    # `#pragma`, a `-Wno-`, a `-fcommon`, a fresh `#if 0` -- is the finding.
    # Counted, not merely listed. A set difference cannot see a second `#if 0`
    # added to a file the pin already has one in, and that file is the cheapest
    # place to hide one precisely because it is already on the list. Line
    # numbers stay out of it: which file silences what is the finding, and a
    # line number would make an unrelated edit above it look like one.
    pin = textscan.pin_suppressions()
    found = collections.Counter((a, c) for a, _, c
                                in textscan.suppressions(root, files))
    ctx.emit("gate/backstop/suppressions-added",
             sorted([a, c, n - pin.get((a, c), 0)]
                    for (a, c), n in found.items() if n > pin.get((a, c), 0)))
    # The instrument before the finding: an empty `added` list reads the same
    # whether nothing was added or the pin list failed to load, so the size of
    # the list that produced it is scored beside it -- both how many sites it
    # names and how many occurrences they stand for, since a fixture that lost
    # its counts would still have the right number of sites.
    ctx.emit("gate/backstop/pin-suppression-sites", len(pin))
    ctx.emit("gate/backstop/pin-suppression-occurrences", sum(pin.values()))

    # The annotated `redis.conf` and `sentinel.conf` the repository ships are
    # named in the instruction as things that stay as they are, and nothing
    # else reads them: this step changes which addresses the tree reaches, and
    # a tree that reached them by rewriting the defaults it ships would satisfy
    # every behaviour scenario while shipping a different product. The same
    # backstop the suite gets in `c14`, for the same reason and in the same
    # shape -- against the pin's own bytes, over only the files the pin has, and
    # with a sentinel of its own so that a fixture that failed to load reads
    # differently from a tree that changed nothing.
    pin_conf = _conf_files(PIN_CONF)
    tree_conf = _conf_files(root)
    ctx.emit("gate/backstop/conf-missing", conf_missing(pin_conf, tree_conf),
             adv=True)
    ctx.emit("gate/backstop/conf-directives-lost",
             conf_directives_lost(pin_conf, tree_conf), adv=True)
    # The same sentinel the suppression list gets, for the same reason: an
    # empty list of losses reads the same whether the tree lost nothing or the
    # fixture failed to load.
    ctx.emit("gate/backstop/pin-conf-files", len(pin_conf))
    ctx.emit("gate/backstop/pin-conf-directives",
             sum(len(_directives(v)) for v in pin_conf.values()))

    ctx.note("preprocessed units: %d; build rc %s" % (len(units), report["rc"]))
