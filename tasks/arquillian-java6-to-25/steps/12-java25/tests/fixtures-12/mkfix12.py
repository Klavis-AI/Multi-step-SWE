#!/usr/bin/env python3
"""Build step 12's rehearsal fixtures for family C, B-10, B-11, B-12, B-13,
B-14, B-15, B-16, B-17 and B-18, and compute what each one must score.

    python3 authoring/fixtures-step12/mkfix12.py

Writes one directory per fixture under fixtures-step12/c/, each holding the
source report the shipped scoring blocks will read, the compile answers they
would have got from javac, a behaviour summary, the `jars/` directory B-12e
unzips and arqgate.Modules reads, the `scan.txt` C-1 reads, and -- new at this
rung -- the `jvmargs.txt` C-5's Gradle probe would have printed. Prints the
score table that c-rehearsal.sh asserts.

ELEVEN blocks, not ten. Step 11 rehearsed the source-AST block, B-10 through
B-17 and family C; this step adds B-18, which is step 11's own family C minus
its C-1, renormalised over the 0.96 that is left. All eleven are extracted from
the shipped test.sh and all eleven are scored here.

THE SCAN STAYS, GAINS A SECOND NUMBER, AND THE CONSTRUCTS MOVE. Step 11's
family C read ${WORK}/scan.txt for the class-file major and the source AST for
four security-API constructs. Here the four constructs are B-18 and the scan is
C-1 again at a NEW major -- 69 where step 11 wanted 66 -- so C-1 keeps its
channel and its two fixtures keep their names. What is new is that C-1 now
reads a MAJOR AND A MINOR. Every one of this rung's three language and runtime
changes was preview in some release before 25, a `--enable-preview` build
stamps minor 65535 on every class it emits (JVMS 4.1), and a major-only reading
cannot tell that tree from a correct one. So the fixtures write
`majorminor.69.0=` and `preview.classes=` beside the `major.N=` lines B-4 still
reads, and the P series has a row that reaches 69 entirely under preview and
scores C-1 zero.

FOUR RECORD KINDS ARRIVE AND NONE OF THE INHERITED POPULATIONS IS RE-CUT.
modimport, jdkimport, threadlocal and scopedvalue are new at this rung; the
first and the last are empty in the seed and are C-2's and C-4's arrivals, the
middle two are full in the seed and are C-3's departures and C-4's hold. The
re-cutting that steps 09 through 11 each had to do stops here, and that is a
measurement rather than a hope: step 12 deletes 452 import lines and inserts
188, which moves almost every line number in 171 files and touches no
population, because every population in this tree is keyed by <file> or by
<file>#<method>. NOTES 16.61 ran step 11's whole verifier against this rung's
golden and all seventeen inherited legs read 1.000000.

TWO DOORS TO THE RUNGS BELOW, AND WHICH ONE A ROW USES IS DERIVED. NOTES 16.22's
problem -- a row that puts back a construct the candidate was to have removed
can only read it out of a tree that HAS it -- is answered at one door per rung
of distance. The N series restores reflective type tests and index-0 reads,
which were last measured TWO rungs down, in step 10's input tree. The P series
restores doPrivileged ladders, unnamed catch parameters and setAccessible
calls, which were last measured ONE rung down, in step 11's input tree. Both
doors are asserted against this rung's seed at the joint, and SEED_OLD below
derives which kinds this rung's OWN solution removes so that neither door can
quietly acquire a second source.

THE TWO ENDS ARE MEASURED REPORTS AND NOT SYNTHESISED ONES, as they have been
since step 07's generator:

    pinned   baseline-step12-source.txt, the input tree's own measured report,
             which is also what the verifier stages as baseline/source.txt
    golden   baseline-step12-golden.txt, the golden tree's own measured report,
             /work/scratch/s12/gate/out/goldall.txt as the gate wrote it

Every fixture below is one of those two with an EXPLICIT, NAMED mutation
applied -- a record moved, a counter bumped, a bucket reverted. Nothing is
invented from whole cloth. That buys two things a feature-dict generator could
not:

  * a fixture claiming "nineteen files got java.base and nothing else" deletes
    the golden's OWN `modimport` rows for the six modules java.base does not
    cover, leaving the rows at their real lines in their real files, because it
    takes them from the report the migrated tree actually produced
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

S12 = os.path.join(AUTH, "baseline-step12-source.txt")
G12 = os.path.join(AUTH, "baseline-step12-golden.txt")
# THE RUNGS BELOW, AND THERE ARE TWO OF THEM AT THIS RUNG. Some rows of the N
# and P series put a construct BACK that the candidate was supposed to have
# removed -- a reflective type test beside the pattern that replaced it, a
# doPrivileged ladder beside the direct call. Such a row can only read the old
# spelling out of a tree that HAS it, and a tree has it only until the rung
# that removed it ran. NOTES 16.22. So the door is one file per rung of
# distance, and door() below picks WHICH ONE PER KIND, by measurement:
#
#   PRIOR11  step 11's INPUT tree, which is where `priv`, `catchparam`'s
#            unnamed-to-unread spelling and the four converted `accessible`
#            calls were last measured. The P series reads here. Step 11's
#            solution removed them, so this rung's seed -- step 11's OUTPUT --
#            has 0 priv rows, 28 already-unnamed catch parameters and 10
#            surviving setAccessible calls rather than 14.
#
#   PRIOR10  step 10's INPUT tree, two rungs down, which is where `reftest`,
#            `seqfirst`, `seqnew` and `swpat` were last measured. The N series
#            reads here, exactly as it did at step 11, because nothing between
#            then and now put those constructs back.
#
# A row that reads the WRONG door restores nothing and scores the golden's
# figures while its note claims a regression, which is the failure NOTES 16.22
# was written about. NO CALL SITE PICKS, at this rung or above: door() walks
# the rungs in order and takes the nearest one carrying a record this rung's
# seed lacks, and RAISES for a kind no rung below has -- so a row about a
# construct that arrived rather than departed cannot quietly restore nothing.
# SEED_OLD below is the third door, this rung's own seed, for the two kinds
# THIS rung removes.
S11 = os.path.join(AUTH, "baseline-step11-source.txt")
G11 = os.path.join(AUTH, "baseline-step11-golden.txt")
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
BASE = Rep(S12)
GOLD = Rep(G12)
PRIOR11 = Rep(S11)                                  # step 11's INPUT tree
PRIOR10 = Rep(S10)                                  # step 10's INPUT tree

# THE LADDER JOINTS, ASSERTED AND NOT ASSUMED, ONE PER DOOR. A door is only
# usable as "the tree N rungs down" if the rung ABOVE it and this rung's seed
# are the same tree where the restored kinds are concerned, and they are never
# the same FILE: each rung's scan reports kinds the one below did not, and each
# rung's own solution shifts lines everywhere. So the claim is made over the
# shared populations with the line number cut off -- same records, same
# buckets, same fields -- and a rung that moved one of these rather than
# leaving it alone would be a diff here rather than a surprise in a note.
_cut = lambda rs: sorted(r.split(":", 1)[0] + " " + r.split(None, 1)[1]
                         for r in rs)
# TWO RUNGS DOWN, FOR THE N SERIES. Step 10's golden is step 11's input, and
# step 11 left all four of these alone, so they arrive here untouched.
# `ipattern` is NOT in this list any more and that is the whole reason the list
# had to be re-derived rather than carried forward: step 11 RE-CUT it, 31 down
# to 24, because its C-2 deleted seven bound-pattern buckets outright. Asserting
# the step-11 list here would fail on a difference that is correct.
_PRIORG = Rep(G10)
for _k in ("reftest", "seqfirst", "seqnew", "swpat"):
    assert _cut(_PRIORG.recs(_k)) == _cut(BASE.recs(_k)), \
        "step 10's golden and step 12's seed disagree on %s beyond line numbers" % _k
del _PRIORG
# ONE RUNG DOWN, FOR THE P SERIES. Step 11's golden against this rung's seed,
# over the four kinds B-18 scores plus the one step 11 re-cut.
#
# AND THE SEED IS NOT THE GOLDEN, WHICH IS WHY THIS JOINT IS NARROWER THAN IT
# LOOKS. Since step 02 every rung's input has been the rung below's audited
# BLIND workspace and not its golden -- two trees that score the same, written
# by different hands. They differ in dozens of places that no population is
# keyed on, and in exactly ONE place that a scored population's kind is: the
# blind workspace writes `fileShouldExist` in ArchiveDeploymentExporterTestCase
# without the catch clause step 11's golden left there, so all-scope catchparam
# reads 89 here against the golden's 90. That row is in src/test/java and
# catchparam.txt is a MAIN-scope population, so B-18.3 cannot see it. The
# assertion is therefore made at main scope, where the two agree exactly at 79,
# rather than waived at all scope where they do not.
_PRIORG = Rep(G11)
_main = lambda rs: _cut([r for r in rs if "/src/test/java/" not in r])
for _k in ("priv", "accessible", "varhandle", "ipattern"):
    assert _cut(_PRIORG.recs(_k)) == _cut(BASE.recs(_k)), \
        "step 11's golden and step 12's seed disagree on %s beyond line numbers" % _k
assert _main(_PRIORG.recs("catchparam")) == _main(BASE.recs("catchparam")), \
    "step 11's golden and step 12's seed disagree on main-scope catchparam"
del _PRIORG

# WHICH KINDS THE SEED STILL CARRIES AN OLD SPELLING OF, DERIVED AND NOT LISTED.
# NOTES 16.22's rule, made mechanical: a row that puts a construct BACK can only
# read it out of the tree the candidate is handed if that tree HAS it, and which
# kinds those are is a fact about this rung's own solution, not a thing to
# remember. One record body, its line number cut off, present in the seed and
# absent from the golden is the whole test.
#
# TWO KINDS, AND NO COLLATERAL AT ALL, WHICH IS NEW. The 452 departing
# `jdkimport` rows are C-3's and the 7 departing `threadlocal` rows are C-4's
# -- one FIELD declaration and the six calls on it, every one of them in
# ManagerImpl.java. Every rung from 09 on removed constructs it was not asked
# to, because deleting a ladder takes the lambdas and patterns inside it; this
# rung deletes import lines and rewrites one field, and neither is a scope that
# contains anything. So the list is exactly the two legs' own work and a row
# restoring one of them is describing a tree that skipped that leg.
_SEEDCUT = lambda r: r.split(":", 1)[0] + " " + r.split(None, 1)[1]
SEED_OLD = tuple(sorted(
    k for k in set(Rep.REC.match(p[0]).group(1) for p in BASE.pairs
                   if Rep.REC.match(p[0]))
    if set(map(_SEEDCUT, BASE.recs(k))) - set(map(_SEEDCUT, GOLD.recs(k)))))
assert SEED_OLD == ("jdkimport", "threadlocal"), \
    "the kinds this rung's solution removes moved: %s" % (SEED_OLD,)
# AND NEITHER DOOR'S KINDS ARE AMONG THEM, which is the finding of NOTES 16.22
# stated as an assertion rather than as a paragraph. If a later edit to this
# rung's solution starts removing reflective type tests or doPrivileged calls,
# the rows that now correctly read one rung or two rungs down would silently
# have a second source, and this is where that gets noticed.
assert not (set(SEED_OLD) & set(("reftest", "seqfirst", "seqnew", "swpat"))), \
    "this rung now removes a construct the N series restores two rungs down"
assert not (set(SEED_OLD) & set(("priv", "catchparam", "accessible", "varhandle"))), \
    "this rung now removes a construct the P series restores one rung down"
TGT = read_env(os.path.join(AUTH, "baseline-step12-target.txt"))
LEVEL = read_env(os.path.join(AUTH, "baseline-step12-level.txt"))


def T(k):
    return int(TGT[k])


# WANT_MAJOR IS SPENT AGAIN AT THIS RUNG and it is a different number. C-1 asks
# how much of the tree reached 69 and B-4 asks that whatever it reached is one
# major and not below 66 -- the level step 11 left it at, which is this rung's
# `was`. Both numbers live in baseline/level.txt -- `major` for C-1 and `was`
# for B-4 -- and neither gate reads a literal. A generator that carried 66
# forward as the want would score the golden's 69 as a miss and the INPUT TREE
# as perfect, which is the one arithmetic error this constant can make.
#
# AND A THIRD NUMBER THAT NO RUNG BELOW NEEDED: WANT_MINOR. A class file carries
# a minor beside its major, and javac writes 65535 there -- and only there -- for
# a compilation unit built with `--enable-preview` (JVMS 4.1). Every one of this
# rung's three language and runtime changes was a preview feature in some
# release before 25, the flag is in every tutorial written while they were, and
# a tree that turns it on emits 69.65535 and has NOT migrated to 25: it has
# migrated to a 25 that the next release refuses to load. `major=69 minor=0` is
# therefore the pair C-1 reads, `majorminor.69.0=` is what the fixtures write,
# and `preview.classes=` is the diagnostic that says which of the two a zero is.
WANT_MAJOR = int(LEVEL["major"])                    # 69, the must-flip
WANT_MINOR = int(LEVEL["minor"])                    # 0, and not 65535
HOLD_MAJOR = int(LEVEL["was"])                      # 66, B-4's floor
BASE_SUP = BASE.num("suppress.sites")               # 44
BASE_RAW = BASE.num("newgeneric.raw")               # 0
# What arqgate.Scan counts over the published jars and the class files the
# build left in the tree: gate.classes=732 on step 11's own runs. Both of them,
# which is the part worth writing down -- NOTES 16.53 tabulated step 11's
# oracle solution against its round-2 blind tree and the two agree at 732 to the
# digit, and it is the BLIND tree that is this rung's input.
#
# THE TWO ENDS DO NOT AGREE, for the second rung running and for a much smaller
# reason than step 11's. Step 11 deleted seven nested privileged-action classes
# with the ladders that made them. This rung deletes ONE anonymous class:
# `ManagerImpl.handledThrowables` is `new ThreadLocal<..>() { initialValue() }`
# in the seed and `ScopedValue.newInstance()` in the golden, so ManagerImpl$1
# stops existing. The reports say so from the other side -- `anonymous.size`
# reads 33 in the seed and 32 in the golden -- and the class file is counted
# twice, once in impl-base's build output and once inside the published
# arquillian-impl-base jar, so the tree publishes 730 where it published 732.
#
# The OTHER anonymous ThreadLocal in that file stays: `eventStack` is the
# debug-only one scopedvalue.txt holds, and a tree that converted it too would
# lose a second class here and C-4's hold with it.
#
# C-1 IS A FRACTION AND NOT A DIFFERENCE, which is why that costs it nothing:
# 730 of 730 is 1.000000 and 0 of 732 is 0.000000. A leg that had scored the
# COUNT would have charged this rung for doing its own work.
CLASSES = 732
GOLD_CLASSES = 730

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
B13_SACCB = T("target.b13.setacc.buckets")   # 10, NOT B13_GUARDS: see acc_score
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

# STEP 11'S LAMBDA FILES, NOT STEP 04'S, and the other two thirds of B-11 are
# step 04's still. Seven of step 04's twenty are SecurityActions classes whose
# only anonymous functional body was the `PrivilegedAction` this rung's C-2
# unwraps; over the twenty a correct tree reads 13 and B-11 = 0.930000, in
# every row of the table below including its golden, which is how it was
# found. samifaces.txt and collectmethods.txt were checked against the golden
# the same way and neither moves. NOTES 16.20.
LAMFILES = read_set(os.path.join(AUTH, "baseline-step11-lambdafiles.txt"))
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
# STEP 11'S accessible.txt, NOT STEP 06'S, and it is one of the two files in
# this whole generator whose inherited population was re-cut rather than
# staged (NOTES 16.16; ipattern.txt below is the other). The `guard` half is
# step 06's twelve buckets verbatim -- every one of those methods is still
# here. The `setacc` half is the TEN calls that outlive this rung's C-4, down
# from fourteen, and three of the four C-4 converts sit in guard buckets. So
# the two halves are two populations now and b13's arrival term reads the one
# it is actually asking about: over the guard list it would read 9 of 12 on a
# correct tree, which is the ninth trap wearing a fixed baseline.
ACC_GUARD = [l.split(None, 1)[1] for l in
             read_set(os.path.join(AUTH, "baseline-step11-accessible.txt"))
             if l.split()[0] == "guard"]
ACC_SETACC = [l.split(None, 1)[1] for l in
              read_set(os.path.join(AUTH, "baseline-step11-accessible.txt"))
              if l.split()[0] == "setacc"]
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
for _l in read_set(os.path.join(AUTH, "baseline-step11-ipattern.txt")):
    _t, _b, _n = _l.split()
    (IP_CONV if _t == "conv" else IP_SKIP)[_b] = int(_n)
IP_CONV_W = sum(IP_CONV.values())                   # 17 sites over 14 buckets
IP_SKIP_W = sum(IP_SKIP.values())                   # 7
IP_TOTAL = T("target.b16.ip.total")                 # 24, was 28 one rung ago

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
assert len(ACC_SETACC) == B13_SACCB
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


# --- step 10's own four, which this rung spends on B-17 ----------------------
#
# reftest, chain, seqfirst and swids, held one rung on. Three of the four carry
# a prose reason on every row -- the `  # why` tail the census script writes --
# because a population whose rows say only `conv` is a population nobody can
# review. The tail is cut HERE and not in the shell: the verifier never reads
# these files, it reads the target keys they were counted into.
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
# B-17.3's run-time factor reads the same list the scorer reads, as B-14.5's
# did when it was step 07's C-5, so the denominator cannot drift between the
# two implementations by editing one file.
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

assert len(RT_CONV) == T("target.b17.rt.conv.buckets"), "reftest conv buckets"
assert RT_CONV_W == T("target.b17.rt.conv.sites"), "reftest conv sites"
assert len(RT_HOLD) == T("target.b17.rt.hold.buckets"), "reftest hold buckets"
assert RT_HOLD_W == T("target.b17.rt.hold.sites"), "reftest hold sites"
assert RT_CONV_W + RT_HOLD_W == T("target.b17.rt.total"), "the two halves are not the 27"
assert not (set(RT_CONV) & set(RT_HOLD)), "a bucket is both converted and held"
# One pair per conv site at this rung. If that ever stops being true the
# arrival half's denominator stops being the departure half's, which is
# survivable -- but silently, so it is asserted rather than assumed.
assert len(RT_ARR) == RT_CONV_W, "a conv bucket names one type twice"

assert len(CH_CONV) == T("target.b17.ch.conv"), "chain conv rows"
assert CH_CONV_W == T("target.b17.ch.arms"), "chain conv arms"
assert CH_HOLD_W == T("target.b17.ch.hold"), "chain hold rows"
assert len(SWIDS) == T("target.b17.ch.ids") and len(set(SWIDS)) == len(SWIDS)
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

assert len(SQ_CONV) == T("target.b17.sq.conv.buckets"), "seqfirst conv buckets"
assert SQ_CONV_W == T("target.b17.sq.conv.sites"), "seqfirst conv sites"
assert len(SQ_HOLD) == T("target.b17.sq.hold.buckets"), "seqfirst hold buckets"
assert SQ_HOLD_W == T("target.b17.sq.hold.sites"), "seqfirst hold sites"
assert SQ_CONV_W + SQ_HOLD_W == T("target.b17.sq.total"), "the two halves are not the 16"
assert not (set(SQ_CONV) & set(SQ_HOLD)), "a bucket is both converted and held"
# `seqnew` reads SEVEN on the way in and B-17.4 asks for seven, which is the
# whole difference between an arrival leg and a held one. Step 10 measured a
# zero here; this rung measures what step 10 left, and a seed reading fewer
# than seven would mean step 10's own C-4 had come undone between the rungs.
assert BASE.num("seqnew.size") == T("target.b17.sq.new"), \
    "the seed does not hold the sequenced reads step 10's C-4 put there"
assert BASE.num("seqnew.size") == len(SQ_CONV), \
    "the held sequenced reads are not one per seqfirst conv bucket"
# And the two `.scanned` figures, which are this rung's seed and not step 10's
# -- 35 - 13 and 26 - 7, the conv sites step 10 converted away. Both are
# published as the `.was` half of a gate pair and scored by nothing, and both
# are asserted here because a stale `.was` misreads a correct run rather than
# mis-scoring it, which is the harder failure to notice. NOTES 16.20.
assert BASE.num("reftest.size") == T("target.b17.rt.scanned"), "rt.scanned"
assert BASE.num("seqfirst.size") == T("target.b17.sq.scanned"), "sq.scanned"


# --- and step 11's own four, which are this rung's and nobody else's ---------
#
# priv, catchparam, varhandle and vhids. NOT the shape the four above have.
# Those are census files: a verdict in column two and a `  # why` tail. These
# are THE PLUGIN'S OWN RECORDS, one raw line per site exactly as
# arqgate.SourceScan wrote it, because for a rung whose subject is a REMOVED
# API the record is the evidence and a reader of a failed run has to be able to
# open the file at the line. So the verdict is a `k=v` field like every other
# field -- `kind=`, `verdict=` -- and the bucket is DERIVED here, by the three
# lines pbuckets() uses in the shell, rather than written into the file.
#
# THE LINE NUMBER IS READ BY NOBODY, ON EITHER SIDE. This rung rewrites every
# one of these files; a method name survives the edit the gate is asking for
# and a line number does not.
#
# `in=-` IS A BUCKET AND NOT A SKIP, and that is the one place these differ
# from the four above. Seven priv.txt rows are the `implements
# PrivilegedAction<ClassLoader>` on a class, which is inside no method at all;
# pbuckets() keys them `<file>#-`, because the guard it applies is `m == ""`
# and "-" is not "". Dropping them would take 7 of the 67 out of C-2's
# denominator and let a tree that unwrapped every call and left every inner
# class declared read the departure half whole.
def praw(path, keep=None, drop=None):
    """pbuckets() in Python: `<file>#<method>` -> how many rows, over the rows
    carrying <keep> as a WHOLE field, minus every bucket in <drop>.

    A whole field and not a substring, as in the shell: `kind=read` must not
    match `kind=unread`, and it is `$i == w1` there for that reason.
    """
    out = {}
    for l in read_set(path):
        f = l.split()
        if keep is not None and keep not in f[1:]:
            continue
        m = None
        for tok in f[1:]:
            if tok.startswith("in="):
                m = tok[3:]
        if not m:
            continue
        b = "%s#%s" % (f[0].split(":")[0], m)
        if drop is not None and b in drop:
            continue
        out[b] = out.get(b, 0) + 1
    return out


def ptot(want):
    """The denominator, summed out of the frozen population rather than read
    from a target key, so the two cannot disagree. Every assertion below then
    checks them against each other once."""
    return sum(want.values())


def pdep(want, have):
    """The departure half: sum of max(0, frozen - still counted), clamped at
    zero per bucket so a method that lost a site it never had cannot pay for
    one that kept the site it did."""
    return sum(max(0, n - have.get(b, 0)) for b, n in want.items())


# C-2's. PRIV_ALL is the departure population, 67 sites in 27 buckets over 7
# files; PRIV_FILE and PRIV_ACC are the hold, and NEITHER IS TYPED. The file
# list is what priv.txt's own first column implies and the setAccessible list
# is what falls out of intersecting accessible.txt's `setacc` half with it, so
# narrowing the departure population narrows the hold with it.
PRIV_ALL = praw(os.path.join(AUTH, "baseline-step11-priv.txt"))
PRIV_FILE = {}
for _l in read_set(os.path.join(AUTH, "baseline-step11-priv.txt")):
    PRIV_FILE[_l.split()[0].split(":")[0]] = 1
PRIV_ACC = {}
for _l in read_set(os.path.join(AUTH, "baseline-step11-accessible.txt")):
    _f = _l.split()
    if _f[0] == "setacc" and _f[1].split("#")[0] in PRIV_FILE:
        PRIV_ACC[_f[1]] = 1
PRIV_DEP_W = ptot(PRIV_ALL)                         # 67 sites
PRIV_CTOR_W = ptot(PRIV_FILE)                       # 7 files, one construction each
PRIV_ACC_W = ptot(PRIV_ACC)                         # 6 scanners inside those files

# C-3's, all three halves out of the one file. The two that are scored per
# ARRIVAL and per HOLD have priv.txt's buckets subtracted, which is the whole
# of this leg's coupling to C-2: 7 unread and 7 read parameters are inside the
# ladders C-2 deletes and cannot be asked to arrive as `_` AND to stay as they
# are. The departure half is stated over all 35, including those 7, because a
# tree that does C-3 and skips C-2 can still rename them where they stand.
CP_DEP = praw(os.path.join(AUTH, "baseline-step11-catchparam.txt"), "kind=unread")
CP_ARR = praw(os.path.join(AUTH, "baseline-step11-catchparam.txt"), "kind=unread",
              PRIV_ALL)
CP_HOLD = praw(os.path.join(AUTH, "baseline-step11-catchparam.txt"), "kind=read",
               PRIV_ALL)
CP_DEP_W = ptot(CP_DEP)                             # 35 over 31 buckets
CP_ARR_W = ptot(CP_ARR)                             # 28 over 24
CP_HOLD_W = ptot(CP_HOLD)                           # 51 over 46

# C-4's. The hold half is stated with NO field filter and the conv buckets
# subtracted, which is `pbuckets ... vhhold "" set-vhconv` in the shell: the
# ten that are left are everything varhandle.txt does not mark convert,
# whichever of the two reasons it gives. The arrival denominator is twice the
# departure's, one `privateLookupIn` and one `unreflectVarHandle` per bucket,
# and it is computed from the population rather than typed for the reason the
# rung below computed its own: a later edit that added a fifth convertible
# field would move both and not one.
VH_CONV = praw(os.path.join(AUTH, "baseline-step11-varhandle.txt"), "verdict=convert")
VH_HOLD = praw(os.path.join(AUTH, "baseline-step11-varhandle.txt"), None, VH_CONV)
VH_DEP_W = ptot(VH_CONV)                            # 4
VH_ARR_W = VH_DEP_W * 2                             # 8
VH_HOLD_W = ptot(VH_HOLD)                           # 10
# C-4's run-time factor reads the same list the scorer reads, as B-17.3's
# SWIDS and B-14.5's C5_HTTPIDS do, so the denominator cannot drift between the
# two implementations by editing one file.
VHIDS = read_set(os.path.join(AUTH, "baseline-step11-vhids.txt"))
VH_EXECN = len(VHIDS)                               # 4

# THE KEYS MOVED AND THE NUMBERS DID NOT. Step 11 published these under
# `target.priv.*`, `target.cp.*` and `target.vh.*`, because they were that
# rung's family C. Here they are a held family-B leg and step 12's target.txt
# names them `target.b18.*`, exactly as step 11 renamed step 10's family C into
# `target.b17.*`. Every assertion below is the same claim about the same frozen
# population; only the key it cross-checks against has a prefix.
assert PRIV_DEP_W == T("target.b18.priv.sites"), "priv sites"
assert len(PRIV_ALL) == T("target.b18.priv.buckets"), "priv buckets"
assert PRIV_CTOR_W == T("target.b18.priv.files"), "priv files"
assert PRIV_CTOR_W == T("target.b18.priv.hold.ctor"), "the construction hold is not one per file"
assert PRIV_ACC_W == T("target.b18.priv.hold.acc"), "priv setAccessible hold"
assert PRIV_CTOR_W + PRIV_ACC_W == T("target.b18.priv.hold"), "the two hold halves are not the 13"
# THE HOLD POPULATION IS A SUBSET OF ACCESSIBLE.TXT'S SURVIVORS AND THAT IS
# WHAT MAKES C-2 AND B-13.3 CONSISTENT. Every bucket C-2 asks to keep its
# `setAccessible` is one B-13.3 also asks to keep it: if C-4 had converted one
# of the six, this rung would be asking for the call to go and to stay.
assert set(PRIV_ACC) <= set(ACC_SETACC), \
    "C-2 holds a setAccessible call that accessible.txt's setacc half does not"
assert not (set(PRIV_ACC) & set(VH_CONV)), \
    "C-2 holds a setAccessible call that C-4 converts"
# 0 of the 7 constructions are inside the buckets this leg rewrites and 6 of
# the 6 scanners are. That is the sharpest evidence available that the wrapper
# was unwrapped rather than deleted, and it is asserted rather than described.
assert set(PRIV_ACC) <= set(PRIV_ALL), \
    "a held scanner is not in a bucket C-2 rewrites"

# AND ONE OF THEM HAS NO KEY TO MOVE TO, WHICH IS A REAL DIFFERENCE RATHER
# THAN A GAP TO PAPER OVER. Step 11's `target.cp.unread` was 35: the count of
# clauses its own seed declared and never read, and its C-3's departure
# denominator. Step 12's target does not republish it, because `target.b18.cp.*`
# is the census of what step 11 PRODUCED -- `unread=0`, which is the whole point
# of the leg -- and not of what it was handed. The departure denominator is
# still 35 and still comes out of the frozen file; what it is checked against is
# the one key that does describe the population before step 11 ran. The 35
# unread plus the 58 read are the 93 clauses step 11's scan found, so a
# catchparam.txt that lost a row, or grew one, fails here.
assert CP_DEP_W + ptot(praw(os.path.join(AUTH, "baseline-step11-catchparam.txt"),
                            "kind=read")) == T("target.b18.cp.was"), \
    "catchparam.txt does not account for every clause step 11's scan found"
assert CP_ARR_W == T("target.b18.cp.unnamed"), "catchparam arrivals"
assert CP_HOLD_W == T("target.b18.cp.read"), "catchparam holds"
assert CP_ARR_W + CP_HOLD_W == T("target.b18.cp.total"), "the two are not the 79"
assert len(set(CP_ARR) | set(CP_HOLD)) == T("target.b18.cp.buckets"), \
    "catchparam want buckets"
assert not (set(CP_ARR) & set(PRIV_ALL)) and not (set(CP_HOLD) & set(PRIV_ALL)), \
    "a scored catch parameter is inside a bucket C-2 deletes"
# A BUCKET CAN BE BOTH, and this is the one population in the file where that
# is true rather than a defect. A method with two catch clauses, one read and
# one not, is asked for both halves at once -- the unread one becomes `_`, the
# read one stays as it is -- and the two halves are counted against two
# different record kinds, `kind=unnamed` and `kind=read`, so neither can pay
# for the other. Asserted as a measured number rather than left implied.
assert len(set(CP_ARR) & set(CP_HOLD)) == 9, \
    "the overlap between the arrival and hold buckets moved"
# `unnamed` READS 28 ON THE WAY IN AND THE SENTENCE HERE IS THE OPPOSITE OF THE
# ONE STEP 11 WROTE. There the seed had 0 unnamed catch parameters -- `_` was
# not a name a Java 6 tree could give one -- and the assertion said so, which is
# what made the arrival a measurement rather than a threshold. Here the seed IS
# step 11's output and carries all 28, so B-18.3 is a leg the candidate starts
# at 1.000000 on and can only LOSE. That is what a held family-B leg is, and the
# P series is built by breaking it rather than by completing it: every row that
# moves this number reads the old spelling out of PRIOR11.
assert BASE.num("catchparam.unnamed") == T("target.b18.cp.unnamed"), \
    "this rung's seed no longer carries step 11's 28 unnamed catch parameters"
# The same sentence for the other two halves of B-18, so all three are stated
# where a reader of one will see the others: the seed has no doPrivileged call
# left, and it already carries the eight VarHandle sites.
assert BASE.num("priv.size") == T("target.b18.priv.want"), \
    "this rung's seed still has a doPrivileged call"

assert VH_DEP_W == T("target.acc.conv") and VH_DEP_W == T("target.b18.vh.buckets"), \
    "varhandle conv"
assert VH_HOLD_W == T("target.acc.hold"), "varhandle hold"
assert VH_ARR_W == T("target.b18.vh.sites"), \
    "the arrival denominator is not twice the departure's"
assert VH_DEP_W + VH_HOLD_W == T("target.acc.total"), "the two halves are not the 14"
# VHIDS IS FOUR, AND ITS FOUR IS DERIVED HERE RATHER THAN TYPED. What stood in
# this place subtracted two id counts out of `target.ids` -- 37 - 5 - 5 is 27,
# which this list has never been -- and passed anyway, because the `or` beside
# it was the tautology on the next line. It asserted nothing, and while it
# asserted nothing vhids.txt collected a fifth id that svcids.txt already
# owned: `svc/sl-package-private-class` raises its IllegalAccessException
# inside java.util.ServiceLoader and belongs to B-15.3. A submission whose only
# behaviour regression was that one service paid 0.048 of family C for it --
# 0.025200 of reward, against the 0.000133 B-15.3 charges -- 191 times the
# price the leg that owns the behaviour sets. NOTES 16.21.
#
# WHAT IS ACTUALLY TRUE OF THIS LIST is that it is disjoint from the five other
# lists behavior.summary is read against, and that the six together are every
# recorded behaviour this rung prices twice-over: 4 + 18 + 69 + 5 + 5 + 4.
# mk-baseline-step11.sh derives the same fifteen pairs from the transcripts and
# the image re-derives them a third time; here they are checked against the
# lists the fixture scorer itself will divide by, which is the only one of the
# three that can be wrong in a way the other two cannot see.
BIDLISTS = (("nio", NIOIDS), ("http", C5_HTTPIDS), ("svc", SVCIDS),
            ("tb", TBIDS), ("sw", SWIDS), ("vh", VHIDS))
_bidpairs = 0
for _i, (_an, _al) in enumerate(BIDLISTS):
    for _bn, _bl in BIDLISTS[_i + 1:]:
        _bidpairs += 1
        assert not (set(_al) & set(_bl)), \
            "%s and %s price the same id twice: %s" % \
            (_an, _bn, " ".join(sorted(set(_al) & set(_bl))))
assert _bidpairs == 15, "the six behaviour-id lists are no longer six"
# The five inherited lengths are the rungs' own, and with them fixed the 105
# pins THIS list at 4 by arithmetic: a sixth id added to vhids.txt trips here
# rather than being priced in silence.
assert len(NIOIDS) == 4 and len(C5_HTTPIDS) == C5_EXECN == 18 and \
    len(SVCIDS) == 69 and len(TBIDS) == 5 and len(SWIDS) == SW_EXECN == 5, \
    "an inherited behaviour-id list changed length under this rung"
assert sum(len(_l) for _, _l in BIDLISTS) == 105, \
    "the six lists no longer account for 105 recorded behaviours"
assert len(VHIDS) == len(set(VHIDS)) and VH_EXECN == 4, "vhids repeats an id"
# `varhandle` READS 8 ON THE WAY IN, and as with the unnamed catch parameters
# above that is the inversion a held leg brings. The measured zero step 11
# asserted here has not been lost -- step 12's target keeps it as
# `target.b18.vh.was`, cut on step 11's seed -- so both ends of the claim
# survive the move and neither is a literal.
assert BASE.num("varhandle.size") == T("target.b18.vh.sites"), \
    "this rung's seed no longer builds step 11's eight VarHandles"
assert T("target.b18.vh.was") == 0 and T("target.b18.priv.want") == 0, \
    "step 12's target no longer records the zeros step 11's C-4 and C-2 moved from"
# AND THE TEN THAT STAY ARE THE TEN B-13.3 ASKS FOR, bucket for bucket. The two
# files were cut by the same script from the same golden and this is where they
# are made to agree: a varhandle.txt that marked a fifth row convert would
# leave B-13.3 asking for a call C-4 removed, which is the ninth trap turned
# inward on one rung's own two legs.
assert sorted(VH_HOLD) == sorted(ACC_SETACC), \
    "varhandle.txt's survivors are not accessible.txt's setacc half"

# --- and this rung's own five, which are family C ----------------------------
#
# THREE FROZEN POPULATIONS, ONE FLAG AND ONE SCAN. The three files are the
# CENSUS shape -- a verdict in a `k=v` field and a `why=` token beside it, one
# row per site, no `  # why` tail -- so _pop() above reads all three and the
# bucket is the first column exactly as it is written.
#
# AND THE BUCKET IS `<file>` ALONE, WHICH IS NEW AT THIS RUNG AND IS THE WHOLE
# REASON THESE POPULATIONS CAN BE FROZEN AT ALL. Every population above keys on
# `<file>#<method>` or on a bucket plus a type. This rung DELETES 452 import
# lines and INSERTS 188, in 171 of the tree's 322 files: every declaration
# below an import block moves, most of them by a double-digit number of lines,
# and a population keyed on position would read the correct tree as a total
# loss. `<file>` survives that edit, `<file> q=<qualified name>` survives it,
# and `<file> field=<name>` survives it -- which is why C-4 is keyed on the
# field name even though ManagerImpl's one convertible field is the only row
# that matters to the arithmetic.
MI_WANT = {}                                        # file -> the modules it needs
MI_NONE = []                                        # and the files that need none
for _f, _kv in _pop(os.path.join(AUTH, "baseline-step12-modimport.txt")):
    if int(_kv["n"]) == 0:
        assert _kv["mods"] == "-", "an n=0 modimport row still names a module: " + _f[0]
        MI_NONE.append(_f[0])
    else:
        MI_WANT[_f[0]] = set(_kv["mods"].split(","))
        assert len(MI_WANT[_f[0]]) == int(_kv["n"]), \
            "a modimport row's n= is not the size of its own set: " + _f[0]
MI_PAIRS = set((f, m) for f, ms in MI_WANT.items() for m in ms)
MI_WANT_W = len(MI_WANT)                            # 171 files with work
MI_NONE_W = len(MI_NONE)                            # 151 with none
MI_LINES = len(MI_PAIRS)                            # 188 declarations

# C-3's. A SET OF PAIRS AND NOT A BUCKET COUNT, because the question the leg
# asks of each row is `is this exact import still written out`, and two files
# importing the same type are two answers.
JI_DEP, JI_HOLD = set(), set()
_JI_WHY = {}
for _f, _kv in _pop(os.path.join(AUTH, "baseline-step12-jdkimport.txt")):
    _k = (_f[0], _kv["q"])
    if _kv["verdict"] == "depart":
        JI_DEP.add(_k)
    else:
        JI_HOLD.add(_k)
        _JI_WHY[_kv["why"]] = _JI_WHY.get(_kv["why"], 0) + 1
JI_DEP_W = len(JI_DEP)                              # 452
JI_HOLD_W = len(JI_HOLD)                            # 43

# C-4's. Eight fields, one verdict each.
SV_CONV, SV_HOLD = set(), set()
for _f, _kv in _pop(os.path.join(AUTH, "baseline-step12-scopedvalue.txt")):
    (SV_CONV if _kv["verdict"] == "convert" else SV_HOLD).add((_f[0], _kv["field"]))
SV_CONV_W = len(SV_CONV)                            # 1
SV_HOLD_W = len(SV_HOLD)                            # 7

# C-5's, WHICH HAS NO POPULATION FILE OF ITS OWN. The twelve projects are
# baseline/modnames.txt's twelve -- the same list B-15 reads module names out
# of -- and the flag is a string in target.txt. Both are read here rather than
# typed, for the reason C-1 reads its three constants out of level.txt: a leg
# whose want is a literal in the scorer is a leg that cannot be re-cut.
CO_FLAG = TGT["target.co.flag"]                     # -XX:+UseCompactObjectHeaders
CO_PROJ = [a for a, _ in MODNAMES]                  # the twelve, in file order
CO_PROJ_W = len(CO_PROJ)                            # 12
# What Gradle hands a test fork in this tree with nothing added: measured off
# the seed by the authoring probe, and carried here so that a row modelling an
# unflagged project prints what an unflagged project actually prints. The
# scorer greps the WHOLE row for the flag, so the rest of the line is scenery --
# but scenery a reader of a failed fixture has to recognise.
CO_BASEARGS = ("-Djunit.jupiter.displayname.generator.default="
               "org.junit.jupiter.api.DisplayNameGenerator$Simple -Xmx512m "
               "-Dfile.encoding=UTF-8 -Duser.country=US -Duser.language=en "
               "-Duser.variant -ea")

assert MI_WANT_W == T("target.mi.files"), "modimport files with work"
assert MI_NONE_W == T("target.mi.none"), "modimport files with none"
assert MI_LINES == T("target.mi.imports"), "module import declarations"
assert len([f for f in MI_WANT if len(MI_WANT[f]) > 1]) == T("target.mi.multi"), \
    "the files needing more than one module"
assert len([f for f in MI_WANT if "java.base" not in MI_WANT[f]]) == \
    T("target.mi.nobase"), "the files needing no java.base"
assert len(set(m for _, m in MI_PAIRS)) == T("target.mi.mods"), "distinct modules"
# EVERY FILE IN SCOPE IS IN ONE HALF OR THE OTHER, which is what makes the
# second half a population rather than an absence: 171 + 151 is the `files=`
# the source gate counted on this tree, so a file that the census forgot would
# be a file C-2 neither asks work of nor forbids work in.
assert MI_WANT_W + MI_NONE_W == BASE.num("files") == GOLD.num("files"), \
    "modimport.txt does not carry a row for every file in scope"
# THE GOLDEN IS THE POPULATION, PAIR FOR PAIR, and this is the assertion that
# says the frozen file and the frozen report were cut from the same tree. C-2's
# set equality is exactly this comparison run against a candidate; running it
# here against the golden is the one place a modimport.txt row that named the
# wrong module would be caught before a fixture spent it.
assert set((site(_b)[0], field(_b, "name=")) for _b in GOLD.recs("modimport")) \
    == MI_PAIRS, "the golden's module imports are not modimport.txt's 188"
assert BASE.num("modimport.size") == T("target.mi.was") == 0, \
    "this rung's seed already declares a module import"

assert JI_DEP_W == T("target.ji.depart"), "jdkimport departures"
assert JI_HOLD_W == T("target.ji.hold"), "jdkimport holds"
assert JI_DEP_W + JI_HOLD_W == T("target.ji.total"), "the two halves are not the 495"
assert JI_HOLD_W == T("target.ji.want"), "the hold is not what the golden must keep"
assert _JI_WHY.get("static") == T("target.ji.hold.static") and \
    _JI_WHY.get("nested") == T("target.ji.hold.nested") and \
    _JI_WHY.get("ambiguous") == T("target.ji.hold.ambiguous"), \
    "the three hold reasons do not add up to the 43"
assert len(set(f for f, _ in JI_DEP | JI_HOLD)) == T("target.ji.files"), \
    "jdkimport files"
# AND THE SAME TWO-ENDED CHECK C-2 GETS. The seed holds all 495 and the golden
# holds exactly the 43, which is the leg's departure and its hold measured
# against the two reports the fixtures are built from rather than asserted.
assert set((site(_b)[0], field(_b, "q=")) for _b in BASE.recs("jdkimport")) \
    == (JI_DEP | JI_HOLD), "the seed's single-type imports are not jdkimport.txt's 495"
assert set((site(_b)[0], field(_b, "q=")) for _b in GOLD.recs("jdkimport")) \
    == JI_HOLD, "the golden's surviving imports are not jdkimport.txt's 43"

assert SV_CONV_W == T("target.sv.conv"), "scopedvalue conversions"
assert SV_HOLD_W == T("target.sv.hold"), "scopedvalue holds"
assert SV_CONV_W + SV_HOLD_W == T("target.sv.fields"), "the two halves are not the 8"
assert BASE.num("scopedvalue.size") == T("target.sv.was") == 0, \
    "this rung's seed already declares a ScopedValue"
assert GOLD.num("scopedvalue.size") - GOLD.num("scopedvalue.decls") == \
    T("target.sv.calls"), "the golden's ScopedValue call sites moved"
# THE DECLARATION COUNTS ARE THE ARITHMETIC OF THE LEG, stated at both ends:
# eight ThreadLocal declarations go in, seven come out, and one ScopedValue
# declaration arrives. A tree that converted a second field would read 6 here.
assert BASE.num("threadlocal.decls") == SV_CONV_W + SV_HOLD_W, \
    "the seed no longer declares eight ThreadLocals"
assert GOLD.num("threadlocal.decls") == SV_HOLD_W and \
    GOLD.num("scopedvalue.decls") == SV_CONV_W, \
    "the golden's declaration counts are not seven and one"

assert CO_PROJ_W == T("target.co.projects"), "the twelve projects"
assert T("target.co.was") == 0, "the seed already passes the flag somewhere"
assert CO_FLAG and " " not in CO_FLAG, "the compact-headers flag is not one token"


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
    # 3: isAccessible() guards gone, the ten surviving setAccessible calls held.
    # TWO WANT-LISTS, one per half, matching test.sh's two setwant() calls: the
    # departure half over ACC_GUARD's 12, the arrival half over ACC_SETACC's 10.
    guard = fbuckets(rep, "accessible", "call=isAccessible")
    setacc = fbuckets(rep, "accessible", "call=setAccessible")
    b3 = minf(minf(clamp01(frac(len([x for x in ACC_GUARD if x not in guard]), B13_GUARDS)),
                   clamp01(frac(len(setacc & set(ACC_SETACC)), B13_SACCB))),
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

    `textblock` is NOT one of them any more. It carries an `in=` as of step
    09's round 1, because the arrival half of the text-block leg is a claim
    about one METHOD -- C-3 when that was written, B-16.3 at this rung -- so it
    reads text blocks through cbuckets() above. This helper keeps `switch`,
    which B-16.5 counts per file, and `sealed`, which B-16.4 does.
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
    """sum of min(have, want) over the want buckets -- TWO functions in the shell.

    Capped per bucket so that a tree which left thirty newline literals in one
    held method cannot pay for three it deleted from another.

    The shell spells this rule twice, over two different file shapes, and one
    python helper serves both: `capsum()` reads the cnt-/nam- files B-16 and
    B-17 count into, and `psum()` reads the set-/cnt- pair B-18 does -- the
    pair that was this rung's own family C one rung ago and is an inherited
    leg now. `psum()`'s own comment says it is capsum()'s rule kept for
    capsum()'s reason. A fixture note about a B-18 leg must name psum(), not
    capsum(), because the block it is describing calls psum().
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


def b17_score(rep, measured, execbroken=()):
    """B-17: step 10's family C, minus its C-1, over the 0.96 that is left.

    Three legs and no fourth. Step 10's C-1 read the class-file major at 65 and
    THIS rung's C-1 reads it at 66; republishing the first here would be two
    gates asking one question of one number, and every rung of this ladder has
    dropped the level leg on the way up for that reason. The three that are
    left keep step 10's own weights -- 0.17, 0.65, 0.14, which that rung's
    calibration measured and this one does not re-argue -- renormalised over
    the 0.96 they carried between them.

    Returns (B17, [B17.2, B17.3, B17.4], gate facts).

    THE POPULATIONS ARE STEP 10'S, STAGED BYTE FOR BYTE: reftest.txt,
    chain.txt, seqfirst.txt and swids.txt cross the rung boundary unedited,
    which is the ladder rule and is checked in the image by diffing all four
    against the step-10 tree's copies rather than by asserting it here.

    WHAT THIS RUNG'S OWN SOLUTION DOES TO THE RECORDS IS A DIFFERENT QUESTION
    and the seed is where it is answered: 22 `reftest` records where step 10
    finished with 35, 19 `seqfirst` where it finished with 26, 7 `seqnew`
    where it started from 0. Those three figures are published by the shell
    as `.was` facts and are denominators of nothing -- C-2 unwraps thirteen
    reflective tests and seven first-element reads out of existence along with
    the ladders that held them, and a bucket that is gone is a bucket neither
    half of any leg here asks about. NOTES 16.20 is where that distinction
    cost an afternoon.

    TWO SPELLINGS OF ZERO AND BOTH ARE MODELLED, as everywhere else in this
    file. B-17.2 and B-17.4 are the literal `0` when their guard does not open,
    because the shell assigns the initialiser and the else branch only prints a
    line. B-17.3 is `0.000000` even then, because its final assignment sits
    OUTSIDE the measured guard: it multiplies by the execution scaling, which
    is read from the behaviour summary and not from the report.
    """
    g = {}

    # ---- B-17.2: reflective type tests became type patterns ----------------
    #
    # THE ARRIVAL HALF UNIONS TWO RECORD KINDS and that is the whole of why it
    # is not simply "a bound ipattern is there". Ten of the thirteen conv sites
    # sit in the four chains B-17.3 asks for a pattern switch over, and a
    # pattern switch emits `swpat` records and no `ipattern` ones; the other
    # three are single tests and are `instanceof` patterns. A leg that read
    # only ipattern would score a correct tree at 3/13 and tell it to un-write
    # the switches.
    if measured:
        left = cbuckets(rep, "reftest")
        arrived = (cnamesets(rep, "ipattern", "type")
                   | cnamesets(rep, "swpat", "types"))
        dep_n = sum(max(0, w - left.get(b, 0)) for b, w in RT_CONV.items())
        hold_n = capsum(RT_HOLD, left)
        arr_n = len([k for k in RT_ARR if k in arrived])
        g["b17.2.departed"] = dep_n
        g["b17.2.departed.want"] = RT_CONV_W
        g["b17.2.arrived"] = arr_n
        g["b17.2.arrived.want"] = len(RT_ARR)
        g["b17.2.held"] = hold_n
        g["b17.2.held.want"] = RT_HOLD_W
        g["b17.2.reftests"] = rep.num("reftest.size")
        g["b17.2.patterns"] = rep.num("ipattern.size")
        g["b17.2.swpatterns"] = rep.num("swpat.patterns")
        b2dep = clamp01(frac(dep_n, RT_CONV_W))
        b2arr = clamp01(frac(arr_n, len(RT_ARR)))
        b2hold = clamp01(frac(hold_n, RT_HOLD_W))
        g["b17.2.depfrac"] = "%.6f" % b2dep
        g["b17.2.arrfrac"] = "%.6f" % b2arr
        g["b17.2.holdfrac"] = "%.6f" % b2hold
        b2 = minf(minf(b2dep, b2arr), b2hold)
        b2s = "%.6f" % b2
    else:
        b2 = 0.0
        b2s = "0"

    # ---- B-17.3: four chains became four pattern switches -------------------
    #
    # ARMS AND NOT CHAINS on the conv side, so a three-arm chain converted is
    # worth more than a two-arm one; CHAINS and not arms on the hold side,
    # because the hold half asks whether one method was left alone and a
    # per-arm denominator there would pay a candidate for converting half of a
    # dispatch it should not have touched at all.
    #
    # `cbuckets("reftest")` A SECOND TIME, and the repetition is the shell's:
    # B-17.2 built the same map a few dozen lines above under a guard on
    # reftest.txt and this one is under a guard on chain.txt, so a missing set
    # file costs ONE leg and not two.
    #
    # THE RESIDUAL IS TWO RECORD KINDS, which was step 10's round-2 correction
    # and is inherited with the leg. Rewrite each chain arm as
    # `if (v instanceof EnterpriseArchive e)` -- which OpenRewrite does
    # unattended on eight of these ten arms -- leave the chain standing and
    # plant the covering switch beside it, and every `reftest` record is gone
    # while the if-chain is still what dispatches. So what is left in the
    # bucket is the reftest records there PLUS how many of that row's types are
    # still named by an `ipattern` there.
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
        g["b17.3.departed"] = dep_n
        g["b17.3.departed.want"] = CH_CONV_W
        # Published and not scored, the shell's spelling: the `instanceof` half
        # of the residual on its own, so a failed run says WHICH old spelling
        # is still there -- the reflective call nothing touched, or the
        # `instanceof` step 10's requirement 4 asked for and its requirement 5
        # did not stop at.
        g["b17.3.ipleft"] = sum(ipleft.values())
        g["b17.3.arrived"] = arr_n
        g["b17.3.arrived.want"] = CH_CONV_W
        g["b17.3.held"] = hold_n
        g["b17.3.held.want"] = CH_HOLD_W
        g["b17.3.switches"] = rep.num("swpat.size")
        g["b17.3.patterns"] = rep.num("swpat.patterns")
        g["b17.3.nullcase"] = rep.num("swpat.nullcase")
        b3dep = clamp01(frac(dep_n, CH_CONV_W))
        b3arr = clamp01(frac(arr_n, CH_CONV_W))
        b3hold = clamp01(frac(hold_n, CH_HOLD_W))
    else:
        # The shell LITERAL `0`: the three sub-terms are initialised above the
        # measured guard and published below it, so a tree nobody could compile
        # publishes the initialiser and not a computed zero.
        b3dep = b3arr = b3hold = 0.0
    g["b17.3.depfrac"] = "%.6f" % b3dep if measured else "0"
    g["b17.3.arrfrac"] = "%.6f" % b3arr if measured else "0"
    g["b17.3.holdfrac"] = "%.6f" % b3hold if measured else "0"

    # `case null` IS NOT SCORED STATICALLY -- NOTES 15.4. What scales this leg
    # is the five recorded dispatches, which is the only reading of the four
    # chains' behaviour on an input no arm matches that is not an author's
    # opinion frozen into a gate. A summary that could not be read scales the
    # leg to zero, which is B-10's rule about a proviso that cannot be checked.
    if execbroken is None:
        b3exec, b3bad = 0.0, len(SWIDS)
    else:
        b3bad = len([x for x in execbroken if x in SWIDS])
        b3exec = clamp01(frac(len(SWIDS) - b3bad, len(SWIDS)))
    g["b17.3.exec.want"] = len(SWIDS)
    g["b17.3.exec.broken"] = b3bad
    g["b17.3.exec"] = "%.6f" % b3exec
    b3 = f6(min(b3dep, b3arr, b3hold) * b3exec)
    b3s = "%.6f" % b3

    # ---- B-17.4: seven guarded first-element reads became getFirst() --------
    #
    # The arrival half counts BUCKETS and the departure half counts SITES, and
    # they are 7 and 7 at this rung only because every conv bucket holds
    # exactly one site. The shell computes its own `sq_arr_w` from the
    # population rather than reusing `sq_dep_w`, and this reimplementation does
    # the same.
    if measured:
        left = cbuckets(rep, "seqfirst")
        first = cbuckets(rep, "seqnew", "op=getFirst")
        dep_n = sum(max(0, w - left.get(b, 0)) for b, w in SQ_CONV.items())
        hold_n = capsum(SQ_HOLD, left)
        arr_n = len([b for b in SQ_CONV if first.get(b, 0) > 0])
        g["b17.4.departed"] = dep_n
        g["b17.4.departed.want"] = SQ_CONV_W
        g["b17.4.arrived"] = arr_n
        g["b17.4.arrived.want"] = len(SQ_CONV)
        g["b17.4.held"] = hold_n
        g["b17.4.held.want"] = SQ_HOLD_W
        g["b17.4.firstreads"] = rep.num("seqfirst.size")
        g["b17.4.sequenced"] = rep.num("seqnew.size")
        g["b17.4.sequenced.read"] = rep.num("seqnew.read")
        b4dep = clamp01(frac(dep_n, SQ_CONV_W))
        b4arr = clamp01(frac(arr_n, len(SQ_CONV)))
        b4hold = clamp01(frac(hold_n, SQ_HOLD_W))
        g["b17.4.depfrac"] = "%.6f" % b4dep
        g["b17.4.arrfrac"] = "%.6f" % b4arr
        g["b17.4.holdfrac"] = "%.6f" % b4hold
        b4 = minf(minf(b4dep, b4arr), b4hold)
        b4s = "%.6f" % b4
    else:
        b4 = 0.0
        b4s = "0"

    # DIVIDE ONCE, AT THE END, as B-13 through B-16 do and for their reason:
    # 0.65/0.96 is not exact to six places, and a composite built from
    # pre-rounded weights drifts from the scorer in the sixth place on any
    # fixture where the three legs disagree.
    tot = f6((0.17 * b2 + 0.65 * b3 + 0.14 * b4) / 0.96)
    return "%.6f" % tot, [b2s, b3s, b4s], g


def b18_score(rep, measured, execbroken=()):
    """B-18: step 11's family C, minus its C-1, renormalised over 0.96.

    The ordinary subtraction and the ordinary reason. Step 11's C-1 asked that
    every class file be major 66; B-4 asks that here, out of `was=66` in
    level.txt, over the same scan -- so a second copy would pay one constant
    twice. What is left is three legs, carried forward whole.

    NO `scan` ARGUMENT, WHICH IS THE VISIBLE HALF OF THAT SUBTRACTION. Step
    11's fc_score took one because its C-1 read the class files. This gate
    reads the source report and nothing else; the scan channel belongs to B-4
    and to this rung's own C-1 now, and a signature that still accepted it
    would let a fixture move a scan and wonder why B-18 did not answer.

    Returns (B18, [B18.2, B18.3, B18.4], gate facts).

    THREE LEGS, EACH A DEPARTURE AND SOMETHING THAT MUST NOT MOVE, and the
    worse answer is the leg. That is the shape step 07 introduced and every
    rung since has kept, and for step 11's work it was the whole defence: JEP
    486's removal has no mechanical replacement, so `stop calling it` and
    `delete the file that called it` produce the same departure count and only
    one of them is a migration. B-18.2's hold is the payload the seven wrappers
    existed to wrap; B-18.3's is the 51 catch parameters a block does read;
    B-18.4's is the ten fields that must stay forced open. B-18.3 AND B-18.4
    ADD AN ARRIVAL ON TOP OF THAT and B-18.2 does not, because there is no
    record kind meaning `this call is not here any more`: the arrival for
    B-18.2 IS its hold, read from the other end.

    B-18.3'S THREE DENOMINATORS ARE NOT THE SAME THREE NUMBERS. Its departure
    is stated over all 35 unread parameters and its arrival over the 28 that
    are outside priv.txt's buckets, because the other 7 leave with the ladders
    B-18.2 unwraps and cannot be asked to arrive as `_` and to survive at once.
    The subtraction is computed from the two frozen files here exactly as
    `pbuckets ... "${WORK}/set-privall.txt"` computes it there.

    THE POPULATIONS ARE STEP 11'S OWN FILES AND THEY ARE STAGED BYTE FOR BYTE,
    which is why everything below still reads
    baseline-step11-{priv,catchparam,varhandle,vhids,accessible}.txt under
    step 11's names. Three rungs running, an inherited population had to be
    re-cut because the rung inheriting it moved one of its own sites; this is
    the first since 09 where the answer is no, and this rung's edit is the
    reason it was asked twice -- 452 import lines deleted and 188 inserted move
    nearly every line number in 171 files and no population key at all, because
    every key here is `<file>` or `<file>#<method>`.

    THE TWO `.was` FACTS ARE PUBLISHED AND SCORED BY NOTHING. They are the
    seed-side figures the two populations were cut from -- 67 sites and 14
    `setAccessible` calls -- read out of target.txt so a reader of the packet
    can see what the tree was handed beside what it kept.

    TWO SPELLINGS OF ZERO AND BOTH ARE MODELLED, as at every rung since 07.
    B-18.2 and B-18.3 are `0` when their guard does not open, because the shell
    assigns the initialiser and the else branch only emits a message. B-18.4 is
    `0.000000` even then, because its final assignment sits OUTSIDE the
    measured guard -- it multiplies by the execution scaling, which is read
    from the behaviour summary and not from the report.
    """
    g = {}

    # ---- B-18.2: the removed API goes and what it wrapped stays ------------
    #
    # TWO TERMS, min()ed, AND THE SECOND ONE IS THE WHOLE REASON THE FIRST IS
    # SAFE. The departure is 67 sites in 27 buckets over 7 files, clamped per
    # bucket. The hold is 13: one reflective construction per file, counted PER
    # FILE and not per site, and the 6 `setAccessible` calls accessible.txt's
    # `setacc` half places inside those same files, counted per METHOD because
    # that is the key accessible.txt froze.
    #
    # THE ASYMMETRY BETWEEN THE TWO HOLD HALVES IS DELIBERATE and it is why
    # they are summed rather than min()ed: a file that grew a second
    # `newInstance` scores the same 1 as before, so a candidate cannot pay for
    # a deleted construction with an added one, while the scanner half is
    # keyed tightly enough that it cannot be paid for at all.
    #
    # NEITHER DENOMINATOR IS TYPED. PRIV_FILE falls out of priv.txt's own first
    # column and PRIV_ACC out of intersecting accessible.txt with it, so
    # narrowing the departure population narrows the hold with it.
    if measured:
        privleft = cbuckets(rep, "priv")
        ctorleft = fcount(rep, "newinstance", "form=ctor")
        accleft = cbuckets(rep, "accessible", "call=setAccessible")
        dep_n = pdep(PRIV_ALL, privleft)
        ctor_n = capsum(PRIV_FILE, ctorleft)
        acc_n = capsum(PRIV_ACC, accleft)
        g["b18.2.departed"] = dep_n
        g["b18.2.departed.want"] = PRIV_DEP_W
        g["b18.2.held"] = ctor_n + acc_n
        g["b18.2.held.want"] = PRIV_CTOR_W + PRIV_ACC_W
        g["b18.2.held.ctor"] = ctor_n
        g["b18.2.held.ctor.want"] = PRIV_CTOR_W
        g["b18.2.held.setacc"] = acc_n
        g["b18.2.held.setacc.want"] = PRIV_ACC_W
        g["b18.2.sites"] = rep.num("priv.size")
        g["b18.2.sites.was"] = T("target.b18.priv.sites")
        b2dep = clamp01(frac(dep_n, PRIV_DEP_W))
        b2hold = clamp01(frac(ctor_n + acc_n, PRIV_CTOR_W + PRIV_ACC_W))
        g["b18.2.depfrac"] = "%.6f" % b2dep
        g["b18.2.holdfrac"] = "%.6f" % b2hold
        b2 = minf(b2dep, b2hold)
        b2s = "%.6f" % b2
    else:
        b2 = 0.0
        b2s = "0"

    # ---- B-18.3: a catch parameter nothing reads is written `_` ------------
    #
    # THREE TERMS, ALL min()ed, OVER THREE DIFFERENT DENOMINATORS -- 35, 28 and
    # 51 -- and the difference between the first two is this leg's entire
    # coupling to B-18.2. The departure is asked of all 35 because a tree that
    # renames the 7 inside the ladders where they stand has done the work; the
    # arrival is asked of the 28 that survive, because the other 7 are deleted
    # by the rung's own solution and a deleted parameter emits no `unnamed`
    # record to be found.
    #
    # THE DEPARTURE HALF ALONE WOULD BE SATISFIED BY DELETION AND THE ARRIVAL
    # IS WHAT CLOSES THAT, the same hole B-18.2's hold closes and closed the same
    # way: drop the catch block and its `unread` record goes, no `unnamed`
    # record appears in the bucket, and the min() reads the arrival.
    if measured:
        cpunread = cbuckets(rep, "catchparam", "kind=unread")
        cpunnamed = cbuckets(rep, "catchparam", "kind=unnamed")
        cpread = cbuckets(rep, "catchparam", "kind=read")
        dep_n = pdep(CP_DEP, cpunread)
        arr_n = capsum(CP_ARR, cpunnamed)
        hold_n = capsum(CP_HOLD, cpread)
        g["b18.3.departed"] = dep_n
        g["b18.3.departed.want"] = CP_DEP_W
        g["b18.3.arrived"] = arr_n
        g["b18.3.arrived.want"] = CP_ARR_W
        g["b18.3.held"] = hold_n
        g["b18.3.held.want"] = CP_HOLD_W
        g["b18.3.unnamed"] = rep.num("catchparam.unnamed")
        g["b18.3.unread"] = rep.num("catchparam.unread")
        g["b18.3.read"] = rep.num("catchparam.read")
        b3dep = clamp01(frac(dep_n, CP_DEP_W))
        b3arr = clamp01(frac(arr_n, CP_ARR_W))
        b3hold = clamp01(frac(hold_n, CP_HOLD_W))
        g["b18.3.depfrac"] = "%.6f" % b3dep
        g["b18.3.arrfrac"] = "%.6f" % b3arr
        g["b18.3.holdfrac"] = "%.6f" % b3hold
        b3 = minf(minf(b3dep, b3arr), b3hold)
        b3s = "%.6f" % b3
    else:
        b3 = 0.0
        b3s = "0"

    # ---- B-18.4: four forced-open fields become VarHandles, and ten do not -
    #
    # THREE STRUCTURAL TERMS AND ONE SCALING, and the arrival denominator is
    # TWICE the departure's: one `privateLookupIn` and one `unreflectVarHandle`
    # per convert bucket, counted and capped SEPARATELY, because a lookup
    # nobody used and a handle with nothing to see are each half a promise. The
    # departure is read off the candidate's `accessible` records rather than
    # its `varhandle` ones, so a handle added beside the reflection it was
    # meant to replace pays nothing.
    #
    # THE HOLD IS THE JUDGEMENT. `unreflectVarHandle` compiles for all
    # fourteen sites; ten of them must not move, and nothing in the language
    # says which. The scaling on top prices the ten's reason: five recordings
    # whose outcome is an `IllegalAccessException`, which is exactly the type a
    # `Lookup` does not throw.
    if measured:
        accleft = cbuckets(rep, "accessible", "call=setAccessible")
        look = cbuckets(rep, "varhandle", "call=privateLookupIn")
        unre = cbuckets(rep, "varhandle", "call=unreflectVarHandle")
        dep_n = pdep(VH_CONV, accleft)
        arr_n = capsum(VH_CONV, look) + capsum(VH_CONV, unre)
        hold_n = capsum(VH_HOLD, accleft)
        g["b18.4.departed"] = dep_n
        g["b18.4.departed.want"] = VH_DEP_W
        g["b18.4.arrived"] = arr_n
        g["b18.4.arrived.want"] = VH_ARR_W
        g["b18.4.held"] = hold_n
        g["b18.4.held.want"] = VH_HOLD_W
        g["b18.4.handles"] = rep.num("varhandle.size")
        g["b18.4.setaccessible"] = rep.num("accessible.size")
        g["b18.4.setaccessible.was"] = T("target.acc.total")
        b4dep = clamp01(frac(dep_n, VH_DEP_W))
        b4arr = clamp01(frac(arr_n, VH_ARR_W))
        b4hold = clamp01(frac(hold_n, VH_HOLD_W))
    else:
        # The shell LITERAL `0` again: B-18.4's three sub-terms are initialised
        # above the measured guard and published below it, so a tree nobody
        # could compile publishes the initialiser.
        b4dep = b4arr = b4hold = 0.0
    g["b18.4.depfrac"] = "%.6f" % b4dep if measured else "0"
    g["b18.4.arrfrac"] = "%.6f" % b4arr if measured else "0"
    g["b18.4.holdfrac"] = "%.6f" % b4hold if measured else "0"

    # A proviso that cannot be checked is NOT satisfied -- B-10's rule. No
    # summary means family A did not run, and a source shape on its own is not
    # evidence that the four sites this leg refuses to convert still fail the
    # way they failed.
    if execbroken is None:
        b4exec, b4bad = 0.0, VH_EXECN
    else:
        b4bad = len([x for x in execbroken if x in VHIDS])
        b4exec = clamp01(frac(VH_EXECN - b4bad, VH_EXECN))
    g["b18.4.exec.want"] = VH_EXECN
    g["b18.4.exec.broken"] = b4bad
    g["b18.4.exec"] = "%.6f" % b4exec
    b4 = f6(min(b4dep, b4arr, b4hold) * b4exec)
    b4s = "%.6f" % b4

    # RENORMALISED OVER THE 0.96 THE THREE CARRIED BETWEEN THEM, and divided
    # rather than restated: their proportions to each other are what step 11's
    # calibration measured (NOTES 16.36), and division is the only operation
    # that keeps all three. 0.26/0.96 = 0.270833 is the figure step 11's
    # calibrator printed as `familyC without C-1` -- the one leg the
    # automigrator shelf reached -- so the number can still be compared with
    # what that measurement said.
    tot = f6((0.46 * b2 + 0.26 * b3 + 0.24 * b4) / 0.96)
    return "%.6f" % tot, [b2s, b3s, b4s], g


def fc_score(rep, scan, measured, jvm):
    """This rung's own family C: 0.02 / 0.38 / 0.33 / 0.25 / 0.02.

    Returns (familyC, [C1, C2, C3, C4, C5], gate facts).

    FIVE LEGS AND THREE CHANNELS, which is one more channel than any rung
    below. C-1 reads the class-file scan, C-2, C-3 and C-4 read the source
    report, and C-5 reads what Gradle said it would hand a test fork -- an
    artefact no rung has scored before, produced by a probe that runs AFTER the
    scored build and can therefore be absent, empty or truncated on its own.
    `jvm` is that artefact: the probe's rows, or None for a probe that printed
    nothing at all.

    THE FOUR POPULATIONS THIS SPENDS ARE `<file>`-KEYED, and the note above
    MI_WANT says why at length: this rung moves nearly every line number in
    171 files and not one population key.

    THREE OF THE FIVE ARE A min() OVER HALVES AND TWO ARE A PLAIN FRACTION,
    and the two are the two that cannot be gamed by deletion. C-1's numerator
    is class files at a level, which a tree cannot raise by removing code
    (removing code lowers both halves of the fraction together); C-5's is
    projects whose every fork carries a flag, which a tree cannot raise by
    removing a project either, because the twelve are frozen and a project
    that forks nothing scores nothing.

    C-1 IS THE ONLY LEG HERE THAT READS EIGHT BYTES WHERE ITS ANCESTORS READ
    FOUR. `majorminor.69.0` and not `major.69`: a `--enable-preview` build
    stamps minor 65535 (JVMS 4.1) and satisfies the major-only reading
    completely. Nothing is subtracted and no multiplier applied -- the preview
    classes simply are not in the numerator, which is the correct arithmetic
    for a fraction of classes at a level -- and the count is published beside
    it so a tree that scores badly is told which of the two reasons it was.

    THE `measured` GUARD COVERS THREE LEGS AND NOT FIVE. C-1's guard is the
    scan and C-5's is the flag and the project list, which are present whether
    or not this tree compiled; the shell's arms are the same three and that is
    not a convenience. A tree whose source gate produced nothing still emitted
    class files on a previous build, and still has a build that can be asked
    what it would fork -- so reading 0 there would be a claim about this tree
    rather than an absence of evidence about it.
    """
    g = {}

    # ---- C-1: every class file is major 69, minor 0 ------------------------
    if scan:
        mm = scan_mm(scan)
        cls = int(scan.get("classes", 0))
        have = mm.get("%d.%d" % (WANT_MAJOR, WANT_MINOR), 0)
        prev = sum(v for k, v in mm.items() if k.endswith(".%d" % PREVIEW_MINOR))
        g["c1.classes"] = cls
        g["c1.at%d.%d" % (WANT_MAJOR, WANT_MINOR)] = have
        g["c1.preview"] = prev
        c1 = frac(have, cls)
        c1s = "%.6f" % c1
    else:
        # THE SHELL PUBLISHES A DIFFERENT FACT HERE RATHER THAN A ZERO ONE, and
        # the model follows it: `gate.c1=no-scan` and no `gate.c1.classes` at
        # all, because a scan that does not exist has no class count to report
        # and a `0` in that field would read as a build that emitted nothing.
        g["c1"] = "no-scan"
        c1 = 0.0
        c1s = "0"

    # ---- C-2: 188 module imports in 171 files, and none in the other 151 ---
    #
    # SET EQUALITY PER FILE AND A SECOND HALF OVER THE FILES WITH NOTHING TO
    # DO. A file is `ok` when the modules it declares are EXACTLY the ones its
    # row names -- one missing or one extra and it scores nothing -- and a file
    # in the `none` half is `clean` when it declares no module import at all.
    # min() of the two is what makes `import module java.base;` at the top of
    # everything worth 0: it would score 152 of 171 on the first half and 0 of
    # 151 on the second.
    #
    # A FILE THE CANDIDATE ADDED IS IN NEITHER HALF. The extras loop below is
    # guarded on `f in MI_WANT` exactly as the shell's is on `a[1] in work`, so
    # a module import in a new file is neither a fault nor a credit.
    if measured:
        got, gotf = set(), set()
        for b in rep.recs("modimport"):
            nm = field(b, "name=")
            if not nm:
                continue
            got.add((site(b)[0], nm))
            gotf.add(site(b)[0])
        bad = set(f for f, m in MI_PAIRS if (f, m) not in got)
        bad |= set(f for f, m in got if f in MI_WANT and (f, m) not in MI_PAIRS)
        ok = len([f for f in MI_WANT if f not in bad])
        cl = len([f for f in MI_NONE if f not in gotf])
        g["c2.files"] = ok
        g["c2.files.want"] = MI_WANT_W
        g["c2.files.was"] = T("target.mi.was")
        g["c2.imports"] = rep.num("modimport.size")
        g["c2.imports.want"] = T("target.mi.imports")
        g["c2.clean"] = cl
        g["c2.clean.want"] = MI_NONE_W
        g["c2.spurious"] = MI_NONE_W - cl
        c2set = clamp01(frac(ok, MI_WANT_W))
        c2cln = clamp01(frac(cl, MI_NONE_W))
        g["c2.setfrac"] = "%.6f" % c2set
        g["c2.cleanfrac"] = "%.6f" % c2cln
        c2 = minf(c2set, c2cln)
        c2s = "%.6f" % c2
    else:
        c2 = 0.0
        c2s = "0"

    # ---- C-3: 452 of the 495 single-type imports go, and 43 stay -----------
    #
    # min(departure, HOLD) over pairs and not over buckets: the question asked
    # of each row is whether THAT import is still written out in THAT file, and
    # two files importing the same type are two answers. The hold is the leg --
    # 25 of the 43 the compiler teaches, loudly, because `Annotation` is
    # ambiguous the moment java.base is imported as a module, and the other 18
    # it does not teach at all.
    if measured:
        got = set()
        for b in rep.recs("jdkimport"):
            q = field(b, "q=")
            if q:
                got.add((site(b)[0], q))
        dep_n = len([k for k in JI_DEP if k not in got])
        hold_n = len([k for k in JI_HOLD if k in got])
        g["c3.departed"] = dep_n
        g["c3.departed.want"] = JI_DEP_W
        g["c3.held"] = hold_n
        g["c3.held.want"] = JI_HOLD_W
        g["c3.total"] = rep.num("jdkimport.size")
        g["c3.total.was"] = T("target.ji.total")
        g["c3.held.static"] = T("target.ji.hold.static")
        g["c3.held.nested"] = T("target.ji.hold.nested")
        g["c3.held.ambiguous"] = T("target.ji.hold.ambiguous")
        c3dep = clamp01(frac(dep_n, JI_DEP_W))
        c3hold = clamp01(frac(hold_n, JI_HOLD_W))
        g["c3.depfrac"] = "%.6f" % c3dep
        g["c3.holdfrac"] = "%.6f" % c3hold
        c3 = minf(c3dep, c3hold)
        c3s = "%.6f" % c3
    else:
        c3 = 0.0
        c3s = "0"

    # ---- C-4: one ThreadLocal becomes a ScopedValue, and seven do not ------
    #
    # THREE HALVES OVER TWO RECORD KINDS. The departure and the hold are read
    # off `threadlocal` declarations and the arrival off `scopedvalue` ones, so
    # a tree that deletes the field scores the departure and loses the arrival,
    # and a tree that adds the ScopedValue beside the ThreadLocal scores the
    # arrival and loses the departure. `form=decl` on both, because both kinds
    # report call sites too and a call is not a declaration.
    if measured:
        tl = set((site(b)[0], field(b, "name=")) for b in rep.recs("threadlocal")
                 if field(b, "form=") == "decl" and field(b, "name="))
        sv = set((site(b)[0], field(b, "name=")) for b in rep.recs("scopedvalue")
                 if field(b, "form=") == "decl" and field(b, "name="))
        dep_n = len([k for k in SV_CONV if k not in tl])
        arr_n = len([k for k in SV_CONV if k in sv])
        hold_n = len([k for k in SV_HOLD if k in tl])
        g["c4.departed"] = dep_n
        g["c4.arrived"] = arr_n
        g["c4.convert.want"] = SV_CONV_W
        g["c4.held"] = hold_n
        g["c4.held.want"] = SV_HOLD_W
        # Size minus declarations, which is the call sites: `scopedvalue`
        # counts a declaration as a record of its own kind too, and the figure
        # this is compared against is the golden's counted the same way.
        # Published and scored by nothing -- a tree that binds correctly with a
        # different number of get() calls is not wrong.
        g["c4.svcalls"] = rep.num("scopedvalue.size") - rep.num("scopedvalue.decls")
        g["c4.svcalls.want"] = T("target.sv.calls")
        g["c4.tldecls"] = rep.num("threadlocal.decls")
        c4dep = clamp01(frac(dep_n, SV_CONV_W))
        c4arr = clamp01(frac(arr_n, SV_CONV_W))
        c4hold = clamp01(frac(hold_n, SV_HOLD_W))
        g["c4.depfrac"] = "%.6f" % c4dep
        g["c4.arrfrac"] = "%.6f" % c4arr
        g["c4.holdfrac"] = "%.6f" % c4hold
        c4 = minf(minf(c4dep, c4arr), c4hold)
        c4s = "%.6f" % c4
    else:
        c4 = 0.0
        c4s = "0"

    # ---- C-5: every test JVM the build forks gets the flag -----------------
    #
    # EVERY Test TASK OF A PROJECT AND NOT ANY. A project whose second Test
    # task was left behind still forks a JVM without the flag, and `any` would
    # score it full; so a project counts only when it printed at least one row
    # and every row it printed carries the flag.
    #
    # THE ROW IS SEARCHED WHOLE, which is how `jvmArgs` and `allJvmArgs` are
    # both honoured by one grep: a flag that arrived through a toolchain or a
    # JAVA_TOOL_OPTIONS is in the second field and not the first, and the
    # question this leg asks is whether the fork gets it.
    #
    # NO `measured` GUARD, AND NO SCAN GUARD EITHER. `jvm` is None when the
    # probe printed nothing -- a build that hangs at configuration time, or
    # one whose `gradlew` is not executable -- and that is the only absence
    # this leg has. It reads 0 legitimately: a build nobody can ask cannot be
    # said to pass the flag.
    g["c5.flag"] = CO_FLAG
    g["c5.projects.want"] = CO_PROJ_W
    # THE SHIPPED FACT IS `gate.c5.probe=<state>,seconds=<n>,limit=<n>` AND
    # THIS MODEL PUBLISHES THE STATE ALONE. The other two fields are wall-clock
    # and a tunable, neither of which a fixture can know; a model that invented
    # them would print a fact the rehearsal could not assert and a reader would
    # believe. The state it can know exactly, because the rehearsal's stub
    # returns non-zero for the fixture that has no jvmargs.txt.
    g["c5.probe.state"] = "ok" if jvm is not None else "failed"
    # THE SHELL'S grep, MODELLED: `grep '^ARQ_TESTJVM ' jvmargs.log` is what
    # separates the rows from everything else Gradle wrote to the same stream,
    # and `gate.c5.tasks` counts what SURVIVED it. A model that counted the
    # file's lines would disagree with the shipped fact for every fixture whose
    # log carries a warning.
    rows = [] if jvm is None else [r for r in jvm if r.startswith("ARQ_TESTJVM ")]
    seen, hit = {}, {}
    for r in rows:
        f = r.split()
        if len(f) < 2 or f[1] not in CO_PROJ:
            continue
        seen[f[1]] = seen.get(f[1], 0) + 1
        if CO_FLAG in r:
            hit[f[1]] = hit.get(f[1], 0) + 1
    okp = len([p for p in CO_PROJ if seen.get(p, 0) > 0 and hit.get(p, 0) == seen[p]])
    g["c5.tasks"] = len(rows)
    g["c5.projects"] = okp
    g["c5.forks"] = sum(seen.values())
    g["c5.forks.flagged"] = sum(hit.values())
    g["c5.projects.was"] = T("target.co.was")
    c5 = clamp01(frac(okp, CO_PROJ_W))
    c5s = "%.6f" % c5

    # On one line, as the shell's is, and in the shell's order.
    tot = f6(0.02 * c1 + 0.38 * c2 + 0.33 * c3 + 0.25 * c4 + 0.02 * c5)
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


# --- and step 11's own five, which no rung below had a helper for -------------
#
# FIVE KINDS AND FOUR LEGS, because C-2 reads three of them: the wrappers it
# removes, the constructions that must survive the removal, and the scanners
# inside the same files. A row that moves one of the three and forgets that the
# other two are keyed off the same population is the defect these resyncs
# exist to make impossible to write by hand.
def priv_resync(rep):
    """`priv` publishes three aggregates and no verdict counter.

    `in=-` IS A BUCKET. Seven of the sixty-seven rows are the
    `implements PrivilegedAction<ClassLoader>` on a class and sit in no method;
    bucket_of keys them `<file>#-` because "-" is not "", pbuckets() does the
    same in the shell, and `priv.methods` counts them. A resync that dropped
    them would publish 20 where the scanner publishes 27.
    """
    n = 0
    files, meths = set(), set()
    for body in rep.recs("priv"):
        n += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("priv.size", n)
    rep.set("priv.methods", len(meths))
    rep.set("priv.files", len(files))
    return rep


def cp_resync(rep):
    """`catchparam` publishes one counter per kind and all three are scored.

    The three kinds are C-3's three halves read from one population --
    `unread` is what must leave, `unnamed` is what must arrive, `read` is what
    must not move -- so a mutation that changes a kind changes two counters at
    once and this is the only place that is arithmetic rather than bookkeeping.
    """
    n = unread = unnamed = read = 0
    files, meths = set(), set()
    for body in rep.recs("catchparam"):
        n += 1
        k = field(body, "kind=")
        if k == "unread":
            unread += 1
        elif k == "unnamed":
            unnamed += 1
        elif k == "read":
            read += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("catchparam.size", n)
    rep.set("catchparam.unnamed", unnamed)
    rep.set("catchparam.read", read)
    rep.set("catchparam.unread", unread)
    rep.set("catchparam.methods", len(meths))
    rep.set("catchparam.files", len(files))
    return rep


def vh_resync(rep):
    """`varhandle` reads ZERO in the seed, which is what makes C-4's arrival a
    measurement. `varhandle.arrival` counts `form=arrival` and the scanner
    emits no other form at this rung; it is counted rather than copied from
    `size` so a row that invents one would show up as a disagreement between
    two keys instead of as nothing at all."""
    n = arr = 0
    files, meths = set(), set()
    for body in rep.recs("varhandle"):
        n += 1
        if field(body, "form=") == "arrival":
            arr += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("varhandle.size", n)
    rep.set("varhandle.arrival", arr)
    rep.set("varhandle.methods", len(meths))
    rep.set("varhandle.files", len(files))
    return rep


def acc_resync(rep):
    """`accessible` is read by THREE legs and that is the point of it.

    B-13.3 asks that the twelve `isAccessible` guards are gone and the ten
    surviving `setAccessible` calls are still there; C-2 asks that six of those
    ten -- the scanners inside the seven wrapper files -- survive the unwrap;
    C-4 asks that four OTHER calls left and that the ten did not. So a row that
    deletes one `setAccessible` record moves up to three columns, and it is
    supposed to: the tree it describes is one tree.
    """
    n = isa = can = sa = 0
    files, meths = set(), set()
    for body in rep.recs("accessible"):
        n += 1
        c = field(body, "call=")
        if c == "isAccessible":
            isa += 1
        elif c == "canAccess":
            can += 1
        elif c == "setAccessible":
            sa += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("accessible.size", n)
    rep.set("accessible.isaccessible", isa)
    rep.set("accessible.canaccess", can)
    rep.set("accessible.setaccessible", sa)
    rep.set("accessible.methods", len(meths))
    rep.set("accessible.files", len(files))
    return rep


def ni_resync(rep):
    """`newinstance` is step 06's population and C-2's hold half at once: one
    `form=ctor` construction per wrapper file is what says the ladder was
    unwrapped rather than deleted. `newinstance.ctor` is also B-13.2's floor
    term, so a deleted construction is charged twice over -- once by the rung
    that asked for it and once by the rung that asks it to stay."""
    n = leg = ctor = 0
    files, meths = set(), set()
    for body in rep.recs("newinstance"):
        n += 1
        f = field(body, "form=")
        if f == "legacy":
            leg += 1
        elif f == "ctor":
            ctor += 1
        files.add(site(body)[0])
        bk = bucket_of(body)
        if bk is not None:
            meths.add(bk)
    rep.set("newinstance.size", n)
    rep.set("newinstance.legacy", leg)
    rep.set("newinstance.ctor", ctor)
    rep.set("newinstance.methods", len(meths))
    rep.set("newinstance.files", len(files))
    return rep


def mi_resync(rep):
    """`modimport` reads ZERO in the seed, like `varhandle` one rung ago and
    for the same reason: the construct is not in the language the input tree
    was written for, so C-2's set half is scored against a measured absence
    rather than against a threshold. Both counters are derived from the
    records, so a fixture that adds a declaration and forgets to move the count
    is not expressible here."""
    n = 0
    files = set()
    for body in rep.recs("modimport"):
        n += 1
        files.add(site(body)[0])
    rep.set("modimport.size", n)
    rep.set("modimport.files", len(files))
    return rep


def ji_resync(rep):
    """`jdkimport` is the one population BOTH of whose ends are measured -- 495
    rows in the seed, 43 in the golden -- and C-3 reads the difference from
    both sides at once.

    `ambig=` IS A CANDIDATE COUNT AND NOT A FLAG, which is the one thing about
    this record a reader gets wrong and the one thing a resync has to get
    right. `ambig=1` says the simple name resolves to exactly one exported type
    under the module imports its file declares, which is the row that DEPARTS;
    `ambig=3` says three, which is the row that cannot. So `jdkimport.ambiguous`
    counts the rows at two or more and reads 25 in BOTH reports: the 25
    `Annotation` imports are ambiguous whether or not the 452 unambiguous ones
    have gone, and a scorer that read the key as "how many are still wrong"
    would read the golden as a failure. Static and nested rows carry `ambig=0`,
    because the question is not asked of them.
    """
    n = amb = 0
    kinds = {}
    files = set()
    for body in rep.recs("jdkimport"):
        n += 1
        k = field(body, "kind=")
        kinds[k] = kinds.get(k, 0) + 1
        a = field(body, "ambig=")
        if a and int(a) >= 2:
            amb += 1
        files.add(site(body)[0])
    rep.set("jdkimport.size", n)
    rep.set("jdkimport.files", len(files))
    for k in ("type", "nested", "static", "ondemand"):
        rep.set("jdkimport." + k, kinds.get(k, 0))
    rep.set("jdkimport.ambiguous", amb)
    return rep


def _decl_resync(rep, kind):
    """size, files and DECLS, which is the only one of the three C-4 scores.

    Both of C-4's kinds report call sites as records of the same kind as the
    declaration they read, so `size` is declarations plus calls and the leg
    reads neither of those without the split. One body for two kinds, because
    the two reports have the same shape and a second copy would be a second
    place for the split to drift.
    """
    n = d = 0
    files = set()
    for body in rep.recs(kind):
        n += 1
        if field(body, "form=") == "decl":
            d += 1
        files.add(site(body)[0])
    rep.set(kind + ".size", n)
    rep.set(kind + ".files", len(files))
    rep.set(kind + ".decls", d)
    return rep


def tl_resync(rep):
    """`threadlocal` is the one population this rung SHRINKS on purpose: 64
    rows and 8 declarations in the seed, 57 and 7 in the golden, and the row
    that left is the field C-4 asks for. `decls` is what the leg reads; `size`
    is published so that a reader can see the six calls left with it."""
    return _decl_resync(rep, "threadlocal")


def sv_resync(rep):
    """`scopedvalue` reads ZERO in the seed, and `decls` is C-4's arrival half
    outright -- one declaration against a measured nothing. `size` minus
    `decls` is the call count the block publishes as `gate.c4.svcalls` and
    scores with nothing: a tree that binds the right field with a different
    number of reads inside the extent is not wrong."""
    return _decl_resync(rep, "scopedvalue")


# THE LAST FOUR ARE THIS RUNG'S OWN AND TWO OF THEM ARE LOAD-BEARING TWICE.
# SEED_OLD is ("jdkimport", "threadlocal"), so restore_old() sources both out
# of BASE and calls the resync registered here on the way out; a kind missing
# from this table raises a KeyError inside a mutator rather than shipping a
# fixture whose counters disagree with its own records.
_RESYNC = {"reftest": rt_resync, "seqfirst": sq_resync, "seqnew": sq_resync,
           "swpat": swp_resync, "priv": priv_resync, "catchparam": cp_resync,
           "varhandle": vh_resync, "accessible": acc_resync,
           "newinstance": ni_resync, "modimport": mi_resync,
           "jdkimport": ji_resync, "threadlocal": tl_resync,
           "scopedvalue": sv_resync}
assert set(SEED_OLD) <= set(_RESYNC), \
    "a kind this rung's solution removes has no resync to restore it through"


_DOORS = (("PRIOR11", PRIOR11), ("PRIOR10", PRIOR10))
_DOORMEMO = {}


def door(kind):
    """WHICH RUNG DOWN CARRIES THE OLD SPELLING OF <kind>, MEASURED.

    NOTES 16.22's rule made mechanical, and made mechanical HERE rather than at
    the call sites, because "which rung" is the exact thing a call site gets
    wrong. A row that asks "what if the candidate left the old spelling in
    beside the new one" needs a tree that HAS the old spelling, and a tree has
    it only until the rung that removed it ran. The nearest door carrying a
    record of this kind that BASE does not carry -- line number cut off, which
    is the normalisation both ladder joints are asserted under -- is that rung,
    and nothing else is.

    ONE RUNG IS NOT ALWAYS THE ANSWER AND AT STEP 11 IT ALWAYS WAS, which is
    why this is a function now. Step 11 removed `priv`, the unread spelling of
    `catchparam` and four `accessible` calls, so those three answer PRIOR11.
    Nothing since step 10 has touched `reftest` or `seqfirst`, so PRIOR11 offers
    them nothing new and they answer PRIOR10 -- two rungs down, which is where
    step 11's single hard-coded door would now be pointing one rung short.

    A KIND WITH NO DOOR RAISES rather than returning an empty list. Step 11's
    version could not: it read a fixed report, and a kind that report had
    nothing for silently restored nothing and shipped a byte copy of the golden
    with a note claiming a regression -- 1.000000 in every cell of the table
    that exists to catch exactly that. Four of fifteen rows shipped that way.
    `seqnew` and `swpat` are the live example: both ARRIVED at step 10, so no
    door below has them and no row may claim to put one back. They are asserted
    at the ladder joint and restored by nobody.
    """
    if kind in _DOORMEMO:
        return _DOORMEMO[kind]
    _cutb = lambda b: b.split(":", 1)[0] + " " + b.split(None, 1)[1]
    mine = set(_cutb(b) for b in BASE.recs(kind))
    for name, rep in _DOORS:
        if set(_cutb(b) for b in rep.recs(kind)) - mine:
            _DOORMEMO[kind] = (name, rep)
            return _DOORMEMO[kind]
    raise KeyError(
        "no rung below carries an old spelling of %s that this rung's seed "
        "lacks -- a row restoring it would restore nothing (NOTES 16.22)" % kind)


def prior_recs(kind, bucket):
    """THE RUNG BELOW's records of one kind for one bucket, at their lines.

    seed_strnl's rule, generalised to five more kinds AND POINTED DOWN THE
    LADDER BY door(), which is the correction that put it here. A row that asks
    "what if the candidate left the old spelling in beside the new one" needs
    the OLD SPELLING, and this rung's input tree is step 11's output, in which
    steps 10 and 11 between them already removed every one of them. Ten of the
    thirteen reflective type tests, both index-0 reads, all 67 doPrivileged
    sites, the 39 unread catch parameters, the four converted setAccessible
    calls: gone from BASE. Step 10 read them out of BASE and step 11 inherited
    the call with one fixed door, so four of fifteen rows shipped as byte copies
    of the golden with notes claiming a regression -- 1.000000 in every cell of
    the table that is supposed to be the instrument that catches exactly that.
    NOTES 16.22.

    These records are measured all the same -- SourceScan produced them, on the
    tree that rung was handed, and every field is that tree's. What they are not
    is CURRENT: the rungs in between shift a line or two in the files they
    touch, so a restored record can sit one line off where this rung's scan
    would have put it. Nothing reads the line (buckets come from `in=`, and the
    grader counts records per bucket), the line-stripped populations are
    asserted equal at both ladder joints where the doors are loaded, and the
    alternative -- synthesising a line -- is the thing seed_strnl's rule exists
    to refuse.
    """
    return [b for b in door(kind)[1].recs(kind) if bucket_of(b) == bucket]


def restore_prior(rep, kind, bucket, howmany=None):
    """Put one bucket's records from the rung below back: the OLD spelling.

    The have-it-already test is the body WITHOUT its line number, which is the
    normalisation both ladder joints are asserted under: a record restored out
    of a door and the same record already in the report differ in the line and
    in nothing else, and comparing whole bodies would add a duplicate every
    time.
    """
    def cut(b):
        return b.split(":", 1)[0] + " " + b.split(None, 1)[1]
    mine = set(cut(b) for b in rep.recs(kind) if bucket_of(b) == bucket)
    add = [b for b in prior_recs(kind, bucket) if cut(b) not in mine]
    if howmany is not None:
        add = add[:howmany]
    rep.setrecs(kind, sorted(rep.recs(kind) + add))
    _RESYNC[kind](rep)
    return len(add)


def restore_as_ipattern(rep, bucket):
    """One conv chain's input-tree reflective tests, back as `instanceof`
    PATTERNS over the same types at the same lines.

    N15's mutation and nothing else's, and the difference from restore_prior is
    the whole of what that row is about. restore_prior puts a bucket back the
    way the rung below had it, which is the tree that did nothing; this puts it
    back the way requirement 4 ALONE leaves it -- `X.class.isInstance(v)`
    rewritten to `v instanceof X x`, which is a language type test and so emits
    an `ipattern` record where the reflective call emitted a `reftest` one.
    Either way the chain is still a chain and still what dispatches.

    The lines and the types are TWO RUNGS BELOW's, for prior_recs' reason: this
    rung's input tree has no reflective test left in any of these four buckets
    to rewrite, so there is nothing here to read out of BASE, and a residual at
    an invented line is a tree no scan ever produced. `kind=`
    follows the rule the record itself carries and the rule step 09 graded --
    an arm whose value was cast after the test binds a pattern variable, an arm
    that only asked binds nothing -- and `conv=no`, because a pattern is not a
    convertible instanceof. None of these four buckets is in step 09's ipattern
    population, so B-16.2's two capsum halves cannot see any of this; its
    total-site term can, and is clamped.
    """
    meth = bucket.split("#", 1)[1]
    add = []
    for body in prior_recs("reftest", bucket):
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
    converted", which B-17.3's hold half is the only thing that sees. Planted
    in the same FILE as a conv chain but a different method it says "a switch
    arrived, somewhere", which is L14's lesson carried forward: every one of
    B-17's arrival halves keys on `file#method` and not on file.
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


# --- the legs' own mutators, and the doors they read the old spelling from
#
# WHICH SIDE OF THE LADDER A ROW SITS ON NO LONGER SAYS WHICH DOOR IT READS,
# and that is the correction this rung forced. A row that puts a construct back
# is describing a candidate who did not do some rung's work, and the old
# spelling is in the tree that candidate was handed -- which tree that is
# depends on the CONSTRUCT and not on the series the row was filed under. Step
# 11 could spell the rule as a preference, because its own removals were in its
# own seed and everything else was exactly one rung down; here `jdkimport` and
# `threadlocal` are in the seed, `priv`, `catchparam`'s unread spelling and the
# four converted `accessible` calls are one rung down, and `reftest` and
# `seqfirst` are two. So restore_old() ASKS -- SEED_OLD first, then door(),
# which measures -- rather than asserting the caller reached for the right
# helper, and the mistake NOTES 16.22 records is not reachable from a call site
# at all.
#
# THE OLD NAMES FOR THE CATCH PARAMETERS, keyed the way the two reports can be
# joined: bucket and exception type, because the line moved and the name is
# exactly what the conversion changes. `pae`, `cce`, `e`. THEY COME FROM THE
# DOOR AND NOT FROM THE SEED, for the same reason and with sharper teeth: step
# 11 removed the unread spelling, so its input has 39 unread parameters and
# this rung's seed has 4 -- the test-scope ones its population never covered --
# and a table built from the seed would hand recatch() the fallback "e" for 24
# of the 28 buckets it can move. 37 keys and not 39: CDIInjectionEnricher
# catches `Exception` three times in one method, under three names, and a table
# keyed by bucket and type keeps the last. Nothing in the scoring reads `name=`
# -- C-3 keys on `kind=` alone -- and the fixtures carry the real names anyway,
# because a report a human opens beside the tree is the whole reason these
# records are records and not counters.
SEED_CPNAME = {}
for _b in door("catchparam")[1].recs("catchparam"):
    if field(_b, "kind=") == "unread":
        SEED_CPNAME[(bucket_of(_b), field(_b, "type="))] = field(_b, "name=")
assert len(SEED_CPNAME) == 37, \
    "the catchparam door offers %d named unread buckets, not 37" % len(SEED_CPNAME)
assert not [_b for _b in BASE.recs("catchparam")
            if field(_b, "kind=") == "unnamed"
            and (bucket_of(_b), field(_b, "type=")) not in SEED_CPNAME], \
    "a catch parameter this rung's seed shows as `_` has no name at the door"


def restore_old(rep, kind, buckets, howmany=None):
    """The OLD spelling of a construct, for named buckets, put back.

    THE DOOR IS DERIVED AND NOT PASSED: this rung's own seed for the kinds THIS
    rung removes, door() for the kinds a rung below did, and a kind no rung
    below carries raises in there rather than restoring nothing out here.
    restore_prior() is the same rule for a single bucket.

    The have-it-already test cuts the line number off for restore_prior's
    reason: two reports on either side of a rung number the same method
    differently, and a record that is already there under another line is
    already there.
    """
    src = BASE if kind in SEED_OLD else door(kind)[1]
    want = set(buckets)

    def cut(b):
        return b.split(":", 1)[0] + " " + b.split(None, 1)[1]

    mine = set(cut(b) for b in rep.recs(kind))
    add, taken = [], {}
    for b in src.recs(kind):
        k = bucket_of(b)
        if k not in want or cut(b) in mine:
            continue
        if howmany is not None and taken.get(k, 0) >= howmany:
            continue
        taken[k] = taken.get(k, 0) + 1
        add.append(b)
    rep.setrecs(kind, sorted(rep.recs(kind) + add))
    _RESYNC[kind](rep)
    return len(add)


def recatch(rep, buckets, frm, to, howmany=None):
    """Move catch parameters between kinds IN PLACE, `howmany` per bucket.

    In place and not delete-plus-add, because the two counters this moves are
    two halves of one leg: a parameter that went from `unread` to `unnamed` has
    departed AND arrived, and one that a careless candidate renamed while it was
    still being read has arrived while breaking the hold. A helper that dropped
    the old record and appended a new one would let a row move one counter
    without the other, which is a tree the scanner cannot produce.

    `name=` moves with `kind=`: `_` on the way to unnamed, and the seed's own
    name on the way back, out of SEED_CPNAME.
    """
    taken, n = {}, [0]

    def f(b):
        k = bucket_of(b)
        if k not in buckets or field(b, "kind=") != frm:
            return b
        if howmany is not None and taken.get(k, 0) >= howmany:
            return b
        taken[k] = taken.get(k, 0) + 1
        n[0] += 1
        nm = "_" if to == "unnamed" else \
            SEED_CPNAME.get((k, field(b, "type=")), "e")
        return b.replace("name=%s " % field(b, "name="), "name=%s " % nm) \
                .replace("kind=%s " % frm, "kind=%s " % to)

    rep.maprecs("catchparam", f)
    cp_resync(rep)
    return n[0]


def drop_catch(rep, buckets, kind, howmany=None):
    """Delete catch parameters of ONE kind out of named buckets.

    The catch block went, not the parameter's name. `kind` is not optional,
    because the three kinds are C-3's three halves and a helper that deleted
    whichever record happened to be first would move a different half from one
    run to the next.
    """
    taken, n = {}, [0]

    def f(b):
        k = bucket_of(b)
        if k not in buckets or field(b, "kind=") != kind:
            return b
        if howmany is not None and taken.get(k, 0) >= howmany:
            return b
        taken[k] = taken.get(k, 0) + 1
        n[0] += 1
        return None

    rep.maprecs("catchparam", f)
    cp_resync(rep)
    return n[0]


def add_ctor(rep, bucket, howmany=1, line=9500):
    """More `Constructor.newInstance` calls in a file than the file had.

    Written for exactly one claim and it is C-2's: the construction hold is
    counted PER FILE and capped there, so a second construction in one file
    cannot pay for a deleted one in another. B-13.2's floor term is a bare
    count over the whole tree and CAN be paid that way, and the row below is
    the one place the two arithmetics are made to disagree on the same tree.
    """
    f, m = bucket.split("#", 1)
    add = ["%s:%d form=ctor in=%s" % (f, line + i, m) for i in range(howmany)]
    rep.setrecs("newinstance", sorted(rep.recs("newinstance") + add))
    ni_resync(rep)
    return len(add)


def drop_vh(rep, buckets, call=None, howmany=None):
    """Delete VarHandle arrival records, optionally only one of the two calls.

    C-4's arrival denominator is TWICE its departure's -- one `privateLookupIn`
    and one `unreflectVarHandle` per convert bucket, capped separately -- so the
    `call` filter is what makes a row that builds half a conversion writable. A
    lookup nobody unreflects is the half-promise that denominator exists to
    price.
    """
    taken, n = {}, [0]

    def f(b):
        k = bucket_of(b)
        if k not in buckets or (call is not None and field(b, "call=") != call):
            return b
        if howmany is not None and taken.get(k, 0) >= howmany:
            return b
        taken[k] = taken.get(k, 0) + 1
        n[0] += 1
        return None

    rep.maprecs("varhandle", f)
    vh_resync(rep)
    return n[0]


def add_vh(rep, bucket, line=9400, calls=("privateLookupIn", "unreflectVarHandle")):
    """Build a VarHandle in a bucket that did not have one.

    Two rows and not one by default, because that is what a conversion emits.
    Used two ways below: beside reflection that is still there, which is the
    tree C-4's departure half reads off `accessible` precisely to refuse, and in
    a HELD bucket, which is the over-conversion the hold half refuses.
    """
    f, m = bucket.split("#", 1)
    add = ["%s:%d form=arrival call=%s in=%s" % (f, line, c, m) for c in calls]
    rep.setrecs("varhandle", sorted(rep.recs("varhandle") + add))
    vh_resync(rep)
    return len(add)


# --- this rung's own four populations -----------------------------------------
#
# THE FIRST MUTATORS SINCE STEP 07 THAT DO NOT KEY ON A BUCKET. `<file>#<method>`
# is the right key for a construct that lives inside a method, and every
# population from 09 to 11 does; these four live at the top of a file or at a
# field declaration, so bucket_of() collapses them -- every `jdkimport` row in
# a file answers to `<file>#<file>` and every declaration to `<file>#-`. A row
# that wants ONE import out of a file of thirty cannot say so in that language,
# and most rows below want exactly that. So they are keyed the way the LEGS are
# keyed: `<file>` and a module name for C-2, `<file>` and a qualified name for
# C-3, `<file>` and a field name for C-4. NOTES 16.37's rule, which is the same
# rule the populations were frozen under.
#
# AND THE LINE NUMBERS THEY WRITE ARE OUT OF THE WAY ON PURPOSE. A record added
# here lands at 9600 or 9700 rather than at a plausible line, for seed_strnl's
# reason: nothing at this rung reads a line number, so an invented one that
# looked real would be a claim the fixture cannot support, and one that reads
# 9600 announces itself to a human diffing two reports.


def drop_modimport(rep, keys):
    """Module import declarations that never arrived. `keys` are (file, module)
    pairs -- C-2's own key -- and the return is how many rows went, which is
    not len(keys) when the row was already absent."""
    want = set(keys)
    n = [0]

    def f(b):
        if (site(b)[0], field(b, "name=")) in want:
            n[0] += 1
            return None
        return b

    rep.maprecs("modimport", f)
    mi_resync(rep)
    return n[0]


def add_modimport(rep, keys, line=9600):
    """Module imports a file did not need.

    Spends three ways below and the three are different faults: an extra
    module in a file that needed one (the set half), a module in a file that
    needed none (the clean half), and a module in a file the population has
    never heard of (neither half, and the row that asserts the guard). One
    helper, because the report cannot tell the three apart -- only the leg can.
    """
    have = set((site(b)[0], field(b, "name=")) for b in rep.recs("modimport"))
    add = ["%s:%d name=%s in=%s" % (f, line, m, f)
           for f, m in sorted(set(keys)) if (f, m) not in have]
    rep.setrecs("modimport", sorted(rep.recs("modimport") + add))
    mi_resync(rep)
    return len(add)


def drop_jdkimport(rep, keys):
    """Single-type imports deleted. `keys` are (file, qualified name) pairs.

    The helper the sed-shaped wrong answer is written with: C-3's hold is 43
    rows out of 495 and every one of the four rows below that spends this is
    deleting rows a module import does not reach.
    """
    want = set(keys)
    n = [0]

    def f(b):
        if (site(b)[0], field(b, "q=")) in want:
            n[0] += 1
            return None
        return b

    rep.maprecs("jdkimport", f)
    ji_resync(rep)
    return n[0]


def restore_jdkimport(rep, keys):
    """Single-type imports that never departed, out of this rung's own seed.

    BASE AND NOT door(), and not because of a judgement call: `jdkimport` is in
    SEED_OLD, so restore_old() would reach the same rows. This exists beside it
    because restore_old() keys on buckets and every jdkimport row in a file
    shares one, so it can say "forty imports from this file" and cannot say
    "these forty imports". The rows below want the second sentence.
    """
    want = set(keys)
    mine = set((site(b)[0], field(b, "q=")) for b in rep.recs("jdkimport"))
    add = [b for b in BASE.recs("jdkimport")
           if (site(b)[0], field(b, "q=")) in want
           and (site(b)[0], field(b, "q=")) not in mine]
    rep.setrecs("jdkimport", sorted(rep.recs("jdkimport") + add))
    ji_resync(rep)
    return len(add)


def _drop_decl(rep, kind, keys):
    """A FIELD DECLARATION out of one of C-4's two kinds, and only the
    declaration. The call sites stay where they are: the leg reads `form=decl`
    on both kinds, a converted field's reads are still reads, and a helper that
    deleted them would be describing a tree that removed the field's users as
    well as the field."""
    want = set(keys)
    n = [0]

    def f(b):
        if field(b, "form=") != "decl":
            return b
        if (site(b)[0], field(b, "name=")) in want:
            n[0] += 1
            return None
        return b

    rep.maprecs(kind, f)
    _RESYNC[kind](rep)
    return n[0]


def drop_tl(rep, keys):
    """A ThreadLocal declaration that is not there any more -- converted away,
    or deleted outright. Which of the two it was is the arrival half's
    question and not this one's."""
    return _drop_decl(rep, "threadlocal", keys)


def drop_sv(rep, keys):
    """A ScopedValue declaration that never arrived."""
    return _drop_decl(rep, "scopedvalue", keys)


def restore_tl(rep, keys):
    """The ThreadLocal declaration of a field, back out of the seed. `keys` are
    (file, field name) pairs, which is C-4's key, and the row restored is the
    one the input tree actually had rather than a plausible reconstruction of
    it -- `threadlocal` is in SEED_OLD, so BASE is the door."""
    want = set(keys)
    mine = set((site(b)[0], field(b, "name=")) for b in rep.recs("threadlocal")
               if field(b, "form=") == "decl")
    add = [b for b in BASE.recs("threadlocal")
           if field(b, "form=") == "decl"
           and (site(b)[0], field(b, "name=")) in want
           and (site(b)[0], field(b, "name=")) not in mine]
    rep.setrecs("threadlocal", sorted(rep.recs("threadlocal") + add))
    tl_resync(rep)
    return len(add)


def arrive_sv(rep, keys, line=9700):
    """A field declared as a ScopedValue.

    `mods=private,final` because that is what the golden's one declaration
    carries and a ScopedValue that is not final is not one; nothing reads the
    field, and a fixture that wrote something else would be inviting a reader
    to believe the scanner emits it.
    """
    have = set((site(b)[0], field(b, "name=")) for b in rep.recs("scopedvalue")
               if field(b, "form=") == "decl")
    add = ["%s:%d name=%s decl=FIELD mods=private,final in=- form=decl" % (f, line, n)
           for f, n in sorted(set(keys)) if (f, n) not in have]
    rep.setrecs("scopedvalue", sorted(rep.recs("scopedvalue") + add))
    sv_resync(rep)
    return len(add)


# --- the class-file scan ------------------------------------------------------
# A SEPARATE CHANNEL FROM THE REPORT AND SEPARATELY MUTABLE, which is the shape
# step 07 had and step 08 lost. C-1 reads ${WORK}/scan.txt, written by
# arqgate.Scan over the class files the build left behind; every other leg of
# family C reads the source AST. A tree can move either without the other --
# L1 and L2 are the two rows that say so -- and at step 08 no fixture could
# make that separation because both channels came off one list of jars.
# The golden publishes GOLD_CLASSES and the input tree CLASSES, and the two
# differ here for the first time -- see the note on GOLD_CLASSES above.
#
# A SCAN IS A CLASS COUNT, A TALLY PER MAJOR, AND -- NEW AT THIS RUNG -- HOW
# MANY OF EACH MAJOR CARRY THE PREVIEW MARKER. `preview.<major>` is the second
# number, defaulting to none, and it is spelled as a MAJOR'S SHARE rather than
# as a total because that is the shape the class files have: a tree compiled
# behind `--enable-preview` stamps minor 65535 on the classes it emits AT THE
# LEVEL IT EMITS THEM, and a tree that moved half its projects and turned the
# flag on in one of them has two majors and one of them split.
PREVIEW_MINOR = 65535
GOLD_SCAN = {"classes": GOLD_CLASSES, "major.%d" % WANT_MAJOR: GOLD_CLASSES}
SEED_SCAN = {"classes": CLASSES, "major.%d" % HOLD_MAJOR: CLASSES}


def scan_mm(scan):
    """The `majorminor.<major>.<minor>` tally a scan implies.

    arqgate.Scan keys this map by the STRING `<major>.<minor>` and prints it in
    a TreeMap's order, which is lexicographic over those strings and not
    numeric over either half. The dict returned here is keyed the same way so
    that write_scan() and fc_score() cannot disagree about what a scan says:
    one derivation, spent by the writer and by the scorer.

    TWO MINORS AND NOT SIXTY-FIVE THOUSAND. javac emits minor 0 for every
    release it supports and 65535 for a preview build; nothing in between is
    reachable from this tree, so a major's count splits in two and the split is
    `preview.<major>` of it.
    """
    mm = {}
    for k in scan:
        if not k.startswith("major."):
            continue
        m, n = int(k[6:]), int(scan[k])
        p = int(scan.get("preview.%d" % m, 0))
        assert 0 <= p <= n, "a scan claims more preview classes at %d than it has" % m
        if n - p:
            mm["%d.0" % m] = n - p
        if p:
            mm["%d.%d" % (m, PREVIEW_MINOR)] = p
    return mm


def scan_split(at_target, rest_major=None, total=None):
    """A scan where only part of the tree reached the target major.

    The remainder is reported at `rest_major`, defaulting to the major the
    input tree was already at, because that is what a partial migration
    actually leaves behind: the modules whose build file moved are at 66 and
    the ones whose did not are still at 65.

    `total` is the class count the scan reports, and it defaults to the
    GOLDEN's. A partial level move is a tree that did the rest of the work --
    the seven ladders are gone and their seven nested classes with them -- and
    forgot one subproject's toolchain line, so 739 is the honest denominator.
    A row that wants the input tree's 746 passes it.
    """
    tot = GOLD_CLASSES if total is None else total
    rest = tot - at_target
    d = {"classes": tot, "major.%d" % WANT_MAJOR: at_target}
    if rest:
        d["major.%d" % (rest_major or HOLD_MAJOR)] = rest
    return d


def scan_preview(preview, total=None, at_target=None):
    """A scan of a tree that compiled behind `--enable-preview`.

    Whole-tree by default: every class file is at the target major and
    `preview` of them carry minor 65535. The point of the fixture that spends
    it is that `major.69` reads FULL and `majorminor.69.0` does not, so C-1
    answers a question B-4 cannot -- which is the entire reason this rung reads
    eight bytes where every rung below it read four.
    """
    tot = GOLD_CLASSES if total is None else total
    at = tot if at_target is None else at_target
    d = {"classes": tot, "major.%d" % WANT_MAJOR: at, "preview.%d" % WANT_MAJOR: preview}
    if tot - at:
        d["major.%d" % HOLD_MAJOR] = tot - at
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
    mm = scan_mm(scan)
    with open(os.path.join(d, "scan.txt"), "w") as fh:
        fh.write("classes=%d\n" % int(scan.get("classes", 0)))
        for m in majors:
            fh.write("major.%d=%d\n" % (m, int(scan["major.%d" % m])))
        fh.write("majors.distinct=%d\n" % len(majors))
        # Lexicographic over the `<major>.<minor>` STRING, which is what a
        # TreeMap<String> does and is not the same order numeric keys would
        # give: "66.0" sorts before "69.0" sorts before "69.65535", but a tree
        # holding majors 9 and 66 would print "66.0" first.
        for k in sorted(mm):
            fh.write("majorminor.%s=%d\n" % (k, mm[k]))
        fh.write("majorminors.distinct=%d\n" % len(mm))
        fh.write("preview.classes=%d\n" %
                 sum(v for k, v in mm.items() if k.endswith(".%d" % PREVIEW_MINOR)))


# --- the fifth artefact: what the build says it would hand a test fork --------
#
# NEW AT THIS RUNG AND UNLIKE THE OTHER FOUR. report.txt, answers.txt,
# behavior.summary and scan.txt are all things the verifier's own gates
# produce from the tree; this one is what GRADLE printed when the verifier
# asked it a question, and the verifier asks that question by running the
# build a second time. So the fixture's artefact is the probe's STDOUT, noise
# and all, and the rehearsal's run_unprivileged stub cats it -- returning
# non-zero when the file is absent, which is the only way a fixture can say
# "the probe never answered".
#
# THE ROW SHAPE IS THE INIT SCRIPT'S, NOT THE AUTHORING PROBE'S. The log saved
# under /work/scratch during authoring prints `ARQ_TESTJVM :arquillian-api:test
# <args>` -- path only -- because that probe predates the decision to match on
# the project's own name. The shipped init script prints
# `${p.name} ${p.path}:${t.name}` and the scoring awk keys on $2, so a fixture
# written to the older shape would have every row skipped and would score
# C-5 = 0 while LOOKING like a flagged tree. The rows below are the shipped
# shape; NOTES 16.38.
def _jvm_row(name, path, how):
    """One `ARQ_TESTJVM` line.

    `how` says where the flag appears, and the two halves are the two the init
    script prints: `explicit` is `t.jvmArgs`, what the build set on the task,
    and `all` is `t.allJvmArgs`, that plus everything Gradle adds of its own.
    A flag reaching the fork through a toolchain or a JAVA_TOOL_OPTIONS is in
    the second and not the first, which is why the scorer greps the row whole.
    """
    explicit = CO_FLAG if how in ("both", "explicit") else ""
    allargs = CO_BASEARGS + ((" " + CO_FLAG) if how in ("both", "all") else "")
    # Two spaces where `explicit` is empty, exactly as the init script's
    # interpolation leaves them, because that is what an unflagged project
    # actually printed on this tree.
    return "ARQ_TESTJVM %s %s %s %s" % (name, path, explicit, allargs)


def co_spec(flagged=None, tasks=None):
    """The probe's plan: project -> [(task, how), ...].

    `flagged` names the projects whose forks carry the flag -- None for all
    twelve, a collection of names for some of them, or a dict name -> how when
    a row wants a particular spelling. `tasks` overrides a project's Test
    tasks; the empty list is a project that forks nothing, which is a state
    the shipped diagnostic has its own sentence for.
    """
    if flagged is None:
        how = dict((p, "both") for p in CO_PROJ)
    elif isinstance(flagged, dict):
        how = dict(flagged)
    else:
        how = dict((p, "both") for p in flagged)
    spec = {}
    for p in CO_PROJ:
        for t in (tasks or {}).get(p, ["test"]):
            spec.setdefault(p, []).append((t, how.get(p, "none")))
    return spec


def jvm_rows(spec, strays=(), noise=()):
    """The probe's stdout for that plan, in the twelve's file order.

    `strays` are rows for projects the population does not name -- a subproject
    the candidate added -- which the scorer skips and the `tasks` fact still
    counts. `noise` is anything else the build printed; the verifier greps
    `^ARQ_TESTJVM ` out of the log, so a fixture carrying a Gradle warning
    proves the grep and not the tally.
    """
    rows = list(noise)
    for p in CO_PROJ:
        for t, how in spec.get(p, []):
            rows.append(_jvm_row(p, ":%s:%s" % (p, t), how))
    for p, t, how in strays:
        rows.append(_jvm_row(p, ":%s:%s" % (p, t), how))
    return rows


# The two ends of this channel, as the other four artefacts have. The golden
# sets the flag on all twelve; the input tree is the measured probe log from
# authoring -- twelve projects, one `test` task each, none of them flagged.
GOLD_JVM = jvm_rows(co_spec())
SEED_JVM = jvm_rows(co_spec(flagged=()))
assert len(GOLD_JVM) == len(SEED_JVM) == CO_PROJ_W, "a project printed twice"
assert all(CO_FLAG in r for r in GOLD_JVM), "the golden's probe is missing the flag"
assert not any(CO_FLAG in r for r in SEED_JVM), "the seed's probe carries the flag"


def write_jvmargs(d, rows):
    """jvmargs.txt: the probe's stdout, or no file at all.

    NO FILE IS A REAL STATE AND NOT AN OVERSIGHT. The rehearsal's
    run_unprivileged stub returns 1 when this file is absent, which stands for
    a build that could not be asked -- a configuration-time hang, a `gradlew`
    that is not executable, a reactor that will not evaluate. The shipped block
    scores that at 0 and says so, and the fixture that spends it is the only
    way to rehearse the arm.
    """
    if rows is None:
        return
    with open(os.path.join(d, "jvmargs.txt"), "w") as fh:
        for r in rows:
            fh.write(r + "\n")


# --- the catalogue ------------------------------------------------------------
# Ninety fixtures and the two ends. Each names the ONE thing it moves, and
# the table at the foot of this file is what c-rehearsal.sh asserts. Read the
# `why` text as the fixture's claim and the row as what the shipped blocks must
# say about it.
#
# FIVE POPULATIONS OF FIXTURE AND ONE TABLE.
#
#   K4..K18                    move the namespace, toArray and HttpClient
#                              records -- step 07's family C, inherited as
#                              B-14. K17 is the everything row and K18 reaches
#                              B-13 instead, for reasons their own notes give.
#   K1..K3, K19..K20, M1..M6   move the PUBLISHED JARS and the service-lookup
#                              records -- step 08's family C, inherited as
#                              B-15. K19 and K20 reach B-12 as well.
#   L3..L14                    move the pattern records, the text block, the
#                              sealed hierarchies and the arrow switches --
#                              step 09's family C, inherited as B-16
#   N1..N15                    move the reflective type tests, the four
#                              if-chains and the first-element reads -- step
#                              10's family C, inherited as B-17
#   P1..P15                    move the privileged blocks, the catch
#                              parameters and the VarHandles -- step 11's
#                              family C, inherited as B-18
#   L1..L2, Q1..Q20            move THIS rung's five legs: the class-file scan
#                              at C-1, the module imports at C-2, the
#                              single-type imports at C-3, the ScopedValue at
#                              C-4 and the compact-header flag at C-5
#
# L1 AND L2 KEPT THEIR NAMES AND CHANGED NOTHING ELSE, at step 10 and again
# here. They move ${WORK}/scan.txt, and the scan is still family C's C-1 at
# this rung -- a different major over the same channel -- so they are the only
# two L rows that did not change column. L3..L14 kept their names for the
# opposite reason: they are step 09's rows unedited, and the only thing that
# moved is which family reads them. The N series is step 10's, moved from
# familyC to B-17 the same way, and the P series is step 11's, moved from
# familyC to B-18 at this rung with nothing edited but the total each row
# quotes, with N14 and N15 re-pointed at the rung below
# rather than at the seed because the seed no longer carries what they restore
# (NOTES 16.22). Renaming any of these groups would have churned every
# cross-reference in the rehearsal to say nothing.
#
# THE Q SERIES IS THE ONE THIS RUNG WROTE, AND P IS THE ONE THE RUNG BELOW
# DID. The letters run K, L, M, N, P, Q: O is skipped because it reads as a
# zero in a column of figures. P1..P5 are B-18.2's two halves and its per-file
# cap, P6..P9 are B-18.3's three denominators, P10..P15 are B-18.4's three
# structural terms and the scaling that multiplies them -- fifteen rows that
# read a family-C column one rung ago and a family-B column here. Q1..Q20 are
# this rung's own C-1 minor, C-2, C-3, C-4 and C-5, written for the same reason
# P was written a rung ago: until they existed those legs read 1.000000 on every
# row of this table but K17, and K17 is the row where the report itself is
# missing and every block reads zero -- so nothing in the table distinguished a
# leg that worked from a leg that was never asked. Their own note, above Q1,
# says which row is aimed at which half.
#
# They share a table because they share a run: every fixture is scored by all
# TEN blocks, and the column that must NOT move is as much of the assertion
# as the one that must. It cuts both ways now that the Q series exists: the
# seventy inherited rows assert that family C notices nothing they do, and the
# twenty new ones assert that nine family-B columns notice nothing they do. A family C that noticed a sealed declaration, or a
# B-15 that noticed a module import, would show up here as a moved cell in a
# column the fixture never touched.
#
# THE INHERITANCE IS NOW FIVE RUNGS DEEP AND THAT IS THE POINT OF KEEPING ALL
# FIVE SETS. B-14 is step 07's family C, B-15 is step 08's, B-16 is step 09's,
# B-17 is step 10's and B-18 is step 11's, and the ninth trap is about exactly
# this: a step-k requirement that quietly makes a step-k-5 test unpassable is
# invisible from inside a step-local mean. The only way to see it is to score
# every inherited gate on every fixture, including the fixtures that were
# written for the rung five below.
#
# FIXTURES THAT SHARE A SCORE AND ARE NOT REDUNDANT. L12 and L13 both read
# B-16 at 0.955556; N1 and N2 both read B-17 at 0.959135; N10 and N11 both
# read it at 0.958333; P10 and P11 both read B-18.4 at 0.750000 and B-18 at
# 0.937500. The Q series adds five such groups in one family: Q3 and Q6 at
# 0.997778, Q4 and Q7 at 0.620000, Q14, Q15 and Q16 at 0.750000, Q18 and Q19 at
# 0.998333, and L2, Q1 and Q20 at 0.980000 -- which is what a family of five
# legs at 0.02, 0.38, 0.33, 0.25 and 0.02 does to a table of totals, and is the
# reason the rehearsal asserts the PARTS line and the gate facts on every one of
# them rather than the total alone. They differ in the GATE FACT that says why --
# `gate.b16.5.arrow=1` against `gate.b16.5.arrow=2`, `gate.b17.2.depfrac`
# against `gate.b17.2.arrfrac`, `gate.b17.4.depfrac` against
# `gate.b17.4.arrfrac`, `gate.b18.4.depfrac` against `gate.b18.4.arrfrac` --
# and a
# block that read one channel wrongly would collapse them onto one message
# while keeping the score. The rehearsal asserts the messages as well as the
# numbers, which is the only way that particular defect is visible from
# outside. N2 is the one of the four that is also separable in the table
# itself, because it moves B-16 to 0.960784 on the way past.
#
# N7 AND N8 WERE A FIFTH SUCH PAIR AT 0.675000 AND THEY ARE NOT ONE NOW. The
# fifth swid moved N8's scaling from 2/4 to 3/5 at step 10 and B-17's own
# weights moved both figures again here, so N7 reads 0.661458 and N8 0.729167.
# Nothing about why the pair was written has changed -- `gate.b17.3.holdfrac`
# against `gate.b17.3.exec`, a missing conversion against a broken execution --
# and both rows still assert both facts, because a figure they happen not to
# share any more is a weaker reason to look than the two halves they still
# fail in separately.
#
# SEVEN ROWS OUTSIDE THE K SERIES MOVE MORE THAN ONE COLUMN AND EVERY ONE OF
# THEM HAS TO. N6 retypes one arm
# of one pattern switch and N9 deletes one pattern switch, and B-17.2's
# arrival half reads the `swpat` type set as well as the `ipattern` one, so
# both rows move B-17.2 AND B-17.3 -- two sub-legs inside one column, which is
# why the table shows them as one figure and the b17 parts line shows them as
# two. N2 moves B-16 and B-17. P2, P12 and P15 move B-13 beside B-18 and
# P3 moves B-13 beside TWO of B-18's legs,
# because `newinstance` and `accessible` are read by two rungs each and a
# deleted record is a report both of them can see. None of that is a leak
# between legs; it is one artefact answering two questions, and a fixture that
# moved only one of them would be describing a tree that cannot exist. Their
# `why` text says which half of each leg fell.
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
    # THERE IS NO `ns.import.javax.management` KEY TO RENAME AT THIS RUNG, and
    # that is C-2's doing rather than an omission here. Step 11's golden still
    # imported the 16 javax.management types one at a time, so this fixture
    # renamed the aggregate beside the records it rewrote; step 12's golden says
    # `import module java.management` instead and SourceScan publishes no
    # `ns.import.javax.management` at all -- nor .naming, nor .xml, and
    # `ns.files` reads 16 where the seed reads 17.
    #
    # NONE OF WHICH B-14 CAN SEE. Its hold half counts `ns` RECORDS keyed
    # pkg|bucket, and the 134 records are byte-identical across the two ends
    # -- C-2 moves where a type is imported from and not where it is used. So
    # f() above is still the whole of this fixture's effect on the score, and
    # the key's absence is asserted rather than worked around, because a golden
    # that imported javax.management by name again would mean C-2 had stopped
    # covering those six files and this fixture would then be under-stating
    # itself by one aggregate.
    assert rep.get("ns.import.javax.management", None) is None, \
        "the golden imports javax.management by name again -- rename the key here too"
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
         "literal 0 their blocks assign; B-12e, B-12, B-13, B-14, B-15, B-16, "
         "B-17, B-18 and familyC publish 0.000000, because their totals are "
         "computed by awk outside the guard; and B-13.2..7 and B-14.2..5 "
         "publish NOTHING AT ALL, because their fact lines are inside it. "
         "THREE SPELLINGS OF ZERO IN ONE ROW, which is what makes this fixture "
         "worth more at this rung than at any of the last three. "
         "FAMILY C HAS A SHAPE OF ITS OWN AND IT IS THE REASON: all five of "
         "its legs are the shell LITERAL 0, assigned above their guards and "
         "assigned nowhere below them, and TWO OF THE FIVE READ FULL ANYWAY, "
         "because their guards are not the compiler's. C-1 is 1.000000: it "
         "reads ${WORK}/scan.txt, and the class files this build left behind "
         "are all at major 69 whatever the scorer's own three probes made of "
         "the sources. C-5 is 1.000000: it reads the argument list Gradle "
         "would hand a test fork, and a tree that does not compile still "
         "configures. C-2, C-3 and C-4 are the literal 0 and their gate facts "
         "are not published at all. Family C is 0.02 + 0.02 = 0.040000 "
         "exactly, and NOT ONE LEG OF IT IS EXECUTION-SCALED -- the three "
         "scaled legs at this rung are B-16.3, B-17.3 and B-18.4, one family "
         "over each, so a row carried across from step 10 unchanged would "
         "have named a family-C leg as the scaled one and read wrong in the "
         "spelling as well as in the total. B-16 is the same arrangement one "
         "family over and lands on 0.000000 rather than 0.040000, because "
         "neither the class-file major nor the argument list came across with "
         "it: B-16.2, .4 and .5 are the literal 0, B-16.3 is 0.000000 outside "
         "its guard, and there is no fifth leg to be full. B-17 IS THE SAME "
         "SHAPE AGAIN: B-17.2 and .4 are the literal 0, B-17.3 is 0.000000 "
         "outside its guard, and B-17 is 0.000000. B-18 IS THE THIRD AND IT "
         "IS THE NEW COLUMN: B-18.2 and .3 are the literal 0, B-18.4 is "
         "0.000000 outside its guard, and B-18 is 0.000000. Four families "
         "with the same internal arrangement and two different totals, "
         "because what differs is which leg survives the guard and not the "
         "spelling of the zero. B-15 is a shape of its own again: "
         "B-15.2 reads the twelve published manifests and is full, B-15.3's "
         "three halves are the literal 0, and the composite is 0.35 / 0.80 = "
         "0.437500. A rung whose gates all sat inside one guard could not "
         "produce any of the four numbers.",
         ok25="no", ok24="no", okdef="no")
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
         "B-18 is untouched because B-18.4's four ids are a different "
         "population read out of the same file -- and they are asserted "
         "disjoint from these 69 where the lists are loaded, which is the "
         "check this row read out loud before it was there.",
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
         "and 700 of the 739 class files are at major 66 -- the build file of "
         "one subproject was never moved, so its classes are still at 65. C-1 "
         "is 700/739 = 0.947226 and nothing else in the row moves; familyC is "
         "0.04 x 0.947226 + 0.96 = 0.997889. The leg is a FRACTION and not a "
         "flag for exactly this: a tree that moved fifteen of sixteen build "
         "files has done most of the work and the gate should say most. THE "
         "MAJOR THE REST SIT AT IS THE ONE THE RUNG BELOW WANTED, which is why "
         "this row is not a repeat of step 10's: the tree it describes passed "
         "the previous rung's C-1 outright and still reads 0.947226 here. "
         "THE DENOMINATOR IS 739 AND NOT THE INPUT TREE'S 746, because this "
         "row is a tree that did the rest of the work: the seven "
         "`SecurityActions` ladders are gone and their seven nested privileged "
         "classes with them, and only one toolchain line was missed.",
         scan=scan_split(700))
def _l1(rep, env):
    return "700 of %d class files at major %d, the rest still at %d" % (
        GOLD_CLASSES, WANT_MAJOR, HOLD_MAJOR)


@fixture("L2", "golden", "no class-file scan at all: the build produced no "
         "class output the gate could walk. C-1 is the shell LITERAL 0 under "
         "gate.c1=no-scan, and the other FOUR legs of family C are full "
         "because C-2, C-3 and C-4 read the source AST, which is intact, and "
         "C-5 reads the build's own answer about its test forks, which no "
         "missing class output touches. familyC = "
         "0.980000, which is NOT step 09's 0.900000: that rung weighted its "
         "class-file leg at 0.10 and this one weighs it at 0.02, because the "
         "automated-migrator calibration reached the level outright at both. "
         "THE COMPANION TO K19 "
         "AND K20 FROM THE OTHER SIDE: those two move the jars and leave the "
         "scan, this one moves the scan and leaves the jars, and B-15.2 stays "
         "at 1.000000 in all three.",
         scan=None)
def _l2(rep, env):
    return "no scan.txt; the source report and the published jars intact"


@fixture("L3", "golden", "nine of the fourteen convertible buckets are still "
         "a plain `instanceof` with a cast on the next line -- twelve of the "
         "seventeen sites, because three of those buckets hold two each. The "
         "converted half is 5/17 = 0.294118, the held half and the total-site "
         "term are both full, and the min() takes the converted half: B-16.2 "
         "is 0.294118 and B-16 is 0.843137. A "
         "half-finished sweep, which is what a file-at-a-time migration looks "
         "like when it runs out of context. THE FIGURES MOVED WITH THE "
         "POPULATION AND THE EDIT DID NOT: this rung re-cut ipattern.txt from "
         "28 sites to 24 -- seven `SecurityActions#getConstructor` ladders "
         "leave the tree and take their tests with them -- so the same nine "
         "buckets now cost 5/17 where they cost 9/21 one rung ago.")
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
# --- B-17, WHICH WAS THIS SERIES' OWN FAMILY C ONE RUNG AGO --------------------
# Fifteen rows over three legs. Step 10 wrote them against its C-2, C-3 and
# C-4 and this rung republishes those three as B-17.2, B-17.3 and B-17.4, so
# the rows are the same fifteen mutations read in a different column: what was
# the leg the rung was graded on is now the leg it must not undo. Step 10's
# C-1 is not here -- it read the class-file major at 65 and this rung's own
# C-1 reads it at 66 -- so the three that are left keep step 10's weights over
# the 0.96 they carried between them.
#
# EVERY ROW MOVES B-17 AND NOTHING ELSE, with one deliberate exception: N2
# takes B-16 to 0.960784 as well, because the three `instanceof` patterns it
# deletes are in B-16.2's population AT THIS RUNG even though they were not in
# step 09's. That is the ninth trap read from the inside -- the held columns
# are supposed to be the ones that do not move, and the one that does, does so
# for a reason this file can name.
#
# FAMILY C READS 1.000000 ON ALL FIFTEEN, which is the other half of the same
# assertion: these rows perturb the tree the rung below was graded on and this
# rung's four legs are measured somewhere else entirely.
#
# THE THREE LEGS DIVIDE THEIR ROWS BY HALF AND NOT BY CONSTRUCT. B-17.2 and
# B-17.4 each ask three questions -- did the convertible sites leave, did
# something arrive where they were, did the sites that must not move stay --
# and there is one row per question per leg, plus one that answers the hold
# half by deleting the population instead of converting it. B-17.3 asks two
# structural questions and multiplies by a recorded one, so it has one row per
# condition: too few arms, the wrong type, a switch where there must not be
# one, a switch in the wrong method, a switch beside a chain that never left,
# and five recordings that stopped matching.
#
# WHAT THE TWO `why` FIGURES MEAN. Each row quotes the fraction its half reads,
# the leg the min() then takes, and the B-17 that composes -- (0.17 x B-17.2 +
# 0.65 x B-17.3 + 0.14 x B-17.4) / 0.96 -- so a rehearsal transcription error
# shows up as a disagreement between two numbers in the same sentence rather
# than as a number nobody can check. The familyC figures step 10's notes
# carried are gone rather than rescaled: they were that rung's composition and
# there is no arithmetic that turns them into this one's.
#
# FOUR OF THE FIFTEEN RESTORE OUT OF THE RUNG BELOW AND NOT OUT OF THE SEED.
# N1, N10, N14 and N15 put a construct BACK that the candidate was supposed to
# have removed, and the tree this rung hands the candidate is step 10's golden,
# which has none of them left to restore. See prior_recs() and NOTES 16.22.
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
# of the thirteen convertible reflective tests sit in the four chains B-17.3 asks
# for a pattern switch over; the other three are single tests and become
# `instanceof` patterns. That is why B-17.2's arrival half unions two record
# kinds, and if the golden ever stopped agreeing -- a fourth chain, or a single
# test converted some other way -- N1 and N2 would be describing a tree that
# does not exist. The assertion is here rather than in a comment.
assert len(RT_SINGLE) == 3 and sum(RT_CONV[b] for b in RT_SINGLE) == 3, RT_SINGLE
# ...and the chains are the other ten, which makes RT_SINGLE and RT_CHAIN a
# PARTITION of the thirteen rather than two lists that happen not to overlap.
# N1 moves the three and N14 moves the ten, and between them they cover every
# conv site B-17.2 has: a row that fell out of both would be a departure nothing
# in the table exercises.
assert sorted(set(RT_SINGLE) | set(RT_CHAIN)) == sorted(RT_CONV), \
    "the singles and the chains are not the thirteen conv buckets"
assert sum(RT_CONV[b] for b in RT_CHAIN) == CH_CONV_W, RT_CHAIN
# AND THIS RUNG ASKS THE QUESTION THE OTHER WAY ROUND. One rung ago the check
# was that the golden's NEW `instanceof` patterns are the three singles; here
# both ends already hold them, because step 10 is done, so what is checkable
# is that neither end has LOST them. The seed holding them is step 10's result
# arriving intact; the golden holding them is this rung's own deletion not
# reaching three methods that have nothing to do with doPrivileged. The second
# is the live one: seven methods do leave this tree.
assert set(RT_SINGLE) <= set(bucket_of(b) for b in BASE.recs("ipattern")), \
    "the seed does not hold the three instanceof patterns step 10 wrote"
assert set(RT_SINGLE) <= set(bucket_of(b) for b in GOLD.recs("ipattern")), \
    "this rung's own deletion reaches one of the three single reflective tests"
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
         "min() takes the departure half: B-17.2 is 0.769231 and B-17 is "
         "0.959135. A GATE THAT ONLY ASKED WHETHER THE PATTERN ARRIVED WOULD "
         "PAY THIS TREE IN FULL, and the tree still runs isAssignableFrom on "
         "every call. N2 is the same figure from the other side. "
         "THE THREE RECORDS COME OUT OF THE RUNG BELOW: the tree this rung "
         "hands the candidate has no reflective test left in any of the three "
         "buckets, so there is nothing in it to restore, and a row that tried "
         "would move nothing and score the golden. It did, until NOTES 16.22.")
def _n1(rep, env):
    n = sum(restore_prior(rep, "reftest", b) for b in RT_SINGLE)
    return "%d reflective test(s) still beside the pattern that replaced it" % n


@fixture("N2", "golden", "the three single reflective tests are gone and "
         "nothing replaced them: the branch was rewritten some other way, or "
         "deleted. The departure half is FULL -- 13/13, because departure is "
         "measured by what is no longer in the report -- the arrival half is "
         "10/13 = 0.769231, and the min() takes the arrival half: B-17.2 is "
         "0.769231 and B-17 is 0.959135, the same two figures as N1. "
         "gate.b17.2.depfrac and gate.b17.2.arrfrac are swapped between the two "
         "rows and that is the whole difference; a leg that scored departures "
         "alone, or arrivals alone, would collapse them onto one message. "
         "B-16.2 MOVES TOO AND IT DID NOT ONE RUNG AGO, which is the only "
         "place in this series where a second held column falls and is worth "
         "the sentence. Step 09's ipattern population had twenty-eight sites "
         "and these three buckets were not among them; THIS rung re-cut the "
         "file -- it deletes seven `SecurityActions#getConstructor` ladders "
         "and a population naming them would ask a correct tree for patterns "
         "inside methods it was told to remove -- and the three patterns step "
         "10 wrote are in the twenty-four that were left. So deleting them "
         "costs B-16.2 its conv half as well: gate.b16.2.converted reads 14 "
         "against 17 for 0.823529, gate.b16.2.sites reads 21 against 24 for "
         "0.875000, the hold half is untouched, and B-16 is 0.960784. Both "
         "legs are supposed to notice a deleted pattern and both do; what "
         "would have been wrong is B-16 reading 1.000000 here because a note "
         "written for a different population said it would.")
def _n2(rep, env):
    n = drop_ip(rep, RT_SINGLE)
    return "%d instanceof pattern(s) removed; ipattern.size=%d" % (
        n, rep.num("ipattern.size"))


@fixture("N3", "golden", "the four reflective tests in ManagerImpl were "
         "converted as well -- getContext, getExtension, resolveObservers and "
         "resolveInterceptorObservers, which test a Class VALUE against a "
         "type the caller supplies and cannot be an `instanceof` pattern at "
         "all. The hold half is 10/14 = 0.714286, the other two halves are "
         "full, and B-17.2 is 0.714286 with B-17 at 0.949405. This is the "
         "fixture that makes B-17.2 a min() of three: a sweep that converted "
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
         "B-17.2 is 0.000000 and B-17 is 0.822917. DELETION SCORES AS BADLY "
         "AS LEAVING IT, which is the rule every rung since 07 has kept, and "
         "the hold half is the term that enforces it here: gate.b17.2.reftests "
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
         "B-17.3 is 0.700000 with B-17 at 0.796875. gate.b17.3.patterns reads 9 "
         "against the golden's 10. THE ARM COUNT AND THE TYPE SET ARE TWO "
         "CONDITIONS and this row moves only the first: the types the switch "
         "does name are still named, so a leg that read coverage alone would "
         "score this tree whole. N6 is the mirror.")
def _n5(rep, env):
    n = clip_swpat(rep, CH_CLIP, 2)
    return "v_2_5 packager switch declares %d arms, not 3" % n


@fixture("N6", "golden", "one arm of the beans.xml processor's pattern switch "
         "names the wrong type: `case Archive` where the chain tested for "
         "WebArchive. The arm count does not move -- gate.b17.3.patterns is "
         "still 10 -- and the coverage condition fails, so the two-arm chain "
         "is lost: the conv half is 8/10 = 0.800000 and B-17.3 is 0.800000. "
         "TWO LEGS MOVE HERE AND BOTH MUST. B-17.2's arrival half reads the same "
         "`swpat` type set, so the WebArchive arrival is gone from it too: "
         "12/13 = 0.923077, and B-17.2 is 0.923077. B-17 is 0.850962. This is "
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
         "half is 1/2 = 0.500000, so B-17.3 "
         "is 0.500000 and B-17 is 0.661458. gate.b17.3.switches reads 5 and "
         "gate.b17.3.patterns 12. THE HOLD HALF COUNTS CHAINS AND NOT ARMS, "
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
         "replaced returned. B-17.3 is scaled by 3/5 = 0.600000 and B-17 is "
         "0.729167. THE ROW SEPARATES A BROKEN EXECUTION FROM A MISSING "
         "CONVERSION and that is the whole of what it is for: every static "
         "half of the leg is full -- gate.b17.3.depfrac, gate.b17.3.arrfrac and "
         "gate.b17.3.holdfrac all read 1.000000 -- and the one fact that moved "
         "is gate.b17.3.exec, which MULTIPLIES all three rather than joining "
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
         "nothing: 7/10 = 0.700000 and B-17.3 is 0.700000. gate.b17.3.switches is "
         "still 4 and gate.b17.3.patterns still 10, which is the pair that "
         "separates this row from N5's identical conv half. TWO LEGS AGAIN, "
         "and for the same reason as N6: the three arrivals B-17.2 counted in "
         "that method are gone with the switch, so B-17.2 is 10/13 = 0.769231 "
         "and B-17 is 0.756010. L14 IS THIS LESSON ONE RUNG DOWN -- a text "
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
         "beans.xml processor and the resource enricher -- at the lines the "
         "RUNG BELOW's own scan measured them at, because the tree this rung "
         "hands the candidate no longer has one of them to restore, and the "
         "golden's four switches, ten arms, type "
         "sets and one `case null` are untouched beside them. The departure "
         "half is 0/10 = 0.000000, the arrival half and the hold half are "
         "both full, all five recordings still match so the scaling is "
         "1.000000, and the min() takes the departure half: B-17.3 is 0.000000. "
         "B-17.2 falls with it, because ten of its thirteen conv sites are back "
         "-- 3/13 = 0.230769 -- and B-17 is 0.186699. THIS IS ROUND 1'S "
         "BLOCKER AS A ROW. A pattern switch planted beside a live "
         "`X.class.isInstance(v)` chain is not a conversion: it compiles, the "
         "chain is still what runs, so no recording moves and exec cannot "
         "catch it, and before this leg had a departure half the tree read "
         "0.65 of step 10's family C at full credit for four switches that "
         "converted "
         "nothing. N1 is the same lesson on B-17.2's three standalone sites; "
         "this is the ten in the chains, and the leg the rung is named after.")
def _n14(rep, env):
    n = sum(restore_prior(rep, "reftest", b) for b in RT_CHAIN)
    return ("%d reflective test(s) back beside the four switches that "
            "replaced none of them" % n)


@fixture("N15", "golden", "N14's tree one rewrite further on, and it is round "
         "2's blocker: the four chains are still chains and every arm of them "
         "is now an `instanceof` PATTERN -- `applicationArchive instanceof "
         "EnterpriseArchive e` where the tree step 10 was handed wrote "
         "`EnterpriseArchive.class.isInstance(applicationArchive)` -- with the "
         "golden's four switches, ten arms, type sets and one `case null` "
         "untouched beside them. Not one reflective call is left, so the "
         "departure half as it shipped in round 1 read 10 of 10 on a tree that "
         "converted no dispatch at all. IT COUNTS THE `instanceof` AS THE OLD "
         "SPELLING NOW: all ten chain types are still named one arm at a time "
         "in the four conv buckets, gate.b17.3.ipleft is 10, the residual is 0 "
         "reftests + 10 names = 10 against 10 arms, and gate.b17.3.departed is 0 "
         "-- 0/10 = 0.000000. The arrival half is 10/10, the hold half 2/2, "
         "all five recordings still match so the scaling is 1.000000, and the "
         "min() takes the departure half: B-17.3 is 0.000000. "
         "B-17.2 DOES NOT FALL WITH IT AND THAT IS THE POINT OF THE ROW, which "
         "is where it parts from N14. B-17.2's departure counts reflective tests "
         "and they are all gone -- 13/13 -- and B-17.2's arrival accepts an "
         "`ipattern` naming the type as readily as a switch arm, so the ten "
         "chain types arrive twice over and the three standalone sites arrive "
         "as the golden's: 13/13, hold 14/14, B-17.2 = 1.000000. B-17 is "
         "(0.17 + 0.65 x 0 + 0.14) / 0.96 = 0.322917, WHICH IS SWALL's FIGURE "
         "IN THE SAME COLUMN and the one collision in this table a doctored "
         "summary can reach -- the Dockerfile's coincidence list names it, and "
         "gate.b17.3.exec.broken separates the two. THE ATTACK IS NOT "
         "HYPOTHETICAL AND THAT IS WHY IT HAD "
         "TO BE PRICED: requirement 4 asks for this rewrite in its own right, "
         "B-17.2 pays for it, and OpenRewrite's "
         "ReplaceClassIsInstanceWithInstanceof performs it unattended on eight "
         "of these ten arms -- so a candidate who plants four covering "
         "switches on top of an automated pass arrives here without having "
         "converted a single dispatch. gate.b17.2.patterns reads 34 against the "
         "golden's 24, which is the ten and is the fact that says which "
         "spelling this tree stopped at. gate.b16.2.sites reads 34 against a "
         "want of 24 and B-16 still reads 1.000000, because that term is "
         "clamped: ten patterns too many is not ten sites converted.")
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
         "half are full, and B-17.4 is 0.714286 with B-17 at 0.958333. "
         "gate.b17.4.firstreads reads 21 against the golden's 19. N11 is the "
         "same figure from the other side, and the pair is B-17.4's version of "
         "N1 and N2. The two `get0` records come out of the rung below, for "
         "N1's reason and by the same door.")
def _n10(rep, env):
    n = sum(restore_prior(rep, "seqfirst", b) for b in SQ_PAIR)
    return "%d index read(s) still beside the getFirst() that replaced it" % n


@fixture("N11", "golden", "the same two reads departed and no sequenced "
         "access arrived: the head is fetched some third way, or the method "
         "no longer fetches it. The departure half is full, the arrival half "
         "is 5/7 = 0.714286, and B-17.4 is 0.714286 with B-17 at 0.958333 -- "
         "N10's two figures, with gate.b17.4.arrfrac and gate.b17.4.depfrac "
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
         "B-17.4 is 0.666667 with B-17 at 0.951389. These nine read index 0 "
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
         "`op=getFirst` and is 6/7 = 0.857143, so B-17.4 is 0.857143 and B-17 "
         "is 0.979167. gate.b17.4.sequenced.read STILL READS 7, because getLast "
         "is a read like any other and that counter counts reads -- it is "
         "reported and not scored, and this row is the reason both of those "
         "are true at once. A leg that had scored the counter would have paid "
         "this tree in full for a method that now returns a different "
         "container.")
def _n13(rep, env):
    op = retype_seqnew(rep, SQ_PAIR[0])
    return "findDefaultContainer reads %s(); seqnew.read=%d" % (
        op, rep.num("seqnew.read"))


# --- P, WHICH MOVED FAMILIES BETWEEN ONE RUNG AND THIS ONE --------------------
#
# Fifteen rows over three legs -- B-18.2's two halves, B-18.3's three, B-18.4's
# three and its scaling -- and every one of them was written one rung ago
# against a family-C column and is read here against a family-B one. That is the
# ladder rule arriving in the calibration table rather than in the weights: step
# 11's C-2, C-3 and C-4 are this rung's B-18.2, B-18.3 and B-18.4, and the rows
# that calibrated them have to come across with them or the leg arrives held by
# a column nothing questions.
#
# THE EDIT WAS THE PROSE AND THE ARITHMETIC, AND NOT THE FIXTURES. Not one of
# the fifteen functions below changed: they move the same records in the same
# buckets and the three per-leg figures they produce are byte for byte step
# 11's. What changed is the TOTAL each row quotes, because the three legs are
# now renormalised over the 0.96 they carry between them -- P1's family C read
# 0.931343 at step 11 and its B-18 reads 0.928482 here, which is
# (0.46*0.850746 + 0.26 + 0.24)/0.96 and not 0.02 + 0.38*0.850746 + 0.33 + 0.25.
# A row left quoting the old total would assert a number the rehearsal cannot
# produce, and would do it in the family that is supposed to be UNMOVED.
#
# THE OTHER LETTERS, AND WHAT IS STILL MISSING. K and M are step 07's and step
# 08's family C, L is step 09's, N is step 10's. C-1 has had L1 and L2 since
# step 10 and still has them, because C-1 is the one leg that does not move
# families -- every rung's own level check is its own C-1. THIS RUNG'S FOUR NEW
# LEGS -- C-2, C-3, C-4, C-5 -- HAVE Q, below, for the reason this header gave
# a rung ago about a column that read 1.000000 on every row: that is not
# evidence a leg works, it is the absence of a question.
#
# THE LETTER IS P AND NOT O. K, L, M and N are taken and O reads as a zero in a
# column of figures, which is the one thing a calibration table must never be
# ambiguous about.
#
# WHAT EACH LEG'S ROWS ARE FOR:
#
#   P1..P5    B-18.2. Two halves, min()ed: 67 wrapper sites must go, 7
#             constructions and 6 scanners must stay. P1 and P4 fail the
#             departure two ways, P2 and P3 fail the two hold halves, P5 is the
#             per-file cap.
#   P6..P9    B-18.3. Three halves, min()ed, over THREE DIFFERENT DENOMINATORS
#             -- 35, 28 and 51 -- and P6 exists because that is the leg's whole
#             design: the same four parameters cost the arrival half more than
#             the departure half, and the reason is the denominator.
#   P10..P15  B-18.4. Three structural terms and a scaling, and the scaling
#             MULTIPLIES where the three min(). P13 is the row the departure
#             half is read off `accessible` for; P15 is the product.
#
# THE BUCKETS ARE DERIVED AND NOT TYPED, in every row. `sorted(CP_ARR)[:4]` and
# `[b for b in PRIV_ALL if b.endswith("#-")]` say what the row is about; a list
# of paths would say only where it happened to land when it was written, and
# would go stale the first time a population is re-cut -- which is a thing this
# rung did to two of them (NOTES 16.16, 16.22).
#
# FIVE ROWS MOVE A SECOND COLUMN AND EVERY ONE OF THEM HAS TO. `accessible` is
# read by three legs and `newinstance` by two, so a tree that deletes one of
# those records is answering B-13 as well, and a row that moved B-18 alone
# would be describing a report the scanner cannot emit. P2 and P5 are the pair
# that makes the point sharpest: the same kind of edit, and B-13 falls for one
# and reads 1.000000 for the other, because one arithmetic caps per file and the
# other counts the tree.
PRIV_DASH = sorted(b for b in PRIV_ALL if b.endswith("#-"))
PRIV_ONEFILE = sorted(
    b for b in PRIV_ALL
    if b.split("#")[0].endswith("impl/enricher/resource/SecurityActions.java"))
PRIV_CTORB = [f + "#newInstance" for f in sorted(PRIV_FILE)]
VH_CONVB = sorted(VH_CONV)
VH_HOLDB = sorted(VH_HOLD)
VH_HOLD_FREE = sorted(set(VH_HOLD) - set(PRIV_ACC))   # held, and not C-2's too
CP_ARRB = sorted(CP_ARR)
CP_HOLDB = sorted(set(CP_HOLD) - set(CP_ARR))         # hold only, so arr cannot move
# The four buckets of the one file P1 restores, and the seven class
# declarations P4 does, are 10 sites and 7 -- asserted, because both notes
# quote the figure and a re-cut population would move it silently.
assert sum(PRIV_ALL[b] for b in PRIV_ONEFILE) == 10 and len(PRIV_ONEFILE) == 4, \
    "the enricher wrapper is no longer 10 sites in 4 buckets"
assert len(PRIV_DASH) == PRIV_CTOR_W == 7, \
    "the class declarations are no longer one per wrapper file"
assert len(CP_HOLDB) >= 12 and len(CP_ARRB) >= 10, \
    "C-3's populations are too small for the rows below to be what they say"


@fixture("P1", "golden", "one of the seven wrapper files was left alone -- "
         "every `doPrivileged` in the resource enricher's SecurityActions is "
         "still there, ten sites in four buckets including the class "
         "declaration. B-18.2's departure is 57 of 67 for 0.850746 and the "
         "hold is untouched: the constructions and the scanners are where they "
         "were, in that file and in the other six. So the min() reads the "
         "departure and B-18 is 0.928482 -- (0.46*0.850746 + 0.26 + 0.24)/0.96, "
         "which is the same tree step 11 priced at 0.931343 of its family C "
         "and is the arithmetic that moved rather than the tree. THE HOLD "
         "BEING WHOLE IS THE "
         "POINT: this is the tree that did most of the work, and it is priced "
         "by what it did not do rather than by what it broke.")
def _p1(rep, env):
    n = restore_old(rep, "priv", PRIV_ONEFILE)
    return "%d wrapper site(s) still in the resource enricher; priv.size=%d" % (
        n, rep.num("priv.size"))


@fixture("P2", "golden", "the seven ladders were DELETED and not unwrapped, in "
         "three of the seven files: no `doPrivileged`, and no "
         "`Constructor.newInstance` either. The departure half reads 1.000000 "
         "-- deletion satisfies it perfectly, which is exactly why it is not "
         "the whole leg -- and the hold reads 10 of 13 for 0.769231, so B-18.2 "
         "is 0.769231 and B-18 0.889423. B-13 FALLS WITH IT, to 0.969231: "
         "`newinstance.ctor` is step 06's floor and reads 10 against 13. One "
         "tree, two rungs, and the second one notices because the first one "
         "asked for the call in the first place.")
def _p2(rep, env):
    n = drop_recs(rep, "newinstance", PRIV_CTORB[:3], 1)
    return "%d construction(s) deleted with the ladder; newinstance.ctor=%d" % (
        n, rep.num("newinstance.ctor"))


@fixture("P3", "golden", "the annotation scanners went with the wrappers, in "
         "two of the six methods that hold one. B-18.2's hold reads 11 of 13 "
         "for 0.846154 -- the seven constructions are all there and four of the "
         "six `setAccessible` calls -- so B-18 is 0.876282. THREE COLUMNS MOVE "
         "OFF ONE RECORD KIND AND THAT IS THE DESIGN: B-13.3 reads 8 of its ten "
         "surviving calls for 0.800000 and carries B-13 down to 0.960000, "
         "B-18.4's hold reads the SAME 8 of 10 for 0.800000, and B-18.2 reads "
         "the 6 it asks for inside the wrapper files. THE TWO B-18 LEGS ARE NOW "
         "IN ONE FAMILY WITH B-13 AND THE ROW IS NOT WEAKER FOR IT: it is the "
         "same three readings of one deleted call, and the family total it "
         "lands in is the only thing about it that changed. "
         "A deleted `setAccessible` is three different failures and the tree it "
         "describes is one tree.")
def _p3(rep, env):
    n = drop_recs(rep, "accessible", sorted(PRIV_ACC)[:2], 1)
    return "%d scanner(s) deleted with the wrapper; accessible.setaccessible=%d" % (
        n, rep.num("accessible.setaccessible"))


@fixture("P4", "golden", "every call was unwrapped and every `implements "
         "PrivilegedAction` left standing -- seven declarations, one per file, "
         "inside no method at all. B-18.2's departure is 60 of 67 for 0.895522 "
         "and B-18 is 0.949938. THIS IS THE ROW THE `in=-` BUCKET RULE EXISTS "
         "FOR. pbuckets() keys these `<file>#-` because its guard is `m == \"\"` "
         "and \"-\" is not \"\"; a census that dropped them would take 7 of the 67 "
         "out of the denominator and hand this tree 1.000000, and the class it "
         "still declares still implements an interface JEP 486 removed.")
def _p4(rep, env):
    n = restore_old(rep, "priv", PRIV_DASH)
    return "%d class declaration(s) still implement PrivilegedAction; priv.size=%d" % (
        n, rep.num("priv.size"))


@fixture("P5", "golden", "one wrapper file lost its construction and another "
         "grew two extra ones -- a refactor that moved the reflective creation "
         "rather than keeping it where it was. B-18.2's hold is 12 of 13 for "
         "0.923077 and B-18 is 0.963141, BECAUSE THE HOLD IS CAPPED PER "
         "FILE: the file that gained two scores the 1 it always scored and the "
         "file that lost its own scores 0. B-13 READS 1.000000 ON THE SAME "
         "TREE. Its floor term is a bare `newinstance.ctor` over the whole "
         "tree, which is 14 against a want of 13 and clamps. Two arithmetics "
         "over one record kind, disagreeing on purpose, and this is the row "
         "that says which one B-18.2 uses.")
def _p5(rep, env):
    d = drop_recs(rep, "newinstance", PRIV_CTORB[:1], 1)
    a = add_ctor(rep, PRIV_CTORB[1], 2)
    return "%d construction(s) moved out, %d added elsewhere; newinstance.ctor=%d" % (
        d, a, rep.num("newinstance.ctor"))


@fixture("P6", "golden", "four catch parameters were never renamed: they are "
         "still `pae` and `cce`, still read by nothing. THE TWO HALVES CHARGE "
         "DIFFERENT PRICES FOR THE SAME FOUR PARAMETERS and that is this row's "
         "whole reason. The departure is stated over all 35 unread parameters "
         "and reads 31 of 35 for 0.885714; the arrival is stated over the 28 "
         "that survive B-18.2 and reads 24 of 28 for 0.857143. The min() takes "
         "the arrival, B-18.3 is 0.857143 and B-18 0.961310. The seven the "
         "denominators differ by are inside the ladders B-18.2 deletes -- they "
         "cannot be asked to arrive as `_` AND to still be there.")
def _p6(rep, env):
    n = recatch(rep, set(CP_ARRB[:4]), "unnamed", "unread", 1)
    return "%d catch parameter(s) still named; catchparam.unread=%d unnamed=%d" % (
        n, rep.num("catchparam.unread"), rep.num("catchparam.unnamed"))


@fixture("P7", "golden", "six catch blocks were DELETED rather than their "
         "parameters renamed -- the exception is swallowed by not being caught "
         "at all. The departure half reads 1.000000, because a parameter that "
         "is not there is not unread; the arrival reads 22 of 28 for 0.785714 "
         "and B-18 is 0.941964. THIS IS THE ROW THE ARRIVAL HALF EXISTS "
         "FOR, and it is B-18.2's hold turned on a different leg: `stop doing "
         "it` "
         "and `delete the code that did it` produce the same departure count "
         "and only one of them is the migration.")
def _p7(rep, env):
    n = drop_catch(rep, set(CP_ARRB[4:10]), "unnamed", 1)
    return "%d catch block(s) deleted; catchparam.unnamed=%d" % (
        n, rep.num("catchparam.unnamed"))


@fixture("P8", "golden", "five catch parameters that WERE read are `_` now: "
         "the candidate deleted the logging along with the name. B-18.3's hold "
         "reads 46 of 51 for 0.901961 and B-18 is 0.973448. The arrival "
         "half does not move -- these five buckets are outside the 24 it is "
         "stated over, and psum() caps each of those at what it asked for "
         "anyway -- so a tree cannot pay for a diagnostic it deleted with a "
         "rename it was owed. THE HOLD IS 51 AND NOT 58: the seven read "
         "parameters inside the deleted ladders are subtracted for B-18.2's "
         "reason, the same subtraction the arrival half makes.")
def _p8(rep, env):
    n = recatch(rep, set(CP_HOLDB[:5]), "read", "unnamed", 1)
    return "%d read parameter(s) renamed `_`; catchparam.read=%d" % (
        n, rep.num("catchparam.read"))


@fixture("P9", "golden", "all three halves fall at once and the min() takes "
         "the worst: twelve read parameters renamed `_` for 39 of 51 and "
         "0.764706, two left unrenamed for 33 of 35 and 0.942857 departing and "
         "26 of 28 and 0.928571 arriving. B-18.3 is 0.764706 and B-18 "
         "0.936275. THREE TERMS AND NOT TWO IS WHAT THIS ROW ASSERTS -- a leg "
         "that min()ed the departure against the arrival and added the hold, or "
         "averaged any two of them, would read this tree above 0.764706 and the "
         "table would show it in one cell.")
def _p9(rep, env):
    h = recatch(rep, set(CP_HOLDB[:12]), "read", "unnamed", 1)
    d = recatch(rep, set(CP_ARRB[:2]), "unnamed", "unread", 1)
    return "%d read parameter(s) renamed, %d never renamed; read=%d unread=%d" % (
        h, d, rep.num("catchparam.read"), rep.num("catchparam.unread"))


@fixture("P10", "golden", "one of the four fields was not converted at all: "
         "`ToolingDeploymentFormatter#getInternalFieldValue` still calls "
         "`setAccessible` and builds no handle. B-18.4's departure is 3 of 4 for "
         "0.750000 and its arrival 6 of 8 for the same, so B-18.4 is 0.750000 "
         "and B-18 0.937500. B-13 READS 1.000000 HERE: its third term counts "
         "`accessible.setaccessible` at 11 against a want of 10 and clamps, and "
         "the ten buckets it names are all still there. A rung's own leg "
         "noticing what an inherited one is not asking about is the shape the "
         "ninth trap turns inward.")
def _p10(rep, env):
    a = restore_old(rep, "accessible", VH_CONVB[:1])
    v = drop_vh(rep, set(VH_CONVB[:1]))
    return "%d field still forced open, %d handle record(s) gone; varhandle.size=%d" % (
        a, v, rep.num("varhandle.size"))


@fixture("P11", "golden", "two conversions built the lookup and never "
         "unreflected it -- `privateLookupIn` is there, `unreflectVarHandle` is "
         "not. The departure reads 1.000000, the arrival 6 of 8 for 0.750000, "
         "B-18.4 is 0.750000 and B-18 0.937500. P10 IS THE SAME FIGURE FROM "
         "THE OTHER SIDE and the pair is why the arrival denominator is twice "
         "the departure's: `gate.b18.4.depfrac` reads 0.750000 there and "
         "1.000000 here, `gate.b18.4.arrfrac` reads 0.750000 in both, and a "
         "leg that "
         "counted one record per bucket instead of two would collapse the two "
         "trees onto one message while keeping the score.")
def _p11(rep, env):
    n = drop_vh(rep, set(VH_CONVB[:2]), call="unreflectVarHandle")
    return "%d handle(s) never unreflected; varhandle.size=%d arrival=%d" % (
        n, rep.num("varhandle.size"), rep.num("varhandle.arrival"))


@fixture("P12", "golden", "a field that must NOT move was converted anyway: "
         "`DynamicServiceLoader#createInstance` drops its `setAccessible` and "
         "builds a handle. B-18.4's hold reads 9 of 10 for 0.900000 and B-18 "
         "is 0.975000; the arrival does not rise, because psum() states it "
         "over the four convert buckets and this is not one of them. B-13.3 "
         "READS 9 OF ITS TEN SURVIVING CALLS FOR 0.900000 and carries B-13 to "
         "0.980000, on the same tree. THE "
         "HOLD IS THE JUDGEMENT THIS LEG IS ABOUT: `unreflectVarHandle` "
         "compiles for all fourteen sites and nothing in the language says "
         "which ten must not move -- the four recorded access failures do, and "
         "P14 is the row that spends them.")
def _p12(rep, env):
    b = VH_HOLD_FREE[0]
    a = drop_recs(rep, "accessible", [b], 1)
    v = add_vh(rep, b)
    return "%d held call(s) converted, %d handle record(s) added; " \
           "accessible.setaccessible=%d" % (a, v, rep.num("accessible.setaccessible"))


@fixture("P13", "golden", "all four handles were built and all four "
         "`setAccessible` calls left beside them -- the tree that added the new "
         "API without removing the old one. The arrival half reads 8 of 8, and "
         "B-18.4 is 0.000000 anyway: the departure is read off the candidate's "
         "`accessible` records and reads 0 of 4. B-18 is 0.750000, which is "
         "(0.46 + 0.26)/0.96 exactly -- the two legs this tree did not touch, "
         "over the three the renormalisation spreads. THE "
         "DEPARTURE IS NOT READ OFF `varhandle` AND THIS IS THE ONLY ROW THAT "
         "SAYS SO. A leg that scored the arrival alone would pay this tree in "
         "full for a migration that removed nothing, which is the same reading "
         "error B-13.3 was rebaselined over one rung ago.")
def _p13(rep, env):
    n = restore_old(rep, "accessible", VH_CONVB)
    return "%d field(s) still forced open beside their handles; " \
           "accessible.setaccessible=%d varhandle.size=%d" % (
               n, rep.num("accessible.setaccessible"), rep.num("varhandle.size"))


@fixture("P14", "golden", "the source shape is perfect and two of the four "
         "recorded access failures answer differently under the candidate's "
         "tree. B-18.4's three structural terms all read 1.000000, the scaling "
         "reads 2 of 4 for 0.500000, and because the scaling MULTIPLIES rather "
         "than joining the min(), B-18.4 is 0.500000 and B-18 0.875000. THE "
         "FOUR IDS ARE B-18.4'S OWN AND NOBODY ELSE'S: the fifth transcript "
         "carrying an `IllegalAccessException` raises it inside "
         "`java.util.ServiceLoader` and is B-15.3's, which is why B-15 reads "
         "1.000000 in this row and not 0.985507. NOTES 16.21.",
         vhbroken=2)
def _p14(rep, env):
    return "2 of 4 access-failure recordings differ"


@fixture("P15", "golden", "the product, and it is the row that says the "
         "scaling is a product. One held field converted -- P12's edit -- for a "
         "structural 0.900000, and one of the four access failures answering "
         "differently for a scaling of 0.750000. B-18.4 is 0.675000 and B-18 "
         "0.918750. A LEG THAT min()ED THE SCALING IN WOULD READ 0.750000 HERE "
         "and be wrong by 0.018750 of B-18; a leg that added it would be "
         "wrong the other way. Neither is visible from a row that moves only "
         "one of the two, which is why this row moves both. B-13.3 reads "
         "0.900000 here for P12's reason and B-13 0.980000; the scaling is "
         "B-18.4's alone and no OTHER leg sees it -- which is a weaker "
         "sentence than the one this row carried at step 11, when the two were "
         "in different families, and it is weaker on purpose: B-13 and B-18 are "
         "family B together now and the row has to say which of the two the "
         "scaling belongs to rather than which family.",
         vhbroken=1)
def _p15(rep, env):
    b = VH_HOLD_FREE[0]
    a = drop_recs(rep, "accessible", [b], 1)
    v = add_vh(rep, b)
    return "%d held call(s) converted and 1 of 4 recordings differ; " \
           "accessible.setaccessible=%d handle record(s) added=%d" % (
               a, rep.num("accessible.setaccessible"), v)


# --- Q1..Q20: THIS RUNG'S OWN FIVE LEGS ---------------------------------------
#
# WRITTEN FOR THE REASON P WAS WRITTEN A RUNG AGO. Until this series existed,
# C-1's preview reading and C-2, C-3, C-4 and C-5 entire read 1.000000 on every
# row of the table but K17 -- and K17 is the row where the report itself is
# missing, so every block reads zero there and nothing in it distinguishes a
# leg that worked from a leg that was never asked. Seventy fixtures scored five
# legs at full marks and proved nothing about any of them.
#
#   Q1..Q2     C-1's SECOND READING. Not the major -- L1 and L2 have moved that
#              since step 09 -- but the MINOR, which is new here. Both rows
#              leave `major.69` full and one of them leaves `majors.distinct=1`,
#              so every figure a four-byte reading prints is perfect.
#   Q3..Q8     C-2's two halves. Four rows on the set equality (a missing
#              module, a wrong module, the blanket edit, nothing at all), one
#              on the clean half, and one on the guard that makes a file the
#              candidate ADDED neither a fault nor a credit.
#   Q9..Q13    C-3's two halves, and the hold's three reasons one at a time:
#              the 25 the compiler teaches, the 16 static, the 2 nested. Q13 is
#              the departure half, which exists only so that adding 188 lines
#              and deleting none collects C-2 and not C-3.
#   Q14..Q17   C-4's three halves. Three of the four read 0.750000, and that is
#              not redundancy: a min() of three over denominators 1, 1 and 7
#              has exactly three ways to fall and each of the three is a tree a
#              reviewer would describe differently.
#   Q18..Q20   C-5. Every Test task and not any, and the probe that never
#              answered.
#
# EVERY ROW MOVES ONE COLUMN AND THAT IS HALF THE ASSERTION. The K, L, M, N and
# P series move nine family-B columns between them; these twenty move familyC
# and nothing else, and the nine cells that must NOT move are as much of what
# the table says as the one that does. A B-18 that noticed a module import, or
# a B-15 that noticed a ScopedValue, would show up here and nowhere else.
#
# THE SETS BELOW ARE DERIVED FROM THE POPULATIONS AND THE GOLDEN REPORT, never
# typed. Three of the four were re-cut during this rung's authoring and a list
# of paths would have gone stale silently, which is the failure NOTES 16.16
# records from step 10.
MI_MULTI = sorted(f for f in MI_WANT if len(MI_WANT[f]) > 1)
MI_BASEONLY = sorted(f for f in MI_WANT if MI_WANT[f] == set(["java.base"]))
MI_NONEB = sorted(MI_NONE)
MI_MODS = sorted(set(m for _, m in MI_PAIRS))
# A file in NEITHER half: not in the 171 with work, not in the 151 with none.
# A path and not a record, because the point of the row that spends it is that
# the population has never heard of it.
MI_NEWFILE = "impl-base/src/main/java/org/jboss/arquillian/impl/core/Added.java"
assert len(MI_MULTI) == T("target.mi.multi"), "the multi-module files moved"
assert len(MI_BASEONLY) == 152, \
    "the files needing java.base and nothing else are no longer 152, so the " \
    "blanket edit no longer scores 152/171 and Q4's note is wrong"
assert MI_NEWFILE not in MI_WANT and MI_NEWFILE not in set(MI_NONE), \
    "Q8's added file is in the population now and the row proves nothing"

# C-3's hold, split by the three reasons the gate publishes separately. Read
# off the GOLDEN's records rather than off the population file, because the
# reason is a property of the import and the report is where the scanner's own
# reading of it lives: `kind=` for the first two and `ambig=` for the third.
JI_STATICK = sorted((site(b)[0], field(b, "q=")) for b in GOLD.recs("jdkimport")
                    if field(b, "kind=") == "static")
JI_NESTEDK = sorted((site(b)[0], field(b, "q=")) for b in GOLD.recs("jdkimport")
                    if field(b, "kind=") == "nested")
JI_AMBIGK = sorted((site(b)[0], field(b, "q=")) for b in GOLD.recs("jdkimport")
                   if int(field(b, "ambig=") or 0) >= 2)
JI_DEPB = sorted(JI_DEP)
assert len(JI_STATICK) == T("target.ji.hold.static"), "the static imports moved"
assert len(JI_NESTEDK) == T("target.ji.hold.nested"), "the nested imports moved"
assert len(JI_AMBIGK) == T("target.ji.hold.ambiguous"), "the ambiguous ones moved"
# DISJOINT AND COVERING, which is what lets Q10, Q11 and Q12 be three rows and
# not three views of one. A reason that overlapped another would make two of
# them the same fixture with different prose.
assert (set(JI_STATICK) | set(JI_NESTEDK) | set(JI_AMBIGK)) == JI_HOLD and \
    len(JI_STATICK) + len(JI_NESTEDK) + len(JI_AMBIGK) == JI_HOLD_W, \
    "the three reasons the 43 hold no longer partition them"

SV_CONVB = sorted(SV_CONV)
SV_HOLDB = sorted(SV_HOLD)
# C-5's odd project out. Any of the twelve would do; it is named once here so
# that three rows can disagree about the same project rather than about three.
CO_ONE = CO_PROJ[3]


@fixture("Q1", "golden", "the whole tree compiled behind `--enable-preview`: "
         "730 class files, every one at major 69 and every one at minor 65535. "
         "`major.69` reads FULL -- which is the entire figure every rung from "
         "05 to 11 read off this channel, and B-4's `was=66` is satisfied too "
         "-- and C-1 is 0.000000, because `majorminor.69.0` is 0 of 730. "
         "familyC = 0.980000. THE ROW THIS RUNG'S FIFTH AND SIXTH BYTES EXIST "
         "FOR: all three of this step's language and runtime changes were "
         "preview in a release before 25, the flag is in every tutorial "
         "written while they were, and a tree that leaves it on has not "
         "migrated to 25 -- it has migrated to a 25 the next release will "
         "refuse to load. It reads the same 0.980000 as L2, which has no scan "
         "at all, and one fact separates them: `gate.c1=no-scan` there, "
         "`gate.c1.preview=730` here.",
         scan=scan_preview(GOLD_CLASSES))
def _q1(rep, env):
    return "%d class file(s) at %d.%d and none at %d.%d" % (
        GOLD_CLASSES, WANT_MAJOR, PREVIEW_MINOR, WANT_MAJOR, WANT_MINOR)


@fixture("Q2", "golden", "one subproject built with `--enable-preview` and the "
         "other eleven without: 690 of the 730 classes at 69.0 and 40 at "
         "69.65535. C-1 is 0.945205 and familyC 0.998904. THE PARTIAL CASE, "
         "and it is the one worth having: the whole-tree version announces "
         "itself to anyone who looks, and this one does not. `classes=730`, "
         "`major.69=730`, `majors.distinct=1` -- every figure the four-byte "
         "reading prints is perfect, and forty class files in this tree cannot "
         "be loaded by a 26 that has moved the preview on.",
         scan=scan_preview(40))
def _q2(rep, env):
    return "40 of %d class file(s) carry the preview minor" % GOLD_CLASSES


@fixture("Q3", "golden", "one of the two modules a file needs was never "
         "declared: it has `import module java.base;` and not the second one. "
         "187 declarations against 188, and the file's SET is wrong, so C-2's "
         "set half is 170 of 171 -- the file scores nothing rather than a "
         "half, which is what set equality means -- the clean half is "
         "untouched, and familyC is 0.997778. THE PAIR WITH Q6: same score, "
         "same denominator, and `gate.c2.imports` reads 187 here and 188 "
         "there. A leg that counted declarations instead of comparing sets "
         "would call one of the two correct.")
def _q3(rep, env):
    f = MI_MULTI[0]
    m = sorted(MI_WANT[f])[-1]
    n = drop_modimport(rep, [(f, m)])
    return "%d module import(s) gone (%s from %s); modimport.size=%d" % (
        n, m, f.split("/")[-1], rep.num("modimport.size"))


@fixture("Q4", "golden", "`import module java.base;` at the top of all 322 "
         "files and nothing else -- the blanket edit, which is what a tool "
         "that has not made the per-file decision produces. 152 of the 171 "
         "files with work are right BY ACCIDENT, because 152 of them need "
         "java.base and nothing else; the other 19 name a type java.base does "
         "not export -- java.naming, java.logging, java.management, java.xml, "
         "java.net.http -- and do not compile. And all 151 files with nothing "
         "to do now import something they do not use. min(0.888889, 0.000000) "
         "is 0.000000 and familyC is 0.620000. THE ROW THE SECOND HALF EXISTS "
         "FOR: the set half alone prices this tree at seven eighths of the "
         "widest leg in the rung.")
def _q4(rep, env):
    d = drop_modimport(rep, MI_PAIRS)
    a = add_modimport(rep, [(f, "java.base") for f in sorted(MI_WANT) + MI_NONEB])
    return "%d declaration(s) replaced by %d blanket java.base import(s)" % (d, a)


@fixture("Q5", "golden", "the 171 files with work are exactly right and 30 of "
         "the 151 with nothing to do picked up an `import module java.base;` "
         "anyway -- the residue of a sweep that ran over the whole tree and "
         "was corrected only where it broke a build. The set half is FULL and "
         "the clean half is 121 of 151 for 0.801325, so C-2 is 0.801325 and "
         "familyC 0.924504. A leg that looked only where it expected to find "
         "work would read this tree as finished, which is the whole argument "
         "for giving the second half a population of its own rather than an "
         "absence to reason from.")
def _q5(rep, env):
    a = add_modimport(rep, [(f, "java.base") for f in MI_NONEB[:30]])
    return "%d spurious module import(s) in files that import no JDK type" % a


@fixture("Q6", "golden", "a file declares the right NUMBER of module imports "
         "and one of them is the wrong module. 188 declarations spread over "
         "171 files, both exactly the golden's -- `modimport.size` and "
         "`modimport.files` do not move, and `gate.c2.files` is neither of "
         "them but the set half's numerator, which reads 170 on this row -- "
         "and one file's set is `{java.base, <the wrong one>}`, so it is "
         "missing a "
         "module it needs and carrying one it does not. C-2's set half charges "
         "it once, not twice: 170 of 171 for 0.994152, and familyC 0.997778. "
         "THE ROW THAT SAYS WHY THE LEG READS PACKAGES AND NOT PREFIXES: "
         "`java.util.List` is java.base and `java.util.logging.Logger` is "
         "java.logging, and no rule over the import's own text tells them "
         "apart.")
def _q6(rep, env):
    f = MI_MULTI[1]
    m = sorted(MI_WANT[f])[-1]
    other = sorted(set(MI_MODS) - MI_WANT[f])[0]
    drop_modimport(rep, [(f, m)])
    add_modimport(rep, [(f, other)])
    return "%s declares %s where it needs %s; modimport.size=%d" % (
        f.split("/")[-1], other, m, rep.num("modimport.size"))


@fixture("Q7", "golden", "not one module import in the tree. The 452 "
         "single-type imports are still gone, so C-3 is full and only C-2 "
         "falls: the set half is 0 of 171 and the clean half is 151 of 151, so "
         "C-2 is 0.000000 and familyC 0.620000. THE SAME TOTAL AS Q4 BY THE "
         "OPPOSITE HALF, which is the pair worth having in a min(): "
         "`gate.c2.setfrac` is 0.000000 here and 0.888889 there, "
         "`gate.c2.cleanfrac` is 1.000000 here and 0.000000 there. One tree "
         "did none of this leg and the other did all of it wrongly, and a "
         "single-figure leg could not say which.")
def _q7(rep, env):
    n = drop_modimport(rep, MI_PAIRS)
    return "%d module import(s) removed; modimport.size=%d files=%d" % (
        n, rep.num("modimport.size"), rep.num("modimport.files"))


@fixture("Q8", "golden", "a file the population has never heard of declares a "
         "module import -- a class the candidate ADDED, which is a thing a "
         "migration is allowed to do and this one does seven times over in the "
         "rungs below. NEITHER HALF COUNTS IT: the set half iterates the 171 "
         "files with work and its extras loop is guarded on membership, and "
         "the clean half iterates the 151 with none. C-2 is 1.000000 and so is "
         "every other leg of every family. THE ONLY ROW IN THIS SERIES THAT "
         "MOVES NOTHING, and it is here for exactly that: a leg that charged "
         "for the extra would make writing a class cost score, and a leg that "
         "credited it would let a candidate raise C-2 by writing files. "
         "`gate.c2.imports` reads 189 against a want of 188 and is published "
         "rather than scored, which is the fact that says the row landed.")
def _q8(rep, env):
    a = add_modimport(rep, [(MI_NEWFILE, "java.base")])
    return "%d module import(s) in a file in neither half; modimport.size=%d" % (
        a, rep.num("modimport.size"))


@fixture("Q9", "golden", "the sed answer: every `import java.` line in the "
         "tree is gone, the 43 a module import does not reach along with the "
         "452 it does. The departure half reads 1.000000 -- perfectly, which "
         "is precisely why it is not the whole leg -- and the hold reads 0 of "
         "43, so C-3 is 0.000000 and familyC 0.670000. Twenty-five of the 43 "
         "are the ambiguous `Annotation` imports and eighteen are static and "
         "nested ones, and all forty-three are deleted by any rule that reads "
         "`the module import replaces these`. One command, four seconds, and "
         "0.33 of the family gone.")
def _q9(rep, env):
    n = drop_jdkimport(rep, JI_HOLD)
    return "%d held import(s) deleted; jdkimport.size=%d" % (
        n, rep.num("jdkimport.size"))


@fixture("Q10", "golden", "the 25 `java.lang.annotation.Annotation` imports "
         "deleted with the rest. THIS IS THE HALF THE COMPILER TEACHES: JEP "
         "484 put `java.lang.classfile.Annotation` in java.base at 24, so "
         "`import module java.base` leaves the simple name ambiguous and none "
         "of these 25 files build. The hold is 18 of 43 for 0.418605, the "
         "departure is full, C-3 is 0.418605 and familyC 0.808140. A candidate "
         "that ran its own build would never ship this tree -- which is the "
         "argument for the other two reasons being worth more than this one, "
         "and the reason the gate publishes `gate.c3.held.ambiguous` beside "
         "`.static` and `.nested` instead of one figure for the 43.")
def _q10(rep, env):
    n = drop_jdkimport(rep, JI_AMBIGK)
    return "%d ambiguous import(s) deleted; jdkimport.ambiguous=%d" % (
        n, rep.num("jdkimport.ambiguous"))


@fixture("Q11", "golden", "the 16 static imports deleted -- the rule that "
         "reads `the module import replaces every java.* import` applied to "
         "sixteen it cannot replace. `import static java.util.Arrays.asList` "
         "names a MEMBER, and a type-import-on-demand over a module's exported "
         "packages names types; nothing the migration added puts `asList` "
         "back. The hold is 27 of 43 for 0.627907, C-3 is 0.627907 and familyC "
         "0.877209. The largest of the three reason-rows after the ambiguous "
         "one, and the one a reviewer reading a diff is least likely to stop "
         "at: sixteen deleted lines among 452 other deleted lines.")
def _q11(rep, env):
    n = drop_jdkimport(rep, JI_STATICK)
    return "%d static import(s) deleted; jdkimport.static=%d" % (
        n, rep.num("jdkimport.static"))


@fixture("Q12", "golden", "the two nested-type imports deleted. "
         "`java.util.ServiceLoader.Provider` is reached through its outer "
         "class and a type-import-on-demand does not walk into one, so the "
         "module import that subsumed 452 siblings does not subsume these. Two "
         "rows out of 43 and the smallest edit in this series: the hold is 41 "
         "of 43 for 0.953488, C-3 is 0.953488 and familyC 0.984651. IT IS HERE "
         "BECAUSE IT IS SMALL. The three reasons have populations of 25, 16 "
         "and 2, and a leg that read the 43 as one number would price this "
         "tree within a thousandth of the golden while leaving two files that "
         "do not compile.")
def _q12(rep, env):
    n = drop_jdkimport(rep, JI_NESTEDK)
    return "%d nested-type import(s) deleted; jdkimport.nested=%d" % (
        n, rep.num("jdkimport.nested"))


@fixture("Q13", "golden", "forty of the 452 single-type imports are still "
         "written out beside the module imports that subsume them: the "
         "`import module` line was ADDED and the lines it replaces were not "
         "removed, in the files the sweep did not come back to. The hold half "
         "is full, the departure half is 412 of 452 for 0.911504, C-3 is "
         "0.911504 and familyC 0.970796. THE HALF THAT KEEPS C-2 HONEST: "
         "without it a tree collects all 0.38 of the widest leg in the rung by "
         "adding 188 lines and deleting nothing, which is the cheapest edit "
         "available and is not this migration.")
def _q13(rep, env):
    n = restore_jdkimport(rep, JI_DEPB[:40])
    return "%d import(s) never departed; jdkimport.size=%d" % (
        n, rep.num("jdkimport.size"))


@fixture("Q14", "golden", "the ScopedValue was added and the ThreadLocal field "
         "it replaces is still declared -- the belt-and-braces conversion, "
         "which leaves two sources of truth for one piece of state and a "
         "`finally` that still has to run. The arrival half is full and the "
         "departure half is 0 of 1, so C-4 is 0.000000 and familyC 0.750000. "
         "THREE ROWS IN THIS SERIES READ 0.750000 AND THEY ARE NOT ONE ROW: a "
         "min() of three halves over denominators 1, 1 and 7 has exactly three "
         "ways to fall, and `gate.c4.departed`, `gate.c4.arrived` and "
         "`gate.c4.held` read 0/1/7 here, 1/0/7 at Q15 and 1/1/0 at Q16.")
def _q14(rep, env):
    n = restore_tl(rep, SV_CONVB)
    return "%d ThreadLocal declaration(s) still there; threadlocal.decls=%d" % (
        n, rep.num("threadlocal.decls"))


@fixture("Q15", "golden", "the ThreadLocal went and nothing arrived: the field "
         "was deleted and its readers rewritten to take the value as a "
         "parameter. A defensible refactor, and not this leg -- the departure "
         "half is full and the arrival half is 0 of 1, so C-4 is 0.000000 and "
         "familyC 0.750000. THE CALL SITES ARE LEFT IN PLACE ON PURPOSE. "
         "`gate.c4.svcalls` still reads 8, because the leg reads `form=decl` "
         "on both kinds and a row that deleted the reads as well would be "
         "arguing its case with a fact nobody scores.")
def _q15(rep, env):
    n = drop_sv(rep, SV_CONVB)
    return "%d ScopedValue declaration(s) removed; scopedvalue.decls=%d " \
           "svcalls=%d" % (n, rep.num("scopedvalue.decls"),
                           rep.num("scopedvalue.size") - rep.num("scopedvalue.decls"))


@fixture("Q16", "golden", "every one of the eight ThreadLocal fields is now a "
         "ScopedValue. The departure and arrival halves are both full -- the "
         "one field that should have moved did move -- and the hold is 0 of 7, "
         "so C-4 is 0.000000 and familyC 0.750000. THE TREE THIS LEG WAS "
         "WRITTEN AGAINST. `ScopedValue` compiles for all eight. Seven of them "
         "are wrong for reasons nothing in the language states: one is "
         "assigned after binding, one is an SPI boundary between two calls a "
         "third party makes, one is a save-register restored after its extent "
         "ends, one is a public static whose readers this tree does not own, "
         "one is written by a constructor and read outside it, and two span a "
         "framework callback rather than a nested region. The tests that would "
         "notice are the ones JUnit and TestNG drive.")
def _q16(rep, env):
    d = drop_tl(rep, SV_HOLDB)
    a = arrive_sv(rep, SV_HOLDB)
    return "%d hold(s) converted (%d ScopedValue declaration(s) added); " \
           "threadlocal.decls=%d scopedvalue.decls=%d" % (
               d, a, rep.num("threadlocal.decls"), rep.num("scopedvalue.decls"))


@fixture("Q17", "golden", "two of the seven ThreadLocals that must not move "
         "were converted anyway -- a candidate that read the rule as `per-"
         "thread state becomes a ScopedValue` and applied it where the shape "
         "looked close enough. The departure and arrival halves are full and "
         "the hold is 5 of 7 for 0.714286, so C-4 is 0.714286 and familyC "
         "0.928571. THE REASON THE HOLD IS SEVEN ROWS AND NOT A BOOLEAN: from "
         "every other channel in this report -- the level, the module imports, "
         "the single-type imports, the flag -- this tree is the golden.")
def _q17(rep, env):
    ks = SV_HOLDB[:2]
    d = drop_tl(rep, ks)
    a = arrive_sv(rep, ks)
    return "%d of %d hold(s) converted (%d added); threadlocal.decls=%d" % (
        d, SV_HOLD_W, a, rep.num("threadlocal.decls"))


@fixture("Q18", "golden", "eleven of the twelve projects fork their test JVM "
         "with the flag and the twelfth does not -- set in eleven build files "
         "by hand rather than once in the convention plugin, which is what "
         "this tree's build layout invites. C-5 is 11 of 12 for 0.916667 and "
         "familyC 0.998333. The block names the project rather than the "
         "figure: `every test JVM this project forks is started without "
         "-XX:+UseCompactObjectHeaders`, which is the sentence a candidate "
         "reading a failed run can act on.",
         jvm=jvm_rows(co_spec(flagged=[p for p in CO_PROJ if p != CO_ONE])))
def _q18(rep, env):
    return "%s forks its test JVM without the flag" % CO_ONE


@fixture("Q19", "golden", "one project has two `Test` tasks and only one of "
         "them was given the flag: `test` forks with it and `integrationTest` "
         "forks without. C-5 counts a project only when EVERY row it printed "
         "carries the flag, so it is 11 of 12 for 0.916667 and familyC "
         "0.998333 -- the same two figures as Q18 and a different tree. "
         "`gate.c5.tasks` reads 13 here and 12 there, `gate.c5.forks.flagged` "
         "12 and 11, and the diagnostic says `1 of 2 test JVMs this project "
         "forks are started without ...` rather than `every`. AN `any` OVER "
         "THE ROWS WOULD HAVE SCORED THIS TREE 1.000000, which is the whole "
         "reason the tally is per project and per task rather than a grep over "
         "the log.",
         jvm=jvm_rows(co_spec(), strays=[(CO_ONE, "integrationTest", "none")]))
def _q19(rep, env):
    return "%s:integrationTest forks without the flag; :test forks with it" % CO_ONE


@fixture("Q20", "golden", "the probe never answered: `arqTestJvmArgs` did not "
         "run, or ran and the build failed at configuration time before any "
         "task printed a row. C-5 is 0.000000 WITH NO GUARD TO EXCUSE IT -- a "
         "build nobody can ask cannot be said to pass the flag, which is the "
         "one place in family C where an absent artefact scores zero rather "
         "than publishing `no-scan` -- and familyC is 0.980000. "
         "`gate.c5.probe` reads `failed`, `gate.c5.tasks` reads 0, and the "
         "block prints `this project forks no test JVM the probe could ask "
         "about` for all twelve, capped at six lines. THE THIRD ROW AT "
         "0.980000 AND THE THIRD WAY TO LOSE ONE 0.02 LEG: L2 has no scan, Q1 "
         "has the preview minor, this one has no probe.",
         jvm=None)
def _q20(rep, env):
    return "no jvmargs.txt: the probe printed nothing the gate could read"


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
    """The fixture's B-16.3 behaviour breakage over step 09's five text-block
    ids, in the same three spellings `svcbroken_of` accepts.

    A SECOND LIST AND NOT A SECOND FILE. There is one behavior.summary and its
    `behavior.differ[]` lines carry whatever ids differed; the scorer greps
    them against tbids.txt for B-16.3 and against svcids.txt for B-15.3 and
    against the step-07 http list for B-14.5. Populations, scalings, one
    artefact -- which is why `score` below takes ONE differ list and splits it,
    rather than taking one per leg.
    """
    v = env.get("tbbroken", ())
    if v == "all":
        return list(TBIDS)
    if isinstance(v, int):
        return list(TBIDS[:v])
    return list(v)


def swbroken_of(env):
    """The fixture's B-17.3 behaviour breakage over step 10's five switch ids,
    in the same three spellings the two helpers above accept.

    A FOURTH LIST AND STILL ONE FILE. behavior.summary is read by SIX provisos
    at this rung against six baseline lists -- B-10.3's four file-API calls,
    B-14.5's eighteen exchanges, B-15.3's 69 services, B-16.3's five text
    blocks, B-17.3's five pattern dispatches and C-4's four access failures --
    and every one of them greps its own list out of the same
    `behavior.differ[]` lines. Disjointness is not a thing to say about them,
    it is a thing to measure: fifteen pairs and 105 ids, asserted where the
    lists are loaded above, asserted again by mk-baseline-step11.sh against the
    era's transcripts, and asserted a third time inside the image. A run that
    breaks one population must leave the other five columns exactly where they
    were, and the tables below are printed so that can be read rather than
    believed.
    """
    v = env.get("swbroken", ())
    if v == "all":
        return list(SWIDS)
    if isinstance(v, int):
        return list(SWIDS[:v])
    return list(v)


def vhbroken_of(env):
    """The fixture's C-4 behaviour breakage over this rung's four access-failure
    ids, in the same three spellings the three helpers above accept.

    THE SIXTH LIST, AND THE FIRST ONE THIS RUNG CUT ITSELF. vhids.txt names the
    four recordings where THIS TREE raised an `IllegalAccessException` -- the
    exact failure C-4's four `behaviour` verdicts refuse to convert -- and it is
    closed in the image against the transcripts rather than trusted as prose.
    FOUR AND NOT THE FIVE THE STRING FINDS: a fifth transcript carries the same
    exception thrown by `java.util.ServiceLoader$ProviderImpl` inside java.base
    and arriving wrapped in a `ServiceConfigurationError`, and that one is
    B-15.3's already. Both derivations subtract it the same way, by the wrapper
    rather than by name.
    """
    v = env.get("vhbroken", ())
    if v == "all":
        return list(VHIDS)
    if isinstance(v, int):
        return list(VHIDS[:v])
    return list(v)


def write_fixture(name, rep, why, env, note):
    d = os.path.join(OUT, name)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    rep.write(os.path.join(d, "report.txt"))
    with open(os.path.join(d, "answers.txt"), "w") as fh:
        # THE THREE TAGS ARE THE RUNG'S, AND THIS LOOP SHIPPED THE RUNG
        # BELOW'S FOR A SECOND GENERATION RUNNING. test.sh runs `srcgate 25 25`
        # first and reads `ok25=` out of this file; a file carrying
        # `ok22/ok21/okdef` answers that probe with the empty string, so every
        # fixture reports gate.src.ok25=no, takes the default-level fallback and
        # never publishes the control at all -- on a rehearsal whose `pinned`
        # and `golden` rows both assert it. The tags are 25, 24 and def.
        #
        # THE NOTE THIS PARAGRAPH REPLACES SAID EXACTLY THAT ABOUT 22 AND 21,
        # and the loop under it still said 22 and 21 one rung later. A comment
        # that names the numbers is a comment the next rung reads as a
        # description rather than as an instruction. The lesson is the same one
        # step 12's head sweep learned twice (NOTES 16.64): grep the inherited
        # file for the previous rung's CONSTANTS BY VALUE before writing new
        # prose over it.
        #
        # `24` IS DERIVED AND NOT DEFAULTED, and what it is derived FROM has
        # moved with the rung. JEP 511 made the MODULE IMPORT DECLARATION
        # standard in 25 after three releases of preview, so a tree that has
        # done C-2's work cannot compile at -source 24 without --enable-preview.
        # Nothing else in this rung moves a level: `ScopedValue` is a class in
        # java.lang and not a language change, `-XX:+UseCompactObjectHeaders` is
        # an argument javac never sees, the class-file major is a property of
        # the emitted artifact, and C-3's 43 holds are ordinary import
        # declarations that have been legal since Java 1. So a fixture answers
        # `no` at -source 24 exactly when its report declares a module import,
        # which is every tree here except `pinned`, K17 and Q7 -- and Q7 is the
        # row that makes the derivation worth having, because Q7 is the golden
        # with every module import taken back out and it is the one row whose
        # control flips back to `yes` for a reason the table can state.
        auto24 = "no" if rep.num("modimport.size") else "yes"
        for tag, dflt in (("25", "yes"), ("24", auto24), ("def", "yes")):
            fh.write("ok%s=%s\n" % (tag, env.get("ok" + tag, dflt)))
        fh.write("errors=0\n")
    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        # The differ ids the fixture's own catalogue row asked for, all four
        # populations in one file because the tree only ever produces one.
        # M5, M6, L8, N8, P14 and P15 are the rows that use it: trees whose
        # source shape is perfect and whose recorded behaviour is not, which is
        # the pairing the run-time provisos exist for.
        #
        # THE FOUR HELPERS AND THE ORDER ARE score()'s, VERBATIM, AND THAT IS
        # NOT A STYLE POINT. This writer publishes the artefact and score()
        # prices it, off the same `env`; a list that appears in one and not the
        # other ships a fixture whose behavior.summary contradicts the row the
        # rehearsal asserts, and the fixture would still LOOK right because the
        # report and the jars are the same file either way. It shipped exactly
        # that way for one run of this generator: vhbroken_of arrived with this
        # rung's sixth id list, score() gained it and this loop did not, so
        # P14 and P15 were priced at C-4 0.500000 and 0.675000 off a
        # behavior.summary that said `behavior.build=ok` and nothing else.
        # NOTES 16.23.
        for i, sid in enumerate(svcbroken_of(env) + tbbroken_of(env)
                                + swbroken_of(env) + vhbroken_of(env)):
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
    # AND A FIFTH, new at this rung: what the build said it would hand a test
    # fork. `None` writes nothing, which is the probe that never answered.
    write_jvmargs(d, env.get("jvm", GOLD_JVM))
    return d


_UNSET = object()


def score(rep, env, differ=_UNSET):
    """Every gate the eleven lifted blocks publish, for one fixture.

    `differ` is the whole `behavior.differ[]` list -- one argument and not
    five, because there is one behavior.summary. `None` is the file being
    absent or unreadable, which is ONE event with FIVE consequences at this
    rung: B-14.5's exchange scaling, B-15.3's service scaling, B-16.3's text
    block scaling, B-17.3's pattern-switch scaling and B-18.4's access-failure
    scaling all read zero off the same missing file. Step 08's generator carried two of these as separate
    arguments and its own note records what that cost: a GONE row printed
    twice, each copy reporting its own gate at zero and the other at 1.000000,
    describing a tree that cannot exist.

    `_UNSET` and not a falsy default, for the reason that note also gives:
    `None` means unreadable and `()` means intact, and `differ or ...` reads
    both as "the caller said nothing".
    """
    # THE PRIMARY PROBE IS ok25 AT THIS RUNG. test.sh runs `srcgate 25 25`
    # first and sets `measured` off it; `ok24` is the CONTROL it runs
    # underneath, and the golden's own answer to that control is `no` -- the
    # module import declarations are standard at 25 and preview at 24. A model
    # keyed on ok24 would therefore call the golden unmeasured, which is the
    # inverted reading of a control that is published and scored by nothing.
    measured = env.get("ok25", "yes") == "yes" or env.get("okdef", "yes") == "yes"
    jars = env.get("jars", B12_FLOORS)
    mods = env.get("mods", GOLDMODS)
    scan = env.get("scan", GOLD_SCAN)
    jvm = env.get("jvm", GOLD_JVM)
    if differ is _UNSET:
        differ = (svcbroken_of(env) + tbbroken_of(env) + swbroken_of(env)
                  + vhbroken_of(env))
    if measured:
        b10, _ = b10_score(rep)
        b11, _ = b11_score(rep)
        b10s, b11s = "%.6f" % b10, "%.6f" % b11
    else:
        b10s = b11s = "0"
    b12s, b12p = b12_score(rep, None if mods is None else jars, measured)
    b13s, b13p = b13_score(rep, measured)
    # B-14's helper is inherited byte for byte and takes its list PRE-FILTERED;
    # B-15's, B-16's, B-17's and family C's filter inside. The asymmetry is
    # deliberate -- the inherited helper is not edited at a rung that did not
    # change its gate -- so the split happens here, once, against the same five
    # baseline lists the shell greps.
    http = None if differ is None else [x for x in differ if x in C5_HTTPIDS]
    b14s, b14p, gb = b14_score(rep, measured, http)
    b15s, b15p, g15 = b15_score(rep, mods, measured, differ)
    b16s, b16p, g16 = b16_score(rep, measured, differ)
    b17s, b17p, g17 = b17_score(rep, measured, differ)
    # B-18 IS LAST OF FAMILY B AND TAKES NO SCAN, which is the shape of the
    # ladder rule: step 11's family C arrived here minus its level leg, and the
    # level leg was the only one of the four that read the class files.
    b18s, b18p, g18 = b18_score(rep, measured, differ)
    cs, cp, gc = fc_score(rep, scan, measured, jvm)
    g = dict(gb)
    g.update(g15)
    g.update(g16)
    g.update(g17)
    g.update(g18)
    g.update(gc)
    return dict(b10=b10s, b11=b11s, b12=b12s, b12p=b12p, b13=b13s, b13p=b13p,
                b14=b14s, b14p=b14p, b15=b15s, b15p=b15p, b16=b16s, b16p=b16p,
                b17=b17s, b17p=b17p, b18=b18s, b18p=b18p, c=cs, cp=cp, gates=g)


def check_pinned():
    """The pinned fixture is the input tree's own report, byte for byte.

    Not a resemblance check. `pinned` is written by copying every pair out of
    baseline-step12-source.txt and writing them back, so if Rep's round trip
    ever stopped being the identity -- a dropped blank line, a renumbered
    record, a key reordered -- this catches it here rather than three fixtures
    downstream where the number would look like a finding. Four new record
    kinds arrive at this rung -- modimport, jdkimport, threadlocal and
    scopedvalue -- and TWO of them are EMPTY in the seed, which the round trip
    has to carry as faithfully as the other two: a kind that vanishes when it
    holds no record is a kind C-2's and C-4's arrival halves cannot tell from a
    scanner that never ran. That is also why the golden half of this check is
    not redundant with the pinned half at this rung -- the two ends are the only
    two reports in the catalogue where those kinds are empty on one side and
    full on the other.
    """
    p = os.path.join(OUT, "pinned", "report.txt")
    with open(p) as a, open(S12) as b:
        got, want = a.read(), b.read()
    assert got == want, "pinned/report.txt is not baseline-step12-source.txt"
    g = os.path.join(OUT, "golden", "report.txt")
    with open(g) as a, open(G12) as b:
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
             {"scan": SEED_SCAN, "jvm": SEED_JVM}),
            ("golden", GOLD, "the golden tree's own measured report, its jars "
             "a scan that reads 69 with minor 0 on every one of its class "
             "files, and a probe that reports the flag on all twelve. "
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
    # SIXTEEN COLUMNS, and the nine inherited family-B ones are not padding.
    # The ninth trap is that a step-k requirement can quietly make a step-k-2
    # test unpassable, and the only place that is visible is a row where the
    # column that must NOT move has moved. Family B is nine legs at this rung
    # because step 11's family C went across as B-18 -- minus its level leg,
    # which this rung's own C-1 replaces three majors higher -- and family C is
    # FIVE for the first time since step 05.
    #
    # THE GOLDEN ROW MUST READ 1.000000 IN EVERY ONE OF THE FIFTEEN SCORED
    # CELLS, and reading it is the cheapest instrument this ladder has. Four
    # of the last rung's inherited denominators were carried across stale and
    # three of the four were found HERE, by a golden cell that was not 1, and
    # not by reading the diff: B-13.3 at 0.750000, B-16.5 at 0.333333 and
    # B-11.1 at 0.650000. NOTES 16.20 records the rule that fell out of it --
    # build this table and run it BEFORE the static review, at every rung from
    # here on.
    hdr = ("%-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s | "
           "%-8s %-8s %-8s %-8s %-8s | %-8s")
    print(hdr % ("fixture", "B10", "B11", "B12", "B13", "B14", "B15", "B16",
                 "B17", "B18", "C1", "C2", "C3", "C4", "C5", "familyC"))
    print("-" * 160)
    for name, st, note in rows:
        c = [cell(x) for x in st["cp"]]
        print(hdr % (name, st["b10"], st["b11"], st["b12"], st["b13"],
                     st["b14"], st["b15"], st["b16"], st["b17"], st["b18"],
                     c[0], c[1], c[2], c[3], c[4], st["c"]))
    print()
    print("B-12 parts (a b c d e), B-13 (2..7), B-14 (2..5), B-15 (2 3), "
          "B-16 (2..5), B-17 (2..4), B-18 (2..4):")
    for name, st, note in rows:
        print("  %-8s b12: %s" % (name, " ".join(cell(x) for x in st["b12p"])))
        print("  %-8s b13: %s" % ("", " ".join(cell(x) for x in st["b13p"])))
        print("  %-8s b14: %s" % ("", " ".join(cell(x) for x in st["b14p"])))
        print("  %-8s b15: %s" % ("", " ".join(cell(x) for x in st["b15p"])))
        print("  %-8s b16: %s" % ("", " ".join(cell(x) for x in st["b16p"])))
        print("  %-8s b17: %s" % ("", " ".join(cell(x) for x in st["b17p"])))
        print("  %-8s b18: %s" % ("", " ".join(cell(x) for x in st["b18p"])))

    # --- the doctored-summary runs, which are not source-tree fixtures
    #
    # c-rehearsal.sh builds these by copying golden/ and appending to nothing
    # but behavior.summary, so the report is the golden's and only the run-time
    # factors move. Their `want` figures are computed here for the same reason
    # every other want is: so the rehearsal asserts a number this file worked
    # out rather than one the scorer handed it.
    #
    # FIVE SETS OF THEM AT THIS RUNG: B-14.5's eighteen http ids, B-15.3's 69
    # service ids, B-16.3's five text-block ids, B-17.3's five pattern-dispatch
    # ids and B-18.4's four access-failure ids. The five tables are the
    # assertion that the five greps read their own list -- a scorer that
    # grepped the whole differ list would show up as a moved cell in the four
    # columns the run never touched. The sixth proviso on the same file,
    # B-10.3's four file-API ids, has no table for the reason the GONE note
    # below gives: `b10_score` carries the leg's three thirds and not the
    # halving, so a table of it here would print five rows of 1.000000 and
    # assert nothing.
    #
    # AND THE familyC COLUMN IN ALL FIVE TABLES IS NOW A CONSTANT, which is
    # worth saying out loud rather than letting a reader discover: not one of
    # this rung's five legs reads behavior.summary. C-1 reads the scan, C-2,
    # C-3 and C-4 read the source report and C-5 reads the probe, so a doctored
    # or deleted summary cannot move familyC by any amount. Every rung from 08
    # to 11 had a family-C leg on that file and this one does not, so the
    # column that used to be the interesting half of these tables is now the
    # boring half -- and the interesting half moved to B18, where step 11's
    # leg went. The column stays because 1.000000 across all fourteen doctored
    # runs is exactly the claim being made.
    print()
    print("BEHAV* (golden's report, doctored behavior.summary) -- B-14.5's ids:")
    print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
          ("run", "exec.want", "broken", "red.hon", "B14.5", "B14",
           "B15", "B17", "familyC"))
    for name, broken in (
            ("BEHAVALL", C5_HTTPIDS),
            ("BEHAVHALF", C5_HTTPIDS[:9]),
            ("BEHAVRED", [C5_REDID]),
            ("BEHAVONE", [i for i in C5_HTTPIDS if i != C5_REDID][:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
              (name, g["b14.c5.exec.want"], g["b14.c5.exec.broken"],
               g["b14.c5.redirects.honoured"], cell(st["b14p"][3]), st["b14"],
               st["b15"], st["b17"], st["c"]))
        print("           ids: %s" %
              (" ".join(broken) if len(broken) <= 2
               else "%s ... %s (%d)" % (broken[0], broken[-1], len(broken))))

    print()
    print("SVC* (golden's report, doctored behavior.summary) -- B-15.3's 69 ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
          ("run", "exec.want", "broken", "B15.3", "B15", "B17", "familyC", "B14"))
    for name, broken in (
            ("SVCALL", SVCIDS),
            ("SVCHALF", SVCIDS[:35]),
            ("SVCONE", SVCIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
              (name, g["b15.exec.want"], g["b15.exec.broken"],
               cell(st["b15p"][1]), st["b15"], st["b17"], st["c"], st["b14"]))

    print()
    print("TB* (golden's report, doctored behavior.summary) -- B-16.3's five ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
          ("run", "exec.want", "broken", "B16.3", "B16", "B17", "familyC", "B15"))
    for name, broken in (
            ("TBALL", TBIDS),
            ("TBTWO", TBIDS[:2]),
            ("TBONE", TBIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
              (name, g["b16.3.exec.want"], g["b16.3.exec.broken"],
               cell(st["b16p"][1]), st["b16"], st["b17"], st["c"], st["b15"]))

    # --- SW* and VH*: five ids and four, and BOTH IN FAMILY B NOW
    #
    # SWIDS scales B-17.3 and VHIDS scales B-18.4, and they are five ids and
    # four: SWTWO reads 0.600000 where VHTWO reads 0.500000, SWONE 0.800000
    # where VHONE 0.750000. The fifth vh id was a defect one rung ago, not a
    # coincidence -- it was svcids.txt's, and while it sat in both files one
    # broken service was priced by B-15.3 and by step 11's C-4 at once. The two
    # fractions differing is a side effect of that repair worth naming, because
    # it means a scorer that handed one list to both legs is visible in the
    # arithmetic as well as in the columns.
    #
    # THE PAIR NOW SITS INSIDE ONE FAMILY, which makes the tables read
    # together harder and not easier: at step 11 a crossed scorer moved a
    # family-B column and a family-C column, two numbers a reader could not
    # confuse. Here it moves B17 and B18, adjacent columns of the same family
    # computed from adjacent weights, and the only thing separating them is
    # that break the switch ids and B18 must not move, break the access-failure
    # ids and B17 must not. Both triples print both columns for that reason.
    print()
    print("SW* (golden's report, doctored behavior.summary) -- B-17.3's five ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
          ("run", "exec.want", "broken", "B17.3", "B17", "B18", "familyC", "B15"))
    for name, broken in (
            ("SWALL", SWIDS),
            ("SWTWO", SWIDS[:2]),
            ("SWONE", SWIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
              (name, g["b17.3.exec.want"], g["b17.3.exec.broken"],
               cell(st["b17p"][1]), st["b17"], st["b18"], st["c"], st["b15"]))

    print()
    print("VH* (golden's report, doctored behavior.summary) -- B-18.4's four ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
          ("run", "exec.want", "broken", "B18.4", "B18", "B17", "familyC", "B15"))
    for name, broken in (
            ("VHALL", VHIDS),
            ("VHTWO", VHIDS[:2]),
            ("VHONE", VHIDS[:1])):
        st = score(GOLD.clone(), {}, list(broken))
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s %-8s %-8s" %
              (name, g["b18.4.exec.want"], g["b18.4.exec.broken"],
               cell(st["b18p"][2]), st["b18"], st["b17"], st["c"], st["b15"]))

    # --- GONE: one absence, six consequences
    #
    # behavior.summary is not B-10's file, or B-14's, or B-15's, or B-16's, or
    # B-17's, or family C's. It is ONE file that six provisos read against six
    # different id lists, and deleting it is one event with six results. Step
    # 08 printed its two-family version of this twice before its own note
    # caught it, each copy reporting one gate at zero and the other at
    # 1.000000 -- a tree that cannot exist. So it is one run, printed once, and
    # c-rehearsal.sh asserts all six sixths of it off the same deletion.
    #
    # FIVE ARE PRINTED HERE AND THE SIXTH IS NOT, for the reason every rung
    # since step 03 has not printed it: `b10_score` above models B-10's three
    # thirds and not the proviso that halves the third of them, so this model
    # would report B-10 at 1.000000 for a deletion the shipped scorer answers
    # with 0.4 + 0.3 + 0.3*0.5 = 0.850000. c-rehearsal.sh asserts that figure
    # by hand off this same deletion, exactly as step 10's did, and the number
    # is the inherited leg's rather than this rung's. Printing five and
    # asserting six is the arrangement; claiming five would be the defect.
    print()
    print("GONE (golden's report, no behavior.summary at all) -- six provisos, one absence:")
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
    print("  B-17.3  want=%-4s broken=%-4s exec=%-13s  B17.3=%-9s B17=%s" %
          (g["b17.3.exec.want"], g["b17.3.exec.broken"], g["b17.3.exec"],
           cell(st["b17p"][1]), st["b17"]))
    print("  B-18.4  want=%-4s broken=%-4s exec=%-13s  B18.4=%-9s B18=%s" %
          (g["b18.4.exec.want"], g["b18.4.exec.broken"], g["b18.4.exec"],
           cell(st["b18p"][2]), st["b18"]))
    # AND THE SIXTH CONSEQUENCE THIS ROW HAS THAT ITS ANCESTORS DID NOT:
    # familyC is untouched. Printed beside the five so the rehearsal can assert
    # the non-event -- a deletion that moved family C at this rung would mean a
    # leg is reading a file none of the five is supposed to read.
    print("  family C unmoved by the deletion: familyC=%s  (C1..C5 %s)" %
          (st["c"], " ".join(cell(x) for x in st["cp"])))

    print()
    print("gate facts, per fixture:")
    for name, st, note in rows:
        print("  %s  (%s)" % (name, note))
        g = st["gates"]
        for k in sorted(g):
            print("      gate.%s=%s" % (k, g[k]))


if __name__ == "__main__":
    main()
