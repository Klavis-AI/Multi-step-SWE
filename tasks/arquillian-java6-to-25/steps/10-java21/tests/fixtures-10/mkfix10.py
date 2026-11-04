#!/usr/bin/env python3
"""Build step 10's rehearsal fixtures for family C, B-10, B-11, B-12, B-13,
B-14, B-15 and B-16, and compute what each one must score.

    python3 authoring/fixtures-step10/mkfix10.py

Writes one directory per fixture under fixtures-step10/c/, each holding the
source report the shipped scoring blocks will read, the compile answers they
would have got from javac, a behaviour summary, the `jars/` directory B-12e
unzips and arqgate.Modules reads, and the `scan.txt` C-1 reads. Prints the
score table that c-rehearsal.sh asserts.

NINE blocks, not eight. Step 09 rehearsed the source-AST block, B-10 through
B-15 and family C; this step adds B-16, which is step 09's own family C minus
its C-1, renormalised over the 0.90 that is left. All nine are extracted from
the shipped test.sh and all nine are scored here.

THE SCAN MOVES AND THE CONSTRUCTS STAY. Step 09's family C read both channels
at once: ${WORK}/scan.txt for the class-file major, and the source AST for the
four Java 17 constructs. At this rung the constructs are B-16 and the scan is
C-1 again at a NEW major -- 65 where step 09 wanted 61 -- so the two channels
have swapped families without either going away. The twelve fixtures that were
written to move the constructs, L3 through L14, are now the rows that assert an
inherited gate moves and this rung's own family C does not.

FOUR RECORD KINDS ARRIVE AND TWO ARE READ TWICE. reftest, seqfirst, seqnew and
swpat are new at this rung. `ipattern` and `switch` are older, and both are now
read by TWO families at once: B-16 spends the kind= and form= columns, family C
spends the type= column and the types= column of the swpat records that share
their buckets. That overlap is deliberate and it is why C-2's arrival half
unions two name sets -- a pattern variable introduced at step 09 is where a
step-10 type pattern gets its type from. A fixture that moved one column
without the other would score the two families as independent when they are
not, so the L-series rows below carry a family C column as much as a B-16 one.

THE TWO ENDS ARE MEASURED REPORTS AND NOT SYNTHESISED ONES, as they have been
since step 07's generator:

    pinned   baseline-step10-source.txt, the input tree's own measured report,
             which is also what the verifier stages as baseline/source.txt
    golden   baseline-step10-golden.txt, the golden tree's own measured report,
             /work/scratch/s10/src-gold.txt as the gate wrote it

Every fixture below is one of those two with an EXPLICIT, NAMED mutation
applied -- a record moved, a counter bumped, a bucket reverted. Nothing is
invented from whole cloth. That buys two things a feature-dict generator could
not:

  * a fixture claiming "five of the seven first-element reads were converted"
    deletes two of the golden's OWN `seqnew` records, at their real line
    numbers with their real receivers, because it takes them out of the report
    the migrated tree actually produced
  * the two ends are themselves fixtures, so the rehearsal asserts that the
    scorer reads 0.000000 on the tree the candidate is handed and 1.000000 on
    the tree the solution produces -- migration_prompt item 6's oracle and nop
    runs, held as a build-time check rather than only as a run

The `want` figures are computed here, by an independent implementation of the
same composition, and then written into c-rehearsal.sh BY HAND. The rehearsal
never calls this file: a rehearsal that computes its own expectations at run
time agrees with the scorer by construction and asserts nothing. The verifier
image has no python3 either, which makes that separation structural rather than
a matter of discipline.
"""
import os
import re
import shutil
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
AUTH = os.path.dirname(HERE)
OUT = os.path.join(HERE, "c")

S10 = os.path.join(AUTH, "baseline-step10-source.txt")
G10 = os.path.join(AUTH, "baseline-step10-golden.txt")


# --- the report, as the scorer reads it ---------------------------------------
class Rep(object):
    """One source-gate report: ordered key/value lines, records among them.

    The scorer reads this file two ways and both are preserved here. `sv` pulls
    a scalar with `sed -n 's/^key=//p' | head -1`, so a duplicate key is the
    first one and line order matters; the bucket helpers pull records with
    `sed -n 's/^kind\\[[0-9]*\\]=//p'`, which ignores the index entirely. So
    mutation is done on the ordered list, and record indices are renumbered on
    write purely so a human diffing two fixtures sees a clean diff.
    """

    REC = re.compile(r"^([a-z]+)\[(\d+)\]$")

    def __init__(self, path=None, pairs=None):
        if pairs is not None:
            self.pairs = list(pairs)
            return
        self.pairs = []
        with open(path) as fh:
            for line in fh:
                line = line.rstrip("\n")
                if not line or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                self.pairs.append([k, v])

    def clone(self):
        return Rep(pairs=[list(p) for p in self.pairs])

    # -- scalars
    def get(self, key, default="0"):
        for k, v in self.pairs:
            if k == key:
                return v
        return default

    def num(self, key):
        v = self.get(key)
        return int(v) if re.match(r"^-?\d+$", v) else 0

    def set(self, key, val):
        val = str(val)
        for p in self.pairs:
            if p[0] == key:
                p[1] = val
                return
        # A key the measured report never carried. Append it at the end of the
        # aggregate run rather than the end of the file, so the records stay in
        # one block and `head -1` still finds the only copy.
        for i, p in enumerate(self.pairs):
            if self.REC.match(p[0]):
                self.pairs.insert(i, [key, val])
                return
        self.pairs.append([key, val])

    def bump(self, key, delta):
        self.set(key, self.num(key) + delta)

    def rename(self, old, new):
        for p in self.pairs:
            if p[0] == old:
                p[0] = new
                return
        raise KeyError(old)

    # -- records
    def recs(self, kind):
        out = []
        for k, v in self.pairs:
            m = self.REC.match(k)
            if m and m.group(1) == kind:
                out.append(v)
        return out

    def setrecs(self, kind, bodies):
        """Replace every record of one kind, in place at the first one's slot."""
        keep, at = [], None
        for p in self.pairs:
            m = self.REC.match(p[0])
            if m and m.group(1) == kind:
                if at is None:
                    at = len(keep)
                continue
            keep.append(p)
        if at is None:
            at = len(keep)
        for i, body in enumerate(bodies):
            keep.insert(at + i, ["%s[%d]" % (kind, i), body])
        self.pairs = keep

    def maprecs(self, kind, fn):
        """fn(body) -> new body, or None to drop the record."""
        out = []
        for body in self.recs(kind):
            nb = fn(body)
            if nb is not None:
                out.append(nb)
        self.setrecs(kind, out)

    def write(self, path):
        seen = {}
        with open(path, "w") as fh:
            for k, v in self.pairs:
                m = self.REC.match(k)
                if m:
                    kind = m.group(1)
                    i = seen.get(kind, 0)
                    seen[kind] = i + 1
                    k = "%s[%d]" % (kind, i)
                fh.write("%s=%s\n" % (k, v))


def read_set(path):
    out = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                out.append(line)
    return out


def read_env(path):
    d = {}
    for line in read_set(path):
        if "=" in line:
            k, v = line.split("=", 1)
            if "[" not in k:
                d[k] = v
    return d


def site(body):
    """The `path:line` first field of a record, split."""
    f = body.split()[0]
    p, _, ln = f.rpartition(":")
    return p, ln


def field(body, pref):
    for f in body.split()[1:]:
        if f.startswith(pref):
            return f[len(pref):]
    return None


def bucket_of(body):
    """`file#method`, exactly as buckets()/fbuckets()/nsbuckets() key it.

    A record with no `in=` is dropped by all three, so it is dropped here.
    """
    m = field(body, "in=")
    if m is None or m == "":
        return None
    return "%s#%s" % (site(body)[0], m)


def strip_pkg(name):
    """`org.jboss.Foo.Bar` -> `Foo.Bar`, the scorer's own loop.

    Leading LOWERCASE-initial segments only: a permits clause may name a nested
    type, and `Service.Provider` must not lose its outer name. Nothing in the
    baseline is spelled qualified, but SourceScan reports what the source says.

    It lives up here rather than beside the leg that first spent it because
    step 10's frozen-number block strips the type column of its own population
    files with it, and those run at import.
    """
    while True:
        m = re.match(r"^[a-z][A-Za-z0-9_]*\.", name)
        if not m:
            return name
        name = name[m.end():]


def strip_generic(name):
    """`Map<String,Foo>` -> `Map`, the `sub(/<.*$/, "", s)` the shell does first.

    SourceScan reports a `swpat` pattern's type as the SOURCE TEXT wrote it, so
    a pattern over a parameterised type arrives with its arguments attached
    while the `reftest` type it has to match arrives resolved and bare. The
    shell cuts at the first `<` before it strips packages; so does this.
    """
    return name.split("<", 1)[0]


def group(bodies):
    d = {}
    for b in bodies:
        k = bucket_of(b)
        if k is not None:
            d.setdefault(k, []).append(b)
    return d


# --- the frozen numbers, read rather than retyped -----------------------------
BASE = Rep(S10)
GOLD = Rep(G10)
TGT = read_env(os.path.join(AUTH, "baseline-step10-target.txt"))
LEVEL = read_env(os.path.join(AUTH, "baseline-step10-level.txt"))


def T(k):
    return int(TGT[k])


# WANT_MAJOR IS SPENT AGAIN AT THIS RUNG and it is a different number. C-1 asks
# how much of the tree reached 65 and B-4 asks that whatever it reached is one
# major and not below 61 -- the level step 09 left it at, which is this rung's
# `was`. Both numbers live in baseline/level.txt -- `major` for C-1 and `was`
# for B-4 -- and neither gate reads a literal. A generator that carried 61
# forward as the want would score the golden's 65 as a miss and the INPUT TREE
# as perfect, which is the one arithmetic error this constant can make.
WANT_MAJOR = int(LEVEL["major"])                    # 65, the must-flip
HOLD_MAJOR = int(LEVEL["was"])                      # 61, B-4's floor
BASE_SUP = BASE.num("suppress.sites")               # 48
BASE_RAW = BASE.num("newgeneric.raw")               # 0
# What arqgate.Scan counts over the published jars and the class files the
# build left in the tree, measured on step 09's blind run: gate.classes=746.
# The step-10 golden publishes the same twelve artifacts with the same class
# files -- a type pattern, a pattern switch and a getFirst() call create no
# types -- so the count is the same at both ends and only the MAJOR moves.
# That is exactly what makes C-1 a fraction worth having.
CLASSES = 746

# B-10's denominators: step 03's work, against THIS rung's tree.
T_DIA, T_NONANON = T("target.diamond"), T("target.explicit.nonanon")
T_RES, T_IO = T("target.try.resources"), T("target.iofile")
B10_DIA, B10_CLOSE = T("target.b10.diamond"), T("target.b10.close")
B10_NIO = T("target.b10.nio")
# B-11's: step 04's.
B11_LAMFILES, B11_SAM = T("target.b11.lambdafiles"), T("target.b11.sam")
B11_CM = T("target.b11.collectmethods")
# B-12's: step 05's four populations and the four constant-pool floors.
B12_OPT, B12_SINKS = T("target.b12.optbuckets"), T("target.b12.sinks")
B12_VT, B12_TIME = T("target.b12.valuetypes"), T("target.b12.timesites")
B12_FLOORS = (T("target.b12.optional.floor"), T("target.b12.collectors.floor"),
              T("target.b12.objects.floor"), T("target.b12.jtime.floor"))
# B-13's: step 06's seven.
B13_LEGACY, B13_CTORFLOOR = T("target.b13.legacy"), T("target.b13.ctor.floor")
B13_GUARDS, B13_SETACC = T("target.b13.guards"), T("target.b13.setacc")
B13_BOXED, B13_BUILDS = T("target.b13.boxed"), T("target.b13.builds")
B13_VIEWS, B13_OPTDEP = T("target.b13.views"), T("target.b13.opt.departed")
B13_OPTARR, B13_CAND = T("target.b13.opt.arrived"), T("target.b13.candidatenew")
B13_WANT = T("target.b13.want")
# B-14's: step 07's five, under the target.b14.* spellings step 08 re-keyed
# them into and this rung carries forward unchanged. Renaming a carry-forward
# key is how a gate quietly stops grading the thing it was named for, so the
# names below are step 08's names and the b14_score() that spends them is step
# 08's function, lifted whole.
C2_BUCKETS, C2_REFS = T("target.b14.c2.buckets"), T("target.b14.c2.refs")
C3_BUCKETS, C3_REFS = T("target.b14.c3.buckets"), T("target.b14.c3.refs")
C4_BUCKETS, C4_SIZED = T("target.b14.c4.buckets"), T("target.b14.c4.sized")
C5_FILE, C5_LEGACY = TGT["target.b14.c5.file"], T("target.b14.c5.legacy")
C5_TYPES, C5_STUB = T("target.b14.c5.types"), T("target.b14.c5.cdistub")
# C-5's two run-time factors read the same list the scorer reads, so the
# denominator cannot drift between the two implementations by editing one file.
C5_HTTPIDS = read_set(os.path.join(AUTH, "baseline-step07-httpids.txt"))
C5_EXECN = len(C5_HTTPIDS)                          # 18
C5_REDID = "servlet/exec-redirect"
assert C5_REDID in C5_HTTPIDS, "the redirect proviso names an id the list does not"

LAMFILES = read_set(os.path.join(AUTH, "baseline-step04-lambdafiles.txt"))
SAMPAIRS = read_set(os.path.join(AUTH, "baseline-step04-samifaces.txt"))
COLLECTMETHODS = [l.split()[0] for l in
                  read_set(os.path.join(AUTH, "baseline-step04-collectmethods.txt"))]
NIOIDS = read_set(os.path.join(AUTH, "baseline-step03-nioids.txt"))

OPTBUCKETS = [l.split()[0] for l in
              read_set(os.path.join(AUTH, "baseline-step05-optbuckets.txt"))]
SINKS, SINKWANT = [], {}
for _l in read_set(os.path.join(AUTH, "baseline-step05-sinks.txt")):
    _f = _l.split()
    SINKS.append(_f[0])
    SINKWANT[_f[0]] = _f[1]
VALUETYPES = [l.split()[0] for l in
              read_set(os.path.join(AUTH, "baseline-step05-valuetypes.txt"))]
TIMESITES = [l.split()[0] for l in
             read_set(os.path.join(AUTH, "baseline-step05-timesites.txt"))]

NI_LEGACY = [l.split(None, 1)[1] for l in
             read_set(os.path.join(AUTH, "baseline-step06-newinstance.txt"))
             if l.split()[0] == "legacy"]
ACC_GUARD = [l.split(None, 1)[1] for l in
             read_set(os.path.join(AUTH, "baseline-step06-accessible.txt"))
             if l.split()[0] == "guard"]
BOXEDW = [bucket_of(l) for l in
          read_set(os.path.join(AUTH, "baseline-step06-boxedctor.txt"))]
BUILDS = [l.split()[0] for l in
          read_set(os.path.join(AUTH, "baseline-step06-builds.txt"))]
VIEWS = [l.split()[0] for l in
         read_set(os.path.join(AUTH, "baseline-step06-views.txt"))]
OPTOPS = [l.split()[0] for l in
          read_set(os.path.join(AUTH, "baseline-step06-optops.txt"))]
OPTARR = [l.split()[0] for l in
          read_set(os.path.join(AUTH, "baseline-step07-optarr.txt"))]

# Step 07's three population files, spent by B-14. nsmove and nshold are keyed
# PACKAGE-FIRST, and that is not a presentation choice: nswant() builds its key
# as `$1 "|" $2`, the departure side compares it against buckets keyed
# `pkg|file#method`, and the arrival side is the same file with the package
# column rewritten to jakarta. A bucket-first file keys `bucket|pkg`, which
# intersects the departure set at zero and scores a finished migration 0.
NSMOVE = [(l.split()[0], l.split()[1]) for l in
          read_set(os.path.join(AUTH, "baseline-step07-nsmove.txt"))]
NSHOLD = [(l.split()[0], l.split()[1]) for l in
          read_set(os.path.join(AUTH, "baseline-step07-nshold.txt"))]
NSHOLD_PKGS = sorted(set(p for p, _ in NSHOLD))
TAWANT = [l.split()[0] for l in
          read_set(os.path.join(AUTH, "baseline-step07-toarray.txt"))]

# --- step 08's three, spent by B-15 ------------------------------------------
#
# MODNAMES is read as an ORDERED LIST OF PAIRS and compared pairwise, because
# that is what the shell does: two sorted `<artifact> <name>` files and a
# `comm -12` between them. A set of names on its own would score a tree that
# gave arquillian-api the spi's name and the spi the api's as perfect.
MODNAMES = [(l.split()[0], l.split()[1]) for l in
            read_set(os.path.join(AUTH, "baseline-step08-modnames.txt"))]
_SVC = [l.split(None, 1) for l in
        read_set(os.path.join(AUTH, "baseline-step08-svc.txt"))]
SVC_LEGACY = [b for t, b in _SVC if t == "legacy"]
SVC_ARRIVE = [b for t, b in _SVC if t == "arrive"]
SVC_KEEP = [b for t, b in _SVC if t == "keep"]
SVCIDS = [l for l in read_set(os.path.join(AUTH, "baseline-step08-svcids.txt"))
          if l and not l.startswith("#")]

MOD_TOTAL = T("target.b15.mod.total")               # 12
T_SVC_LEGACY = T("target.b15.svc.legacy")           # 2
T_SVC_ARRIVE = T("target.b15.svc.arrive")           # 2
T_SVC_KEEP = T("target.b15.svc.keep")               # 11

# --- step 09's five, spent by B-16 -------------------------------------------
#
# THE FILES ARE STEP 09'S AND THE TARGET KEYS ARE NOT. These five populations
# are frozen: they name sites in a tree that stopped moving one rung ago, so
# re-measuring them here would be re-deriving a constant and inviting it to
# drift. The five baseline-step09-*.txt files are read byte for byte, exactly
# as step 09 read them, and the verifier stages the same bytes.
#
# What DID move is the denominators' spelling. Step 09 wrote `target.ip.sites`
# because they were its family C; at this rung they are an inherited gate and
# the shipped test.sh reads `target.b16.ip.sites`. Renaming a carry-forward key
# is how a gate quietly stops grading the thing it was named for, so every one
# of the fourteen lookups below is re-keyed HERE, against the step-10 target
# file, and a key that failed to move would raise a KeyError rather than score.
#
# Each is still read as the SHELL reads it -- a tag column and a bucket or file
# key -- so that a population file whose shape drifted breaks here rather than
# scoring a tree wrongly. ipattern.txt and strnl.txt carry counts per bucket
# because two of those buckets hold more than one site; swarrow.txt carries
# counts per FILE because a switch has no enclosing-method key; sealed.txt
# carries three kinds of row and only two of them are scored.
IP_CONV = {}
IP_SKIP = {}
for _l in read_set(os.path.join(AUTH, "baseline-step09-ipattern.txt")):
    _t, _b, _n = _l.split()
    (IP_CONV if _t == "conv" else IP_SKIP)[_b] = int(_n)
IP_CONV_W = sum(IP_CONV.values())                   # 21
IP_SKIP_W = sum(IP_SKIP.values())                   # 7
IP_TOTAL = T("target.b16.ip.total")                 # 28

TB_MOVE = {}
TB_HOLD = {}
for _l in read_set(os.path.join(AUTH, "baseline-step09-strnl.txt")):
    _t, _b, _n = _l.split()
    (TB_MOVE if _t == "move" else TB_HOLD)[_b] = int(_n)
TB_MOVE_W = len(TB_MOVE)                            # 1 BUCKET, not 16 sites
TB_HOLD_W = sum(TB_HOLD.values())                   # 7 sites
TBIDS = read_set(os.path.join(AUTH, "baseline-step09-tbids.txt"))

SEAL_ROOT = []                                      # (file, name, permits set)
SEAL_OPEN = []                                      # (file, name)
SEAL_LEAF = []
for _l in read_set(os.path.join(AUTH, "baseline-step09-sealed.txt")):
    _f = _l.split()
    if _f[0] == "root":
        SEAL_ROOT.append((_f[1], _f[2], tuple(sorted(_f[4].split(";")))))
    elif _f[0] == "open":
        SEAL_OPEN.append((_f[1], _f[2]))
    else:
        SEAL_LEAF.append(_f[1])

SW_WANT = {}
for _l in read_set(os.path.join(AUTH, "baseline-step09-swarrow.txt")):
    _f = _l.split()
    SW_WANT[_f[0]] = int(_f[1])

assert len(LAMFILES) == B11_LAMFILES and len(SAMPAIRS) == B11_SAM
assert len(COLLECTMETHODS) == B11_CM
assert len(OPTBUCKETS) == B12_OPT and len(SINKS) == B12_SINKS
assert len(VALUETYPES) == B12_VT and len(TIMESITES) == B12_TIME
assert len(NI_LEGACY) == B13_LEGACY and len(ACC_GUARD) == B13_GUARDS
assert len(BOXEDW) == B13_BOXED and len(BUILDS) == B13_BUILDS
assert len(VIEWS) == B13_VIEWS and len(OPTOPS) == B13_OPTDEP
assert len(OPTARR) == B13_OPTARR
assert len(NSMOVE) == C2_BUCKETS and len(NSHOLD) == C3_BUCKETS
assert len(TAWANT) == C4_BUCKETS
# Every nsmove package really is one of the two step 07 moved, and no nshold
# package is. A population file that named javax.enterprise on the move side
# would ask for the CDI migration that rung explicitly deferred.
assert set(p for p, _ in NSMOVE) == set(["javax.annotation", "javax.ejb"])
assert not (set(NSHOLD_PKGS) & set(["javax.annotation", "javax.ejb"]))

# B-15's three, the same way step 08 asserted them when they were its family C.
assert len(MODNAMES) == MOD_TOTAL
assert len(set(a for a, _ in MODNAMES)) == MOD_TOTAL, "an artifact named twice"
assert len(set(n for _, n in MODNAMES)) == MOD_TOTAL, "a module name claimed twice"
assert len(SVC_LEGACY) == T_SVC_LEGACY and len(SVC_ARRIVE) == T_SVC_ARRIVE
assert len(SVC_KEEP) == T_SVC_KEEP
assert set(SVC_LEGACY) == set(SVC_ARRIVE), "the departure and arrival sets differ"
assert not (set(SVC_KEEP) & set(SVC_LEGACY)), "a bucket is both held and moved"
assert len(SVCIDS) == 69 and len(set(SVCIDS)) == 69

# Those five, against the target file the scorer divides by. Every one of them
# is a number the shipped test.sh reads out of target.txt or counts out of the
# population file itself, and both spellings have to agree: a population file
# and a target key that disagreed would score a correct tree below 1.0 and no
# other check in this image would notice.
assert len(IP_CONV) == T("target.b16.ip.buckets"), "ipattern conv buckets"
assert IP_CONV_W == T("target.b16.ip.sites"), "ipattern conv sites"
assert IP_SKIP_W == T("target.b16.ip.skip"), "ipattern skip sites"
assert IP_CONV_W + IP_SKIP_W == IP_TOTAL, "the two halves are not the 28"
assert not (set(IP_CONV) & set(IP_SKIP)), "a bucket is both converted and held"
assert TB_MOVE_W == T("target.b16.tb.move.buckets")
assert sum(TB_MOVE.values()) == T("target.b16.tb.move.sites")
assert len(TB_HOLD) == T("target.b16.tb.hold.buckets")
assert TB_HOLD_W == T("target.b16.tb.hold.sites")
assert not (set(TB_MOVE) & set(TB_HOLD)), "a bucket both moves and holds"
assert len(TBIDS) == T("target.b16.tb.ids") and len(set(TBIDS)) == len(TBIDS)
assert len(SEAL_ROOT) == T("target.b16.sealed.roots")
assert len(SEAL_OPEN) == T("target.b16.sealed.open")
assert sum(len(p) for _, _, p in SEAL_ROOT) == T("target.b16.sealed.permits")
# 40 is roots plus leaves and NOT roots plus permits entries: six of the 35
# permitted names are themselves roots, because the event vocabulary nests --
# ContainerControlEvent permits DeploymentEvent, which permits two of its own.
# A check that added 11 and 35 would read 46 and quietly accept a population
# file whose nesting had been flattened.
assert len(SEAL_ROOT) + len(SEAL_LEAF) == T("target.b16.sealed.types"), \
    "roots + leaves is not the 40 distinct types in the hierarchies"
_SEAL_PERMITS = set()
for _, _, _p in SEAL_ROOT:
    assert not (_SEAL_PERMITS & set(_p)), "a name is permitted by two roots"
    _SEAL_PERMITS |= set(_p)
_SEAL_ROOTNAMES = set(n for _, n, _ in SEAL_ROOT)
assert sorted(_SEAL_PERMITS - _SEAL_ROOTNAMES) == sorted(SEAL_LEAF), \
    "the leaf list is not the permitted names that are not themselves roots"
# The two scored halves are disjoint AS KEYS -- `<file> <name>` and not name
# alone, because DynamicServiceLoaderTestCase holds two nested open types and
# two files in this tree both declare a type called `Service`.
assert not (set((f, n) for f, n, _ in SEAL_ROOT) & set(SEAL_OPEN)), \
    "a type is both sealed and held open"
assert len(SW_WANT) == T("target.b16.sw.files")
assert sum(SW_WANT.values()) == T("target.b16.sw.total")


# --- and step 10's own four, which are this rung's and nobody else's ---------
#
# reftest, chain, seqfirst and swids. Three of the four carry a prose reason on
# every row -- the `  # why` tail the census script writes -- because a
# population whose rows say only `conv` is a population nobody can review. The
# tail is cut HERE and not in the shell: the verifier never reads these files,
# it reads the target keys they were counted into.
#
# ROWS ARE SITES AND THE SCORER COUNTS BUCKETS. reftest.txt and seqfirst.txt
# both hold several sites in one `file#method`, and every one of the scorer's
# capsum() comparisons is per bucket, so the counting into buckets happens
# below and the assertions check BOTH totals: a file that lost a row would
# still have the right bucket count.
def _pop(path):
    """One population row: the trailing `  # why` cut, then whitespace fields.

    Returns (fields, kv) so a caller can read both the positional columns --
    bucket, verdict, form -- and the `k=v` ones without knowing their order.
    """
    out = []
    for l in read_set(path):
        row = l.split("  #", 1)[0].strip()
        f = row.split()
        kv = {}
        for tok in f:
            if "=" in tok:
                k, v = tok.split("=", 1)
                kv[k] = v
        out.append((f, kv))
    return out


# C-2's population. RT_ARR is the pair set the arrival half divides by, built
# here the way the shell builds `rt_arr_w`: DISTINCT `bucket|simple-type` over
# the conv rows. It comes to one per site at this rung and that is a fact about
# this tree rather than a law -- two conv sites in one method naming the same
# type would collapse to one pair, and the assertion below would then be the
# thing that noticed.
RT_CONV, RT_HOLD = {}, {}
RT_ARR = set()
for _f, _kv in _pop(os.path.join(AUTH, "baseline-step10-reftest.txt")):
    _b = _f[0]
    if _f[1] == "conv":
        RT_CONV[_b] = RT_CONV.get(_b, 0) + 1
        _t = strip_pkg(_kv["type"])
        assert _t and _t != "-", "a conv reftest row names no type: " + _b
        RT_ARR.add((_b, _t))
    else:
        RT_HOLD[_b] = RT_HOLD.get(_b, 0) + 1
RT_CONV_W = sum(RT_CONV.values())                   # 13 sites
RT_HOLD_W = sum(RT_HOLD.values())                   # 14 sites

# C-3's. An ARM COUNT per row and not a chain count, because the conv half's
# denominator is arms: a three-arm chain converted is worth more than a
# two-arm one, and `ch_conv_w` in the shell is a sum over `arms=`.
CH_CONV, CH_HOLD = [], []                           # (bucket, arms, types)
for _f, _kv in _pop(os.path.join(AUTH, "baseline-step10-chain.txt")):
    _row = (_f[0], int(_kv["arms"]),
            tuple(strip_pkg(t) for t in _kv["types"].split(";")))
    (CH_CONV if _f[1] == "conv" else CH_HOLD).append(_row)
CH_CONV_W = sum(a for _, a, _ in CH_CONV)           # 10 arms
CH_HOLD_W = len(CH_HOLD)                            # 2 chains
# C-3's run-time factor reads the same list the scorer reads, as C-5's did at
# step 07, so the denominator cannot drift between the two implementations by
# editing one file.
SWIDS = read_set(os.path.join(AUTH, "baseline-step10-swids.txt"))
SW_EXECN = len(SWIDS)                               # 5

# C-4's.
SQ_CONV, SQ_HOLD = {}, {}
for _f, _kv in _pop(os.path.join(AUTH, "baseline-step10-seqfirst.txt")):
    _b = _f[0]
    (SQ_CONV if _f[1] == "conv" else SQ_HOLD)[_b] = \
        (SQ_CONV if _f[1] == "conv" else SQ_HOLD).get(_b, 0) + 1
SQ_CONV_W = sum(SQ_CONV.values())                   # 7 sites
SQ_HOLD_W = sum(SQ_HOLD.values())                   # 9 sites

# The level, which family C spends as C-1 and B-4 spends as its floor. Both
# spellings are in the target file and both are in level.txt; a rung that let
# them disagree would grade one number and report the other.
assert WANT_MAJOR == T("target.level"), "level.txt and target.txt disagree on the want"
assert HOLD_MAJOR == T("target.level.was"), "level.txt and target.txt disagree on the was"

assert len(RT_CONV) == T("target.rt.conv.buckets"), "reftest conv buckets"
assert RT_CONV_W == T("target.rt.conv.sites"), "reftest conv sites"
assert len(RT_HOLD) == T("target.rt.hold.buckets"), "reftest hold buckets"
assert RT_HOLD_W == T("target.rt.hold.sites"), "reftest hold sites"
assert RT_CONV_W + RT_HOLD_W == T("target.rt.total"), "the two halves are not the 27"
assert not (set(RT_CONV) & set(RT_HOLD)), "a bucket is both converted and held"
# One pair per conv site at this rung. If that ever stops being true the
# arrival half's denominator stops being the departure half's, which is
# survivable -- but silently, so it is asserted rather than assumed.
assert len(RT_ARR) == RT_CONV_W, "a conv bucket names one type twice"

assert len(CH_CONV) == T("target.ch.conv"), "chain conv rows"
assert CH_CONV_W == T("target.ch.arms"), "chain conv arms"
assert CH_HOLD_W == T("target.ch.hold"), "chain hold rows"
assert len(SWIDS) == T("target.ch.ids") and len(set(SWIDS)) == len(SWIDS)
# THE CHAINS ARE DRAWN FROM THE REFTEST ROWS AND THIS IS WHERE THAT IS PROVED.
# A chain is two or more reftest sites in one bucket over one selector, so its
# bucket must be a reftest bucket on the SAME side, and its arm count must not
# exceed the sites that bucket actually holds. A chain file that named a
# method with no reflective test in it would ask C-3 for a pattern switch over
# code that does not exist, and C-2 would never notice.
for _b, _a, _ts in CH_CONV:
    assert _b in RT_CONV, "a conv chain names a bucket C-2 does not convert: " + _b
    assert _a <= RT_CONV[_b], "a conv chain has more arms than the bucket has sites"
    assert set(_ts) <= set(t for k, t in RT_ARR if k == _b), \
        "a conv chain names a type its reftest rows do not: " + _b
for _b, _a, _ts in CH_HOLD:
    assert _b in RT_HOLD, "a hold chain names a bucket C-2 does not hold: " + _b
    assert _a <= RT_HOLD[_b], "a hold chain has more arms than the bucket has sites"
# AND THE DEPARTURE HALF'S DENOMINATOR IS THAT POPULATION, WHICH IS A TIGHTER
# CLAIM THAN THE ONE ABOVE. C-3's departure reads `arms - rtleft[bucket]` and
# divides the sum by CH_CONV_W, so a conv chain's arms must be EXACTLY the conv
# reftest rows its bucket holds. One row fewer and a tree that removed every
# reflective test in the method still reads short of the denominator, so the
# leg could never be collected; one row more and a bucket that kept one reads
# full anyway, so the half it was added for scores nothing. The bucket must
# also hold no `hold` row, because cbuckets() counts RECORDS and not verdicts:
# a held reflective test in a converted chain's method would sit in `rtleft`
# forever and cap the departure half below 1 for every tree there is.
#
# It is 3 + 3 + 2 + 2 = 10 today, and if it ever stops being that this is the
# only place it is visible -- the shell reads `arms=` and the report, never
# these two files together, so a baseline edit that broke it would show up as
# a leg nobody could score rather than as an error.
for _b, _a, _ts in CH_CONV:
    assert _a == RT_CONV[_b], \
        "a conv chain's arms are not its bucket's conv reftest rows: " + _b
    assert _b not in RT_HOLD, \
        "a conv chain's bucket also holds a reftest row that must not move: " + _b
assert sum(RT_CONV[_b] for _b, _, _ in CH_CONV) == CH_CONV_W, \
    "the chain arms and the chain buckets' reftest rows are not the same total"

assert len(SQ_CONV) == T("target.sq.conv.buckets"), "seqfirst conv buckets"
assert SQ_CONV_W == T("target.sq.conv.sites"), "seqfirst conv sites"
assert len(SQ_HOLD) == T("target.sq.hold.buckets"), "seqfirst hold buckets"
assert SQ_HOLD_W == T("target.sq.hold.sites"), "seqfirst hold sites"
assert SQ_CONV_W + SQ_HOLD_W == T("target.sq.total"), "the two halves are not the 16"
assert not (set(SQ_CONV) & set(SQ_HOLD)), "a bucket is both converted and held"
# `seqnew` reads ZERO on the way in, across the whole tree, and that is what
# makes C-4's arrival half a measurement rather than a threshold. The target
# file says so and the input tree's own report has to agree.
assert BASE.num("seqnew.size") == T("target.sq.new"), \
    "the input tree already has a sequenced-collection read"


class Mod(object):
    """One published artifact, as the jar writer will lay it down and as
    arqgate.Modules will read it back.

    Three independent properties, because the gate asks three questions and a
    tree can answer one and not the next:

      artifact  the coordinate the jar file name carries, which is what
                B-15.2 keys its pairwise comparison on. The file is written as
                <artifact>-1.0.0-SNAPSHOT.jar because Modules.artifactOf cuts
                at exactly that string.
      name      the Automatic-Module-Name in the manifest, or "" for a jar that
                declares nothing. This is what mod.declared counts.
      poison    an illegal META-INF/services entry -- a line that is not the
                binary name of a class this jar carries. ModuleFinder refuses
                the whole artifact over it, which is what mod.derivable counts,
                AND IT REFUSES EVEN WHEN THE MANIFEST NAMES THE MODULE. That
                last clause is the one worth a fixture: it is the state the
                input tree's arquillian-impl-base is in. At step 08 it was a
                scored term; at this rung B-15 reports mod.derivable and does
                not spend it, and M1 is the row that asserts the difference.
    """

    def __init__(self, artifact, name="", poison=False):
        self.artifact = artifact
        self.name = name
        self.poison = poison

    @property
    def derived(self):
        """What ModuleFinder derives from the file name when nothing is
        declared: the coordinate with every non-alphanumeric run folded to a
        dot, the trailing -1.0.0-SNAPSHOT having been read as the version."""
        return self.artifact.replace("-", ".")


# The twelve, which is what BOTH ends of this rung publish -- and that is the
# difference from step 08, where the input tree declared nothing and the golden
# declared twelve. Step 09's input tree IS step 08's output, so the manifests
# are already right on the tree the candidate is handed and B-15 reads
# 1.000000 on the pinned fixture. A rung whose inherited gate starts full is
# exactly what item 6's floor has to be argued around, and the NOTES item-6
# entry for this rung does.
POISONED = "arquillian-impl-base"


def goldmods():
    return [Mod(a, n, poison=(a == POISONED)) for a, n in MODNAMES]


def baremods():
    """The same twelve jars, published by a build that has never heard of
    Automatic-Module-Name -- a candidate that deleted step 08's manifest work
    on the way through this rung.

    NOT the input tree's jars, which is what this list was at step 08. Here it
    is a MUTATION and K2 is the fixture that applies it: declared is 0,
    derivable is still 11 -- nothing about the descriptor file changed -- and
    every name the platform hands back is derived from the file name, which
    matches no row of baseline/modnames.txt because every row is a dotted
    org.jboss. prefix and every derived name is `arquillian.something`."""
    return [Mod(a, "", poison=(a == POISONED)) for a, _ in MODNAMES]


GOLDMODS = goldmods()
BAREMODS = baremods()


# --- the arithmetic, reimplemented --------------------------------------------
def f6(x):
    return float("%.6f" % x)


def frac(n, d):
    return f6(0.0 if d <= 0 else float(n) / d)


def clamp01(v):
    return f6(0 if v < 0 else (1 if v > 1 else v))


def minf(a, b):
    return f6(a if a < b else b)


def nsbuckets(rep, keep):
    """nsbuckets(): `pkg|file#method` for every ns record whose package passes."""
    out = set()
    for body in rep.recs("ns"):
        p = field(body, "pkg=")
        if p is None or not keep(p):
            continue
        b = bucket_of(body)
        if b is not None:
            out.add("%s|%s" % (p, b))
    return out


def fbuckets(rep, kind, *wants):
    """fbuckets(): `file#method` for records carrying every named field token."""
    out = set()
    for body in rep.recs(kind):
        toks = body.split()[1:]
        if all(w in toks for w in wants):
            b = bucket_of(body)
            if b is not None:
                out.add(b)
    return out


def b10_score(rep):
    nonanon = max(0, rep.num("newgeneric.explicit") - rep.num("newgeneric.explicitanon"))
    dia_back = max(0, nonanon - T_NONANON)
    d = minf(clamp01(frac(rep.num("newgeneric.diamond"), T_DIA)),
             clamp01(frac(B10_DIA - dia_back, B10_DIA)))
    r = minf(clamp01(frac(rep.num("try.resources"), T_RES)),
             clamp01(frac(B10_CLOSE - rep.num("close.manual"), B10_CLOSE)))
    now_io = rep.num("iofile.new") + rep.num("iofile.decl")
    io_back = max(0, now_io - T_IO)
    n = minf(clamp01(frac(rep.num("niofile.ref") + rep.num("niofile.staticref"), B10_NIO)),
             clamp01(frac(B10_NIO - io_back, B10_NIO)))
    return f6(0.4 * d + 0.3 * r + 0.3 * n), dict(d=d, r=r, n=n, io=now_io)


def b11_score(rep):
    paths = set(site(b)[0] for b in rep.recs("lambda")) \
        | set(site(b)[0] for b in rep.recs("methodref"))
    l = clamp01(frac(len(paths & set(LAMFILES)), B11_LAMFILES))
    sam = set()
    for body in rep.recs("interfacedecl"):
        if "annotated=yes" in body.split():
            sam.add("%s %s" % (site(body)[0], body.split()[1]))
    s = clamp01(frac(len(sam & set(SAMPAIRS)), B11_SAM))
    covc = set(group(rep.recs("stream"))) & set(group(rep.recs("streamop")))
    c = clamp01(frac(len(covc & set(COLLECTMETHODS)), B11_CM))
    return f6(0.20 * l + 0.20 * s + 0.60 * c), dict(l=l, s=s, c=c)


def cell(v):
    """A fact the block never publishes prints as `-`, never as a zero."""
    return "-" if v is None else v


def b12_score(rep, jars, measured):
    """B-12, in the shell's own spellings: a..d inside the measured guard, e out."""
    if not measured:
        a = b = c = jt = 0.0
        sp = ["0", "0", "0", "0"]
    else:
        left = set(group(rep.recs("retnull")))
        got = set(group(rep.recs("optret")))
        opt_dep = len([x for x in OPTBUCKETS if x not in left])
        opt_arr = len(got & set(OPTBUCKETS))
        uses = [x for x in rep.recs("optop") if x.split()[1] == "use"]
        getn = len([x for x in uses if x.split()[2] == "get"])
        optcons = clamp01(frac(len(uses) - getn, len(uses)))
        a = minf(minf(clamp01(frac(opt_dep, B12_OPT)),
                      clamp01(frac(opt_arr, B12_OPT))), optcons)
        aleft = set(group(rep.recs("accum")))
        cgot = {}
        for x in rep.recs("collect"):
            bk = bucket_of(x)
            if bk is not None and field(x, "to=") is not None:
                cgot.setdefault(bk, set()).add(field(x, "to="))
        sink_dep = len([x for x in SINKS if x not in aleft])
        sink_fid = 0
        for x in SINKS:
            w, have = SINKWANT[x], cgot.get(x, set())
            if w in have or (w in ("toList", "toSet") and "toCollection" in have):
                sink_fid += 1
        b = minf(clamp01(frac(sink_dep, B12_SINKS)),
                 clamp01(frac(sink_fid, B12_SINKS)))
        objset = set((bucket_of(x), x.split()[1]) for x in rep.recs("objects"))
        vt = len([v for v in VALUETYPES
                  if (v + "#equals", "equals") in objset
                  and (v + "#hashCode", "hash") in objset])
        c = clamp01(frac(vt, B12_VT))
        mleft = set(group(rep.recs("millis")))
        jgot = set(group(rep.recs("jtime")))
        jt = minf(clamp01(frac(len([x for x in TIMESITES if x not in mleft]), B12_TIME)),
                  clamp01(frac(len(jgot & set(TIMESITES)), B12_TIME)))
        sp = ["%.6f" % v for v in (a, b, c, jt)]
    if jars is None:
        e, esp = 0.0, "0"
    else:
        legs = [clamp01(frac(n, fl)) for n, fl in zip(jars, B12_FLOORS)]
        e = f6((legs[0] * a + legs[1] * b + legs[2] * c + legs[3] * jt) / 4)
        esp = "%.6f" % e
    tot = f6(0.30 * a + 0.35 * b + 0.10 * c + 0.10 * jt + 0.15 * e)
    return "%.6f" % tot, sp + [esp]


def b13_score(rep, measured):
    # `None` is not zero. B-13's six sub-facts live INSIDE the block's measured
    # guard, so a tree the scorer could not compile publishes the total
    # (0.000000, from an awk outside the guard) and NO sub-facts at all. The
    # rehearsal has to assert their absence, which it cannot do if this table
    # hands it a nought to look for.
    if not measured:
        return "0.000000", [None] * 6
    # 2: Class.newInstance -> Constructor.newInstance
    nileg = fbuckets(rep, "newinstance", "form=legacy")
    nictor = fbuckets(rep, "newinstance", "form=ctor")
    dep = len([x for x in NI_LEGACY if x not in nileg])
    arr = len(nictor & set(NI_LEGACY))
    b2 = minf(minf(clamp01(frac(dep, B13_LEGACY)), clamp01(frac(arr, B13_LEGACY))),
              clamp01(frac(rep.num("newinstance.ctor"), B13_CTORFLOOR)))
    # 3: isAccessible() guards gone, setAccessible held
    guard = fbuckets(rep, "accessible", "call=isAccessible")
    setacc = fbuckets(rep, "accessible", "call=setAccessible")
    b3 = minf(minf(clamp01(frac(len([x for x in ACC_GUARD if x not in guard]), B13_GUARDS)),
                   clamp01(frac(len(setacc & set(ACC_GUARD)), B13_GUARDS))),
              clamp01(frac(rep.num("accessible.setaccessible"), B13_SETACC)))
    # 4: boxed constructors gone
    boxed = set(group(rep.recs("boxedctor")))
    b4 = minf(clamp01(frac(len([x for x in BOXEDW if x not in boxed]), len(BOXEDW))),
              clamp01(frac(B13_BOXED - rep.num("boxedctor.size"), B13_BOXED)))
    # 5: hand-rolled builds -> List/Set/Map.of, views held
    lcb = fbuckets(rep, "legacycoll", "kind=build") \
        | fbuckets(rep, "legacycoll", "kind=view", "arg=local")
    lcvf = fbuckets(rep, "legacycoll", "kind=view", "arg=field")
    fact = set(group(rep.recs("factory")))
    b5 = minf(minf(clamp01(frac(len([x for x in BUILDS if x not in lcb]), B13_BUILDS)),
                   clamp01(frac(len(fact & set(BUILDS)), B13_BUILDS))),
              clamp01(frac(len(lcvf & set(VIEWS)), B13_VIEWS)))
    # 6: isPresent() branches -> or/ifPresentOrElse/stream
    isp = fbuckets(rep, "optop", "isPresent", "use")
    opt9 = set()
    for x in rep.recs("optop"):
        f = x.split()
        if len(f) > 2 and f[1] == "use" and f[2] in ("or", "ifPresentOrElse", "stream"):
            bk = bucket_of(x)
            if bk is not None:
                opt9.add(bk)
    b6 = minf(clamp01(frac(len([x for x in OPTOPS if x not in isp]), B13_OPTDEP)),
              clamp01(frac(len(opt9 & set(OPTARR)), B13_OPTARR)))
    # 7: var on the redundant declarations
    b7 = minf(clamp01(frac(B13_CAND - rep.num("var.candidatenew"), B13_WANT)),
              clamp01(frac(rep.num("var.size"), B13_WANT)))
    tot = f6((0.12 * b2 + 0.18 * b3 + 0.03 * b4 + 0.26 * b5
              + 0.23 * b6 + 0.08 * b7) / 0.90)
    return "%.6f" % tot, ["%.6f" % v for v in (b2, b3, b4, b5, b6, b7)]


def b14_score(rep, measured, execbroken=()):
    """B-14: step 07's whole family C, inherited as ONE gate of family B.

    execbroken: the exchange ids family A no longer matches, or None for a
    behaviour summary that could not be read at all. Every source-tree fixture
    leaves it empty -- the summaries those runs produce carry `behavior.build=ok`
    and nothing else -- and the BEHAV* runs in c-rehearsal.sh pass a list.

    Returns (b14, [b14_2..b14_5], gate facts).

    TWO THINGS MOVED FROM STEP 07 AND NOTHING ELSE DID.

    The FIRST is the multiplier. Step 07 wrote its family total as
    `c1 = reach * (composite/0.90)` and then spent `reach` a second time inside
    `tot`; the composite here is multiplied by `p`, the anti-gaming PENALTY,
    and by nothing else. There is no reach at this rung to multiply by -- the
    class-file level does not move -- and putting the penalty there rather than
    outside is what keeps a raw type or a silenced warning costing the same
    fraction of a gate it used to cost of a family.

    The SECOND is the divisor's provenance. 0.07 + 0.17 + 0.12 + 0.54 = 0.90 is
    step 07's four legs with its C-1's 0.10 removed, and dividing by 0.90 is
    what renormalises the remaining four onto [0, 1] so that B-14 is a gate
    like every other gate in family B. Stated as a comment rather than folded
    into four rescaled literals, because the four ORIGINAL weights are what the
    Dockerfile greps for and what makes the inheritance legible.

    The gate-fact keys carry a `b14.` prefix -- `gate.b14.c2.want` and not
    `gate.c2.want` -- because this rung's family C published four of those
    spellings first and a scorer with two live `gate.c2.` families would be one
    typo away from reading the wrong one.
    """
    g = {}
    if not measured:
        b, c, d, e = 0.0, 0.0, 0.0, 0.0
        pen = 1.0
    else:
        jxann, jxejb = rep.num("ns.ref.javax.annotation"), rep.num("ns.ref.javax.ejb")
        jkann, jkejb = rep.num("ns.ref.jakarta.annotation"), rep.num("ns.ref.jakarta.ejb")
        tasized, tactor = rep.num("toarray.sized"), rep.num("toarray.ctorref")
        taraw = rep.num("toarray.raw")
        hlegref, hlegcall = rep.num("http.legacyref"), rep.num("http.legacycall")
        g.update({"src.jxann": jxann, "src.jxejb": jxejb, "src.jkann": jkann,
                  "src.jkejb": jkejb, "src.tasized": tasized, "src.tactor": tactor,
                  "src.taraw": taraw, "src.hlegref": hlegref, "src.hlegcall": hlegcall,
                  "src.hclient": rep.num("http.clientref"),
                  "src.hredir": rep.num("http.redirects")})

        # B-14.2. The want set is keyed pkg|bucket; the arrival side is the same
        # keys with the leading javax. rewritten, exactly as the shell's
        # `sed 's/^javax\./jakarta./'` rewrites them.
        MOVED = ("javax.annotation", "javax.ejb")
        jxmove = nsbuckets(rep, lambda p: p in MOVED)
        jkmove = nsbuckets(rep, lambda p: p in ("jakarta.annotation", "jakarta.ejb"))
        wantjx = set("%s|%s" % (p, q) for p, q in NSMOVE)
        wantjk = set(re.sub(r"^javax\.", "jakarta.", x) for x in wantjx)
        b2dep, b2arr = len(wantjx - jxmove), len(wantjk & jkmove)
        b2jx, b2jk = jxann + jxejb, jkann + jkejb
        g.update({"b14.c2.want": len(wantjx), "b14.c2.departed": b2dep,
                  "b14.c2.arrived": b2arr, "b14.c2.javax.left": b2jx,
                  "b14.c2.jakarta.refs": b2jk})
        b = minf(minf(clamp01(frac(b2dep, C2_BUCKETS)),
                      clamp01(frac(b2arr, C2_BUCKETS))),
                 minf(clamp01(frac(C2_REFS - b2jx, C2_REFS)),
                      clamp01(frac(b2jk, C2_REFS))))

        # B-14.3, and the reference count is over PACKAGES rather than buckets:
        # the shell reads field 2 of every ns record and strips `pkg=`, so a
        # reference in a bucket nobody froze still counts towards the total.
        hold = nsbuckets(rep, lambda p: p not in MOVED and not p.startswith("jakarta."))
        wanthold = set("%s|%s" % (p, q) for p, q in NSHOLD)
        b3held = len(wanthold & hold)
        b3refs = len([x for x in rep.recs("ns")
                      if x.split()[1][4:] in NSHOLD_PKGS])
        g.update({"b14.c3.want": len(wanthold), "b14.c3.held": b3held,
                  "b14.c3.refs": b3refs})
        b3raw = minf(clamp01(frac(b3held, C3_BUCKETS)),
                     clamp01(frac(b3refs, C3_REFS)))
        g["b14.c3.unscaled"] = "%.6f" % b3raw
        c = f6(b3raw * b)

        # B-14.4
        sizedb = fbuckets(rep, "toarray", "form=sized")
        ctorb = fbuckets(rep, "toarray", "form=ctorref")
        b4dep, b4arr = len(set(TAWANT) - sizedb), len(ctorb & set(TAWANT))
        g.update({"b14.c4.want": len(TAWANT), "b14.c4.departed": b4dep,
                  "b14.c4.arrived": b4arr})
        d = minf(minf(clamp01(frac(b4dep, C4_BUCKETS)),
                      clamp01(frac(b4arr, C4_BUCKETS))),
                 minf(clamp01(frac(C4_SIZED - tasized, C4_SIZED)),
                      clamp01(frac(tactor, C4_SIZED))))

        # B-14.5, scoped to one file and not to a bucket.
        hf = [x for x in rep.recs("http") if site(x)[0] == C5_FILE]
        b5leg = len([x for x in hf
                     if x.split()[1] in ("form=legacytype", "form=legacycall")])
        names = set()
        for x in hf:
            if x.split()[1] != "form=clienttype":
                continue
            n = field(x, "name=")
            if n is None:
                continue
            q = n.split(".")
            if len(q) >= 4 and q[0] == "java" and q[1] == "net" and q[2] == "http" \
               and q[3] != "":
                n = ".".join(q[:4])
            names.add(n)
        b5red = 1 if [x for x in hf if x.split()[1] == "form=redirects"] else 0
        b5stub = max(0, hlegref + hlegcall - b5leg)
        g.update({"b14.c5.legacy": b5leg, "b14.c5.types": len(names),
                  "b14.c5.redirects": b5red, "b14.c5.cdistub": b5stub})
        b5d = clamp01(frac(C5_LEGACY - b5leg, C5_LEGACY))
        b5a = clamp01(frac(len(names), C5_TYPES))
        # The two run-time factors. `execbroken is None` is the unreadable
        # summary: it takes the whole of B-14.5, and it takes the redirect
        # quarter with it, because a proviso that cannot be checked is not
        # satisfied.
        if execbroken is None:
            b5exec, redbroken = 0.0, True
        else:
            b5exec = clamp01(frac(C5_EXECN - len(execbroken), C5_EXECN))
            redbroken = C5_REDID in execbroken
        b5redok = 0 if redbroken else b5red
        g.update({"b14.c5.exec.want": C5_EXECN,
                  "b14.c5.exec.broken": C5_EXECN if execbroken is None else len(execbroken),
                  "b14.c5.exec": "%.6f" % b5exec,
                  "b14.c5.redirects.honoured": b5redok})
        e = f6(b5exec * minf(b5d, b5a) * (0.75 + 0.25 * b5redok))

        # The three penalty terms, all inside the measured guard.
        raw_added = max(0, rep.num("newgeneric.raw") - BASE_RAW)
        g["src.raw.added"] = raw_added
        pen, pens = 1.0, "1"
        if raw_added > 0:
            pen = f6(max(0.0, 1 - raw_added / 10.0))
            pens = "%.6f" % pen
        if rep.num("suppress.sites") > BASE_SUP:
            pen = f6(pen / 2)
            pens = "%.6f" % pen
        if taraw > 0:
            pen = f6(pen / 2)
            pens = "%.6f" % pen
        g["src.penalty"] = pens

    b14 = f6(pen * ((0.07 * b + 0.17 * c + 0.12 * d + 0.54 * e) / 0.90))
    # B-14's total is published either way -- the awk that computes it sits
    # outside the guard -- but its four parts are `None` on a broken gate for
    # the same reason B-13's sub-facts are: the block never reaches their
    # `fact` lines.
    parts = [None] * 4 if not measured else ["%.6f" % v for v in (b, c, d, e)]
    return "%.6f" % b14, parts, g


def cbuckets(rep, kind, *wants):
    """cbuckets(): `file#method` -> HOW MANY records, not whether any.

    Family C needs counts where family B needed membership, because three of
    this rung's buckets hold more than one site -- ObserverImpl#invoke has two
    `instanceof`s and ServletTestRunner#doGet has sixteen newline literals --
    and a set would score "one of the two converted" as done. The shell keeps
    the two helpers separate for the same reason; so does this.
    """
    out = {}
    for body in rep.recs(kind):
        toks = body.split()[1:]
        if not all(w in toks for w in wants):
            continue
        b = bucket_of(body)
        if b is None:
            continue
        out[b] = out.get(b, 0) + 1
    return out


def fcount(rep, kind, *wants):
    """fcount(): `file` -> how many records. cbuckets' key without the method.

    A `switch` and a `sealed` have no enclosing-method key in SourceScan's
    output -- a statement and a declaration are not in a method in the sense
    the bucket key means -- so those populations are counted per file. A record
    with no `in=` is kept here and dropped by cbuckets: the difference is the
    point of having both.

    `textblock` is NOT one of them any more. It carries an `in=` as of round 1,
    because C-3's arrival half is a claim about one METHOD, so C-3 reads text
    blocks through cbuckets() above. This helper keeps C-5's `switch`.
    """
    out = {}
    for body in rep.recs(kind):
        toks = body.split()[1:]
        if not all(w in toks for w in wants):
            continue
        f = site(body)[0]
        out[f] = out.get(f, 0) + 1
    return out


def capsum(want, have):
    """sum of min(have, want) over the want buckets -- capsum() in the shell.

    Capped per bucket so that a tree which left thirty newline literals in one
    held method cannot pay for three it deleted from another.
    """
    return sum(min(have.get(b, 0), n) for b, n in want.items())


def cnamesets(rep, kind, fld):
    """cnames(): the set of `(bucket, simple type name)` pairs one field names.

    The shell's third record helper, and the only one that reads a field whose
    value is a LIST. `types=` on a swpat record is semicolon-separated and
    spelled as the SOURCE wrote it, so a pattern over `Map<String,Foo>` arrives
    with its arguments attached; `type=` on an ipattern record is one resolved
    FQN. Both are cut at the first `<` and THEN stripped of leading package
    segments, in that order, because a parameterised type's arguments are
    package-qualified too and stripping first would leave the argument behind.

    A record with no bucket, an empty value or a `-` is dropped whole, which is
    how the ten `type=-` rows of the reftest population never reach the
    arrival set. Ordering and duplicates do not survive the shell's `sort -u`,
    so this returns a set and not a list.
    """
    out = set()
    for body in rep.recs(kind):
        b = bucket_of(body)
        v = field(body, fld + "=")
        if b is None or not v or v == "-":
            continue
        for part in v.split(";"):
            s = strip_pkg(strip_generic(part))
            if s:
                out.add((b, s))
    return out


def swpat_pat(rep):
    """bucket -> the LARGEST `patterns=` any switch in it declares.

    A MAX and not a sum. C-3 compares one number against one chain's arm count,
    and two two-arm switches in one method are not a four-arm switch -- they
    are two dispatches, and the chain the leg is asking about is one. A sum
    would let a candidate who split the three-arm chain into a two-arm switch
    and a leftover `if` score the leg whole.

    A record whose `patterns=` is 0 or unreadable creates no entry at all,
    which is the awk's behaviour rather than a choice: `if (p > n[b])` is false
    for an unset n[b] when p is 0, so the bucket never enters the array and
    the caller's `($1 in pat)` test reads false rather than zero. The two agree
    here only because the caller compares `>= arms` and arms is never 0.
    """
    out = {}
    for body in rep.recs("swpat"):
        b = bucket_of(body)
        if b is None:
            continue
        v = field(body, "patterns=")
        n = int(v) if v and re.match(r"^\d+$", v) else 0
        if n > out.get(b, 0):
            out[b] = n
    return out



def b15_score(rep, mods, measured, execbroken=()):
    """B-15: step 08's family C, minus its C-1, over the 0.80 that is left.

    Two legs and no third. Step 08's C-1 was min(declared/12, derivable/11,
    right/12) and every one of those three terms is either held from inside
    C-2 -- the right-name count IS C-2's fraction -- or was measured against a
    number its own input tree already reached. What survives is the square of
    the pairwise name fraction, and the service-lookup min() scaled by the 69
    recordings, at step 08's own weights renormalised over 0.35 + 0.45.

    `mods` is the fixture's jar plan, the same object write_jars() was handed.
    Returns (B15, [B15.2, B15.3], gate facts).
    """
    g = {}
    if mods is None:
        g["b15.mod"] = "no-module-jars"
        total = declared = derivable = 0
        got = set()
    else:
        total = len(mods)
        declared = len([m for m in mods if m.name])
        derivable = len([m for m in mods if not m.poison])
        got = set()
        for m in mods:
            got.add("%s %s" % (m.artifact, m.name if m.name
                               else (m.derived if not m.poison else "-")))
    g["b15.mod.total"] = total
    g["b15.mod.declared"] = declared
    g["b15.mod.derivable"] = derivable

    # DERIVABILITY IS REPORTED AND NOT SCORED, which is the one behavioural
    # difference between this gate and the step-08 gate it carries forward.
    # M1 below is the fixture that proves it: a poisoned service file that cost
    # step 08 a third of its C-1 costs this rung nothing, because the question
    # step 08 was asking with it -- "can the platform read a descriptor out of
    # this jar at all" -- is one the input tree already answered.
    want = set("%s %s" % (a, n) for a, n in MODNAMES)
    names = 0 if mods is None else len(want & got)
    g["b15.names"] = names
    b15right = clamp01(frac(names, MOD_TOTAL))
    b15_2 = f6(b15right * b15right)

    if measured:
        legacy = fbuckets(rep, "svcload", "form=legacyscan") \
            | fbuckets(rep, "svcload", "form=loadclass")
        arrive = fbuckets(rep, "svcload", "form=platformload")
        dep = clamp01(frac(len([x for x in SVC_LEGACY if x not in legacy]),
                           T_SVC_LEGACY))
        arr = clamp01(frac(len([x for x in SVC_ARRIVE if x in arrive]),
                           T_SVC_ARRIVE))
        keep = clamp01(frac(len([x for x in SVC_KEEP if x in legacy]),
                            T_SVC_KEEP))
        g["b15.departed"] = "%.6f" % dep
        g["b15.arrived"] = "%.6f" % arr
        g["b15.held"] = "%.6f" % keep
    else:
        # The shell literal `0`, carried forward from step 08's own note: the
        # three are initialised above the guard and the else branch only
        # echoes, so an uncompilable tree publishes `0` here and not the
        # `0.000000` a clamp01 would have returned.
        dep = arr = keep = 0.0
        g["b15.departed"] = g["b15.arrived"] = g["b15.held"] = "0"

    # `execbroken` is the WHOLE differ/missing id list and the filter is done
    # here, because this rung has two of these scalings reading one summary --
    # B-15.3 against svcids.txt and C-3 against tbids.txt -- and the shell
    # greps each against its own baseline list. A caller that pre-filtered
    # would have to know which gate it was feeding.
    if execbroken is None:
        b15exec, broken = 0.0, len(SVCIDS)
    else:
        broken = len([x for x in execbroken if x in SVCIDS])
        b15exec = clamp01(frac(len(SVCIDS) - broken, len(SVCIDS)))
    g["b15.exec.want"] = len(SVCIDS)
    g["b15.exec.broken"] = broken
    g["b15.exec"] = "%.6f" % b15exec
    b15_3 = f6(min(dep, arr, keep) * b15exec)

    b15 = f6((0.35 * b15_2 + 0.45 * b15_3) / 0.80)
    return "%.6f" % b15, ["%.6f" % b15_2, "%.6f" % b15_3], g


def b16_score(rep, measured, execbroken=()):
    """B-16: step 09's family C, minus its C-1, over the 0.90 that is left.

    Four legs and no fifth. Step 09's C-1 was the class-file major, and the
    major is this rung's OWN must-flip -- 65 where step 09 wanted 61 -- so
    carrying it forward would have graded the same artefact twice, once as an
    inherited gate that is satisfied by 61 and once as family C's C-1 that is
    only satisfied by 65. The two would contradict each other on every tree in
    between. What is left are the four SOURCE constructs, which a step-10 tree
    has no reason to touch and every reason to keep, renormalised over the 0.90
    they carried between them: 0.20, 0.26, 0.36 and 0.08 divided by 0.90.

    THE LEGS KEEP STEP 09'S NUMBERING -- 2 through 5 -- because that is what
    the shipped block calls them, `b16_2` .. `b16_5`, and renumbering them 1..4
    here would make the two implementations unreadable side by side. `c2` below
    is `b16_2` in test.sh and nothing else.

    Returns (B16, [B16.2..B16.5], gate facts).

    TWO SPELLINGS OF ZERO AND BOTH ARE MODELLED. B-16.2, B-16.4 and B-16.5 are
    `0` when nothing compiled, because the shell initialises them `0` above the
    measured guard and the guard is where the computation lives. B-16.3 is
    `0.000000` even then, because its final assignment sits OUTSIDE that guard
    -- it multiplies by the execution scaling, which is read from the behaviour
    summary and not from the report. Normalising the two into one would make
    the rehearsal's transcription disagree with the scorer on exactly the
    fixtures where a disagreement is hardest to read.
    """
    g = {}

    # ---- B-16.2: pattern instanceof, three fractions through a min() --------
    if measured:
        bound = cbuckets(rep, "ipattern", "kind=bound")
        plain = cbuckets(rep, "ipattern", "kind=plain")
        conv_n = capsum(IP_CONV, bound)
        hold_n = capsum(IP_SKIP, plain)
        size = rep.num("ipattern.size")
        g["b16.2.converted"] = conv_n
        g["b16.2.converted.want"] = IP_CONV_W
        g["b16.2.held"] = hold_n
        g["b16.2.held.want"] = IP_SKIP_W
        g["b16.2.sites"] = size
        g["b16.2.sites.want"] = IP_TOTAL
        c2conv = clamp01(frac(conv_n, IP_CONV_W))
        c2hold = clamp01(frac(hold_n, IP_SKIP_W))
        # THE TOTAL-SITE TERM IS WHAT STOPS DELETION FROM SCORING. Without it a
        # tree that deleted the seven sites with no cast after them would read
        # the hold half at 7/7 by vacuum -- capsum caps at the want and reads 0
        # for a bucket that is gone, so the hold half already catches that one;
        # what this term catches is the reverse, a tree that converted all 28
        # including the seven that must not move. L4 and L5 are the two rows.
        c2tot = clamp01(frac(size, IP_TOTAL))
        g["b16.2.convfrac"] = "%.6f" % c2conv
        g["b16.2.holdfrac"] = "%.6f" % c2hold
        g["b16.2.totfrac"] = "%.6f" % c2tot
        c2 = minf(minf(c2conv, c2hold), c2tot)
        c2s = "%.6f" % c2
    else:
        c2 = 0.0
        c2s = "0"

    # ---- B-16.3: one text block, and the XML builders left alone -----------
    #
    # c3move and c3hold are initialised above the guard, but c3 itself is
    # assigned below it, so this leg prints a %.6f on every fixture.
    if measured:
        left = cbuckets(rep, "strnl")
        tbf = cbuckets(rep, "textblock")
        move_w = len(TB_MOVE)
        move_n = 0
        for b in TB_MOVE:
            # BOTH HALVES OF ONE MOVE, ON ONE KEY. The literals have to be gone
            # from the method AND a text block has to have arrived in the
            # method: a deletion satisfies the first on its own, which is why
            # L6 exists, and a text block planted elsewhere in the same file
            # satisfied the second until round 1, which is why L14 exists.
            if b not in left and b in tbf:
                move_n += 1
        hold_n = capsum(TB_HOLD, left)
        g["b16.3.moved"] = move_n
        g["b16.3.moved.want"] = move_w
        g["b16.3.held"] = hold_n
        g["b16.3.held.want"] = TB_HOLD_W
        g["b16.3.textblocks"] = rep.num("textblock.size")
        c3move = clamp01(frac(move_n, move_w))
        c3hold = clamp01(frac(hold_n, TB_HOLD_W))
        g["b16.3.movefrac"] = "%.6f" % c3move
        g["b16.3.holdfrac"] = "%.6f" % c3hold
    else:
        # The shell LITERAL `0`, the same way b15.departed/arrived/held are
        # spelled above. b16_3move and b16_3hold are initialised `0` above the
        # measured guard and published below it, so a tree nobody could compile
        # publishes the initialiser and not a computed zero. Formatting these
        # two with %.6f unconditionally claimed a measurement had been taken on
        # K17, which is the one fixture where no measurement was possible.
        #
        # B-16.3's TOTAL is different and stays %.6f on every fixture: it is
        # assigned below the guard and multiplies by the execution scaling,
        # which is read from the behaviour summary rather than from the report.
        # Two facts of one leg, two spellings of zero, and the difference is
        # which side of the guard the assignment sits on.
        c3move = c3hold = 0.0
        g["b16.3.movefrac"] = "0"
        g["b16.3.holdfrac"] = "0"

    if execbroken is None:
        c3exec, c3bad = 0.0, len(TBIDS)
    else:
        c3bad = len([x for x in execbroken if x in TBIDS])
        c3exec = clamp01(frac(len(TBIDS) - c3bad, len(TBIDS)))
    g["b16.3.exec.want"] = len(TBIDS)
    g["b16.3.exec.broken"] = c3bad
    g["b16.3.exec"] = "%.6f" % c3exec
    c3 = f6(min(c3move, c3hold) * c3exec)
    c3s = "%.6f" % c3

    # ---- B-16.4: eleven hierarchies closed, eleven types left open ---------
    if measured:
        got, gotkeys = set(), set()
        for body in rep.recs("sealed"):
            nm = field(body, "name=")
            kd = field(body, "kind=")
            pn = field(body, "names=")
            if kd != "sealed" or not nm:
                continue
            f = site(body)[0]
            perm = [] if not pn else sorted(strip_pkg(x) for x in pn.split(";"))
            got.add((f, nm, ";".join(perm)))
            gotkeys.add((f, nm))
        want = set((f, n, ";".join(sorted(p))) for f, n, p in SEAL_ROOT)
        c4roots = len(want & got)
        c4open = len([k for k in SEAL_OPEN if k not in gotkeys])
        g["b16.4.decl"] = rep.num("sealed.decl")
        g["b16.4.nonsealed"] = rep.num("sealed.nonsealed")
        g["b16.4.permits"] = rep.num("sealed.permits")
        g["b16.4.roots"] = c4roots
        g["b16.4.roots.want"] = len(SEAL_ROOT)
        g["b16.4.open"] = c4open
        g["b16.4.open.want"] = len(SEAL_OPEN)
        c4a = clamp01(frac(c4roots, len(SEAL_ROOT)))
        c4b = clamp01(frac(c4open, len(SEAL_OPEN)))
        g["b16.4.closedfrac"] = "%.6f" % c4a
        g["b16.4.openfrac"] = "%.6f" % c4b
        c4 = minf(c4a, c4b)
        c4s = "%.6f" % c4
    else:
        c4 = 0.0
        c4s = "0"

    # ---- B-16.5: two colon-form switches become arrow form -----------------
    if measured:
        arrow = fcount(rep, "switch", "form=arrow")
        colon = fcount(rep, "switch", "form=colon")
        # PER FILE AND BOTH CONDITIONS: enough arrows AND no colon left. A file
        # that gained an arrow switch beside the colon one it already had has
        # done nothing, and L13 is the row that says so.
        c5hit = len([f for f, n in SW_WANT.items()
                     if arrow.get(f, 0) >= n and colon.get(f, 0) == 0])
        g["b16.5.files"] = c5hit
        g["b16.5.files.want"] = len(SW_WANT)
        g["b16.5.arrow"] = rep.num("switch.arrow")
        g["b16.5.colon"] = rep.num("switch.colon")
        g["b16.5.expr"] = rep.num("switch.expr")
        g["b16.5.size"] = rep.num("switch.size")
        c5 = clamp01(frac(c5hit, len(SW_WANT)))
        c5s = "%.6f" % c5
    else:
        c5 = 0.0
        c5s = "0"

    # The shell divides the weighted sum by 0.90 on one line, which is not the
    # same arithmetic as scaling each weight to six places first and summing
    # those: 0.36/0.90 is 0.4 exactly but 0.26/0.90 is not, and a table built
    # from rounded weights drifts from the scorer in the sixth place on any
    # fixture where the legs disagree. Divide once, at the end, as it does.
    tot = f6((0.20 * c2 + 0.26 * c3 + 0.36 * c4 + 0.08 * c5) / 0.90)
    return "%.6f" % tot, [c2s, c3s, c4s, c5s], g


def fc_score(rep, scan, measured, execbroken=()):
    """Family C: Java 21 itself. Four legs, 0.04/0.17/0.65/0.14.

    `scan` is the fixture's arqgate.Scan reading -- a dict of the two keys C-1
    spends, or None for a tree that produced no scan at all. It is a SEPARATE
    channel from `rep`: the class files and the source AST are read by
    different jars from different inputs, and L2 below is the fixture that
    moves one without the other.

    Returns (familyC, [C1..C4], gate facts).

    THREE OF THE FOUR LEGS ASK A DEPARTURE AND AN ARRIVAL AND SCORE THE WORSE
    ANSWER, which is the shape step 07 introduced and every rung since has
    kept: a min(), so that deleting the code scores as badly as leaving it.
    All three add a third term -- the population that must NOT move -- because
    each of this rung's constructs has a look-alike that cannot be converted
    at all, and a candidate who converts all 27 reflective tests, or the two
    chains whose arms are Class values, or all 16 first-element reads, has
    written a tree that does not compile.
    C-3'S DEPARTURE HALF IS ROUND 1'S AND IT IS THE ONE THIS LEG DID NOT HAVE.
    Arrival asks that a covering pattern switch be IN the method, which a
    switch planted beside a live `isInstance` chain satisfies while the chain
    goes on running -- the whole 0.65, for four switches that convert nothing.
    N14 is that tree and it now reads 0 on the leg.

    TWO SPELLINGS OF ZERO AND BOTH ARE MODELLED, as at every rung since 07.
    C-1, C-2 and C-4 are `0` when their guard does not open, because the shell
    assigns the initialiser and the else branch only emits a message. C-3 is
    `0.000000` even then, because its final assignment sits OUTSIDE the
    measured guard -- it multiplies by the execution scaling, which is read
    from the behaviour summary and not from the report.
    """
    g = {}

    # ---- C-1: the class-file major, over the class files there are ----------
    #
    # A different major from step 09's and the same arithmetic. The guard is on
    # scan.txt alone and not on `measured`: a tree can emit class files and no
    # source report, and L2 is the row that says the two channels are read
    # independently.
    if scan is None:
        c1 = 0.0
        g["c1"] = "no-scan"
        c1s = "0"
    else:
        cls = int(scan.get("classes", 0))
        have = int(scan.get("major.%d" % WANT_MAJOR, 0))
        g["c1.classes"] = cls
        g["c1.at%d" % WANT_MAJOR] = have
        c1 = frac(have, cls)
        c1s = "%.6f" % c1

    # ---- C-2: reflective type tests become type patterns --------------------
    #
    # THE ARRIVAL HALF UNIONS TWO RECORD KINDS and that is the whole of why it
    # is not simply "a bound ipattern appeared". Ten of the thirteen conv sites
    # sit in the four chains C-3 asks for a pattern switch over, and a pattern
    # switch emits `swpat` records and no `ipattern` ones; the other three are
    # single tests and become `instanceof` patterns. A leg that read only
    # ipattern would score a correct tree at 3/13 and tell it to un-write the
    # switches.
    if measured:
        left = cbuckets(rep, "reftest")
        arrived = (cnamesets(rep, "ipattern", "type")
                   | cnamesets(rep, "swpat", "types"))
        dep_n = sum(max(0, w - left.get(b, 0)) for b, w in RT_CONV.items())
        hold_n = capsum(RT_HOLD, left)
        arr_n = len([k for k in RT_ARR if k in arrived])
        g["c2.departed"] = dep_n
        g["c2.departed.want"] = RT_CONV_W
        g["c2.arrived"] = arr_n
        g["c2.arrived.want"] = len(RT_ARR)
        g["c2.held"] = hold_n
        g["c2.held.want"] = RT_HOLD_W
        g["c2.reftests"] = rep.num("reftest.size")
        g["c2.patterns"] = rep.num("ipattern.size")
        g["c2.swpatterns"] = rep.num("swpat.patterns")
        c2dep = clamp01(frac(dep_n, RT_CONV_W))
        c2arr = clamp01(frac(arr_n, len(RT_ARR)))
        c2hold = clamp01(frac(hold_n, RT_HOLD_W))
        g["c2.depfrac"] = "%.6f" % c2dep
        g["c2.arrfrac"] = "%.6f" % c2arr
        g["c2.holdfrac"] = "%.6f" % c2hold
        c2 = minf(minf(c2dep, c2arr), c2hold)
        c2s = "%.6f" % c2
    else:
        c2 = 0.0
        c2s = "0"

    # ---- C-3: four chains become four pattern switches ----------------------
    #
    # ARMS AND NOT CHAINS on the conv side, so a three-arm chain converted is
    # worth more than a two-arm one; CHAINS and not arms on the hold side,
    # because the hold half is asking whether one method was left alone and a
    # per-arm denominator there would pay a candidate for converting half of a
    # dispatch it should not have touched at all.
    #
    # THE DEPARTURE HALF IS C-2'S, ARM FOR ARM, and it is here because arrival
    # on its own was collectable by ADDITION. `cbuckets("reftest")` a second
    # time, and the repetition is the shell's: C-2 built the same map a few
    # dozen lines above under a guard on reftest.txt and this one is under a
    # guard on chain.txt, so a missing set file costs ONE leg and not two.
    # The denominator is CH_CONV_W for both halves -- the same 10 arms counted
    # once -- so the two cannot come to disagree about how much there is to do.
    #
    # AND THE RESIDUAL IS TWO RECORD KINDS, WHICH IS ROUND 2'S CORRECTION.
    # `reftest` alone is not the old spelling: rewrite each chain arm as
    # `if (v instanceof EnterpriseArchive e)` -- requirement 4's own work, which
    # OpenRewrite does unattended on eight of these ten arms -- leave the chain
    # standing and plant the covering switch beside it, and every reftest record
    # is gone while the if-chain is still what dispatches. So what is left in
    # the bucket is counted as the reftest records there PLUS how many of that
    # row's types are still named by an `ipattern` there, and the departure is
    # max(0, arms - that). The `ipattern` map is the one C-2's arrival half
    # unions, read here with the opposite sign: C-2 asks whether a type arrived
    # anywhere, C-3 asks whether a chain type is still tested one arm at a time
    # in the method that dispatched on it. N15 is the row.
    if measured:
        left = cbuckets(rep, "reftest")
        pat = swpat_pat(rep)
        ip = cnamesets(rep, "ipattern", "type")
        nm = cnamesets(rep, "swpat", "types")
        ipleft = {b: len([t for t in ts if (b, t) in ip]) for b, _, ts in CH_CONV}
        dep_n = sum(max(0, arms - left.get(b, 0) - ipleft[b])
                    for b, arms, _ in CH_CONV)
        arr_n = 0
        for b, arms, ts in CH_CONV:
            ok = pat.get(b, 0) >= arms
            for t in ts:
                if (b, t) not in nm:
                    ok = False
            if ok:
                arr_n += arms
        hold_n = 0
        for b, arms, ts in CH_HOLD:
            if not [t for t in ts if (b, t) in nm]:
                hold_n += 1
        g["c3.departed"] = dep_n
        g["c3.departed.want"] = CH_CONV_W
        # Published and not scored, the shell's spelling: the `instanceof` half
        # of the residual on its own, so a failed run says WHICH old spelling
        # is still there -- the reflective call nothing touched, or the
        # `instanceof` requirement 4 asked for and requirement 5 did not stop
        # at. The golden reads 0 because none of the four conv methods holds an
        # `instanceof` at all; N15 reads 10.
        g["c3.ipleft"] = sum(ipleft.values())
        g["c3.arrived"] = arr_n
        g["c3.arrived.want"] = CH_CONV_W
        g["c3.held"] = hold_n
        g["c3.held.want"] = CH_HOLD_W
        g["c3.switches"] = rep.num("swpat.size")
        g["c3.patterns"] = rep.num("swpat.patterns")
        g["c3.nullcase"] = rep.num("swpat.nullcase")
        c3dep = clamp01(frac(dep_n, CH_CONV_W))
        c3arr = clamp01(frac(arr_n, CH_CONV_W))
        c3hold = clamp01(frac(hold_n, CH_HOLD_W))
    else:
        # The shell LITERAL `0`: the three sub-terms are initialised above the
        # measured guard and published below it, so a tree nobody could compile
        # publishes the initialiser and not a computed zero.
        c3dep = c3arr = c3hold = 0.0
    g["c3.depfrac"] = "%.6f" % c3dep if measured else "0"
    g["c3.arrfrac"] = "%.6f" % c3arr if measured else "0"
    g["c3.holdfrac"] = "%.6f" % c3hold if measured else "0"

    # `case null` IS NOT SCORED STATICALLY -- NOTES 15.4. What scales C-3 is
    # the five recorded transcripts, which is the only reading of the four
    # chains' behaviour on an input no arm matches that is not an author's
    # opinion frozen into a gate. A summary that could not be read scales the
    # leg to zero, which is B-10's rule about a proviso that cannot be
    # checked. EXEC IS A MULTIPLIER AND NOT A TERM, so it cannot stand in for
    # the departure half: a tree that adds a switch beside a live chain breaks
    # no recording at all.
    if execbroken is None:
        c3exec, c3bad = 0.0, len(SWIDS)
    else:
        c3bad = len([x for x in execbroken if x in SWIDS])
        c3exec = clamp01(frac(len(SWIDS) - c3bad, len(SWIDS)))
    g["c3.exec.want"] = len(SWIDS)
    g["c3.exec.broken"] = c3bad
    g["c3.exec"] = "%.6f" % c3exec
    c3 = f6(min(c3dep, c3arr, c3hold) * c3exec)
    c3s = "%.6f" % c3

    # ---- C-4: seven guarded first-element reads become getFirst() -----------
    #
    # The arrival half counts BUCKETS and the departure half counts SITES, and
    # they are 7 and 7 at this rung only because every conv bucket holds
    # exactly one site. The shell computes its own `sq_arr_w` from the
    # population rather than reusing `sq_dep_w`, so a later rung that added a
    # second convertible read to one of these seven methods would move one
    # denominator and not the other; this reimplementation does the same.
    if measured:
        left = cbuckets(rep, "seqfirst")
        first = cbuckets(rep, "seqnew", "op=getFirst")
        dep_n = sum(max(0, w - left.get(b, 0)) for b, w in SQ_CONV.items())
        hold_n = capsum(SQ_HOLD, left)
        arr_n = len([b for b in SQ_CONV if first.get(b, 0) > 0])
        g["c4.departed"] = dep_n
        g["c4.departed.want"] = SQ_CONV_W
        g["c4.arrived"] = arr_n
        g["c4.arrived.want"] = len(SQ_CONV)
        g["c4.held"] = hold_n
        g["c4.held.want"] = SQ_HOLD_W
        g["c4.firstreads"] = rep.num("seqfirst.size")
        g["c4.sequenced"] = rep.num("seqnew.size")
        g["c4.sequenced.read"] = rep.num("seqnew.read")
        c4dep = clamp01(frac(dep_n, SQ_CONV_W))
        c4arr = clamp01(frac(arr_n, len(SQ_CONV)))
        c4hold = clamp01(frac(hold_n, SQ_HOLD_W))
        g["c4.depfrac"] = "%.6f" % c4dep
        g["c4.arrfrac"] = "%.6f" % c4arr
        g["c4.holdfrac"] = "%.6f" % c4hold
        c4 = minf(minf(c4dep, c4arr), c4hold)
        c4s = "%.6f" % c4
    else:
        c4 = 0.0
        c4s = "0"

    tot = f6(0.04 * c1 + 0.17 * c2 + 0.65 * c3 + 0.14 * c4)
    return "%.6f" % tot, [c1s, c2s, c3s, c4s], g


# --- mutation helpers ---------------------------------------------------------
# Every fixture below is one of the two measured reports with one of these
# applied. They all end by resyncing the counters the records imply, so that a
# fixture cannot be internally inconsistent in a way the scorer would read as a
# finding about a tree: `ns.ref.javax.annotation=17` next to zero javax records
# is not a tree any compiler produces, and a gate that disagreed with itself on
# such a fixture would be reporting the fixture rather than the block.
def resync(rep):
    """Recompute the counters that are derived from records, in the gate's own
    spelling. `toarray.raw` and every ns.import.* are NOT derived -- SourceScan
    counts them on their own channel -- so they are left exactly as set."""
    ns = {}
    for b in rep.recs("ns"):
        p = b.split()[1][4:]
        ns[p] = ns.get(p, 0) + 1
    at = None
    keep = []
    for p in rep.pairs:
        if p[0].startswith("ns.ref."):
            if at is None:
                at = len(keep)
            continue
        keep.append(p)
    if at is None:
        at = 0
    for i, p in enumerate(sorted(ns)):
        keep.insert(at + i, ["ns.ref." + p, str(ns[p])])
    rep.pairs = keep

    ta = dict(sized=0, ctorref=0)
    for b in rep.recs("toarray"):
        f = b.split()[1][5:]
        if f in ta:
            ta[f] += 1
    rep.set("toarray.sized", ta["sized"])
    rep.set("toarray.ctorref", ta["ctorref"])

    ht = dict(legacytype=0, legacycall=0, clienttype=0, redirects=0)
    for b in rep.recs("http"):
        f = b.split()[1][5:]
        if f in ht:
            ht[f] += 1
    rep.set("http.legacyref", ht["legacytype"])
    rep.set("http.legacycall", ht["legacycall"])
    rep.set("http.clientref", ht["clienttype"])
    rep.set("http.redirects", ht["redirects"])
    return rep


def ns_in(rep, pkg, bucket):
    return [b for b in rep.recs("ns")
            if b.split()[1][4:] == pkg and bucket_of(b) == bucket]


def revert_ns_bucket(rep, pkg, bucket):
    """One of the six buckets never moved: put the input tree's own records back.

    THE INPUT TREE NO LONGER HAS ANY, and that is the one thing about this
    helper that changed at this rung. At step 07 the javax records were lifted
    out of baseline-step07-source.txt at their real lines with their real
    qualified names, because that rung was handed a tree that still had them.
    Step 08 is handed step 07's OUTPUT: baseline-step08-source.txt carries
    ns.ref.jakarta.annotation and ns.ref.jakarta.ejb and not one javax record
    in either package, which is exactly what B-14 exists to hold.

    So the javax form is derived from the jakarta records in place -- the
    package column and the qualified type name rewritten back, the line numbers
    and buckets left alone. That is what a REGRESSION looks like, which is the
    thing B-14 is scoring, and it is a stronger fixture than the old one rather
    than a weaker: a candidate who undid the rename would not restore step 06's
    line numbers either.
    """
    jk = re.sub(r"^javax\.", "jakarta.", pkg)
    back = [b.replace("pkg=" + jk, "pkg=" + pkg).replace("type=" + jk + ".",
                                                         "type=" + pkg + ".")
            for b in ns_in(rep, jk, bucket)]
    assert back, (pkg, bucket)
    drop = set(ns_in(rep, jk, bucket))
    assert len(drop) == len(set(back)) or drop, (pkg, bucket)
    out = []
    put = False
    for b in rep.recs("ns"):
        if b in drop and b.split()[1][4:] == jk and bucket_of(b) == bucket:
            if not put:
                out.extend(back)
                put = True
            continue
        out.append(b)
    if not put:
        out.extend(back)
    rep.setrecs("ns", out)
    rep.bump("ns.import." + jk, -1)
    rep.set("ns.import." + pkg, rep.num("ns.import." + pkg) + 1)
    return resync(rep), len(back)


def drop_ns(rep, pred):
    """Delete every ns record the predicate accepts, and resync."""
    n = [0]

    def f(b):
        if pred(b):
            n[0] += 1
            return None
        return b

    rep.maprecs("ns", f)
    resync(rep)
    return n[0]


def map_ns(rep, fn):
    rep.maprecs("ns", fn)
    return resync(rep)


def recs_in_file(rep, kind, path):
    return [b for b in rep.recs(kind) if site(b)[0] == path]


# --- B-16's five kinds, and the helpers that move them ------------------------
# Five record kinds arrived at step 09 and none of the helpers above know about
# them. Each of the five gets a resync of its own, for the reason the block
# above gives: `sealed.decl=11` beside nine sealed records is not a report any
# compiler plugin writes, and a fixture that disagreed with itself would be
# telling the rehearsal about the fixture rather than about the scorer.
#
# THE HELPERS ARE UNEDITED AND THE GATE NAMES IN THIS COMMENT ARE NOT, which
# is the whole of what inheriting a family costs: the five kinds still mean
# what they meant, and the block that reads them is called B-16 here.
#
# The scorer reads the RECORDS and not these scalars for four of the five legs
# -- B-16.2 counts kind=bound records, B-16.4 compares permits lists, B-16.5
# counts form=arrow records per file -- so keeping the scalars honest is about
# the facts the trajectory carries and about the two places a scalar IS read:
# B-16.2's total-site term reads `ipattern.size`, and gate.b16.3.textblocks
# reads `textblock.size`.
def ip_resync(rep):
    n = b = p = c = 0
    files, meths = set(), set()
    for body in rep.recs("ipattern"):
        n += 1
        k = field(body, "kind=")
        if k == "bound":
            b += 1
        elif k == "plain":
            p += 1
        if field(body, "conv=") == "yes":
            c += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("ipattern.size", n)
    rep.set("ipattern.bound", b)
    rep.set("ipattern.plain", p)
    rep.set("ipattern.convertible", c)
    rep.set("ipattern.files", len(files))
    rep.set("ipattern.methods", len(meths))
    return rep


def tb_resync(rep):
    """textblock and strnl together: they are two halves of one question."""
    tb, tbf, tbm = 0, set(), set()
    for body in rep.recs("textblock"):
        tb += 1
        tbf.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            tbm.add(bk)
    rep.set("textblock.size", tb)
    rep.set("textblock.files", len(tbf))
    rep.set("textblock.methods", len(tbm))
    n, files, meths = 0, set(), set()
    for body in rep.recs("strnl"):
        n += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("strnl.size", n)
    rep.set("strnl.files", len(files))
    rep.set("strnl.methods", len(meths))
    return rep


def seal_resync(rep):
    d = ns = perm = 0
    files = set()
    for body in rep.recs("sealed"):
        k = field(body, "kind=")
        if k == "sealed":
            d += 1
        elif k == "nonsealed":
            ns += 1
        pn = field(body, "names=")
        perm += 0 if not pn else len(pn.split(";"))
        files.add(site(body)[0])
    rep.set("sealed.decl", d)
    rep.set("sealed.nonsealed", ns)
    rep.set("sealed.permits", perm)
    rep.set("sealed.files", len(files))
    return rep


def sw_resync(rep):
    n = arrow = colon = expr = 0
    files = set()
    for body in rep.recs("switch"):
        n += 1
        f = field(body, "form=")
        if f == "arrow":
            arrow += 1
        elif f == "colon":
            colon += 1
        if field(body, "expr=") == "yes":
            expr += 1
        files.add(site(body)[0])
    rep.set("switch.size", n)
    rep.set("switch.arrow", arrow)
    rep.set("switch.colon", colon)
    rep.set("switch.expr", expr)
    rep.set("switch.files", len(files))
    return rep


def unbind(rep, buckets):
    """Named buckets go back to plain -- a pattern variable never introduced.

    `conv=yes` comes back with the plain spelling, because that is what the
    seed's own records carry: a plain `instanceof` with a cast after it IS
    convertible, and a fixture that set kind=plain while leaving conv=no would
    be describing a site with no cast, which is the other half of C-2 and a
    different claim entirely.
    """
    want = set(buckets)
    n = [0]

    def f(b):
        if bucket_of(b) in want and field(b, "kind=") == "bound":
            n[0] += 1
            return b.replace("kind=bound", "kind=plain").replace("conv=no", "conv=yes")
        return b

    rep.maprecs("ipattern", f)
    ip_resync(rep)
    return n[0]


def bind_all(rep):
    """Every remaining plain site becomes bound -- including the seven that
    have no cast after them and therefore cannot be a pattern at all."""
    n = [0]

    def f(b):
        if field(b, "kind=") == "plain":
            n[0] += 1
            return b.replace("kind=plain", "kind=bound").replace("conv=yes", "conv=no")
        return b

    rep.maprecs("ipattern", f)
    ip_resync(rep)
    return n[0]


def drop_ip(rep, buckets):
    """The sites themselves go -- a tree that deleted the code rather than
    converting it. This is what C-2's total-site term is for."""
    want = set(buckets)
    n = [0]

    def f(b):
        if bucket_of(b) in want:
            n[0] += 1
            return None
        return b

    rep.maprecs("ipattern", f)
    ip_resync(rep)
    return n[0]


def seed_strnl(bucket):
    """The INPUT TREE's own strnl records for one bucket, at their own lines.

    Reverting a bucket by synthesising records would put the literals back at
    invented line numbers and invented lengths. These are the ones SourceScan
    measured on the tree the candidate is handed.
    """
    return [b for b in BASE.recs("strnl") if bucket_of(b) == bucket]


def restore_strnl(rep, bucket):
    """Put one bucket's newline literals back, keeping the report's order."""
    have = [b for b in rep.recs("strnl") if bucket_of(b) != bucket]
    add = seed_strnl(bucket)
    rep.setrecs("strnl", sorted(have + add))
    tb_resync(rep)
    return len(add)


def drop_strnl(rep, bucket, howmany=None):
    """Delete literals out of a HELD bucket -- the XML builder that was
    flattened along with the HTML document. `howmany` from the front."""
    left = [howmany if howmany is not None else 10 ** 6]
    n = [0]

    def f(b):
        if bucket_of(b) == bucket and left[0] > 0:
            left[0] -= 1
            n[0] += 1
            return None
        return b

    rep.maprecs("strnl", f)
    tb_resync(rep)
    return n[0]


def drop_textblocks(rep):
    n = len(rep.recs("textblock"))
    rep.setrecs("textblock", [])
    tb_resync(rep)
    return n


def declass_textblocks(rep):
    """Every text block moves to class scope, staying in its own FILE.

    SourceScan writes `in=-` for a site with no enclosing method -- a field
    initialiser, a static block, an annotation value -- and its header says
    what that costs: such a site "carries a bucket that is the same before and
    after the migration and that no frozen set of METHOD buckets can name, so
    it can never satisfy an arrival half on its own". This mutator produces
    exactly that record and changes nothing else: same file, same line, same
    len=, so textblock.size and textblock.files do not move and only the
    bucket does.
    """
    n = [0]

    def f(b):
        if field(b, "in=") in (None, "-"):
            return b
        n[0] += 1
        return b.rsplit(" in=", 1)[0] + " in=-"

    rep.maprecs("textblock", f)
    tb_resync(rep)
    return n[0]


def unseal(rep, names):
    """Named roots stop being sealed -- the record goes, as it does in a tree
    where the modifier was never written."""
    want = set(names)
    n = [0]

    def f(b):
        if field(b, "name=") in want:
            n[0] += 1
            return None
        return b

    rep.maprecs("sealed", f)
    seal_resync(rep)
    return n[0]


def seal_open(rep, keys, permits=("Alpha", "Beta")):
    """Close a type the tree is extended through.

    The permits list is invented and that is correct: these types have no
    frozen permits list, because the baseline says they must not be sealed at
    all. What C-4's open half reads is the KEY -- file and name -- so any
    well-formed record on that key closes the type as far as the gate is
    concerned, which is exactly the claim the fixture is making.
    """
    add = []
    for f, nm in keys:
        add.append("%s:9100 name=%s kind=sealed permits=%d names=%s"
                   % (f, nm, len(permits), ";".join(permits)))
    rep.setrecs("sealed", rep.recs("sealed") + add)
    seal_resync(rep)
    return len(add)


def clip_permits(rep, name):
    """One root is sealed and its permits clause is one name short.

    The subtlest of the C-4 failures and the reason the closed half compares
    TRIPLES rather than keys: a `sealed` modifier with an incomplete `permits`
    does not compile in the real tree, but a gate that only asked "is this type
    sealed" could not say so, and the golden would then share a score with a
    tree that sealed eleven roots and named their subtypes wrongly.
    """
    dropped = [None]

    def f(b):
        if field(b, "name=") != name or dropped[0] is not None:
            return b
        pn = field(b, "names=").split(";")
        dropped[0] = pn[-1]
        rest = pn[:-1]
        return b.replace("permits=%d" % len(pn), "permits=%d" % len(rest)) \
                .replace("names=" + ";".join(pn), "names=" + ";".join(rest))

    rep.maprecs("sealed", f)
    seal_resync(rep)
    assert dropped[0] is not None, "no sealed record named " + name
    return dropped[0]


def uncurl(rep, path):
    """One file's switch goes back to colon form."""
    n = [0]

    def f(b):
        if site(b)[0] == path and field(b, "form=") == "arrow":
            n[0] += 1
            return b.replace("form=arrow", "form=colon")
        return b

    rep.maprecs("switch", f)
    sw_resync(rep)
    return n[0]


def add_arrow(rep, path, cases=3):
    """A NEW arrow switch, in a file whose colon one is still there.

    C-5 asks two things per file -- enough arrows AND no colon left -- and this
    is the fixture that separates them. A tree that added a switch is not a
    tree that converted one, and a per-file arrow count on its own reads the
    two the same way.
    """
    rep.setrecs("switch", rep.recs("switch")
                + ["%s:9200 form=arrow expr=no cases=%d" % (path, cases)])
    sw_resync(rep)
    return 1




# --- step 10's four new kinds -------------------------------------------------
# reftest, seqfirst, seqnew and swpat. Same rule as the block above: a fixture
# that disagreed with itself -- `seqnew.size=0` beside seven seqnew records --
# would be telling the rehearsal about the fixture rather than about the
# scorer. Family C reads the RECORDS for all four legs and the scalars for
# nothing it scores, so these resyncs are about the facts the trajectory
# carries; gate.c2.reftests, gate.c3.patterns and gate.c4.sequenced are read by
# a human and by nothing else.
def rt_resync(rep):
    n = lit = var = 0
    files, meths = set(), set()
    for body in rep.recs("reftest"):
        n += 1
        f = field(body, "form=") or ""
        # `lit` and `var` are which side of the test holds the TYPE, not which
        # side holds the value: litinstance, litassign and litassignget all
        # name a type literally and are convertible in principle; varinstance
        # and varassign have a Class value where the type must go and are the
        # ten that cannot move at all.
        if f.startswith("lit"):
            lit += 1
        elif f.startswith("var"):
            var += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("reftest.size", n)
    rep.set("reftest.literal", lit)
    rep.set("reftest.var", var)
    rep.set("reftest.files", len(files))
    rep.set("reftest.methods", len(meths))
    return rep


def sq_resync(rep):
    """seqfirst and seqnew together: they are two halves of one question."""
    n = guarded = 0
    files, meths = set(), set()
    for body in rep.recs("seqfirst"):
        n += 1
        if (field(body, "guard=") or "none") != "none":
            guarded += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("seqfirst.size", n)
    rep.set("seqfirst.guarded", guarded)
    rep.set("seqfirst.files", len(files))
    rep.set("seqfirst.methods", len(meths))
    m = read = 0
    nfiles, nmeths = set(), set()
    for body in rep.recs("seqnew"):
        m += 1
        # A READ and not a write. SourceScan reports addFirst and putLast off
        # the same interface, and a tree that added an element where it used to
        # read one has not done C-4's work -- `seqnew.read` is the counter that
        # would say so, and the leg itself keys on `op=getFirst`.
        if (field(body, "op=") or "").startswith("get"):
            read += 1
        nfiles.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            nmeths.add(bk)
    rep.set("seqnew.size", m)
    rep.set("seqnew.read", read)
    rep.set("seqnew.files", len(nfiles))
    rep.set("seqnew.methods", len(nmeths))
    return rep


def swp_resync(rep):
    n = pat = nul = 0
    files, meths = set(), set()
    for body in rep.recs("swpat"):
        n += 1
        v = field(body, "patterns=")
        pat += int(v) if v and re.match(r"^\d+$", v) else 0
        if field(body, "nullcase=") == "yes":
            nul += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("swpat.size", n)
    rep.set("swpat.patterns", pat)
    rep.set("swpat.nullcase", nul)
    rep.set("swpat.files", len(files))
    rep.set("swpat.methods", len(meths))
    return rep


_RESYNC = {"reftest": rt_resync, "seqfirst": sq_resync, "seqnew": sq_resync,
           "swpat": swp_resync}


def seed_recs(kind, bucket):
    """The INPUT TREE's own records of one kind for one bucket, at their lines.

    seed_strnl's rule, generalised to the four new kinds: reverting a bucket by
    synthesising records would put the reflective tests back at invented line
    numbers with invented selectors. These are the ones SourceScan measured on
    the tree the candidate is handed.
    """
    return [b for b in BASE.recs(kind) if bucket_of(b) == bucket]


def restore_recs(rep, kind, bucket, howmany=None):
    """Put one bucket's input-tree records back, keeping the report's order."""
    mine = [b for b in rep.recs(kind) if bucket_of(b) == bucket]
    rest = [b for b in rep.recs(kind) if bucket_of(b) != bucket]
    add = [b for b in seed_recs(kind, bucket) if b not in mine]
    if howmany is not None:
        add = add[:howmany]
    rep.setrecs(kind, sorted(rest + mine + add))
    _RESYNC[kind](rep)
    return len(add)


def restore_as_ipattern(rep, bucket):
    """One conv chain's input-tree reflective tests, back as `instanceof`
    PATTERNS over the same types at the same lines.

    N15's mutation and nothing else's, and the difference from restore_recs is
    the whole of what that row is about. restore_recs puts a bucket back the
    way the INPUT TREE had it, which is the tree that did nothing; this puts it
    back the way requirement 4 ALONE leaves it -- `X.class.isInstance(v)`
    rewritten to `v instanceof X x`, which is a language type test and so emits
    an `ipattern` record where the reflective call emitted a `reftest` one.
    Either way the chain is still a chain and still what dispatches.

    The lines and the types are the input tree's, for seed_recs' reason: a
    residual at an invented line is a tree no scan ever produced. `kind=`
    follows the rule the record itself carries and the rule step 09 graded --
    an arm whose value was cast after the test binds a pattern variable, an arm
    that only asked binds nothing -- and `conv=no`, because a pattern is not a
    convertible instanceof. None of these four buckets is in step 09's ipattern
    population, so B-16.2's two capsum halves cannot see any of this; its
    total-site term can, and is clamped.
    """
    meth = bucket.split("#", 1)[1]
    add = []
    for body in seed_recs("reftest", bucket):
        f, ln = site(body)
        add.append("%s:%s kind=%s type=%s conv=no in=%s"
                   % (f, ln,
                      "bound" if field(body, "cast=") == "yes" else "plain",
                      field(body, "type="), meth))
    rep.setrecs("ipattern", sorted(rep.recs("ipattern") + add))
    ip_resync(rep)
    return len(add)


def drop_recs(rep, kind, buckets, howmany=None):
    """Delete records out of named buckets -- `howmany` per bucket, from the
    front, or all of them."""
    want = set(buckets)
    taken, n = {}, [0]

    def f(b):
        k = bucket_of(b)
        if k in want and (howmany is None or taken.get(k, 0) < howmany):
            taken[k] = taken.get(k, 0) + 1
            n[0] += 1
            return None
        return b

    rep.maprecs(kind, f)
    _RESYNC[kind](rep)
    return n[0]


def clip_swpat(rep, bucket, patterns):
    """One pattern switch declares fewer arms than the chain it replaced.

    The arm-count half of C-3 on its own: the types the switch DOES name are
    still named, so the coverage condition is untouched and only `patterns=`
    moves. A leg that read the type set and not the count would score this
    fixture whole, which is the reason the two conditions are separate.
    """
    hit = [0]

    def f(b):
        if bucket_of(b) != bucket:
            return b
        was = field(b, "patterns=")
        hit[0] += 1
        return b.replace("patterns=" + was, "patterns=%d" % patterns)

    rep.maprecs("swpat", f)
    swp_resync(rep)
    assert hit[0] == 1, "clip_swpat matched %d switches in %s" % (hit[0], bucket)
    return patterns


def retype_swpat(rep, bucket, name, other):
    """One arm of one pattern switch names a DIFFERENT type.

    The coverage half on its own, and the mirror of clip_swpat: the arm count
    does not move, so a leg that only counted arms would score this whole. It
    is also the closest thing this rung has to a plausible mistake -- a
    candidate who wrote `case Archive a` where the chain tested for
    `JavaArchive` has a switch that compiles, has the right number of arms, and
    dispatches the wrong inputs to the wrong branch.
    """
    hit = [0]

    def f(b):
        if bucket_of(b) != bucket:
            return b
        ts = field(b, "types=")
        parts = ts.split(";")
        assert name in parts, "%s does not name %s" % (bucket, name)
        hit[0] += 1
        return b.replace("types=" + ts,
                         "types=" + ";".join(other if x == name else x
                                             for x in parts))

    rep.maprecs("swpat", f)
    swp_resync(rep)
    assert hit[0] == 1, "retype_swpat matched %d switches in %s" % (hit[0], bucket)
    return other


def add_swpat(rep, path, method, types, patterns=None, nullcase="no", line=9300):
    """A pattern switch where the baseline says there must not be one.

    Used two ways below and they are different claims. Planted in a HOLD
    chain's method it says "the candidate converted the dispatch that cannot be
    converted", which C-3's hold half is the only thing that sees. Planted in
    the same FILE as a conv chain but a different method it says "a switch
    arrived, somewhere", which is L14's lesson carried to this rung: every one
    of family C's arrival halves keys on `file#method` and not on file.
    """
    n = len(types) if patterns is None else patterns
    rep.setrecs("swpat", rep.recs("swpat")
                + ["%s:%d sel=x cases=%d patterns=%d nullcase=%s deflt=yes "
                   "types=%s in=%s" % (path, line, n + 1, n, nullcase,
                                       ";".join(types), method)])
    swp_resync(rep)
    return 1


def retype_seqnew(rep, bucket, op="getLast"):
    """The sequenced read that arrived reads the WRONG END.

    C-4's arrival half counts `op=getFirst` and not `seqnew` records, because
    `getLast()` on a list that used to be read at index 0 is a behaviour change
    and not a migration. The scalar `seqnew.read` still counts it, which is why
    that counter is reported and not scored.
    """
    hit = [0]

    def f(b):
        if bucket_of(b) != bucket:
            return b
        was = field(b, "op=")
        hit[0] += 1
        return b.replace("op=" + was, "op=" + op)

    rep.maprecs("seqnew", f)
    sq_resync(rep)
    assert hit[0] == 1, "retype_seqnew matched %d reads in %s" % (hit[0], bucket)
    return op


# --- the class-file scan ------------------------------------------------------
# A SEPARATE CHANNEL FROM THE REPORT AND SEPARATELY MUTABLE, which is the shape
# step 07 had and step 08 lost. C-1 reads ${WORK}/scan.txt, written by
# arqgate.Scan over the class files the build left behind; every other leg of
# family C reads the source AST. A tree can move either without the other --
# L1 and L2 are the two rows that say so -- and at step 08 no fixture could
# make that separation because both channels came off one list of jars.
GOLD_SCAN = {"classes": CLASSES, "major.%d" % WANT_MAJOR: CLASSES}
SEED_SCAN = {"classes": CLASSES, "major.%d" % HOLD_MAJOR: CLASSES}


def scan_split(at_target, rest_major=None):
    """A scan where only part of the tree reached the target major.

    The remainder is reported at `rest_major`, defaulting to the major the
    input tree was already at, because that is what a partial migration
    actually leaves behind: the modules whose build file moved are at 61 and
    the ones whose did not are still at 55.
    """
    rest = CLASSES - at_target
    d = {"classes": CLASSES, "major.%d" % WANT_MAJOR: at_target}
    if rest:
        d["major.%d" % (rest_major or HOLD_MAJOR)] = rest
    return d


def write_scan(d, scan):
    """scan.txt in arqgate.Scan's own spelling, or no file at all.

    `classes=` first, the majors in numeric order, then `majors.distinct=` --
    the order Scan.report() prints them in. The scorer reads two of those three
    with `sed -n 's/^key=//p' | head -1`, so order does not change the score;
    it changes whether a human reading a failed fixture recognises the file.
    """
    if scan is None:
        return
    majors = sorted(int(k[6:]) for k in scan if k.startswith("major."))
    with open(os.path.join(d, "scan.txt"), "w") as fh:
        fh.write("classes=%d\n" % int(scan.get("classes", 0)))
        for m in majors:
            fh.write("major.%d=%d\n" % (m, int(scan["major.%d" % m])))
        fh.write("majors.distinct=%d\n" % len(majors))
# --- the catalogue ------------------------------------------------------------
# Fifty-three fixtures and the two ends. Each names the ONE thing it moves, and
# the table at the foot of this file is what c-rehearsal.sh asserts. Read the
# `why` text as the fixture's claim and the row as what the shipped blocks must
# say about it.
#
# FOUR POPULATIONS OF FIXTURE AND ONE TABLE.
#
#   K1..K3, K19..K20, M1..M6   move the PUBLISHED JARS and the service-lookup
#                              records -- step 08's family C, inherited whole
#                              as B-15
#   K4..K18                    move the namespace, toArray and HttpClient
#                              records -- step 07's family C, inherited as B-14
#   L3..L14                    move the pattern records, the text block, the
#                              sealed hierarchies and the arrow switches --
#                              step 09's family C, inherited as B-16
#   L1..L2, N1..N13            move THIS rung's four legs: the class-file scan,
#                              the reflective type tests, the four if-chains
#                              and the first-element reads
#
# L1 AND L2 KEPT THEIR NAMES AND CHANGED NOTHING ELSE. They move
# ${WORK}/scan.txt, and the scan is still family C's C-1 at this rung -- a
# different major over the same channel -- so they are the only two L rows that
# did not change column. L3..L14 kept their names for the opposite reason: they
# are step 09's rows unedited, and the only thing that moved is which family
# reads them. Renaming either group would have churned every cross-reference in
# the rehearsal to say nothing.
#
# They share a table because they share a run: every fixture is scored by all
# NINE blocks, and the column that must NOT move is as much of the assertion
# as the one that must. A family C that noticed a module name, or a B-15 that
# noticed a sealed declaration, would show up here as a moved cell in a column
# the fixture never touched.
#
# THE INHERITANCE IS NOW THREE RUNGS DEEP AND THAT IS THE POINT OF KEEPING ALL
# THREE SETS. B-14 is step 07's family C, B-15 is step 08's and B-16 is step
# 09's, and the ninth trap is about exactly this: a step-k requirement that
# quietly makes a step-k-3 test unpassable is invisible from inside a
# step-local mean. The only way to see it is to score every inherited gate on
# every fixture, including the fixtures that were written for the rung three
# below.
#
# FIXTURES THAT SHARE A SCORE AND ARE NOT REDUNDANT. L12 and L13 both read B-16
# at the same number, and two pairs in the N series do the same for familyC:
# N1 and N2 at 0.960769, N10 and N11 at 0.960000. They differ in the GATE FACT
# that says why -- `gate.b16.5.arrow=1` against `gate.b16.5.arrow=2`,
# `gate.c2.depfrac` against `gate.c2.arrfrac`, `gate.c4.depfrac` against
# `gate.c4.arrfrac` -- and a block that read one channel wrongly would collapse
# them onto one message while keeping the score. The rehearsal asserts the
# messages as well as the numbers, which is the only way that particular defect
# is visible from outside.
#
# N7 AND N8 WERE A THIRD SUCH PAIR AT 0.675000 AND THEY ARE NOT ONE NOW. The
# fifth swid moved N8's scaling from 2/4 to 3/5, so it reads 0.740000 while N7
# stays at 0.675000. Nothing about why the pair was written has changed --
# `gate.c3.holdfrac` against `gate.c3.exec`, a missing conversion against a
# broken execution -- and both rows still assert both facts, because a figure
# they happen not to share any more is a weaker reason to look than the two
# halves they still fail in separately.
#
# TWO ROWS MOVE TWO LEGS AND THEY HAVE TO. N6 retypes one arm of one pattern
# switch and N9 deletes one pattern switch, and C-2's arrival half reads the
# `swpat` type set as well as the `ipattern` one, so both rows move C-2 AND
# C-3. That is not a leak between legs; it is one artefact answering two
# questions, and a fixture that moved only one of them would be describing a
# tree that cannot exist. Their `why` text says which half of each leg fell.
CAT = []


def fixture(name, base, why, **env):
    def deco(fn):
        CAT.append((name, base, why, fn, env))
        return fn
    return deco


@fixture("K1", "golden", "eleven artifacts declare the name the packages "
         "imply and the twelfth declares nothing -- a build file that listed "
         "the modules by hand and missed one. B-15.2 is the SQUARE of 11/12, "
         "which is 0.840278 and not 0.916667: step 08 scaled its name leg by "
         "its own C-1 and that scaling survives the drop of C-1 as the square, "
         "so the last name still costs more than the first ten. "
         "mod.declared=11 is reported and not scored, which is the whole of "
         "what this rung stopped reading.",
         mods=[Mod(a, "" if a == "arquillian-testng" else n,
                   poison=(a == POISONED)) for a, n in MODNAMES])
def _k1(rep, env):
    return "arquillian-testng publishes no Automatic-Module-Name"


@fixture("K2", "golden", "the input tree's own jars under the golden's source "
         "report: nothing declares a module name at all. B-15.2 is 0.000000 "
         "and mod.derivable is STILL 11, which is the fact that says the "
         "descriptor file was never the candidate's doing. B-15 keeps only its "
         "service leg -- 0.45/0.80 = 0.562500 -- and family C is untouched, "
         "because the manifests and the class-file major are two artefacts.",
         mods=BAREMODS)
def _k2(rep, env):
    return "twelve jars, no Automatic-Module-Name in any of them"


@fixture("K3", "golden", "every artifact declares a name and two of them are "
         "the names the FILE NAME implies rather than the ones the packages "
         "do: arquillian.impl.base for a project whose sources are all under "
         "org.jboss.arquillian.impl, and arquillian.protocol.jmx.osgi for one "
         "whose sources are all under org.jboss.arquillian.packager.osgi. "
         "B-15.2 is the square of 10/12 -- 0.694444. This is the exact failure "
         "an agent that derived the names from the artifact ids produces, and "
         "the two it gets wrong are the two whose name would be wrong forever "
         "once published.",
         mods=[Mod(a, a.replace("-", ".")
                   if a in ("arquillian-impl-base", "arquillian-protocol-jmx-osgi")
                   else n, poison=(a == POISONED)) for a, n in MODNAMES])
def _k3(rep, env):
    return "two module names derived from the file name, not the packages"
@fixture("K4", "golden", "five of the six nsmove buckets moved and one did not: "
         "ResourceInjectionEnricher#getResourceName still references "
         "javax.annotation, with the input tree's own records restored at their "
         "own lines.")
def _k4(rep, env):
    pkg, bucket = NSMOVE[0]
    _, n = revert_ns_bucket(rep, pkg, bucket)
    return "%s reverted, %d javax reference(s) back" % (bucket.split("#")[-1], n)


@fixture("K5", "golden", "every bucket moved, and one javax.annotation "
         "reference was then added somewhere nsmove does not name. The bucket "
         "halves are full; the departed-total term is what catches it.")
def _k5(rep, env):
    holdfile, holdbucket = NSHOLD[0][1].split("#")[0], NSHOLD[0][1]
    meth = holdbucket.split("#")[-1]
    rep.setrecs("ns", rep.recs("ns") + [
        "%s:9001 pkg=javax.annotation type=javax.annotation.Resource in=%s"
        % (holdfile, meth)])
    resync(rep)
    return "one javax.annotation reference in %s" % holdbucket


@fixture("K6", "golden", "all six buckets hold a jakarta reference and six of "
         "the twenty-two references are gone -- a migration that rewrote the "
         "imports and deleted annotation uses rather than moving them.")
def _k6(rep, env):
    want, seen, out = 6, {}, []
    for b in rep.recs("ns"):
        p = b.split()[1][4:]
        bk = bucket_of(b)
        if p in ("jakarta.annotation", "jakarta.ejb"):
            seen[bk] = seen.get(bk, 0) + 1
    kept = {}
    dropped = 0
    for b in rep.recs("ns"):
        p = b.split()[1][4:]
        bk = bucket_of(b)
        if p in ("jakarta.annotation", "jakarta.ejb") and dropped < want:
            kept[bk] = kept.get(bk, 0) + 1
            if kept[bk] > 1 and (seen[bk] - (kept[bk] - 1)) >= 1:
                dropped += 1
                continue
        out.append(b)
    rep.setrecs("ns", out)
    resync(rep)
    assert dropped == want, dropped
    return "%d jakarta reference(s) dropped, every bucket still occupied" % dropped


@fixture("K7", "golden", "one of the 43 held (package, bucket) pairs is gone: "
         "CDIInjectionEnricher#getInstanceByType no longer references "
         "javax.enterprise. B-14.3 loses a bucket and the references it held.")
def _k7(rep, env):
    pkg, bucket = NSHOLD[0]
    n = drop_ns(rep, lambda b: b.split()[1][4:] == pkg and bucket_of(b) == bucket)
    return "%s dropped, %d reference(s)" % (pkg, n)


@fixture("K8", "golden", "javax.management was migrated to jakarta.management "
         "-- the broad package rename step 07's hold half exists to price. The tree "
         "would not compile at any Java version; the report is what the AST "
         "would say if it did.")
def _k8(rep, env):
    n = [0]

    def f(b):
        if b.split()[1][4:] != "javax.management":
            return b
        n[0] += 1
        return b.replace("pkg=javax.management", "pkg=jakarta.management") \
                .replace("type=javax.management.", "type=jakarta.management.")

    map_ns(rep, f)
    rep.rename("ns.import.javax.management", "ns.import.jakarta.management")
    return "%d javax.management reference(s) renamed" % n[0]


@fixture("K9", "golden", "B-14.4, step 07's toArray leg, fully reverted: all eight "
         "sites size the array at the call site again, as the input tree does. "
         "Departure and arrival are both zero.")
def _k9(rep, env):
    # Derived from the golden's own records rather than lifted out of the input
    # tree's, for the reason revert_ns_bucket() gives at length: step 08's
    # input tree IS step 07's output, so BASE.recs("toarray") is already the
    # migrated form and copying it would make this fixture a no-op that scored
    # 1.000000 and looked like a pass.
    rep.maprecs("toarray", lambda b: b.replace("form=ctorref", "form=sized"))
    rep.set("toarray.raw", 0)
    resync(rep)
    return "%d sized, %d ctorref" % (rep.num("toarray.sized"),
                                     rep.num("toarray.ctorref"))


@fixture("K10", "golden", "two of the five toArray buckets converted and three "
         "left: the bucket halves and the site totals disagree, and the gate "
         "takes the smaller.")
def _k10(rep, env):
    keep = set(sorted(TAWANT)[:2])
    rep.maprecs("toarray", lambda b: b if bucket_of(b) in keep
                else b.replace("form=ctorref", "form=sized"))
    resync(rep)
    return "%d ctorref, %d sized" % (rep.num("toarray.ctorref"),
                                     rep.num("toarray.sized"))


@fixture("K11", "golden", "the HttpClient migration is complete and names no "
         "redirect policy. HttpURLConnection followed redirects and HttpClient "
         "does not, so this tree quietly changed behaviour: 0.75 of C-5.")
def _k11(rep, env):
    rep.maprecs("http", lambda b: None if b.split()[1] == "form=redirects" else b)
    resync(rep)
    return "no followRedirects record"


@fixture("K12", "golden", "half the migration: seven of the fourteen legacy "
         "records in ServletMethodExecutor survive alongside the HttpClient "
         "exchange -- the shape a candidate who added the new path without "
         "deleting the old one produces.")
def _k12(rep, env):
    # SYNTHESISED and not lifted, and this is the one fixture where that is
    # worth a sentence of its own. Step 07's version copied the first seven
    # java.net.URLConnection records out of the input tree's own report. This
    # rung's input tree has none in this file -- the migration happened at the
    # rung below and B-14 exists to hold it -- so the seven are written here in
    # the gate's own record shape, at the lines the golden's HttpClient
    # exchange occupies, which is where a candidate who added the new path
    # without deleting the old one would leave them.
    tmpl = ("%s:%d form=legacytype name=java.net.%s in=execute")
    back = [tmpl % (C5_FILE, 96 + i, t) for i, t in enumerate(
        ("URL", "URLConnection", "HttpURLConnection", "URLConnection",
         "HttpURLConnection", "URLConnection", "HttpURLConnection"))]
    rep.setrecs("http", sorted(rep.recs("http") + back))
    resync(rep)
    return "%d legacy record(s) survive in %s" % (len(back), C5_FILE.split("/")[-1])


@fixture("K13", "golden", "every java.net.http reference in the file is spelled "
         "HttpClient: one arrival bucket of three. A migration that opened a "
         "client and then went back to the URLConnection body would read this "
         "way.")
def _k13(rep, env):
    def f(b):
        if site(b)[0] == C5_FILE and b.split()[1] == "form=clienttype":
            return re.sub(r"name=\S+", "name=java.net.http.HttpClient", b)
        return b

    rep.maprecs("http", f)
    resync(rep)
    return "1 distinct arrival type"


@fixture("K14", "golden", "the migration is complete and two raw types were "
         "added over the baseline's zero. The first penalty term takes B-14 "
         "to 0.8 of what it earned.")
def _k14(rep, env):
    rep.set("newgeneric.raw", BASE_RAW + 2)
    return "newgeneric.raw=%d" % rep.num("newgeneric.raw")


@fixture("K15", "golden", "one @SuppressWarnings site more than the baseline's "
         "48 -- a warning silenced rather than fixed. The second penalty term "
         "halves B-14.")
def _k15(rep, env):
    rep.set("suppress.sites", BASE_SUP + 1)
    return "suppress.sites=%d" % rep.num("suppress.sites")


@fixture("K16", "golden", "one of the eight constructor references is "
         "Class[]::new where Class<?>[]::new was meant. newgeneric.raw cannot "
         "see it -- a member reference is not a new expression -- and "
         "toarray.raw can. Family C is halved.")
def _k16(rep, env):
    out, done = [], False
    for b in rep.recs("toarray"):
        if not done and "elem=Class<?>[]" in b:
            b = b.replace("elem=Class<?>[]", "elem=Class[]")
            done = True
        out.append(b)
    rep.setrecs("toarray", out)
    resync(rep)
    rep.set("toarray.raw", 1)
    assert done
    return "toarray.raw=1"


@fixture("K17", "golden", "nothing compiles: all three probes refused, so "
         "there is no measured report. B-10, B-11 and B-12a..d publish the "
         "literal 0 their blocks assign; B-12e, B-12, B-13, B-14, B-15, B-16 "
         "and familyC publish 0.000000, because their totals are computed by "
         "awk outside the guard; and B-13.2..7, B-14.2..5 and B-16.2/.4/.5 "
         "publish NOTHING AT ALL, because their fact lines are inside it. "
         "THREE SPELLINGS OF ZERO IN ONE ROW, which is what makes this fixture "
         "worth more at this rung than at any of the last three. C-1 is FULL "
         "-- 1.000000 -- because it reads ${WORK}/scan.txt, and the class "
         "files this build left behind are all at major 65 whatever the "
         "scorer's own three probes made of the sources. C-2 and C-4 are "
         "the shell LITERAL 0, assigned above their guards. C-3 is 0.000000, "
         "because its conv and hold halves are the literal 0 and its final "
         "assignment then multiplies them by the execution scaling OUTSIDE the "
         "guard. Family C is 0.04 x 1 = 0.040000 exactly. B-16 is the same "
         "shape one family over and lands on 0.000000 rather than 0.040000, "
         "because the class-file major did NOT come across with it: B-16.2, "
         ".4 and .5 are the literal 0, B-16.3 is 0.000000 outside its guard, "
         "and there is no fifth leg to be full. B-15 is a third shape again: "
         "B-15.2 reads the twelve published manifests and is full, B-15.3's "
         "three halves are the literal 0, and the composite is 0.35 / 0.80 = "
         "0.437500. A rung whose gates all sat inside one guard could not "
         "produce any of the three numbers.",
         ok21="no", ok20="no", okdef="no")
def _k17(rep, env):
    return "no report; the twelve published jars and the class-file scan intact"


@fixture("K18", "golden", "step 06's Class.newInstance work is undone: both "
         "frozen buckets call the legacy form again. B-13's first leg falls and "
         "B-15 and family C are both untouched, which is the separation the "
         "ninth trap asks for.")
def _k18(rep, env):
    n = [0]

    def f(b):
        if bucket_of(b) in NI_LEGACY and "form=ctor" in b.split():
            n[0] += 1
            return b.replace("form=ctor", "form=legacy")
        return b

    rep.maprecs("newinstance", f)
    rep.set("newinstance.legacy", n[0])
    rep.set("newinstance.ctor", rep.num("newinstance.size") - n[0])
    assert n[0] == B13_LEGACY, n[0]
    return "%d bucket(s) back to Class.newInstance" % n[0]
# THE TWO ARTIFACT CHANNELS ARE TWO FILES AGAIN (K19, K20, and L1..L2 below).
# B-12e unzips the jars the build published; B-15.2 hands the same list to
# java.lang.module; C-1 reads ${WORK}/scan.txt, which arqgate.Scan writes over
# the class files the build left in the tree. At step 07 the scan and the jars
# were two channels and three fixtures existed to keep them apart; at step 08
# they collapsed into one list and K20 said so rather than pretending
# otherwise. Here they are two again, and the pair below is the jar half:
# K19 publishes twelve perfectly named jars that carry none of the four Java 8
# package names, and K20 publishes nothing at all. Neither touches the scan, so
# C-1 stays at 1.000000 in both rows -- which is the separation L2 then moves
# from the other side.
@fixture("K19", "golden", "the build published its twelve named jars and not "
         "one class in them names java.util.Optional, "
         "java.util.stream.Collectors, java.util.Objects or java.time. "
         "B-12e's four legs read zero against a source tree whose four AST "
         "legs are full -- the module that was dropped from the build, which "
         "is the failure B-12e was written for. The manifests are untouched, "
         "so B-15 is unmoved, and the class files are untouched, so C-1 is "
         "too: one jar set, three readings, and only one of them falls.",
         jars=(0, 0, 0, 0))
def _k19(rep, env):
    return "nothing in the report; the jars carry no Java 8 library names"


@fixture("K20", "golden", "the build published nothing. ONE ABSENCE WITH TWO "
         "CONSEQUENCES and NOT three, which is the honest shape: B-12e is the "
         "literal 0 under gate.cp=no-jars and B-15.2 is 0.000000 under "
         "gate.mod=no-module-jars. C-1 is UNMOVED at 1.000000, because the "
         "class files are not the jars -- arqgate.Scan reads the build's own "
         "output tree and a tree that compiled but published nothing still has "
         "them. B-15 keeps its service leg and reads 0.562500.",
         jars=None, mods=None)
def _k20(rep, env):
    return "nothing in the report; the build published no jars"


# --- B-15's own six -----------------------------------------------------------
# B-15.3 is 0.45 of 0.80 and it is the leg no build file can reach. Six
# fixtures, one per way of getting it wrong, and every one of them leaves the
# module channel and the class-file scan at the golden so that the row's
# B-15.2 and C-1 stay at 1.000000 and the reader can see which leg moved.
@fixture("M1", "golden", "a thirteenth malformed provider file: the candidate "
         "moved the DynamicServiceLoader work and, on the way, wrote a "
         "META-INF/services entry into arquillian-spi whose line is not the "
         "binary name of a class the jar carries. "
         "THIS ROW SCORES 1.000000 AND THAT IS WHAT IT ASSERTS. At step 08 it "
         "cost a third of C-1 -- min(1.000000, 10/11) -- because that rung read "
         "mod.derivable as a scored term. B-15 reports mod.derivable=10 as a "
         "fact and scores none of it, deliberately: the question 'can the "
         "platform read a descriptor out of this jar' is one the input tree "
         "already answered, so carrying the term forward would have been a "
         "gate the candidate passes for free. This fixture is the evidence "
         "that the term really is gone rather than merely unmentioned, and "
         "gate.b15.mod.derivable=10 beside B15=1.000000 is the assertion.",
         mods=[Mod(a, n, poison=(a in (POISONED, "arquillian-spi")))
               for a, n in MODNAMES])
def _m1(rep, env):
    return "arquillian-spi carries an illegal META-INF/services entry"


@fixture("M2", "golden", "half the service migration: DynamicServiceLoader#load "
         "calls java.util.ServiceLoader.load and spi ServiceLoader#reload still "
         "reads META-INF/services off the class loader by hand. The departure "
         "half is 1/2 and the arrival half is 1/2, and B-15.3 is the min of "
         "those and the hold -- half the leg, not three quarters of it.")
def _m2(rep, env):
    tgt = "spi/src/main/java/org/jboss/arquillian/spi/util/ServiceLoader.java"

    def f(b):
        if site(b)[0] != tgt or field(b, "in=") != "reload":
            return b
        form = b.split()[1]
        if form in ("form=platformtype", "form=providertype"):
            return None
        if form == "form=platformload":
            return b.replace("form=platformload", "form=legacyscan") \
                    .replace("name=load", "name=getResources")
        return b

    rep.maprecs("svcload", f)
    return "spi ServiceLoader#reload back to a hand-rolled scan"


@fixture("M3", "golden", "the two lookups moved and a by-name load went with "
         "them: FilteredURLClassLoader#loadClass is gone, which is what an "
         "agent that grepped for Class.forName and rewrote every hit does to a "
         "ClassLoader subclass. Departure and arrival are both full; the HOLD "
         "is 10/11 and the min() takes it. This is the leg's whole reason for "
         "having three terms instead of two.")
def _m3(rep, env):
    tgt = ("impl-base/src/main/java/org/jboss/arquillian/impl/domain/"
           "FilteredURLClassLoader.java")

    def f(b):
        if site(b)[0] == tgt and field(b, "in=") == "loadClass":
            return None
        return b

    rep.maprecs("svcload", f)
    return "FilteredURLClassLoader#loadClass no longer loads anything by name"


@fixture("M4", "golden", "java.util.ServiceLoader is IMPORTED and never called: "
         "both methods declare the type and neither performs a lookup. The "
         "departure half is full -- the hand-rolled scans really are gone -- "
         "and the arrival half is 0, so B-15.3 is 0 and the tree has deleted "
         "its service lookup rather than migrated it. form=platformtype is not "
         "form=platformload, and this fixture is the only thing that says so.")
def _m4(rep, env):
    def f(b):
        if b.split()[1] == "form=platformload":
            return b.replace("form=platformload", "form=platformtype") \
                    .replace("name=load", "name=java.util.ServiceLoader")
        return b

    rep.maprecs("svcload", f)
    return "both platformload records become platformtype"


@fixture("M5", "golden", "the source shape is perfect and the behaviour is "
         "gone: every one of the 69 recorded service-lookup transcripts "
         "differs. The three source halves are full, the scaling is 0.000000 "
         "and B-15.3 is 0 -- a tree that rewrote the lookup into something that "
         "does not resolve a provider. B-15 keeps B-15.2 and nothing else, and "
         "family C is untouched because C-3's five ids are a different "
         "population read out of the same file.",
         svcbroken="all")
def _m5(rep, env):
    return "nothing in the report; all 69 recorded behaviours differ"


@fixture("M6", "golden", "nine of the 69 differ -- the partial regression a "
         "candidate who moved the lookup but lost the package-private provider "
         "produces. B-15.3 is scaled by 60/69 and every source half stays full, "
         "which is the shape that tells a behaviour regression from a source "
         "one.",
         svcbroken=9)
def _m6(rep, env):
    return "nothing in the report; 9 of 69 recorded behaviours differ"


# --- C-1's pair and B-16's twelve ---------------------------------------------
# Step 09's fourteen rows, unedited, and the column they land in has changed
# under twelve of them. L1 and L2 move ${WORK}/scan.txt and the scan is still
# family C's first leg -- a different major over the same channel -- so those
# two are still family C rows. L3..L14 move the four SOURCE constructs step 09
# scored, and at this rung those are B-16: a step-10 tree has no reason to
# touch a sealed hierarchy or an arrow switch and every reason to keep one, so
# the twelve rows that break them are now the ninth trap's instrument rather
# than this rung's own.
#
# WHAT DID NOT CHANGE IS THE POINT. Not one figure in L3..L14's own halves
# moved -- 9/21, 0/7, 4/7, 6/11, 1/2 are step 09's numbers and still are -- and
# only the composite they roll up into did, because B-16 renormalises four legs
# over the 0.90 that step 09's five shared. Every one of the twelve leaves
# familyC at 1.000000, which is the assertion that this rung's four legs read
# none of the four constructs below.
IP_CONV_BUCKETS = sorted(IP_CONV)
IP_SKIP_BUCKETS = sorted(IP_SKIP)
TB_MOVE_BUCKET = sorted(TB_MOVE)[0]
TB_HOLD_BUCKETS = sorted(TB_HOLD)
SW_FILES = sorted(SW_WANT)


@fixture("L1", "golden", "the migration is complete in every source respect "
         "and 700 of the 746 class files are at major 65 -- the build file of "
         "one subproject was never moved, so its classes are still at 61. C-1 "
         "is 700/746 = 0.938338 and nothing else in the row moves; familyC is "
         "0.04 x 0.938338 + 0.96 = 0.997534. The leg is a FRACTION and not a "
         "flag for exactly this: a tree that moved fifteen of sixteen build "
         "files has done most of the work and the gate should say most. THE "
         "MAJOR THE REST SIT AT IS THE ONE THE RUNG BELOW WANTED, which is why "
         "this row is not a repeat of step 09's: the tree it describes passed "
         "the previous rung's C-1 outright and still reads 0.938338 here.",
         scan=scan_split(700))
def _l1(rep, env):
    return "700 of 746 class files at major %d, the rest still at %d" % (
        WANT_MAJOR, HOLD_MAJOR)


@fixture("L2", "golden", "no class-file scan at all: the build produced no "
         "class output the gate could walk. C-1 is the shell LITERAL 0 under "
         "gate.c1=no-scan, and the other three legs of family C are full "
         "because they read the source AST, which is intact. familyC = "
         "0.960000, which is NOT step 09's 0.900000: that rung weighted its "
         "class-file leg at 0.10 and this one weighs it at 0.04, because the "
         "automated-migrator calibration reached the level outright at both. "
         "THE COMPANION TO K19 "
         "AND K20 FROM THE OTHER SIDE: those two move the jars and leave the "
         "scan, this one moves the scan and leaves the jars, and B-15.2 stays "
         "at 1.000000 in all three.",
         scan=None)
def _l2(rep, env):
    return "no scan.txt; the source report and the published jars intact"


@fixture("L3", "golden", "nine of the eighteen convertible buckets are still "
         "a plain `instanceof` with a cast on the next line -- twelve of the "
         "twenty-one sites, because three of those buckets hold two each. The "
         "converted half is 9/21 = 0.428571, the held half and the total-site "
         "term are both full, and the min() takes the converted half: B-16.2 "
         "is 0.428571 and B-16 is 0.873016. A "
         "half-finished sweep, which is what a file-at-a-time migration looks "
         "like when it runs out of context.")
def _l3(rep, env):
    n = unbind(rep, IP_CONV_BUCKETS[:9])
    return "%d site(s) in 9 bucket(s) back to a plain instanceof" % n


@fixture("L4", "golden", "every one of the twenty-eight sites is a pattern, "
         "including the seven that have no cast after them. The converted half "
         "is full and the HELD half is 0/7, so B-16.2 is 0.000000 and B-16 is "
         "0.777778: a tree that "
         "bound a variable at every `instanceof` it could find has changed "
         "seven sites that had nothing to gain and reads worse than the tree "
         "it started from. This is the fixture that makes B-16.2 a min() of three "
         "and not a count of one.")
def _l4(rep, env):
    n = bind_all(rep)
    return "%d held site(s) bound as well; ipattern.plain=0" % n


@fixture("L5", "golden", "three of the seven held sites were DELETED rather "
         "than left alone -- the `instanceof` and its branch removed on the "
         "grounds that nothing needed converting there. The held half is 4/7 = "
         "0.571429 and the total-site term is 25/28 = 0.892857, and the min() "
         "takes the held half: B-16.2 is 0.571429 and B-16 is 0.904762. Two "
         "terms fall from one edit and that is not "
         "double counting: one says the site is gone and the other says the "
         "population is short.")
def _l5(rep, env):
    n = drop_ip(rep, IP_SKIP_BUCKETS[:3])
    return "%d held site(s) deleted; ipattern.size=%d" % (n, rep.num("ipattern.size"))


@fixture("L6", "golden", "the sixteen newline literals have gone from "
         "ServletTestRunner#doGet and NO text block replaced them -- the "
         "document was moved into a resource file, or concatenated, or "
         "deleted. The move half asks BOTH questions and this row is why: the "
         "literals being gone is half an answer, and a gate that stopped there "
         "would pay a tree that deleted the page. B-16.3 is 0.000000 and B-16 "
         "is 0.711111.")
def _l6(rep, env):
    n = drop_textblocks(rep)
    return "%d text block(s) removed; the literals are still gone" % n


@fixture("L7", "golden", "the text block arrived and the XML fragment builder "
         "was flattened with it: ToolingDeploymentFormatter#format lost three "
         "of its three newline literals. The held half is 4/7 = 0.571429 and "
         "the move half is full, so B-16.3 is 0.571429 and B-16 is 0.876191. "
         "The hold half is what "
         "makes B-16.3 a migration gate rather than a census of text blocks: "
         "these two methods build an XML fragment a line at a time and a text "
         "block would change the bytes they emit.")
def _l7(rep, env):
    n = drop_strnl(rep, TB_HOLD_BUCKETS[0])
    return "%d newline literal(s) gone from %s" % (n, TB_HOLD_BUCKETS[0].split("/")[-1])


@fixture("L8", "golden", "the source shape is perfect and two of the five "
         "recorded document behaviours differ: the text block is there, the "
         "XML builders are untouched, and the page the runner serves is not "
         "the page it served. B-16.3 is scaled by 3/5 = 0.600000 and B-16 is "
         "0.884444. A text block "
         "with an incidental-whitespace mistake produces exactly this row, and "
         "no source-shape term can see it.",
         tbbroken=2)
def _l8(rep, env):
    return "nothing in the report; 2 of 5 recorded document behaviours differ"


@fixture("L9", "golden", "six of the eleven event hierarchies are sealed and "
         "five are not -- the sweep stopped at the package boundary. The "
         "closed half is 6/11 = 0.545455, the open half is full, and the min() "
         "takes the closed half: B-16.4 is 0.545455 and B-16 is 0.818182.")
def _l9(rep, env):
    names = [n for _, n, _ in SEAL_ROOT][:5]
    n = unseal(rep, names)
    return "%d root(s) no longer sealed: %s" % (n, ", ".join(names))


@fixture("L10", "golden", "all twenty-two candidates are sealed: the eleven "
         "event roots AND the eleven types this library is extended through -- "
         "the resource provider interface, the context and descriptor bases, "
         "the tests' own service doubles, and the two event roots javac itself "
         "refuses to seal. The closed half is full and the OPEN half is 0/11, "
         "so B-16.4 is 0.000000 and B-16 is 0.600000. NOTHING ELSE IN THIS "
         "IMAGE CAN SEE THIS MISTAKE: nothing in the tree subclasses any of "
         "the eleven from outside, so family A replays this rung's whole era "
         "and finds every transcript identical. The open half is the only "
         "witness.")
def _l10(rep, env):
    n = seal_open(rep, SEAL_OPEN)
    return "%d extension point(s) closed as well; sealed.decl=%d" % (
        n, rep.num("sealed.decl"))


@fixture("L11", "golden", "eleven roots are sealed and one permits clause is a "
         "name short: ContainerControlEvent names three of its four direct "
         "subtypes. The closed half is 10/11 = 0.909091, so B-16.4 is "
         "0.909091 and B-16 is 0.963636. The comparison is a "
         "TRIPLE -- file, name and the sorted permits list -- and this is the "
         "row that says so: a gate that asked only 'is this type sealed' would "
         "score this tree with the golden, and the tree does not compile.")
def _l11(rep, env):
    gone = clip_permits(rep, SEAL_ROOT[0][1])
    return "%s no longer permits %s" % (SEAL_ROOT[0][1], gone)


@fixture("L12", "golden", "one of the two switches is arrow form and the other "
         "is still colon form, fall-through and all. B-16.5 is 1/2 = 0.500000 "
         "and B-16 is 0.955556. "
         "gate.b16.5.arrow=1 and gate.b16.5.colon=1 are the facts that "
         "distinguish "
         "this row from L13, which scores the same number for the opposite "
         "reason.")
def _l12(rep, env):
    n = uncurl(rep, SW_FILES[0])
    return "%d switch(es) in %s back to colon form" % (n, SW_FILES[0].split("/")[-1])


@fixture("L13", "golden", "one file's colon-form switch is untouched and a NEW "
         "arrow-form switch was added beside it. B-16.5 is 1/2 = 0.500000 and "
         "B-16 is 0.955556, the same as L12, and gate.b16.5.arrow=2 against "
         "L12's 1 is the difference. "
         "A tree-wide arrow count would read this fixture as finished: two "
         "arrow switches, which is exactly what the golden has. The gate is "
         "per file and asks both halves -- enough arrows AND no colon left -- "
         "and this row is the whole reason for the second half.")
def _l13(rep, env):
    uncurl(rep, SW_FILES[1])
    add_arrow(rep, SW_FILES[1])
    return "%s: 1 colon switch held, 1 arrow switch added" % SW_FILES[1].split("/")[-1]


@fixture("L14", "golden", "the sixteen newline literals have gone from "
         "ServletTestRunner#doGet and a text block does sit in that file -- as "
         "an UNUSED CONSTANT AT CLASS SCOPE, not in the method that used to "
         "write the document a line at a time. gate.b16.3.textblocks is still 1 "
         "and the move half is 0.000000, so B-16.3 is 0.000000 and B-16 is "
         "0.711111. This is the row that makes the arrival half method-scoped: "
         "keyed by file it scored this tree 1.000000, which paid full marks "
         "for sixteen deleted literals and a constant nobody reads. L14 and L6 "
         "score the same thirteen figures and gate.b16.3.textblocks=1 against "
         "L6's 0 is the difference -- and N9 below is the same lesson carried "
         "to this rung's own C-3, where the construct that lands in the wrong "
         "method is a pattern switch.")
def _l14(rep, env):
    n = declass_textblocks(rep)
    return ("%d text block(s) moved to class scope; textblock.size=%d, "
            "textblock.methods=%d"
            % (n, rep.num("textblock.size"), rep.num("textblock.methods")))
# --- this rung's own thirteen -------------------------------------------------
# Three legs and thirteen ways of getting one of them wrong; C-1 keeps L1 and
# L2. Every row leaves the four inherited families at 1.000000, which is the
# ninth trap's assertion read from the other side: these are the fixtures
# written for THIS rung, and the four columns that must not move are the ones
# written for the four below.
#
# THE THREE LEGS DIVIDE THEIR ROWS BY HALF AND NOT BY CONSTRUCT. C-2 and C-4
# each ask three questions -- did the convertible sites leave, did something
# arrive where they were, did the sites that must not move stay -- and there
# is one row per question per leg, plus one that answers the hold half by
# deleting the population instead of converting it. C-3 asks two structural
# questions and multiplies by a recorded one, so it has one row per condition:
# too few arms, the wrong type, a switch where there must not be one, a switch
# in the wrong method, a switch beside a chain that never left, and five
# recordings that stopped matching.
#
# WHAT THE THREE `why` FIGURES MEAN. Each row quotes the fraction its half
# reads, the leg the min() then takes, and the familyC that composes -- 0.04 x
# C-1 + 0.17 x C-2 + 0.65 x C-3 + 0.14 x C-4 -- so a rehearsal transcription
# error shows up as a disagreement between two numbers in the same sentence
# rather than as a number nobody can check.
RT_SINGLE = sorted(set(RT_CONV) - set(b for b, _, _ in CH_CONV))
RT_CHAIN = sorted(b for b, _, _ in CH_CONV)
RT_MANAGER = [b for b in sorted(RT_HOLD) if "/impl/core/ManagerImpl.java#" in b]
RT_EVERY = sorted(set(RT_CONV) | set(RT_HOLD))
CH_CLIP = CH_CONV[0][0]
CH_RETYPE = CH_CONV[2][0]
CH_MOVE = CH_CONV[1][0]
CH_SHUT = CH_HOLD[1][0]
SQ_PAIR = sorted(SQ_CONV)[:2]
SQ_KEEP = [b for b in sorted(SQ_HOLD)
           if b.endswith("DynamicServiceLoader.java#onlyOne")
           or b.endswith("EventTestRunnerAdaptor.java#test")]

# THE THREE SINGLES ARE THE THREE NEW PATTERNS, asserted and not assumed. Ten
# of the thirteen convertible reflective tests sit in the four chains C-3 asks
# for a pattern switch over; the other three are single tests and become
# `instanceof` patterns. That is why C-2's arrival half unions two record
# kinds, and if the golden ever stopped agreeing -- a fourth chain, or a single
# test converted some other way -- N1 and N2 would be describing a tree that
# does not exist. The assertion is here rather than in a comment.
assert len(RT_SINGLE) == 3 and sum(RT_CONV[b] for b in RT_SINGLE) == 3, RT_SINGLE
# ...and the chains are the other ten, which makes RT_SINGLE and RT_CHAIN a
# PARTITION of the thirteen rather than two lists that happen not to overlap.
# N1 moves the three and N14 moves the ten, and between them they cover every
# conv site C-2 has: a row that fell out of both would be a departure nothing
# in the table exercises.
assert sorted(set(RT_SINGLE) | set(RT_CHAIN)) == sorted(RT_CONV), \
    "the singles and the chains are not the thirteen conv buckets"
assert sum(RT_CONV[b] for b in RT_CHAIN) == CH_CONV_W, RT_CHAIN
assert (sorted(set(bucket_of(b) for b in GOLD.recs("ipattern"))
               - set(bucket_of(b) for b in BASE.recs("ipattern")))
        == RT_SINGLE), "the golden's new instanceof patterns are not the singles"
assert len(RT_MANAGER) == 4 and sum(RT_HOLD[b] for b in RT_MANAGER) == 4, RT_MANAGER
assert sum(RT_CONV[b] for b in RT_CONV) + sum(RT_HOLD[b] for b in RT_HOLD) == 27
assert sum(SQ_CONV[b] for b in SQ_PAIR) == 2, SQ_PAIR
assert sum(SQ_HOLD[b] for b in SQ_KEEP) == 3, SQ_KEEP


@fixture("N1", "golden", "the three single reflective tests are still there "
         "AND the three `instanceof` patterns that replace them are there too "
         "-- DeploymentGenerator#buildTestableDeployments, "
         "OSGiDeploymentPackager#generateDeployment and "
         "ResourceInjectionEnricher#isPrimitiveNull each test the type twice, "
         "once the old way and once the new. The departure half is 10/13 = "
         "0.769231, the arrival half and the hold half are full, and the "
         "min() takes the departure half: C-2 is 0.769231 and familyC is "
         "0.960769. A GATE THAT ONLY ASKED WHETHER THE PATTERN ARRIVED WOULD "
         "PAY THIS TREE IN FULL, and the tree still runs isAssignableFrom on "
         "every call. N2 is the same figure from the other side.")
def _n1(rep, env):
    n = sum(restore_recs(rep, "reftest", b) for b in RT_SINGLE)
    return "%d reflective test(s) still beside the pattern that replaced it" % n


@fixture("N2", "golden", "the three single reflective tests are gone and "
         "nothing replaced them: the branch was rewritten some other way, or "
         "deleted. The departure half is FULL -- 13/13, because departure is "
         "measured by what is no longer in the report -- the arrival half is "
         "10/13 = 0.769231, and the min() takes the arrival half: C-2 is "
         "0.769231 and familyC is 0.960769, the same two figures as N1. "
         "gate.c2.depfrac and gate.c2.arrfrac are swapped between the two "
         "rows and that is the whole difference; a leg that scored departures "
         "alone, or arrivals alone, would collapse them onto one message. "
         "B-16.2 does NOT move: the three buckets are disjoint from step 09's "
         "twenty-eight sites, and gate.b16.2.sites reads 28 rather than 31.")
def _n2(rep, env):
    n = drop_ip(rep, RT_SINGLE)
    return "%d instanceof pattern(s) removed; ipattern.size=%d" % (
        n, rep.num("ipattern.size"))


@fixture("N3", "golden", "the four reflective tests in ManagerImpl were "
         "converted as well -- getContext, getExtension, resolveObservers and "
         "resolveInterceptorObservers, which test a Class VALUE against a "
         "type the caller supplies and cannot be an `instanceof` pattern at "
         "all. The hold half is 10/14 = 0.714286, the other two halves are "
         "full, and C-2 is 0.714286 with familyC at 0.951429. This is the "
         "fixture that makes C-2 a min() of three: a sweep that converted "
         "every isAssignableFrom it could find has written a tree that does "
         "not compile, and the two halves that measure the work are both "
         "full on it.")
def _n3(rep, env):
    n = drop_recs(rep, "reftest", RT_MANAGER)
    return "%d held reflective test(s) gone from ManagerImpl" % n


@fixture("N4", "golden", "every reflective type test in the population is "
         "gone: the thirteen that had to leave and the fourteen that had to "
         "stay. The departure half is full and the arrival half is full -- "
         "the patterns really did arrive -- and the hold half is 0/14, so "
         "C-2 is 0.000000 and familyC is 0.830000. DELETION SCORES AS BADLY "
         "AS LEAVING IT, which is the rule every rung since 07 has kept, and "
         "the hold half is the term that enforces it here: gate.c2.reftests "
         "reads 8 rather than 22, and the eight that are left are the ones "
         "no baseline row names.")
def _n4(rep, env):
    n = drop_recs(rep, "reftest", RT_EVERY)
    return "%d reflective test(s) deleted; reftest.size=%d" % (
        n, rep.num("reftest.size"))


@fixture("N5", "golden", "the v_2_5 packager's pattern switch has two arms "
         "where the chain it replaced had three: EnterpriseArchive and "
         "WebArchive are matched and JavaArchive falls to the default. The "
         "conv half counts ARMS, so this costs the whole three-arm chain and "
         "not one arm of it -- 7/10 = 0.700000 -- the hold half is full, and "
         "C-3 is 0.700000 with familyC at 0.805000. gate.c3.patterns reads 9 "
         "against the golden's 10. THE ARM COUNT AND THE TYPE SET ARE TWO "
         "CONDITIONS and this row moves only the first: the types the switch "
         "does name are still named, so a leg that read coverage alone would "
         "score this tree whole. N6 is the mirror.")
def _n5(rep, env):
    n = clip_swpat(rep, CH_CLIP, 2)
    return "v_2_5 packager switch declares %d arms, not 3" % n


@fixture("N6", "golden", "one arm of the beans.xml processor's pattern switch "
         "names the wrong type: `case Archive` where the chain tested for "
         "WebArchive. The arm count does not move -- gate.c3.patterns is "
         "still 10 -- and the coverage condition fails, so the two-arm chain "
         "is lost: the conv half is 8/10 = 0.800000 and C-3 is 0.800000. "
         "TWO LEGS MOVE HERE AND BOTH MUST. C-2's arrival half reads the same "
         "`swpat` type set, so the WebArchive arrival is gone from it too: "
         "12/13 = 0.923077, and C-2 is 0.923077. familyC is 0.856923. This is "
         "the most plausible mistake at this rung -- the switch compiles, has "
         "the right shape, and sends WebArchive to the wrong branch -- and it "
         "is the one the recorded behaviour would also catch, which is why "
         "the static halves and the transcripts multiply rather than vote.")
def _n6(rep, env):
    t = retype_swpat(rep, CH_RETYPE, "WebArchive", "Archive")
    return "beans.xml processor switch matches %s instead of WebArchive" % t


@fixture("N7", "golden", "the annotation scenario generator's `validate` "
         "dispatch was converted too -- a pattern switch on Archive and "
         "Descriptor where the baseline says the chain must stay a chain, "
         "because its arms are not disjoint and its order is the semantics. "
         "The departure and arrival halves are both full and the HOLD "
         "half is 1/2 = 0.500000, so C-3 "
         "is 0.500000 and familyC is 0.675000. gate.c3.switches reads 5 and "
         "gate.c3.patterns 12. THE HOLD HALF COUNTS CHAINS AND NOT ARMS, "
         "which this row is the reason for: a per-arm denominator would have "
         "paid the candidate for converting half of a dispatch that should "
         "not have been touched at all.")
def _n7(rep, env):
    add_swpat(rep, CH_SHUT.split("#")[0], CH_SHUT.split("#")[1],
              ("Archive", "Descriptor"))
    return "a pattern switch planted in %s" % CH_SHUT.split("/")[-1]


@fixture("N8", "golden", "the source shape is perfect and two of the five "
         "recorded dispatch inputs behave differently: the switches are "
         "there, the arms are right, the types are right, and the two "
         "enricher chains a null reaches now throw where the chain they "
         "replaced returned. C-3 is scaled by 3/5 = 0.600000 and familyC is "
         "0.740000. THE ROW SEPARATES A BROKEN EXECUTION FROM A MISSING "
         "CONVERSION and that is the whole of what it is for: every static "
         "half of the leg is full -- gate.c3.depfrac, gate.c3.arrfrac and "
         "gate.c3.holdfrac all read 1.000000 -- and the one fact that moved "
         "is gate.c3.exec, which MULTIPLIES all three rather than joining "
         "them. N7 loses the same leg through its hold half and lands on "
         "0.500000, so the two rows no longer meet on a figure, and what "
         "says which half failed is the gate facts and not the totals. JEP "
         "441 THROWS ON NULL WHERE AN IF-CHAIN FALLS THROUGH, and this tree "
         "answers that input three different ways across the four chains, so "
         "no static rule is right at all four sites and the recordings are "
         "the only reading that is not an author's opinion. A candidate "
         "loses these in family A as well.",
         swbroken=2)
def _n8(rep, env):
    return "nothing in the report; 2 of 5 recorded dispatch inputs differ"


@fixture("N9", "golden", "the v_3 packager's chain is gone and a pattern "
         "switch on the same three types sits in the same FILE -- in "
         "`handleArchive`, not in `generateDeployment` where the chain was. "
         "The conv half keys on file#method, so the three-arm chain scores "
         "nothing: 7/10 = 0.700000 and C-3 is 0.700000. gate.c3.switches is "
         "still 4 and gate.c3.patterns still 10, which is the pair that "
         "separates this row from N5's identical conv half. TWO LEGS AGAIN, "
         "and for the same reason as N6: the three arrivals C-2 counted in "
         "that method are gone with the switch, so C-2 is 10/13 = 0.769231 "
         "and familyC is 0.765769. L14 IS THIS LESSON ONE RUNG DOWN -- a text "
         "block hoisted to class scope -- and this is why every arrival half "
         "in this family is method-scoped.")
def _n9(rep, env):
    n = drop_recs(rep, "swpat", [CH_MOVE])
    add_swpat(rep, CH_MOVE.split("#")[0], "handleArchive",
              ("EnterpriseArchive", "WebArchive", "JavaArchive"))
    return ("%d switch(es) out of generateDeployment, 1 into handleArchive; "
            "swpat.size=%d" % (n, rep.num("swpat.size")))


@fixture("N14", "golden", "nothing was converted and four correct pattern "
         "switches were added beside the chains: the reflective tests are "
         "back in all four conv chain buckets -- both servlet packagers, the "
         "beans.xml processor and the resource enricher -- at the input "
         "tree's own lines, and the golden's four switches, ten arms, type "
         "sets and one `case null` are untouched beside them. The departure "
         "half is 0/10 = 0.000000, the arrival half and the hold half are "
         "both full, all five recordings still match so the scaling is "
         "1.000000, and the min() takes the departure half: C-3 is 0.000000. "
         "C-2 falls with it, because ten of its thirteen conv sites are back "
         "-- 3/13 = 0.230769 -- and familyC is 0.219231. THIS IS ROUND 1'S "
         "BLOCKER AS A ROW. A pattern switch planted beside a live "
         "`X.class.isInstance(v)` chain is not a conversion: it compiles, the "
         "chain is still what runs, so no recording moves and exec cannot "
         "catch it, and before this leg had a departure half the tree read "
         "0.65 of family C at full credit for four switches that converted "
         "nothing. N1 is the same lesson on C-2's three standalone sites; "
         "this is the ten in the chains, and the leg the rung is named after.")
def _n14(rep, env):
    n = sum(restore_recs(rep, "reftest", b) for b in RT_CHAIN)
    return ("%d reflective test(s) back beside the four switches that "
            "replaced none of them" % n)


@fixture("N15", "golden", "N14's tree one rewrite further on, and it is round "
         "2's blocker: the four chains are still chains and every arm of them "
         "is now an `instanceof` PATTERN -- `applicationArchive instanceof "
         "EnterpriseArchive e` where the input tree wrote "
         "`EnterpriseArchive.class.isInstance(applicationArchive)` -- with the "
         "golden's four switches, ten arms, type sets and one `case null` "
         "untouched beside them. Not one reflective call is left, so the "
         "departure half as it shipped in round 1 read 10 of 10 on a tree that "
         "converted no dispatch at all. IT COUNTS THE `instanceof` AS THE OLD "
         "SPELLING NOW: all ten chain types are still named one arm at a time "
         "in the four conv buckets, gate.c3.ipleft is 10, the residual is 0 "
         "reftests + 10 names = 10 against 10 arms, and gate.c3.departed is 0 "
         "-- 0/10 = 0.000000. The arrival half is 10/10, the hold half 2/2, "
         "all five recordings still match so the scaling is 1.000000, and the "
         "min() takes the departure half: C-3 is 0.000000. "
         "C-2 DOES NOT FALL WITH IT AND THAT IS THE POINT OF THE ROW, which "
         "is where it parts from N14. C-2's departure counts reflective tests "
         "and they are all gone -- 13/13 -- and C-2's arrival accepts an "
         "`ipattern` naming the type as readily as a switch arm, so the ten "
         "chain types arrive twice over and the three standalone sites arrive "
         "as the golden's: 13/13, hold 14/14, C-2 = 1.000000. familyC is "
         "0.04 + 0.17 + 0.65 x 0 + 0.14 = 0.350000, which no other row in the "
         "table reads. THE ATTACK IS NOT HYPOTHETICAL AND THAT IS WHY IT HAD "
         "TO BE PRICED: requirement 4 asks for this rewrite in its own right, "
         "C-2 pays for it, and OpenRewrite's "
         "ReplaceClassIsInstanceWithInstanceof performs it unattended on eight "
         "of these ten arms -- so a candidate who plants four covering "
         "switches on top of an automated pass arrives here without having "
         "converted a single dispatch. gate.c2.patterns reads 41 against the "
         "golden's 31, which is the ten and is the fact that says which "
         "spelling this tree stopped at.")
def _n15(rep, env):
    n = sum(restore_as_ipattern(rep, b) for b in RT_CHAIN)
    return ("%d chain arm(s) rewritten as instanceof patterns beside the four "
            "switches that replaced none of them; ipattern.size=%d"
            % (n, rep.num("ipattern.size")))


@fixture("N10", "golden", "two of the seven guarded first-element reads are "
         "still index arithmetic AND the getFirst() that replaces them is "
         "also there: ContainerRegistry#findDefaultContainer and "
         "ProtocolRegistry#findDefaultProtocol each read the head twice. The "
         "departure half is 5/7 = 0.714286, the arrival half and the hold "
         "half are full, and C-4 is 0.714286 with familyC at 0.960000. "
         "gate.c4.firstreads reads 21 against the golden's 19. N11 is the "
         "same figure from the other side, and the pair is C-4's version of "
         "N1 and N2.")
def _n10(rep, env):
    n = sum(restore_recs(rep, "seqfirst", b) for b in SQ_PAIR)
    return "%d index read(s) still beside the getFirst() that replaced it" % n


@fixture("N11", "golden", "the same two reads departed and no sequenced "
         "access arrived: the head is fetched some third way, or the method "
         "no longer fetches it. The departure half is full, the arrival half "
         "is 5/7 = 0.714286, and C-4 is 0.714286 with familyC at 0.960000 -- "
         "N10's two figures, with gate.c4.arrfrac and gate.c4.depfrac "
         "swapped. THE TWO DENOMINATORS ARE COMPUTED SEPARATELY and are both "
         "7 only because every convertible bucket holds exactly one site: "
         "departures count SITES and arrivals count BUCKETS, and the shell "
         "derives each from the population rather than reusing the other.")
def _n11(rep, env):
    n = drop_recs(rep, "seqnew", SQ_PAIR)
    return "%d sequenced read(s) removed; seqnew.size=%d" % (
        n, rep.num("seqnew.size"))


@fixture("N12", "golden", "three of the nine first-element reads that must "
         "not move are gone -- DynamicServiceLoader#onlyOne, which reads the "
         "head of a list it has just asserted has exactly one element, and "
         "EventTestRunnerAdaptor#test, which reads a queue it is draining. "
         "The hold half is 6/9 = 0.666667, the other two halves are full, and "
         "C-4 is 0.666667 with familyC at 0.953333. These nine read index 0 "
         "of something that is not a List, or read it under a guard that "
         "makes the sequenced form a different program; the hold half is what "
         "stops a blanket sweep from scoring.")
def _n12(rep, env):
    n = drop_recs(rep, "seqfirst", SQ_KEEP)
    return "%d held first-element read(s) gone; seqfirst.size=%d" % (
        n, rep.num("seqfirst.size"))


@fixture("N13", "golden", "the sequenced access arrived and reads the WRONG "
         "END: ContainerRegistry#findDefaultContainer calls getLast() where "
         "the code it replaced read index 0. The arrival half keys on "
         "`op=getFirst` and is 6/7 = 0.857143, so C-4 is 0.857143 and familyC "
         "is 0.980000. gate.c4.sequenced.read STILL READS 7, because getLast "
         "is a read like any other and that counter counts reads -- it is "
         "reported and not scored, and this row is the reason both of those "
         "are true at once. A leg that had scored the counter would have paid "
         "this tree in full for a method that now returns a different "
         "container.")
def _n13(rep, env):
    op = retype_seqnew(rep, SQ_PAIR[0])
    return "findDefaultContainer reads %s(); seqnew.read=%d" % (
        op, rep.num("seqnew.read"))


CLASSES_PER_FIXTURE = 24
B12_MARKERS = ("java/util/Optional", "java/util/stream/Collectors",
               "java/util/Objects", "java/time/Instant")


CLASSES_PER_JAR = 2


def write_jars(d, counts, mods):
    """The published artifacts: real jars, read by two blocks on two channels.

    B-12e unzips them and binary-greps their class files for four package
    names -- step 05's must-flip, held at this rung as one leg of B-12.
    B-15's name leg hands the same list to java.lang.module and asks what each
    one calls itself.

    THE TWO CHANNELS ARE TWO AGAIN, which reverses step 08 and is worth
    stating rather than discovering. At step 07 C-1 read ${WORK}/scan.txt and
    B-12e read the jars, so a fixture could carry one and not the other; at
    step 08 both read ${WORK}/modulejars.txt and `mods=None` was ONE absence
    with two consequences. This rung's C-1 is back on scan.txt -- the
    class-file major over the class files there are -- so the jars feed B-12e
    and B-15 and nothing else. `scan=None` and `mods=None` are now two
    separate absences with two separate gate facts, and there are four
    fixtures on the split: L1 and L2 move the scan and leave the jars alone,
    K19 and K20 move the jars and leave the scan alone.

    Twelve jars and not two, so the `while read` over modulejars.txt runs
    twelve times and the marker classes are spread across all of them: no
    package's whole count can be collected from one archive, which is the
    failure a build that dropped one module produces.

    The class bytes are a magic number and a run of Utf8-shaped entries. B-12e
    is a binary grep over constant pools, not a class parser. The PACKAGE those
    entries sit in is real, though, because ModuleFinder does read entry names:
    a jar with no .class entry at all derives an automatic module with no
    packages, which is legal but is not what a published artifact looks like.

    Every entry is written at a FROZEN timestamp. zipfile defaults each ZipInfo
    to the wall clock, which at step 05 made two runs of the generator produce
    jars with identical contents and different bytes -- trap 10, in the
    artefact rather than the expectation.
    """
    if mods is None:
        return
    jd = os.path.join(d, "jars")
    if not os.path.isdir(jd):
        os.makedirs(jd)

    def put(z, name, data):
        zi = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        z.writestr(zi, data)

    marks = list(counts) if counts is not None else [0, 0, 0, 0]
    slot = 0
    for m in mods:
        pkg = "org/jboss/arquillian/%s" % m.artifact.replace("-", "/")
        z = zipfile.ZipFile(os.path.join(jd, "%s-1.0.0-SNAPSHOT.jar" % m.artifact), "w")
        try:
            mf = "Manifest-Version: 1.0\n"
            if m.name:
                # Written through the same 72-byte wrap the jar tool produces,
                # because that is the whole reason arqgate.Modules reads the
                # manifest with java.util.jar.Manifest instead of grepping for
                # the line: a name long enough to wrap is invisible to a grep
                # and plain to the parser. Every name in modnames.txt is long
                # enough once the attribute name is counted.
                mf += wrap_manifest("Automatic-Module-Name", m.name)
            put(z, "META-INF/MANIFEST.MF", mf)
            for i in range(CLASSES_PER_JAR):
                blob = bytearray(b"\xca\xfe\xba\xbe\x00\x00\x00\x37")
                blob += b"\x01\x00\x20org/jboss/arquillian/zz/C%02d" % slot
                for name, n in zip(B12_MARKERS, marks):
                    if slot < n:
                        blob += b"\x01" + bytes([0, len(name)]) + name.encode()
                blob += b"\x00" * 8
                put(z, "%s/C%02d.class" % (pkg, slot), bytes(blob))
                slot += 1
            if m.poison:
                # The input tree's own shape, in one entry: a provider file
                # whose lines are `key=value` properties rather than binary
                # class names. shrinkwrap-descriptors-api reads this format and
                # ModuleFinder refuses it.
                put(z, "META-INF/services/org.jboss.arquillian.impl."
                       "configuration.api.ArquillianDescriptor",
                    "implClass=org.jboss.arquillian.impl.configuration."
                    "ArquillianDescriptorImpl\n"
                    "importerClass=org.jboss.arquillian.impl.configuration."
                    "ArquillianDescriptorImporter\n"
                    "defaultName=arquillian.xml\n")
        finally:
            z.close()
    for n in marks:
        assert n <= CLASSES_PER_JAR * len(mods), n


def wrap_manifest(key, value):
    """A manifest attribute wrapped the way java.util.jar.Manifest writes it:
    72 bytes to a line, every continuation beginning with one space."""
    line = "%s: %s" % (key, value)
    out, first = [], line[:71]
    out.append(first)
    rest = line[71:]
    while rest:
        out.append(" " + rest[:70])
        rest = rest[70:]
    return "\n".join(out) + "\n"


def svcbroken_of(env):
    """The fixture's B-15.3 behaviour breakage, in the three spellings the
    catalogue uses: `"all"` for every id, an int for the first n, and the
    default empty tuple for a tree whose recorded behaviour is intact.

    Step 08's own helper, renamed in its docstring only: these 69 ids were
    that rung's C-3 scaling and are this rung's B-15.3 scaling, and the ids
    themselves have not moved. `tbbroken_of` below is the same shape over this
    rung's five text-block ids, and the two lists are kept apart because one
    behavior.summary feeds both and the shell greps each against its own
    baseline file."""
    v = env.get("svcbroken", ())
    if v == "all":
        return list(SVCIDS)
    if isinstance(v, int):
        return list(SVCIDS[:v])
    return list(v)




def tbbroken_of(env):
    """The fixture's C-3 behaviour breakage over this rung's five text-block
    ids, in the same three spellings `svcbroken_of` accepts.

    A SECOND LIST AND NOT A SECOND FILE. There is one behavior.summary and its
    `behavior.differ[]` lines carry whatever ids differed; the scorer greps
    them against tbids.txt for C-3 and against svcids.txt for B-15.3 and
    against the step-07 http list for B-14.5. Three populations, three
    scalings, one artefact -- which is why `score` below takes ONE differ list
    and splits it, rather than taking three.
    """
    v = env.get("tbbroken", ())
    if v == "all":
        return list(TBIDS)
    if isinstance(v, int):
        return list(TBIDS[:v])
    return list(v)


def swbroken_of(env):
    """The fixture's C-3 behaviour breakage over this rung's five switch ids,
    in the same three spellings the two helpers above accept.

    A FOURTH LIST AND STILL ONE FILE. behavior.summary is now read by four
    provisos against four baseline lists -- B-14.5's eighteen exchanges,
    B-15.3's 69 services, B-16.3's five text blocks and C-3's five pattern
    dispatches -- and every one of them greps its own list out of the same
    `behavior.differ[]` lines. The lists are disjoint by construction and the
    rehearsal asserts that four times over: a run that breaks one population
    must leave the other three columns exactly where they were.
    """
    v = env.get("swbroken", ())
    if v == "all":
        return list(SWIDS)
    if isinstance(v, int):
        return list(SWIDS[:v])
    return list(v)


def write_fixture(name, rep, why, env, note):
    d = os.path.join(OUT, name)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    rep.write(os.path.join(d, "report.txt"))
    with open(os.path.join(d, "answers.txt"), "w") as fh:
        # `20` IS DERIVED AND NOT DEFAULTED, and what it is derived FROM has
        # moved with the rung. JEP 441 was preview in 17 through 20 and
        # standard in 21, so the pattern switch is the one construct in this
        # rung's work that -source 20 rejects; everything step 09 put in --
        # sealed types, text blocks, pattern `instanceof`, arrow switches --
        # is standard at 20 and leaves the control at `yes` on its own. So a
        # fixture answers `no` at -source 20 exactly when its report declares
        # a pattern switch, which is every tree here except `pinned` and the
        # handful of C-3 rows that took the switches back out. JEP 431's
        # sequenced methods are a LIBRARY change and do not move the control
        # at all: `getFirst()` compiles at -source 20 against a 21 class
        # library, which is why C-4 has no control of its own and why the
        # control cannot be read as a proxy for the whole rung.
        auto20 = "no" if rep.recs("swpat") else "yes"
        for tag, dflt in (("21", "yes"), ("20", auto20), ("def", "yes")):
            fh.write("ok%s=%s\n" % (tag, env.get("ok" + tag, dflt)))
        fh.write("errors=0\n")
    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        # The differ ids the fixture's own catalogue row asked for, all four
        # populations in one file because the tree only ever produces one.
        # M5, M6, L8 and N8 are the rows that use it: trees whose source shape
        # is perfect and whose recorded behaviour is not, which is the pairing
        # the run-time provisos exist for.
        for i, sid in enumerate(svcbroken_of(env) + tbbroken_of(env)
                                + swbroken_of(env)):
            fh.write("behavior.differ[%d]=%s\n" % (i, sid))
    with open(os.path.join(d, "why.txt"), "w") as fh:
        fh.write(why + "\n")
        fh.write("\nwhat this fixture moves: " + note + "\n")
    write_jars(d, env.get("jars", B12_FLOORS), env.get("mods", GOLDMODS))
    # A FOURTH ARTEFACT, back after a rung away. The rehearsal copies this into
    # ${WORK} before it runs the blocks, the way the verifier copies the one
    # arqgate.Scan writes; a fixture with no scan.txt is a tree whose build
    # produced no class files to read, which is L2.
    write_scan(d, env.get("scan", GOLD_SCAN))
    return d


_UNSET = object()


def score(rep, env, differ=_UNSET):
    """Every gate the nine lifted blocks publish, for one fixture.

    `differ` is the whole `behavior.differ[]` list -- one argument and not
    four, because there is one behavior.summary. `None` is the file being
    absent or unreadable, which is ONE event with FOUR consequences at this
    rung: B-14.5's exchange scaling, B-15.3's service scaling, B-16.3's text
    block scaling and C-3's pattern-switch scaling all read zero off the same
    missing file. Step 08's generator carried two of these as separate
    arguments and its own note records what that cost: a GONE row printed
    twice, each copy reporting its own gate at zero and the other at 1.000000,
    describing a tree that cannot exist.

    `_UNSET` and not a falsy default, for the reason that note also gives:
    `None` means unreadable and `()` means intact, and `differ or ...` reads
    both as "the caller said nothing".
    """
    measured = env.get("ok21", "yes") == "yes" or env.get("okdef", "yes") == "yes"
    jars = env.get("jars", B12_FLOORS)
    mods = env.get("mods", GOLDMODS)
    scan = env.get("scan", GOLD_SCAN)
    if differ is _UNSET:
        differ = svcbroken_of(env) + tbbroken_of(env) + swbroken_of(env)
    if measured:
        b10, _ = b10_score(rep)
        b11, _ = b11_score(rep)
        b10s, b11s = "%.6f" % b10, "%.6f" % b11
    else:
        b10s = b11s = "0"
    b12s, b12p = b12_score(rep, None if mods is None else jars, measured)
    b13s, b13p = b13_score(rep, measured)
    # B-14's helper is inherited byte for byte and takes its list PRE-FILTERED;
    # B-15's, B-16's and family C's filter inside. The asymmetry is deliberate
    # -- the inherited helper is not edited at a rung that did not change its
    # gate -- so the split happens here, once, against the same four baseline
    # lists the shell greps.
    http = None if differ is None else [x for x in differ if x in C5_HTTPIDS]
    b14s, b14p, gb = b14_score(rep, measured, http)
    b15s, b15p, g15 = b15_score(rep, mods, measured, differ)
    b16s, b16p, g16 = b16_score(rep, measured, differ)
    cs, cp, gc = fc_score(rep, scan, measured, differ)
    g = dict(gb)
    g.update(g15)
    g.update(g16)
    g.update(gc)
    return dict(b10=b10s, b11=b11s, b12=b12s, b12p=b12p, b13=b13s, b13p=b13p,
                b14=b14s, b14p=b14p, b15=b15s, b15p=b15p, b16=b16s, b16p=b16p,
                c=cs, cp=cp, gates=g)


def check_pinned():
    """The pinned fixture is the input tree's own report, byte for byte.

    Not a resemblance check. `pinned` is written by copying every pair out of
    baseline-step10-source.txt and writing them back, so if Rep's round trip
    ever stopped being the identity -- a dropped blank line, a renumbered
    record, a key reordered -- this catches it here rather than three fixtures
    downstream where the number would look like a finding. Five new record
    kinds arrive at this rung and all five go through that round trip.
    """
    p = os.path.join(OUT, "pinned", "report.txt")
    with open(p) as a, open(S10) as b:
        got, want = a.read(), b.read()
    assert got == want, "pinned/report.txt is not baseline-step10-source.txt"
    g = os.path.join(OUT, "golden", "report.txt")
    with open(g) as a, open(G10) as b:
        assert a.read() == b.read(), "golden/report.txt is not the golden's report"


# --- generate -----------------------------------------------------------------
def main():
    # Wipe and rebuild. The generator is the only author of this directory, so
    # a fixture that used to be in the catalogue and no longer is has to leave
    # the tree with it: c-rehearsal.sh iterates the DIRECTORY, not the table
    # below, and a step-08 fixture left behind here would be scored by step
    # 09's blocks against step 08's report and read as a finding.
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    rows = []

    for name, src, why, env in (
            ("pinned", BASE, "the input tree's own measured report, its twelve "
             "already-named jars and a class-file scan that reads 61 from end "
             "to end. Family C must read 0.000000 on it and every family-B "
             "gate 1.000000 -- INCLUDING B-15 AND B-16, both of which the tree "
             "arrives holding, because step 09's solution is what step 10 is "
             "handed: that is migration_prompt item 6's nop run, and the "
             "reason this rung's floor is argued in the NOTES item-6 entry "
             "rather than assumed.",
             {"scan": SEED_SCAN}),
            ("golden", GOLD, "the golden tree's own measured report, its jars "
             "and a scan that reads 65 on every one of the 746 class files. "
             "Every family-C leg full, every family-B gate held: item 6's "
             "oracle run.", {})):
        rep = src.clone()
        write_fixture(name, rep, why, env, "nothing -- this is the report as "
                      "the source gate wrote it")
        rows.append((name, score(rep, env), "-"))

    check_pinned()

    for name, base, why, fn, env in CAT:
        rep = (BASE if base == "pinned" else GOLD).clone()
        note = fn(rep, env)
        write_fixture(name, rep, why, env, note)
        rows.append((name, score(rep, env), note))

    # --- the table, for transcription into c-rehearsal.sh by hand
    # THIRTEEN COLUMNS, and the eight family-B ones are not padding. The
    # ninth trap is that a step-k requirement can quietly make a step-k-2 test
    # unpassable, and the only place that is visible is a row where the column
    # that must NOT move has moved. Family C is down to four legs at this rung
    # because step 09's five came across whole as B-16.
    hdr = ("%-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s | %-8s %-8s %-8s %-8s | %-8s")
    print(hdr % ("fixture", "B10", "B11", "B12", "B13", "B14", "B15", "B16",
                 "C1", "C2", "C3", "C4", "familyC"))
    print("-" * 130)
    for name, st, note in rows:
        c = [cell(x) for x in st["cp"]]
        print(hdr % (name, st["b10"], st["b11"], st["b12"], st["b13"],
                     st["b14"], st["b15"], st["b16"], c[0], c[1], c[2], c[3],
                     st["c"]))
    print()
    print("B-12 parts (a b c d e), B-13 (2..7), B-14 (2..5), B-15 (2 3), "
          "B-16 (2..5):")
    for name, st, note in rows:
        print("  %-8s b12: %s" % (name, " ".join(cell(x) for x in st["b12p"])))
        print("  %-8s b13: %s" % ("", " ".join(cell(x) for x in st["b13p"])))
        print("  %-8s b14: %s" % ("", " ".join(cell(x) for x in st["b14p"])))
        print("  %-8s b15: %s" % ("", " ".join(cell(x) for x in st["b15p"])))
        print("  %-8s b16: %s" % ("", " ".join(cell(x) for x in st["b16p"])))

    # --- the doctored-summary runs, which are not source-tree fixtures
    #
    # c-rehearsal.sh builds these by copying golden/ and appending to nothing
    # but behavior.summary, so the report is the golden's and only the run-time
    # factors move. Their `want` figures are computed here for the same reason
    # every other want is: so the rehearsal asserts a number this file worked
    # out rather than one the scorer handed it.
    #
    # FOUR SETS OF THEM AT THIS RUNG, because four provisos now scale on one
    # file: B-14.5's eighteen http ids, B-15.3's 69 service ids, B-16.3's five
    # text-block ids and C-3's four pattern-switch ids. The four tables are
    # the assertion that the four greps read their own list -- a scorer that
    # grepped the whole differ list would show up as a moved cell in the three
    # columns the run never touched.
    print()
    print("BEHAV* (golden's report, doctored behavior.summary) -- B-14.5's ids:")
    print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
          ("run", "exec.want", "broken", "red.hon", "B14.5", "B14", "B15", "familyC"))
    for name, broken in (
            ("BEHAVALL", C5_HTTPIDS),
            ("BEHAVHALF", C5_HTTPIDS[:9]),
            ("BEHAVRED", [C5_REDID]),
            ("BEHAVONE", [i for i in C5_HTTPIDS if i != C5_REDID][:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
              (name, g["b14.c5.exec.want"], g["b14.c5.exec.broken"],
               g["b14.c5.redirects.honoured"], cell(st["b14p"][3]), st["b14"],
               st["b15"], st["c"]))
        print("           ids: %s" %
              (" ".join(broken) if len(broken) <= 2
               else "%s ... %s (%d)" % (broken[0], broken[-1], len(broken))))

    print()
    print("SVC* (golden's report, doctored behavior.summary) -- B-15.3's 69 ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
          ("run", "exec.want", "broken", "B15.3", "B15", "familyC", "B14"))
    for name, broken in (
            ("SVCALL", SVCIDS),
            ("SVCHALF", SVCIDS[:35]),
            ("SVCONE", SVCIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
              (name, g["b15.exec.want"], g["b15.exec.broken"],
               cell(st["b15p"][1]), st["b15"], st["c"], st["b14"]))

    print()
    print("TB* (golden's report, doctored behavior.summary) -- B-16.3's five ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
          ("run", "exec.want", "broken", "B16.3", "B16", "familyC", "B15"))
    for name, broken in (
            ("TBALL", TBIDS),
            ("TBTWO", TBIDS[:2]),
            ("TBONE", TBIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
              (name, g["b16.3.exec.want"], g["b16.3.exec.broken"],
               cell(st["b16p"][1]), st["b16"], st["c"], st["b15"]))

    # --- SW*: this rung's OWN behaviour scaling
    #
    # FIVE IDS AND FIVE TEXT-BLOCK IDS, and a different file from TB*'s. The
    # two lists are now the same LENGTH, so the fractions they produce are
    # identical -- TBTWO and SWTWO both read 0.600000, TBONE and SWONE both
    # 0.800000 -- and arithmetic no longer separates the two legs at all.
    # What separates them is the pair of tables read together: break the
    # switch ids and B-16 must not move; break the text block ids and familyC
    # must not move. A scorer that gave one list to both legs would move both
    # columns in both triples, which is the only place it is visible.
    print()
    print("SW* (golden's report, doctored behavior.summary) -- C-3's five ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
          ("run", "exec.want", "broken", "C3", "familyC", "B16", "B15"))
    for name, broken in (
            ("SWALL", SWIDS),
            ("SWTWO", SWIDS[:2]),
            ("SWONE", SWIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
              (name, g["c3.exec.want"], g["c3.exec.broken"],
               cell(st["cp"][2]), st["c"], st["b16"], st["b15"]))

    # --- GONE: one absence, three consequences
    #
    # behavior.summary is not B-14's file, or B-15's, or B-16's, or C-3's. It
    # is ONE file that four provisos read against four different id lists, and
    # deleting it is one event with four results. Step 08 printed its
    # two-family version of this twice before its own note caught it, each
    # copy reporting one gate at zero and the other at 1.000000 -- a tree that
    # cannot exist. So it is one run, printed once, and c-rehearsal.sh asserts
    # all four quarters of it off the same deletion.
    print()
    print("GONE (golden's report, no behavior.summary at all) -- four provisos, one absence:")
    st = score(GOLD.clone(), {}, None)
    g = st["gates"]
    print("  B-14.5  want=%-4s broken=%-4s red.honoured=%-4s  B14.5=%-9s B14=%s" %
          (g["b14.c5.exec.want"], g["b14.c5.exec.broken"],
           g["b14.c5.redirects.honoured"], cell(st["b14p"][3]), st["b14"]))
    print("  B-15.3  want=%-4s broken=%-4s exec=%-13s  B15.3=%-9s B15=%s" %
          (g["b15.exec.want"], g["b15.exec.broken"], g["b15.exec"],
           cell(st["b15p"][1]), st["b15"]))
    print("  B-16.3  want=%-4s broken=%-4s exec=%-13s  B16.3=%-9s B16=%s" %
          (g["b16.3.exec.want"], g["b16.3.exec.broken"], g["b16.3.exec"],
           cell(st["b16p"][1]), st["b16"]))
    print("  C-3     want=%-4s broken=%-4s exec=%-13s  C3=%-12s familyC=%s" %
          (g["c3.exec.want"], g["c3.exec.broken"], g["c3.exec"],
           cell(st["cp"][2]), st["c"]))

    print()
    print("gate facts, per fixture:")
    for name, st, note in rows:
        print("  %s  (%s)" % (name, note))
        g = st["gates"]
        for k in sorted(g):
            print("      gate.%s=%s" % (k, g[k]))


if __name__ == "__main__":
    main()
