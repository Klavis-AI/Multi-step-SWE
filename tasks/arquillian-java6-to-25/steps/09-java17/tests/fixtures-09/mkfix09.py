#!/usr/bin/env python3
"""Build step 09's rehearsal fixtures for family C, B-10, B-11, B-12, B-13,
B-14 and B-15, and compute what each one must score.

    python3 authoring/fixtures-step09/mkfix09.py

Writes one directory per fixture under fixtures-step09/c/, each holding the
source report the shipped scoring blocks will read, the compile answers they
would have got from javac, a behaviour summary, the `jars/` directory B-12e
unzips and arqgate.Modules reads, and the `scan.txt` C-1 reads. Prints the
score table that c-rehearsal.sh asserts.

EIGHT blocks, not seven. Step 08 rehearsed the source-AST block, B-10, B-11,
B-12, B-13, B-14 and family C; this step adds B-15, which is step 08's own
family C minus its C-1, renormalised over the 0.80 that is left. All eight are
extracted from the shipped test.sh and all eight are scored here.

THE SCAN IS BACK AND THE JARS NEVER LEFT. Step 08's family C read the
published jars and no class-file level; step 07's read ${WORK}/scan.txt and
step 08 dropped it. Here both channels are live at once and they are read by
different families: C-1 reads scan.txt for the class-file major, B-15.2 reads
the jars for the module names, and B-12e reads the same jars for step 05's
four package names. A fixture can move any one of the three and the other two
have to stay where they were, which is what the columns that do NOT move in
each row are asserting.

THE TWO ENDS ARE MEASURED REPORTS AND NOT SYNTHESISED ONES, as they have been
since step 07's generator:

    pinned   baseline-step09-source.txt, the input tree's own measured report,
             which is also what the verifier stages as baseline/source.txt
    golden   baseline-step09-golden.txt, the golden tree's own measured report,
             /work/scratch/s09/src-gold.txt as the gate wrote it

Every fixture below is one of those two with an EXPLICIT, NAMED mutation
applied -- a record moved, a counter bumped, a bucket reverted. Nothing is
invented from whole cloth. That buys two things a feature-dict generator could
not:

  * a fixture claiming "nine of the eleven hierarchies were sealed" deletes two
    of the golden's OWN `sealed` records, at their real line numbers with their
    real permits lists, because it takes them out of the report the migrated
    tree actually produced
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

S09 = os.path.join(AUTH, "baseline-step09-source.txt")
G09 = os.path.join(AUTH, "baseline-step09-golden.txt")


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


def group(bodies):
    d = {}
    for b in bodies:
        k = bucket_of(b)
        if k is not None:
            d.setdefault(k, []).append(b)
    return d


# --- the frozen numbers, read rather than retyped -----------------------------
BASE = Rep(S09)
GOLD = Rep(G09)
TGT = read_env(os.path.join(AUTH, "baseline-step09-target.txt"))
LEVEL = read_env(os.path.join(AUTH, "baseline-step09-level.txt"))


def T(k):
    return int(TGT[k])


# WANT_MAJOR IS SPENT AGAIN AT THIS RUNG, which is the difference from step 08.
# There the level was held and family C asked about manifests; here C-1 asks
# how much of the tree reached 61 and B-4 asks that whatever it reached is one
# major and not below 55. Both numbers live in baseline/level.txt -- `major`
# for C-1 and `was` for B-4 -- and neither gate reads a literal.
WANT_MAJOR = int(LEVEL["major"])                    # 61, the must-flip
HOLD_MAJOR = int(LEVEL["was"])                      # 55, B-4's floor
BASE_SUP = BASE.num("suppress.sites")               # 48
BASE_RAW = BASE.num("newgeneric.raw")               # 0
# What arqgate.Scan counts over the published jars and the class files the
# build left in the tree, measured on step 08's blind run: gate.classes=746.
# The step-09 golden publishes the same twelve artifacts with the same class
# files -- a pattern variable, a sealed modifier, a text block and an arrow
# create no types -- so the count is the same at both ends and only the MAJOR
# moves. That is exactly what makes C-1 a fraction worth having.
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

# --- and family C's own five, which are this rung's and nobody else's --------
#
# Each is read as the SHELL reads it -- a tag column and a bucket or file key --
# so that a population file whose shape drifted breaks here rather than scoring
# a tree wrongly. ipattern.txt and strnl.txt carry counts per bucket because
# two of this rung's buckets hold more than one site; swarrow.txt carries
# counts per FILE because a switch has no enclosing-method key; sealed.txt
# carries three kinds of row and only two of them are scored.
IP_CONV = {}
IP_SKIP = {}
for _l in read_set(os.path.join(AUTH, "baseline-step09-ipattern.txt")):
    _t, _b, _n = _l.split()
    (IP_CONV if _t == "conv" else IP_SKIP)[_b] = int(_n)
IP_CONV_W = sum(IP_CONV.values())                   # 21
IP_SKIP_W = sum(IP_SKIP.values())                   # 7
IP_TOTAL = T("target.ip.total")                     # 28

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

# Family C's five, against the target file the scorer divides by. Every one of
# these five is a number the shipped test.sh reads out of target.txt or counts
# out of the population file itself, and both spellings have to agree: a
# population file and a target key that disagreed would score a correct tree
# below 1.0 and no other check in this image would notice.
assert len(IP_CONV) == T("target.ip.buckets"), "ipattern conv buckets"
assert IP_CONV_W == T("target.ip.sites"), "ipattern conv sites"
assert IP_SKIP_W == T("target.ip.skip"), "ipattern skip sites"
assert IP_CONV_W + IP_SKIP_W == IP_TOTAL, "the two halves are not the 28"
assert not (set(IP_CONV) & set(IP_SKIP)), "a bucket is both converted and held"
assert TB_MOVE_W == T("target.tb.move.buckets")
assert sum(TB_MOVE.values()) == T("target.tb.move.sites")
assert len(TB_HOLD) == T("target.tb.hold.buckets")
assert TB_HOLD_W == T("target.tb.hold.sites")
assert not (set(TB_MOVE) & set(TB_HOLD)), "a bucket both moves and holds"
assert len(TBIDS) == T("target.tb.ids") and len(set(TBIDS)) == len(TBIDS)
assert len(SEAL_ROOT) == T("target.sealed.roots")
assert len(SEAL_OPEN) == T("target.sealed.open")
assert sum(len(p) for _, _, p in SEAL_ROOT) == T("target.sealed.permits")
# 40 is roots plus leaves and NOT roots plus permits entries: six of the 35
# permitted names are themselves roots, because the event vocabulary nests --
# ContainerControlEvent permits DeploymentEvent, which permits two of its own.
# A check that added 11 and 35 would read 46 and quietly accept a population
# file whose nesting had been flattened.
assert len(SEAL_ROOT) + len(SEAL_LEAF) == T("target.sealed.types"), \
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
assert len(SW_WANT) == T("target.sw.files")
assert sum(SW_WANT.values()) == T("target.sw.total")
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
# exactly what item 6's floor has to be argued around, and NOTES 15.3 does.
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


def strip_pkg(name):
    """`org.jboss.Foo.Bar` -> `Foo.Bar`, the scorer's own loop.

    Leading LOWERCASE-initial segments only: a permits clause may name a nested
    type, and `Service.Provider` must not lose its outer name. Nothing in the
    baseline is spelled qualified, but SourceScan reports what the source says.
    """
    while True:
        m = re.match(r"^[a-z][A-Za-z0-9_]*\.", name)
        if not m:
            return name
        name = name[m.end():]


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


def fc_score(rep, scan, measured, execbroken=()):
    """Family C: Java 17 itself. Five legs, 0.10/0.20/0.26/0.36/0.08.

    `scan` is the fixture's arqgate.Scan reading -- a dict of the two keys C-1
    spends, or None for a tree that produced no scan at all. It is a SEPARATE
    channel from `rep`: the class files and the source AST are read by
    different jars from different inputs, and L2 below is the fixture that
    moves one without the other.

    Returns (familyC, [C1..C5], gate facts).

    THREE SPELLINGS OF ZERO AND ALL THREE ARE MODELLED. C-1 is `0` when there
    is no scan, because the shell assigns `c1=0` and the else branch only
    emits a fact. C-2, C-4 and C-5 are `0` when nothing compiled, for the same
    reason. C-3 is `0.000000` even then, because its final assignment sits
    OUTSIDE the measured guard -- it multiplies by the execution scaling, which
    is read from the behaviour summary and not from the report. Normalising the
    three into one would make the rehearsal's transcription disagree with the
    scorer on exactly the fixtures where a disagreement is hardest to read.
    """
    g = {}

    # ---- C-1: the class-file major, over the class files there are ----------
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

    # ---- C-2: pattern instanceof, three fractions through a min() -----------
    if measured:
        bound = cbuckets(rep, "ipattern", "kind=bound")
        plain = cbuckets(rep, "ipattern", "kind=plain")
        conv_n = capsum(IP_CONV, bound)
        hold_n = capsum(IP_SKIP, plain)
        size = rep.num("ipattern.size")
        g["c2.converted"] = conv_n
        g["c2.converted.want"] = IP_CONV_W
        g["c2.held"] = hold_n
        g["c2.held.want"] = IP_SKIP_W
        g["c2.sites"] = size
        g["c2.sites.want"] = IP_TOTAL
        c2conv = clamp01(frac(conv_n, IP_CONV_W))
        c2hold = clamp01(frac(hold_n, IP_SKIP_W))
        # THE TOTAL-SITE TERM IS WHAT STOPS DELETION FROM SCORING. Without it a
        # tree that deleted the seven sites with no cast after them would read
        # the hold half at 7/7 by vacuum -- capsum caps at the want and reads 0
        # for a bucket that is gone, so the hold half already catches that one;
        # what this term catches is the reverse, a tree that converted all 28
        # including the seven that must not move. L4 and L5 are the two rows.
        c2tot = clamp01(frac(size, IP_TOTAL))
        g["c2.convfrac"] = "%.6f" % c2conv
        g["c2.holdfrac"] = "%.6f" % c2hold
        g["c2.totfrac"] = "%.6f" % c2tot
        c2 = minf(minf(c2conv, c2hold), c2tot)
        c2s = "%.6f" % c2
    else:
        c2 = 0.0
        c2s = "0"

    # ---- C-3: one text block, and the XML builders left alone ---------------
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
        g["c3.moved"] = move_n
        g["c3.moved.want"] = move_w
        g["c3.held"] = hold_n
        g["c3.held.want"] = TB_HOLD_W
        g["c3.textblocks"] = rep.num("textblock.size")
        c3move = clamp01(frac(move_n, move_w))
        c3hold = clamp01(frac(hold_n, TB_HOLD_W))
        g["c3.movefrac"] = "%.6f" % c3move
        g["c3.holdfrac"] = "%.6f" % c3hold
    else:
        # The shell LITERAL `0`, the same way b15.departed/arrived/held are
        # spelled above. c3move and c3hold are initialised `0` at test.sh:4345
        # and published at :4383 -- above the guard and below it -- so a tree
        # nobody could compile publishes the initialiser and not a computed
        # zero. Formatting these two with %.6f unconditionally claimed a
        # measurement had been taken on K17, which is the one fixture where no
        # measurement was possible.
        #
        # C-3's TOTAL is different and stays %.6f on every fixture: it is
        # assigned below the guard and multiplies by the execution scaling,
        # which is read from the behaviour summary rather than from the report.
        # Two facts of one leg, two spellings of zero, and the difference is
        # which side of the guard the assignment sits on.
        c3move = c3hold = 0.0
        g["c3.movefrac"] = "0"
        g["c3.holdfrac"] = "0"

    if execbroken is None:
        c3exec, c3bad = 0.0, len(TBIDS)
    else:
        c3bad = len([x for x in execbroken if x in TBIDS])
        c3exec = clamp01(frac(len(TBIDS) - c3bad, len(TBIDS)))
    g["c3.exec.want"] = len(TBIDS)
    g["c3.exec.broken"] = c3bad
    g["c3.exec"] = "%.6f" % c3exec
    c3 = f6(min(c3move, c3hold) * c3exec)
    c3s = "%.6f" % c3

    # ---- C-4: eleven hierarchies closed, eleven types left open -------------
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
        g["c4.decl"] = rep.num("sealed.decl")
        g["c4.nonsealed"] = rep.num("sealed.nonsealed")
        g["c4.permits"] = rep.num("sealed.permits")
        g["c4.roots"] = c4roots
        g["c4.roots.want"] = len(SEAL_ROOT)
        g["c4.open"] = c4open
        g["c4.open.want"] = len(SEAL_OPEN)
        c4a = clamp01(frac(c4roots, len(SEAL_ROOT)))
        c4b = clamp01(frac(c4open, len(SEAL_OPEN)))
        g["c4.closedfrac"] = "%.6f" % c4a
        g["c4.openfrac"] = "%.6f" % c4b
        c4 = minf(c4a, c4b)
        c4s = "%.6f" % c4
    else:
        c4 = 0.0
        c4s = "0"

    # ---- C-5: two colon-form switches become arrow form ---------------------
    if measured:
        arrow = fcount(rep, "switch", "form=arrow")
        colon = fcount(rep, "switch", "form=colon")
        # PER FILE AND BOTH CONDITIONS: enough arrows AND no colon left. A file
        # that gained an arrow switch beside the colon one it already had has
        # done nothing, and L13 is the row that says so.
        c5hit = len([f for f, n in SW_WANT.items()
                     if arrow.get(f, 0) >= n and colon.get(f, 0) == 0])
        g["c5.files"] = c5hit
        g["c5.files.want"] = len(SW_WANT)
        g["c5.arrow"] = rep.num("switch.arrow")
        g["c5.colon"] = rep.num("switch.colon")
        g["c5.expr"] = rep.num("switch.expr")
        g["c5.size"] = rep.num("switch.size")
        c5 = clamp01(frac(c5hit, len(SW_WANT)))
        c5s = "%.6f" % c5
    else:
        c5 = 0.0
        c5s = "0"

    tot = f6(0.10 * c1 + 0.20 * c2 + 0.26 * c3 + 0.36 * c4 + 0.08 * c5)
    return "%.6f" % tot, [c1s, c2s, c3s, c4s, c5s], g
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


# --- this rung's own mutation helpers -----------------------------------------
# Five record kinds arrive at step 09 and none of the helpers above know about
# them. Each of the five gets a resync of its own, for the reason the block
# above gives: `sealed.decl=11` beside nine sealed records is not a report any
# compiler plugin writes, and a fixture that disagreed with itself would be
# telling the rehearsal about the fixture rather than about the scorer.
#
# The scorer reads the RECORDS and not these scalars for four of the five legs
# -- C-2 counts kind=bound records, C-4 compares permits lists, C-5 counts
# form=arrow records per file -- so keeping the scalars honest is about the
# facts the trajectory carries and about the two places a scalar IS read:
# C-2's total-site term reads `ipattern.size`, and gate.c3.textblocks reads
# `textblock.size`.
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
# Thirty-nine fixtures and the two ends. Each names the ONE thing it moves, and
# the table at the foot of this file is what c-rehearsal.sh asserts. Read the
# `why` text as the fixture's claim and the row as what the shipped blocks must
# say about it.
#
# THREE POPULATIONS OF FIXTURE AND ONE TABLE.
#
#   K1..K3, K19..K20, M1..M6   move the PUBLISHED JARS and the service-lookup
#                              records -- step 08's family C, inherited whole
#                              as B-15
#   K4..K18                    move the namespace, toArray and HttpClient
#                              records -- step 07's family C, inherited as B-14
#   L1..L13                    move THIS rung's five legs: the class-file scan,
#                              the pattern records, the text block, the sealed
#                              hierarchies and the switches
#
# They share a table because they share a run: every fixture is scored by all
# EIGHT blocks, and the column that must NOT move is as much of the assertion
# as the one that must. A family C that noticed a module name, or a B-15 that
# noticed a sealed declaration, would show up here as a moved cell in a column
# the fixture never touched.
#
# THE INHERITANCE IS NOW TWO RUNGS DEEP AND THAT IS THE POINT OF KEEPING BOTH
# SETS. B-14 is step 07's family C and B-15 is step 08's, and the ninth trap is
# about exactly this: a step-k requirement that quietly makes a step-k-2 test
# unpassable is invisible from inside a step-local mean. The only way to see it
# is to score every inherited gate on every fixture, including the fixtures
# that were written for the rung two below.
#
# FIXTURES THAT SHARE A SCORE AND ARE NOT REDUNDANT. L12 and L13 both read
# familyC at the same number, and so do several of the C-2 rows. They differ in
# the GATE FACT that says why -- `gate.c5.arrow=1` against `gate.c5.arrow=2`,
# `gate.c2.convfrac` against `gate.c2.holdfrac` -- and a block that read one
# channel wrongly would collapse them onto one message while keeping the score.
# The rehearsal asserts the messages as well as the numbers, which is the only
# way that particular defect is visible from outside.
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
         "literal 0 their blocks assign; B-12e, B-12, B-13, B-14, B-15 and "
         "familyC publish 0.000000, because their totals are computed by awk "
         "outside the guard; and B-13.2..7 and B-14.2..5 publish NOTHING AT "
         "ALL, because their fact lines are inside it. "
         "THREE SPELLINGS OF ZERO IN ONE ROW, which is what makes this fixture "
         "worth more at this rung than at either of the last two. C-1 is FULL "
         "-- 1.000000 -- because it reads ${WORK}/scan.txt, and the class "
         "files this build left behind are all at major 61 whatever the "
         "scorer's own three probes made of the sources. C-2, C-4 and C-5 are "
         "the shell LITERAL 0, assigned above their guards. C-3 is 0.000000, "
         "because its move and hold halves are the literal 0 and its final "
         "assignment then multiplies them by the execution scaling OUTSIDE the "
         "guard. Family C is 0.10 x 1 = 0.100000 exactly. B-15 is the same "
         "shape one family over: B-15.2 reads the twelve published manifests "
         "and is full, B-15.3's three halves are the literal 0, and the "
         "composite is 0.35 / 0.80 = 0.437500. A rung whose gates all sat "
         "inside one guard could not produce either number.",
         ok17="no", ok16="no", okdef="no")
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


# --- family C's own thirteen --------------------------------------------------
# Five legs and thirteen ways of getting one of them wrong. Every row leaves
# the other four at the golden, so the table reads as a diagonal: the cell that
# moved is the leg the fixture is about, and every other cell in the row is an
# assertion that the leg did not notice.
#
# TWO OF THE THIRTEEN MOVE A CHANNEL AND NOT A RECORD. L1 and L2 move
# ${WORK}/scan.txt, which no source mutation can reach, and that is the
# separation this rung recovered from step 07 and step 08 could not express.
IP_CONV_BUCKETS = sorted(IP_CONV)
IP_SKIP_BUCKETS = sorted(IP_SKIP)
TB_MOVE_BUCKET = sorted(TB_MOVE)[0]
TB_HOLD_BUCKETS = sorted(TB_HOLD)
SW_FILES = sorted(SW_WANT)


@fixture("L1", "golden", "the migration is complete in every source respect "
         "and 700 of the 746 class files are at major 61 -- the build file of "
         "one subproject was never moved, so its classes are still at 55. C-1 "
         "is 700/746 = 0.938338 and nothing else in the row moves. The leg is "
         "a FRACTION and not a flag for exactly this: a tree that moved "
         "fifteen of sixteen build files has done most of the work and the "
         "gate should say most.",
         scan=scan_split(700))
def _l1(rep, env):
    return "700 of 746 class files at major %d, the rest still at %d" % (
        WANT_MAJOR, HOLD_MAJOR)


@fixture("L2", "golden", "no class-file scan at all: the build produced no "
         "class output the gate could walk. C-1 is the shell LITERAL 0 under "
         "gate.c1=no-scan, and every other leg of family C is full because "
         "they read the source AST, which is intact. familyC = 0.900000. THE "
         "COMPANION TO K19 AND K20 FROM THE OTHER SIDE: those two move the "
         "jars and leave the scan, this one moves the scan and leaves the "
         "jars, and B-15.2 stays at 1.000000 in all three.",
         scan=None)
def _l2(rep, env):
    return "no scan.txt; the source report and the published jars intact"


@fixture("L3", "golden", "nine of the eighteen convertible buckets are still "
         "a plain `instanceof` with a cast on the next line -- twelve of the "
         "twenty-one sites, because three of those buckets hold two each. The "
         "converted half is 9/21 = 0.428571, the held half and the total-site "
         "term are both full, and the min() takes the converted half. A "
         "half-finished sweep, which is what a file-at-a-time migration looks "
         "like when it runs out of context.")
def _l3(rep, env):
    n = unbind(rep, IP_CONV_BUCKETS[:9])
    return "%d site(s) in 9 bucket(s) back to a plain instanceof" % n


@fixture("L4", "golden", "every one of the twenty-eight sites is a pattern, "
         "including the seven that have no cast after them. The converted half "
         "is full and the HELD half is 0/7, so C-2 is 0.000000: a tree that "
         "bound a variable at every `instanceof` it could find has changed "
         "seven sites that had nothing to gain and reads worse than the tree "
         "it started from. This is the fixture that makes C-2 a min() of three "
         "and not a count of one.")
def _l4(rep, env):
    n = bind_all(rep)
    return "%d held site(s) bound as well; ipattern.plain=0" % n


@fixture("L5", "golden", "three of the seven held sites were DELETED rather "
         "than left alone -- the `instanceof` and its branch removed on the "
         "grounds that nothing needed converting there. The held half is 4/7 = "
         "0.571429 and the total-site term is 25/28 = 0.892857, and the min() "
         "takes the held half. Two terms fall from one edit and that is not "
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
         "would pay a tree that deleted the page. C-3 is 0.000000.")
def _l6(rep, env):
    n = drop_textblocks(rep)
    return "%d text block(s) removed; the literals are still gone" % n


@fixture("L7", "golden", "the text block arrived and the XML fragment builder "
         "was flattened with it: ToolingDeploymentFormatter#format lost three "
         "of its three newline literals. The held half is 4/7 = 0.571429 and "
         "the move half is full, so C-3 is 0.571429. The hold half is what "
         "makes C-3 a migration gate rather than a census of text blocks: "
         "these two methods build an XML fragment a line at a time and a text "
         "block would change the bytes they emit.")
def _l7(rep, env):
    n = drop_strnl(rep, TB_HOLD_BUCKETS[0])
    return "%d newline literal(s) gone from %s" % (n, TB_HOLD_BUCKETS[0].split("/")[-1])


@fixture("L8", "golden", "the source shape is perfect and two of the five "
         "recorded document behaviours differ: the text block is there, the "
         "XML builders are untouched, and the page the runner serves is not "
         "the page it served. C-3 is scaled by 3/5 = 0.600000. A text block "
         "with an incidental-whitespace mistake produces exactly this row, and "
         "no source-shape term can see it.",
         tbbroken=2)
def _l8(rep, env):
    return "nothing in the report; 2 of 5 recorded document behaviours differ"


@fixture("L9", "golden", "six of the eleven event hierarchies are sealed and "
         "five are not -- the sweep stopped at the package boundary. The "
         "closed half is 6/11 = 0.545455, the open half is full, and the min() "
         "takes the closed half.")
def _l9(rep, env):
    names = [n for _, n, _ in SEAL_ROOT][:5]
    n = unseal(rep, names)
    return "%d root(s) no longer sealed: %s" % (n, ", ".join(names))


@fixture("L10", "golden", "all twenty-two candidates are sealed: the eleven "
         "event roots AND the eleven types this library is extended through -- "
         "the resource provider interface, the context and descriptor bases, "
         "the tests' own service doubles, and the two event roots javac itself "
         "refuses to seal. The closed half is full and the OPEN half is 0/11, "
         "so C-4 is 0.000000. NOTHING ELSE IN THIS IMAGE CAN SEE THIS "
         "MISTAKE: nothing in the tree subclasses any of the eleven from "
         "outside, so family A replays 88 transcripts and finds every one of "
         "them identical. The open half is the only witness.")
def _l10(rep, env):
    n = seal_open(rep, SEAL_OPEN)
    return "%d extension point(s) closed as well; sealed.decl=%d" % (
        n, rep.num("sealed.decl"))


@fixture("L11", "golden", "eleven roots are sealed and one permits clause is a "
         "name short: ContainerControlEvent names three of its four direct "
         "subtypes. The closed half is 10/11 = 0.909091. The comparison is a "
         "TRIPLE -- file, name and the sorted permits list -- and this is the "
         "row that says so: a gate that asked only 'is this type sealed' would "
         "score this tree with the golden, and the tree does not compile.")
def _l11(rep, env):
    gone = clip_permits(rep, SEAL_ROOT[0][1])
    return "%s no longer permits %s" % (SEAL_ROOT[0][1], gone)


@fixture("L12", "golden", "one of the two switches is arrow form and the other "
         "is still colon form, fall-through and all. C-5 is 1/2 = 0.500000. "
         "gate.c5.arrow=1 and gate.c5.colon=1 are the facts that distinguish "
         "this row from L13, which scores the same number for the opposite "
         "reason.")
def _l12(rep, env):
    n = uncurl(rep, SW_FILES[0])
    return "%d switch(es) in %s back to colon form" % (n, SW_FILES[0].split("/")[-1])


@fixture("L13", "golden", "one file's colon-form switch is untouched and a NEW "
         "arrow-form switch was added beside it. C-5 is 1/2 = 0.500000, the "
         "same as L12, and gate.c5.arrow=2 against L12's 1 is the difference. "
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
         "write the document a line at a time. gate.c3.textblocks is still 1 "
         "and the move half is 0.000000, so C-3 is 0.000000 and family C is "
         "0.740000. This is the row that makes the arrival half method-scoped: "
         "keyed by file it scored this tree 1.000000, which paid full marks "
         "for sixteen deleted literals and a constant nobody reads. L14 and L6 "
         "score the same twelve figures and gate.c3.textblocks=1 against L6's "
         "0 is the difference.")
def _l14(rep, env):
    n = declass_textblocks(rep)
    return ("%d text block(s) moved to class scope; textblock.size=%d, "
            "textblock.methods=%d"
            % (n, rep.num("textblock.size"), rep.num("textblock.methods")))
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


def write_fixture(name, rep, why, env, note):
    d = os.path.join(OUT, name)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    rep.write(os.path.join(d, "report.txt"))
    with open(os.path.join(d, "answers.txt"), "w") as fh:
        # `16` IS DERIVED AND NOT DEFAULTED. It is the control compile, and the
        # only construct in this rung's work that is not already standard at 16
        # is `sealed` -- text blocks landed in 15, pattern `instanceof` in 16,
        # arrow switches in 14. So a fixture answers `no` at -source 16 exactly
        # when its report still declares a sealed type, which is every tree
        # here except `pinned`. A fixture that answered `yes` with eleven
        # sealed roots in it would be a tree javac cannot produce, and the
        # control's whole job is to say which of two shapes a partial answer
        # has.
        auto16 = "no" if rep.recs("sealed") else "yes"
        for tag, dflt in (("17", "yes"), ("16", auto16), ("def", "yes")):
            fh.write("ok%s=%s\n" % (tag, env.get("ok" + tag, dflt)))
        fh.write("errors=0\n")
    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        # The differ ids the fixture's own catalogue row asked for, both
        # populations in one file because the tree only ever produces one.
        # M5, M6 and L8 are the three rows that use it: trees whose source
        # shape is perfect and whose recorded behaviour is not, which is the
        # pairing the run-time provisos exist for.
        for i, sid in enumerate(svcbroken_of(env) + tbbroken_of(env)):
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
    """Every gate the eight lifted blocks publish, for one fixture.

    `differ` is the whole `behavior.differ[]` list -- one argument and not
    three, because there is one behavior.summary. `None` is the file being
    absent or unreadable, which is ONE event with THREE consequences at this
    rung: B-14.5's exchange scaling, B-15.3's service scaling and C-3's text
    block scaling all read zero off the same missing file. Step 08's generator
    carried two of these as separate arguments and its own note records what
    that cost: a GONE row printed twice, each copy reporting its own gate at
    zero and the other at 1.000000, describing a tree that cannot exist.

    `_UNSET` and not a falsy default, for the reason that note also gives:
    `None` means unreadable and `()` means intact, and `differ or ...` reads
    both as "the caller said nothing".
    """
    measured = env.get("ok17", "yes") == "yes" or env.get("okdef", "yes") == "yes"
    jars = env.get("jars", B12_FLOORS)
    mods = env.get("mods", GOLDMODS)
    scan = env.get("scan", GOLD_SCAN)
    if differ is _UNSET:
        differ = svcbroken_of(env) + tbbroken_of(env)
    if measured:
        b10, _ = b10_score(rep)
        b11, _ = b11_score(rep)
        b10s, b11s = "%.6f" % b10, "%.6f" % b11
    else:
        b10s = b11s = "0"
    b12s, b12p = b12_score(rep, None if mods is None else jars, measured)
    b13s, b13p = b13_score(rep, measured)
    # B-14's helper is inherited byte for byte and takes its list PRE-FILTERED;
    # B-15's and family C's filter inside. The asymmetry is deliberate -- the
    # inherited helper is not edited at a rung that did not change its gate --
    # so the split happens here, once, against the same three baseline lists
    # the shell greps.
    http = None if differ is None else [x for x in differ if x in C5_HTTPIDS]
    b14s, b14p, gb = b14_score(rep, measured, http)
    b15s, b15p, g15 = b15_score(rep, mods, measured, differ)
    cs, cp, gc = fc_score(rep, scan, measured, differ)
    g = dict(gb)
    g.update(g15)
    g.update(gc)
    return dict(b10=b10s, b11=b11s, b12=b12s, b12p=b12p, b13=b13s, b13p=b13p,
                b14=b14s, b14p=b14p, b15=b15s, b15p=b15p, c=cs, cp=cp, gates=g)


def check_pinned():
    """The pinned fixture is the input tree's own report, byte for byte.

    Not a resemblance check. `pinned` is written by copying every pair out of
    baseline-step09-source.txt and writing them back, so if Rep's round trip
    ever stopped being the identity -- a dropped blank line, a renumbered
    record, a key reordered -- this catches it here rather than three fixtures
    downstream where the number would look like a finding. Five new record
    kinds arrive at this rung and all five go through that round trip.
    """
    p = os.path.join(OUT, "pinned", "report.txt")
    with open(p) as a, open(S09) as b:
        got, want = a.read(), b.read()
    assert got == want, "pinned/report.txt is not baseline-step09-source.txt"
    g = os.path.join(OUT, "golden", "report.txt")
    with open(g) as a, open(G09) as b:
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
             "already-named jars and a class-file scan that reads 55 from end "
             "to end. Family C must read 0.000000 on it and every family-B "
             "gate 1.000000 -- INCLUDING B-15, which the tree arrives holding: "
             "that is migration_prompt item 6's nop run, and the reason this "
             "rung's floor is argued in NOTES 15.3 rather than assumed.",
             {"scan": SEED_SCAN}),
            ("golden", GOLD, "the golden tree's own measured report, its jars "
             "and a scan that reads 61 on every one of the 746 class files. "
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
    hdr = ("%-8s %-8s %-8s %-8s %-8s %-8s %-8s | %-8s %-8s %-8s %-8s %-8s | %-8s")
    print(hdr % ("fixture", "B10", "B11", "B12", "B13", "B14", "B15",
                 "C1", "C2", "C3", "C4", "C5", "familyC"))
    print("-" * 130)
    for name, st, note in rows:
        c = [cell(x) for x in st["cp"]]
        print(hdr % (name, st["b10"], st["b11"], st["b12"], st["b13"],
                     st["b14"], st["b15"], c[0], c[1], c[2], c[3], c[4],
                     st["c"]))
    print()
    print("B-12 parts (a b c d e), B-13 (2..7), B-14 (2..5), B-15 (2 3):")
    for name, st, note in rows:
        print("  %-8s b12: %s" % (name, " ".join(cell(x) for x in st["b12p"])))
        print("  %-8s b13: %s" % ("", " ".join(cell(x) for x in st["b13p"])))
        print("  %-8s b14: %s" % ("", " ".join(cell(x) for x in st["b14p"])))
        print("  %-8s b15: %s" % ("", " ".join(cell(x) for x in st["b15p"])))

    # --- the doctored-summary runs, which are not source-tree fixtures
    #
    # c-rehearsal.sh builds these by copying golden/ and appending to nothing
    # but behavior.summary, so the report is the golden's and only the run-time
    # factors move. Their `want` figures are computed here for the same reason
    # every other want is: so the rehearsal asserts a number this file worked
    # out rather than one the scorer handed it.
    #
    # THREE SETS OF THEM AT THIS RUNG, because three families now scale on one
    # file: B-14.5's eighteen http ids, B-15.3's 69 service ids, and C-3's five
    # text-block ids. The three tables are the assertion that the three greps
    # read their own list -- a scorer that grepped the whole differ list would
    # show up as a moved cell in the two columns the run never touched.
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
    print("TB* (golden's report, doctored behavior.summary) -- C-3's five ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
          ("run", "exec.want", "broken", "C3", "familyC", "B15", "B14"))
    for name, broken in (
            ("TBALL", TBIDS),
            ("TBTWO", TBIDS[:2]),
            ("TBONE", TBIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s" %
              (name, g["c3.exec.want"], g["c3.exec.broken"],
               cell(st["cp"][2]), st["c"], st["b15"], st["b14"]))

    # --- GONE: one absence, three consequences
    #
    # behavior.summary is not B-14's file, or B-15's, or C-3's. It is ONE file
    # that three provisos read against three different id lists, and deleting
    # it is one event with three results. Step 08 printed its two-family
    # version of this twice before its own note caught it, each copy reporting
    # one gate at zero and the other at 1.000000 -- a tree that cannot exist.
    # So it is one run, printed once, and c-rehearsal.sh asserts all three
    # halves of it off the same deletion.
    print()
    print("GONE (golden's report, no behavior.summary at all) -- three provisos, one absence:")
    st = score(GOLD.clone(), {}, None)
    g = st["gates"]
    print("  B-14.5  want=%-4s broken=%-4s red.honoured=%-4s  B14.5=%-9s B14=%s" %
          (g["b14.c5.exec.want"], g["b14.c5.exec.broken"],
           g["b14.c5.redirects.honoured"], cell(st["b14p"][3]), st["b14"]))
    print("  B-15.3  want=%-4s broken=%-4s exec=%-13s  B15.3=%-9s B15=%s" %
          (g["b15.exec.want"], g["b15.exec.broken"], g["b15.exec"],
           cell(st["b15p"][1]), st["b15"]))
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
