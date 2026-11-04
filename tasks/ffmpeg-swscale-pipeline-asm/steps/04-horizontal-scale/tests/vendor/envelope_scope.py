#!/usr/bin/env python3
"""Report how much of a milestone's own scope the candidate changed.

    envelope_scope.py <candidate-seam> <base-seam> <ISA.stepN>

The instruction-set envelope is a fairness rule about code the candidate
*wrote*, so the gate that enforces it is scoped to what the candidate changed.
A tree whose milestone files are all byte-identical to the base's cannot have
brought wider registers than the reference -- it is the tree the base's own
behaviour was recorded on -- so for it the gate is a vacuous pass. That case is
reported here, with its counts, rather than being left to isa.py: a scoped gate
that resolves to no object must not be indistinguishable from a clean
milestone, and the fix is to say why it examined nothing, not to let it pass in
silence. A tree that changed any file this milestone owns is checked in full.

Why not the whole seam. In a cumulative task the candidate's tree carries its
own earlier milestones, so at step k it also differs from the base in the files
steps 1..k-1 own -- and at some milestones a file is owned by several steps
(`x86/swscale.c` by input conversion, horizontal scaling and output). The files
a milestone owns are therefore declared per step, in its ISA file, as
`ownfile` lines named relative to the seam (`libswscale/x86/`). The Makefile is
deliberately not one of them: it is shared bookkeeping that every earlier
milestone also edits, so counting it would report a cumulative candidate as
having changed this milestone's scope when it changed nothing of it, and the
gate would then resolve this milestone's dispatch to nothing and fail a tree
whose behaviour is correct.

Output, one record per line:

    CHANGED  <file> (<state>)   added | removed | modified
    SCOPED   <n>                files this milestone owns
    UNCHANGED <n>               of those, byte-identical to the base's copy

A file absent from both sides counts as unchanged: there is nothing there to
have changed.

Exit status: 0 when the scope was determined, whatever the counts; 2 when the
milestone declares no scope at all -- a spec that names no own files is a
defect, not a vacuous pass.
"""
import hashlib
import os
import sys


def read_ownfiles(specfile):
    own = []
    try:
        fh = open(specfile, errors="replace")
    except OSError as e:
        print("CANNOT READ %s: %s" % (specfile, e))
        sys.exit(2)
    for line in fh:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        k, _, v = line.partition(" ")
        if k == "ownfile":
            own.append(v.strip())
    return [f for f in own if f]


def digest(path):
    try:
        with open(path, "rb") as fh:
            return hashlib.sha256(fh.read()).hexdigest()
    except OSError:
        return None


def main(argv):
    cand, base, specfile = argv[1], argv[2], argv[3]
    own = read_ownfiles(specfile)
    if not own:
        print("NO SCOPED FILES DECLARED in %s -- the envelope gate has nothing "
              "to scope to, which is a failure, not a vacuous pass"
              % os.path.basename(specfile))
        return 2
    unchanged = 0
    for rel in own:
        a = digest(os.path.join(cand, rel))
        b = digest(os.path.join(base, rel))
        if a == b:                      # byte-identical, including both absent
            unchanged += 1
            continue
        if a is None:
            state = "removed"
        elif b is None:
            state = "added"
        else:
            state = "modified"
        print("CHANGED  %s (%s)" % (rel, state))
    print("SCOPED   %d" % len(own))
    print("UNCHANGED %d" % unchanged)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
