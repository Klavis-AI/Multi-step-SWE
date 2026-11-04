"""Compare a cell by the category the instruction names, not by the golden's value.

Replay compares observation by observation, and for almost every cell the
recorded value *is* the requirement: a reply, a count that has to reach zero, a
boolean. A handful of cells recorded something more specific than the
instruction asks for -- the golden's own wording of an error, its own choice of
example address, its own number of colon searches -- and a tree that met the
instruction in its own way missed them. Those cells are listed here, each with
the reading that the instruction does name, and `runner.py replay` compares the
recorded value and the candidate's value through that reading. Both sides go
through the same function, so the recorded expectations stay exactly as they
were recorded.

A reading may only widen a cell to the category the instruction names. For a
`progress` cell it must still tell the golden apart from the untouched tree and
from the previous step's tree (both are noted beside each rule), otherwise the
cell would pay a tree that did nothing.

`DROP` takes a cell out of the comparison altogether. It is used once, for a
count that no reading can turn into the instruction's category without also
paying the untouched tree (see `RULES`).
"""

import os
import re
import socket
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.join(os.path.dirname(HERE), "corpus")

DROP = object()


def _corpus(name):
    """A corpus module, imported the way `run_one.py` imports it."""
    if CORPUS not in sys.path:
        sys.path.insert(0, CORPUS)
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    return __import__(name)


# -- g05 colon-split total ----------------------------------------------------
# Step 3 asks that the last-colon rule be "written in one place the readers
# call". The focus-set total recorded 5 (untouched), 4 (steps 1-2) and 3 (step
# 3's golden). Fewer is the direction of the step and a solution that searches
# for the colon once fewer times than the golden has met it at least as well,
# so the cell is the ceiling "at most 3": 5 and 4 still read as above it.
def _at_most_3(v):
    if isinstance(v, int) and not isinstance(v, bool):
        return "at-most-3" if v <= 3 else "more-than-3"
    return v


# -- d06 no-IPv6 failure log --------------------------------------------------
# Step 6: the line the server logs "names the address it could not bind -- the
# IPv6 wildcard, written and joined to the port the way this tree writes every
# address -- and the reason it could not". The recorded value is glibc's whole
# sentence; what the instruction names is the bracketed wildcard joined to the
# port. The reason is scored, in any of its wordings, by the companion cell
# `v6def/no-v6/default/log-says-unsupported`. The untouched tree and step 5's
# tree start on IPv4 here and log only that they are ready, so no line of
# theirs carries `[::]:<port>`.
_V6_WILDCARD_PORT = re.compile(r"\[::\]:\d+")


def _names_v6_wildcard(v):
    if not isinstance(v, list):
        return v
    found = set()
    for line in v:
        if isinstance(line, str):
            found.update(_V6_WILDCARD_PORT.findall(line))
    return ["names-v6-wildcard", sorted(found)]


# -- c11/c12 refusals of a special address ------------------------------------
# Step 5: "A command answers with an error that says which address was refused
# and why." Scored as `d05_scoped` scores the same refusals -- is it an error,
# does it name the address, does it name the class -- with d05's own
# predicates, so the two checks of one behaviour cannot disagree.
def _refusal(addr, klass):
    def reading(v):
        if (isinstance(v, list) and len(v) == 2 and v[0] == "error"
                and isinstance(v[1], str)):
            d05 = _corpus("d05_scoped")
            return ["error", bool(d05._names_addr(v[1], addr)),
                    bool(d05._names_class(v[1], klass))]
        return v
    return reading


# -- usage text whose example addresses moved ---------------------------------
# Step 6: every dotted quad that is an example "becomes the address the tree
# would actually use". Which address is the tree's choice, so an IPv6 literal
# (bracketed or not, with or without a zone) and the name `localhost` all read
# as `<ADDR>`. A dotted quad is left as it is, so a quad that was not moved
# still reads differently from one that was.
_BRACKETED = re.compile(r"\[([0-9A-Fa-f:.]+(?:%[0-9A-Za-z_.-]+)?)\]")
_BARE = re.compile(r"(?<![0-9A-Za-z_:.%])([0-9A-Fa-f:.]*:[0-9A-Fa-f:.]*"
                   r"(?:%[0-9A-Za-z_.-]+)?)")
_LOCALHOST = re.compile(r"(?<![0-9A-Za-z_.-])localhost(?![0-9A-Za-z_-])")


def _is_v6(text):
    base = text.split("%", 1)[0]
    try:
        socket.inet_pton(socket.AF_INET6, base)
        return True
    except (OSError, ValueError):
        return False


def _mask_bare(m):
    s = m.group(1)
    # A sentence's own `:` or `.` can follow an address ("against ::1:");
    # the longest prefix that is an address is the address.
    for end in range(len(s), 1, -1):
        if _is_v6(s[:end]):
            return "<ADDR>" + s[end:]
    return s


def _mask_text(s):
    s = _BRACKETED.sub(lambda m: "<ADDR>" if _is_v6(m.group(1)) else m.group(0), s)
    s = _BARE.sub(_mask_bare, s)
    return _LOCALHOST.sub("<ADDR>", s)


def _example_addresses(v):
    if isinstance(v, str):
        return _mask_text(v)
    if isinstance(v, list):
        return [_example_addresses(x) for x in v]
    return v


# (scenario, observation) -> reading, or DROP.
RULES = {
    ("g05_canonical_text", "gate/fmt/focus-colon-splits"): _at_most_3,
    # Dropped: recorded 1 untouched and 2 at step 3's golden. A correct
    # parser written once can read 0 (placed in another file), 1 (one strrchr
    # and a `]` check) or 2 (the golden), so the instruction's category takes
    # in the untouched tree's value and no reading can separate them. The
    # step's property is still scored by the focus total above and by every
    # per-file count that has to reach zero.
    ("g05_canonical_text", "gate/fmt/by-file/src/anet.c/colon-splits"): DROP,
    ("d06_v6_default", "v6def/no-v6/default/log"): _names_v6_wildcard,
    ("c11_cluster", "cluster/meet-multicast"): _refusal("ff02::1", "multicast"),
    ("c11_cluster", "cluster/meet-unspecified"): _refusal("::", "unspecified"),
    ("c11_cluster", "cluster/meet-v4-compatible"):
        _refusal("::127.0.0.1", "v4-compatible"),
    ("c12_sentinel", "sentinel/monitor-unspecified"):
        _refusal("::", "unspecified"),
    ("c13_benchmark", "bench/usage/out"): _example_addresses,
    ("c13_benchmark", "bench/missing-host-argument/out"): _example_addresses,
    ("c08_listeners", "argv/help-out"): _example_addresses,
}


def rule(scenario, name):
    return RULES.get((scenario, name))


def same(reading, got, expected):
    """Whether `got` meets `expected` under `reading`.

    A reading that cannot read the candidate's value -- anything unexpected
    from a tree that answered in a shape nobody recorded -- falls back to the
    exact comparison rather than to a pass.
    """
    try:
        return reading(got) == reading(expected)
    except Exception:  # noqa: BLE001 - an unreadable value is compared as is
        return got == expected
