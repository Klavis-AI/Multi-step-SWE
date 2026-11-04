#!/usr/bin/env python3
"""Enforce a milestone's instruction-set envelope, on the machine code.

    isa.py <builddir> <ISA.stepN> <seamdir>

Why this exists. The candidate and the reference must be allowed the same
machine: if a candidate may use a capability the reference does not, beating the
reference measures access to a wider machine rather than the quality of its
code. The host this task is graded on executes SSE2 through AVX2 and no AVX-512,
so what it executes -- AVX2 -- is the ceiling for every milestone, and AVX-512
is above that ceiling rather than a narrower one; the unscaled YUV-to-RGB
conversion, however, is implemented upstream in SSSE3 only, and for that
milestone the envelope is narrowed to what the reference uses.

One check, run over the objects the milestone's own dispatch reaches.

  * The symbols this milestone's dispatch installs are collected from the seam,
    followed to the object files that define them, and those objects are
    checked for registers wider than the ceiling -- where the ceiling is below
    what the host executes. Where it is not, there is nothing narrower to
    check, and the scope is still reported. Scoping the check this way is what
    keeps it a statement about this milestone rather than about the whole
    cumulative diff -- at a late milestone the diff also contains every earlier
    milestone's legitimate 256-bit work, and a gate that reads it wholesale
    charges the late milestone for the early one.

Why it disassembles rather than greps the source. A source-level ban on `ymm`
fires on comments and on the `%if cpuflag(avx2)` guards a port of upstream's own
dispatch ladder legitimately contains. Register width in the emitted
instructions is the property that matters and `objdump` is the instrument that
reports it. It also catches a candidate that reaches wider registers by adding
`-mavx2` to the Makefile and letting the compiler emit them from intrinsics,
which no source grep would see.

It asserts it examined something: a guard reading "0 violations" over an empty
object list is indistinguishable from a clean milestone. A scoped run that follows the dispatch to no object
at all is reported as a failure, not as a pass. So that this assertion cannot
punish a dispatch written in a shape the name scrape below does not recognise,
the scope is re-followed from the milestone's dispatch entry point and its own
object files before that failure is declared: see `own_objects` and `follow`.
"""
import os
import re
import subprocess
import sys

# A milestone's dispatch assigns either through a context field
# (`c->hcScale = ff_...`) or to a global function pointer
# (`interleaveBytes = interleave_bytes_sse2`), and sometimes through a macro
# that takes the field as an argument. All three are matched; a macro the
# caller cannot see through simply yields a smaller scope, which the run
# reports.
FIELD = r"(?:->\s*|^\s*)%s\s*=\s*([A-Za-z_][A-Za-z0-9_]*)"
RETURN = re.compile(r"^\s*return\s+([A-Za-z_][A-Za-z0-9_]*)\s*;")

WIDTH = {"sse2": 16, "ssse3": 16, "sse4": 16, "avx": 32, "avx2": 32,
         "avx512": 64, "avx512icl": 64}


def read_spec(path):
    spec = {"ceiling": "avx2", "enforce": False, "fields": [],
            "returnfile": None, "syms": [], "ownfiles": [], "entries": []}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        k, _, v = line.partition(" ")
        v = v.strip()
        if k == "ceiling":
            spec["ceiling"] = v
        elif k == "enforce":
            spec["enforce"] = v.lower() in ("yes", "true", "1")
        elif k == "field":
            spec["fields"].append(v)
        elif k == "returnfile":
            spec["returnfile"] = v
        elif k == "sym":
            spec["syms"].append(v)
        elif k == "ownfile":
            spec["ownfiles"].append(v)
        elif k == "entry":
            spec["entries"].append(v)
    return spec


def candidate_symbols(seam, spec):
    """Identifiers this milestone's dispatch installs, by name.

    A name scrape, and therefore a statement about the *shape* of the source:
    a dispatch that returns a kernel's name matches, one that installs it
    through a helper, a local variable or a table does not. It is a seed, not
    the last word -- `follow` closes over the linkage from whatever it names,
    and `main` re-follows from the milestone's own objects when it names
    nothing.
    """
    found = set(spec["syms"])
    pats = [re.compile(FIELD % re.escape(f), re.M) for f in spec["fields"]]
    for dirpath, _, names in os.walk(seam):
        for n in sorted(names):
            if not n.endswith((".c", ".h")):
                continue
            f = os.path.join(dirpath, n)
            try:
                text = open(f, errors="replace").read()
            except OSError:
                continue
            for p in pats:
                found.update(p.findall(text))
            if spec["returnfile"] and n == spec["returnfile"]:
                for line in text.split("\n"):
                    m = RETURN.match(line)
                    if m:
                        found.add(m.group(1))
    return {s for s in found if s and s not in ("NULL", "0")}


def own_objects(objects, spec):
    """The built objects that came from files this milestone owns.

    The candidate's seam is grafted onto the verifier's build, so the object
    that implements an owned source file is built from the candidate's copy of
    it: the only question is which object name it has.
    """
    stems = set()
    for f in spec["ownfiles"]:
        stem, _, ext = f.rpartition(".")
        if stem and ext in ("c", "cc", "cpp", "asm", "S"):
            stems.add(os.path.basename(stem))
    return [o for o in objects
            if os.path.splitext(os.path.basename(o))[0] in stems]


def defined(sobj, globals_only=False):
    cmd = ["nm", "--defined-only"] + (["-g"] if globals_only else []) + [sobj]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=False).stdout
    except OSError:
        return set()
    return {l.split()[-1] for l in out.split("\n") if l.strip()}


def callees(sobj):
    """Symbols this object references, from its relocations.

    Needed because a milestone's dispatch usually installs a thin wrapper that
    calls the kernel: the wrapper is what `return X;` names, and the register
    width is in the object the wrapper calls. One hop of closure over the whole
    object, repeated, is enough to reach it.
    """
    try:
        out = subprocess.run(["objdump", "-dr", sobj], capture_output=True,
                             text=True, check=False).stdout
    except OSError:
        return set()
    names = set()
    for m in re.finditer(r"R_X86_64_\S*\s+(\S+)", out):
        # objdump appends the addend: `sym-0x4`, `sym+0x10`.
        names.add(re.split(r"[-+]0x", m.group(1))[0])
    return names


def follow(objects, syms, rounds=2):
    """Objects defining `syms`, then the objects those call, `rounds` deep.

    The dispatch usually names a thin wrapper and the width is in the object
    the wrapper calls, so the closure is what makes the scope the milestone's
    own kernels rather than just its dispatch glue. It is bounded on purpose:
    the closure from a milestone's own files reaches every object the whole
    library ever calls if it is let run, and that is exactly the cumulative-diff
    mistake the scoping exists to avoid. Two rounds is the primary path's
    documented reach -- the object that defines the name, then the kernels it
    installs -- and the fallback in `main` is seeded so that the same two
    rounds land in the same place.
    """
    scope = set()
    syms = set(syms)
    for _ in range(rounds):
        before = (len(scope), len(syms))
        for o in objects:
            if o not in scope and defined(o) & syms:
                scope.add(o)
        for o in sorted(scope):
            syms |= callees(o)
        if (len(scope), len(syms)) == before:
            break
    return scope, syms


def disassemble(sobj):
    try:
        return subprocess.run(["objdump", "-d", "--no-show-raw-insn", sobj],
                              capture_output=True, text=True, check=False).stdout
    except OSError:
        return ""


def widest(dis):
    w = 0
    for m in re.finditer(r"%(ymm|zmm)\d+", dis):
        w = max(w, 32 if m.group(1) == "ymm" else 64)
    return w


def main(argv):
    bld, specfile, seam = argv[1], argv[2], argv[3]
    spec = read_spec(specfile)
    limit = WIDTH.get(spec["ceiling"], 64)

    x86dir = os.path.join(bld, "libswscale", "x86")
    objects = []
    for dirpath, _, names in os.walk(x86dir):
        objects += [os.path.join(dirpath, n) for n in names if n.endswith(".o")]
    objects.sort()
    if not objects:
        print("    NO OBJECT FILES BUILT under libswscale/x86 -- examined nothing")
        return 2

    syms = candidate_symbols(seam, spec)
    # Follow the dispatch through relocations, so a wrapper that installs a
    # kernel and calls it brings the kernel into scope. Two hops: wrapper ->
    # kernel -> anything the kernel calls.
    scope, _ = follow(objects, syms, rounds=2)

    # The seed above is a name scrape and a dispatch need not put a kernel's
    # name anywhere for it to install it: a wrapper that returns a helper's
    # result, a helper in another of the candidate's files, a table walked at
    # run time -- all install kernels and none of them names one in the
    # returnfile. When that happens the seed becomes the milestone's dispatch
    # entry point and the global symbols its own object files define, and the
    # same two rounds are run again, so the kernels are reached through the
    # objects' own relocations instead of through the source's shape. Nothing
    # is added to a scope that already resolved: this is a second way to find
    # the dispatch, not a wider net over the tree.
    followed_from = None
    if spec["enforce"] and not scope:
        seed = own_objects(objects, spec)
        names = set(spec["entries"])
        for o in seed:
            names |= defined(o, globals_only=True)
        if names:
            scope, _ = follow(objects, names, rounds=2)
            if scope:
                followed_from = sorted(names)

    print("  TOTAL_OBJECTS %d" % len(objects))
    print("  MILESTONE_OBJECTS %d" % len(scope))
    if followed_from:
        print("    the dispatch named no object directly; scope followed from "
              "this milestone's own symbols (%s%s)"
              % (", ".join(followed_from[:8]),
                 ", ..." if len(followed_from) > 8 else ""))
    print("  %d objects under libswscale/x86; %d implement this milestone's "
          "dispatch (ceiling %s, <= %d-bit)"
          % (len(objects), len(scope), spec["ceiling"], limit * 8))
    for o in sorted(scope):
        print("    in scope: %s" % os.path.basename(o))

    violations = []
    if spec["enforce"]:
        for o in sorted(scope):
            w = widest(disassemble(o))
            if w > limit:
                violations.append("%s implements this milestone's dispatch and "
                                  "uses %d-bit registers"
                                  % (os.path.basename(o), w * 8))
        for v in violations:
            print("    VIOLATION %s" % v)
    else:
        # The ceiling is what this machine executes, so a wider instruction
        # cannot run and cannot be credited. Two things would go wrong if the
        # width were checked anyway: upstream ships kernels wider than the
        # machine (x86/rgb_2_rgb.asm's AVX-512ICL shuffles are in the same
        # object as the SSSE3 ones the dispatch installs), and the check would
        # fail the reference solution. The scope is still reported, because a
        # scope that resolves to nothing is worth seeing.
        print("    ceiling equals what this machine executes; no width to enforce")

    if not spec["enforce"]:
        return 0
    if not scope:
        print("    NO OBJECT MATCHED THE MILESTONE'S DISPATCH -- the scoped gate "
              "examined nothing, which is a failure, not a pass")
        return 2
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
