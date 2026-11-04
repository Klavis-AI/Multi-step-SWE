#!/usr/bin/env python3
"""Build step 06's rehearsal fixtures for family C, B-10, B-11 and B-12, and
compute what each one must score.

    python3 authoring/fixtures-step06/mkfix06.py

Writes one directory per fixture under fixtures-step06/c/, each holding the
source report the shipped scoring blocks will read, the compile answers they
would have got from javac, a behaviour summary, the `jars/` directory B-12e
unzips, and -- new at this step -- the `scan.txt` C-1 reads. Prints the score
table that c-rehearsal.sh asserts.

FIVE blocks, not four. Step 05 rehearsed the source-AST block, B-10, B-11 and
family C; this step adds B-12, which holds step 05's own four library gates in
place and reads the same report. All five are extracted from the shipped
test.sh and all five are scored here.

TWO ARTEFACT CHANNELS, not one. B-12e unzips the published jars and binary-greps
their constant pools; C-1 reads ${WORK}/scan.txt, which the build stage writes
from arqgate.Scan over the same jars. They are independent inputs to the
scorer -- a fixture can have jars and no scan, or a scan and no jars -- and the
fixtures below exercise each on its own, because a rehearsal that always supplied
both could not tell a gate reading the wrong one apart from a gate reading the
right one.

THE RECORDS ARE THE INPUT TREE'S OWN, LIFTED VERBATIM. Step 05 synthesised the
arrival halves and lifted the departure halves, because the arrival tree did not
exist yet. At this rung the relationship is INVERTED for B-12 and unchanged for
family C, and the reason is worth stating: step 06's input is the step-05
GOLDEN, so B-12's four gates are already satisfied on it -- the 16 Optional
returns are there, the 23 collectors are there, the 3 value types delegate, the
4 timing sites read java.time. What has to be synthesised for B-12 is therefore
the UN-migration, and the records it restores are lifted out of
baseline-step05-source.txt, which is the step-04 golden's own measured report.
Family C is the other way round, as at every rung: its departure records
(newinstance, accessible, boxedctor, legacycoll, optop) are lifted out of
baseline-step06-source.txt and its arrival records (the ctor form, the factory
calls, the Java-9 Optional idioms) are synthesised.

So PINNED is not a plausible-looking retyping of the input tree. It is the input
tree's own measured report, counter for counter and record for record, and
check_pinned() below compares every counter it emits against
baseline-step06-source.txt directly.

The `want` figures are computed here, by an independent implementation of the
same composition, and then written into c-rehearsal.sh BY HAND. The rehearsal
never calls this file: a rehearsal that computes its own expectations at run
time agrees with the scorer by construction and asserts nothing. The verifier
image has no python3 either, which makes that separation structural rather than
a matter of discipline.
"""
import os
import shutil
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
AUTH = os.path.dirname(HERE)
OUT = os.path.join(HERE, "c")

S06 = os.path.join(AUTH, "baseline-step06-source.txt")
S05 = os.path.join(AUTH, "baseline-step05-source.txt")


def read_env(path):
    d = {}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            if "[" in k:
                continue
            d[k] = v
    return d


def read_set(path):
    out = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                out.append(line)
    return out


def read_records(path, kind):
    """Every `kind[i]=...` record body in a report, in file order."""
    pref = kind + "["
    out = []
    with open(path) as fh:
        for line in fh:
            if line.startswith(pref):
                out.append(line.rstrip("\n").split("=", 1)[1])
    return out


def bucket_of(body):
    """A record body -> its `<path>#<in= method>` bucket, exactly as buckets()
    in test.sh builds it: field 1 up to the colon, joined to the LAST in=."""
    fields = body.split()
    path = fields[0].split(":")[0]
    meth = None
    for f in fields[1:]:
        if f.startswith("in="):
            meth = f[3:]
    return None if meth is None else path + "#" + meth


def strip_in(body):
    """The same record as a gate jar too old to write the in= field emits it."""
    return " ".join(f for f in body.split() if not f.startswith("in="))


def split_bucket(b):
    path, meth = b.split("#", 1)
    return path, meth


def group(bodies):
    """{bucket: [body, ...]} in file order, dropping anything unattributed."""
    d = {}
    for body in bodies:
        b = bucket_of(body)
        if b is not None:
            d.setdefault(b, []).append(body)
    return d


# --- the frozen numbers, read rather than retyped -----------------------------
BASE = read_env(S06)
TGT = read_env(os.path.join(AUTH, "baseline-step06-target.txt"))
LEVEL = read_env(os.path.join(AUTH, "baseline-step06-level.txt"))

WANT_MAJOR = int(LEVEL["major"])                             # 54
BASE_SUP = int(BASE["suppress.sites"])                       # 44
BASE_RAW = int(BASE["newgeneric.raw"])                       # 0
# B-10's denominators: step 03's work.
T_DIA = int(TGT["target.diamond"])                           # 73
T_NONANON = int(TGT["target.explicit.nonanon"])              # 23
T_RES = int(TGT["target.try.resources"])                     # 9
T_IO = int(TGT["target.iofile"])                             # 2
B10_DIA = int(TGT["target.b10.diamond"])                     # 93
B10_CLOSE = int(TGT["target.b10.close"])                     # 11
B10_NIO = int(TGT["target.b10.nio"])                         # 11
# B-11's denominators: step 04's.
B11_LAMFILES = int(TGT["target.b11.lambdafiles"])            # 20
B11_SAM = int(TGT["target.b11.sam"])                         # 24
B11_CM = int(TGT["target.b11.collectmethods"])               # 24
# B-12's: step 05's four populations and the four constant-pool floors.
B12_OPT = int(TGT["target.b12.optbuckets"])                  # 16
B12_SINKS = int(TGT["target.b12.sinks"])                     # 23
B12_VT = int(TGT["target.b12.valuetypes"])                   # 3
B12_TIME = int(TGT["target.b12.timesites"])                  # 4
B12_FLOORS = (int(TGT["target.b12.optional.floor"]),         # 5
              int(TGT["target.b12.collectors.floor"]),       # 6
              int(TGT["target.b12.objects.floor"]),          # 2
              int(TGT["target.b12.jtime.floor"]))            # 2
# Family C's own seven.
C2_LEGACY = int(TGT["target.c2.legacy"])                     # 2
C2_CTORFLOOR = int(TGT["target.c2.ctor.floor"])              # 13
C3_GUARDS = int(TGT["target.c3.guards"])                     # 12
C3_SETACC = int(TGT["target.c3.setacc"])                     # 14
C4_BOXED = int(TGT["target.c4.boxed"])                       # 12
C5_BUILDS = int(TGT["target.c5.builds"])                     # 7
C5_VIEWS = int(TGT["target.c5.views"])                       # 7
C6_METHODS = int(TGT["target.c6.methods"])                   # 3
C7_CAND = int(TGT["target.c7.candidatenew"])                 # 81
C7_WANT = int(TGT["target.c7.want"])                         # 80

LAMFILES = read_set(os.path.join(AUTH, "baseline-step04-lambdafiles.txt"))
SAMPAIRS = read_set(os.path.join(AUTH, "baseline-step04-samifaces.txt"))
COLLECTMETHODS = [l.split()[0] for l in
                  read_set(os.path.join(AUTH, "baseline-step04-collectmethods.txt"))]
NIOIDS = read_set(os.path.join(AUTH, "baseline-step03-nioids.txt"))

# Step 05's four frozen sets, which B-12 divides by.
OPTBUCKETS, OPTTYPE = [], {}
for line in read_set(os.path.join(AUTH, "baseline-step05-optbuckets.txt")):
    b, t = line.split(None, 1)
    OPTBUCKETS.append(b)
    OPTTYPE[b] = t
SINKS, SINKWANT = [], {}
for line in read_set(os.path.join(AUTH, "baseline-step05-sinks.txt")):
    f = line.split()
    SINKS.append(f[0])
    SINKWANT[f[0]] = f[1]
VALUETYPES = [l.split()[0] for l in
              read_set(os.path.join(AUTH, "baseline-step05-valuetypes.txt"))]
TIMESITES = [l.split()[0] for l in
             read_set(os.path.join(AUTH, "baseline-step05-timesites.txt"))]

# This step's own six, with the tag column the two two-list files carry.
NI_LEGACY = [l.split(None, 1)[1] for l in
             read_set(os.path.join(AUTH, "baseline-step06-newinstance.txt"))
             if l.split()[0] == "legacy"]
NI_CTOR = [l.split(None, 1)[1] for l in
           read_set(os.path.join(AUTH, "baseline-step06-newinstance.txt"))
           if l.split()[0] == "ctor"]
ACC_GUARD = [l.split(None, 1)[1] for l in
             read_set(os.path.join(AUTH, "baseline-step06-accessible.txt"))
             if l.split()[0] == "guard"]
ACC_SETACC = [l.split(None, 1)[1] for l in
              read_set(os.path.join(AUTH, "baseline-step06-accessible.txt"))
              if l.split()[0] == "setacc"]
VIEWS = read_set(os.path.join(AUTH, "baseline-step06-views.txt"))
BUILDS = read_set(os.path.join(AUTH, "baseline-step06-builds.txt"))
OPTOPS = [l.split()[0] for l in
          read_set(os.path.join(AUTH, "baseline-step06-optops.txt"))]
OPTOP_SITES = dict((l.split()[0], int(l.split()[1].split("=")[1]))
                   for l in read_set(os.path.join(AUTH, "baseline-step06-optops.txt")))
BOXED = read_set(os.path.join(AUTH, "baseline-step06-boxedctor.txt"))
VARBUCKETS = read_set(os.path.join(AUTH, "baseline-step06-varbuckets.txt"))

assert len(LAMFILES) == B11_LAMFILES, len(LAMFILES)
assert len(SAMPAIRS) == B11_SAM, len(SAMPAIRS)
assert len(COLLECTMETHODS) == B11_CM, len(COLLECTMETHODS)
assert len(OPTBUCKETS) == B12_OPT and len(SINKS) == B12_SINKS
assert len(VALUETYPES) == B12_VT and len(TIMESITES) == B12_TIME
assert len(NI_LEGACY) == C2_LEGACY, len(NI_LEGACY)
assert len(NI_LEGACY) + len(NI_CTOR) == C2_CTORFLOOR, len(NI_CTOR)
assert len(ACC_GUARD) == C3_GUARDS and len(ACC_SETACC) == C3_SETACC
assert set(ACC_GUARD) < set(ACC_SETACC)
assert len(BOXED) == C4_BOXED and len(BUILDS) == C5_BUILDS
assert len(VIEWS) == C5_VIEWS and len(OPTOPS) == C6_METHODS
assert len(VARBUCKETS) == C7_CAND
assert sum(OPTOP_SITES.values()) == int(BASE["optop.ispresent"]) == 6

# --- the input tree's records, by kind ----------------------------------------
# Everything the five blocks read is lifted out of the input's own report. The
# per-kind assertions below are checklist item 63 at this rung: every bucket a
# gate will ask about has to appear in the baseline's records with the record
# type that gate reads, or the fixture is rehearsing a tree that does not exist.
R_LAMBDA = read_records(S06, "lambda")
R_METHODREF = read_records(S06, "methodref")
R_IFACE = read_records(S06, "interfacedecl")
R_STREAM = read_records(S06, "stream")
R_STREAMOP = read_records(S06, "streamop")
R_RETNULL = read_records(S06, "retnull")       # 25, none of them in OPTBUCKETS
R_OPTRET = read_records(S06, "optret")         # 16, exactly OPTBUCKETS
R_OPTOP = read_records(S06, "optop")           # 46
R_ACCUM = read_records(S06, "accum")           # 1, not in SINKS
R_COLLECT = read_records(S06, "collect")       # 23, exactly SINKS
R_OBJECTS = read_records(S06, "objects")       # 6
R_JTIME = read_records(S06, "jtime")           # 11 in the 4 TIMESITES
R_MILLIS = read_records(S06, "millis")         # 0
R_NEWINST = read_records(S06, "newinstance")   # 13
R_ACCESS = read_records(S06, "accessible")     # 26
R_BOXED = read_records(S06, "boxedctor")       # 12
R_LEGACY = read_records(S06, "legacycoll")     # 25
R_GETPKG = read_records(S06, "getpackage")     # 2

assert BOXED == R_BOXED, "boxedctor.txt is not the report's own records"

# B-12's arrival side, already standing on the input tree.
OPTRET_BY = group(R_OPTRET)
COLLECT_BY = group(R_COLLECT)
OBJECTS_BY = group(R_OBJECTS)
JTIME_BY = group(R_JTIME)
assert sorted(OPTRET_BY) == sorted(OPTBUCKETS)
assert sorted(COLLECT_BY) == sorted(SINKS)
assert sorted(JTIME_BY) == sorted(TIMESITES)
# ...and its departure side, already gone: the residue that was never in scope.
RETNULL_REST = list(R_RETNULL)
ACCUM_REST = list(R_ACCUM)
MILLIS_REST = list(R_MILLIS)
assert not (set(group(R_RETNULL)) & set(OPTBUCKETS))
assert not (set(group(R_ACCUM)) & set(SINKS))
assert not MILLIS_REST

# The collector each sink actually uses on the input tree, and the Objects call
# each value type actually makes. Read, not retyped: seven of the 23 are
# toCollection where sinks.txt names toList or toSet, and the acceptance rule
# that lets them through is load-bearing on the tree this rung is handed.
def _field(body, pref):
    for f in body.split()[1:]:
        if f.startswith(pref):
            return f[len(pref):]
    return None


BASE_COLLECT = dict((b, _field(v[0], "to=")) for b, v in COLLECT_BY.items())
assert len([b for b in SINKS if BASE_COLLECT[b] != SINKWANT[b]]) == 7
BASE_OBJECTS = tuple((bucket_of(body), body.split()[1]) for body in R_OBJECTS)
BASE_JTIME = dict((b, tuple(v.split()[1] for v in vs)) for b, vs in JTIME_BY.items())
assert sum(len(v) for v in BASE_JTIME.values()) == len(R_JTIME) == 11

# The 42 records that were the step-04 golden's, for the fixtures that UNDO
# B-12. Lifted from step 05's own baseline rather than invented, so that a
# fixture claiming "the 16 helpers return null again" restores the sites that
# really did.
def by_bucket(path, kind, wanted):
    inset, rest = {}, []
    for body in read_records(path, kind):
        b = bucket_of(body)
        if b in wanted:
            inset.setdefault(b, []).append(body)
        else:
            rest.append(body)
    return inset, rest


UNDO_RETNULL, _r = by_bucket(S05, "retnull", set(OPTBUCKETS))
UNDO_ACCUM, _a = by_bucket(S05, "accum", set(SINKS))
UNDO_MILLIS, _m = by_bucket(S05, "millis", set(TIMESITES))
assert sorted(UNDO_RETNULL) == sorted(OPTBUCKETS)
assert sorted(UNDO_ACCUM) == sorted(SINKS)
assert sorted(UNDO_MILLIS) == sorted(TIMESITES)
assert len(_r) == len(RETNULL_REST) and len(_a) == len(ACCUM_REST)

# Family C's departure side, indexed the way the fixtures address it.
NEWINST_BY = {}
for body in R_NEWINST:
    NEWINST_BY[(bucket_of(body), _field(body, "form="))] = body
assert sorted(b for (b, f) in NEWINST_BY if f == "legacy") == sorted(NI_LEGACY)
assert sorted(b for (b, f) in NEWINST_BY if f == "ctor") == sorted(NI_CTOR)

ACCESS_BY = {}
for body in R_ACCESS:
    ACCESS_BY[(bucket_of(body), _field(body, "call="))] = body
assert sorted(b for (b, c) in ACCESS_BY if c == "isAccessible") == sorted(ACC_GUARD)
assert sorted(b for (b, c) in ACCESS_BY if c == "setAccessible") == sorted(ACC_SETACC)
assert len(ACCESS_BY) == len(R_ACCESS) == 26

BOXED_BY = group(R_BOXED)

# legacycoll splits three ways, and the third way is the one the gate needed a
# union for: `kind=build` is six of builds.txt's seven, and the seventh --
# DeploymentScenario#getStartupDeploymentsFor -- carries `kind=view arg=local`,
# an unmodifiableList over a list the method has just finished building. It is a
# copy wearing a window's spelling. C-5's departure half reads both shapes.
LC_BUILD, LC_LOCAL, LC_VIEWF = {}, {}, {}
for body in R_LEGACY:
    b = bucket_of(body)
    kind, arg = _field(body, "kind="), _field(body, "arg=")
    if kind == "build":
        LC_BUILD.setdefault(b, []).append(body)
    elif kind == "view" and arg == "local":
        LC_LOCAL.setdefault(b, []).append(body)
    elif kind == "view" and arg == "field":
        LC_VIEWF.setdefault(b, []).append(body)
    else:
        raise AssertionError("unclassified legacycoll record: " + body)
assert sorted(LC_VIEWF) == sorted(VIEWS)
assert set(BUILDS) <= (set(LC_BUILD) | set(LC_LOCAL))
assert len(set(BUILDS) & set(LC_LOCAL)) == 1
assert sum(len(v) for v in LC_BUILD.values()) == int(BASE["legacycoll.build"]) == 17
assert sum(len(v) for v in LC_VIEWF.values()) == int(BASE["legacycoll.viewfield"]) == 7
# Only 6 of the 7 builds.txt buckets hold a `kind=build` record. Asserted rather
# than remarked: if a later gate rebuild classified DeploymentScenario:122 as a
# build, the union in test.sh would become redundant and this line would say so.
assert len(set(BUILDS) & set(LC_BUILD)) == 6

# The two setAccessible sites that are NOT among C-3's twelve guarded ones.
# K26 is built on one of them: the penalty has to fire on a tree that lost a
# reflective capability WITHOUT C-3 itself noticing, and deleting a guarded
# site would take a C-3 arrival with it and confound the two readings.
ACC_UNGUARDED = [b for b in ACC_SETACC if b not in set(ACC_GUARD)]
assert len(ACC_UNGUARDED) == C3_SETACC - C3_GUARDS == 2, ACC_UNGUARDED

# optop splits four ways: the 21 constructions, the 19 consumptions that are
# neither isPresent nor get, the 6 isPresent branches C-6 asks to depart, and
# the 0 get() calls step 05 already removed.
OPT_MAKES = [b for b in R_OPTOP if b.split()[1] == "make"]
OPT_USES_OTHER = [b for b in R_OPTOP
                  if b.split()[1] == "use" and b.split()[2] not in ("isPresent", "get")]
OPT_ISPRESENT = group([b for b in R_OPTOP
                       if b.split()[1] == "use" and b.split()[2] == "isPresent"])
assert len(OPT_MAKES) == 21 and len(OPT_USES_OTHER) == 19
assert sorted(OPT_ISPRESENT) == sorted(OPTOPS)
assert int(BASE["optop.get"]) == 0
assert len(R_OPTOP) == 46

# The 81 `Foo x = new Foo(..)` declarations, and the one of them that is exempt.
VAR_EXEMPT = [l for l in VARBUCKETS if "DynamicServiceLoader.java:196" in l]
assert len(VAR_EXEMPT) == 1

# --- somewhere that is not one of the frozen sets -----------------------------
# Every arrival half is a set intersection, so the fixture that writes the right
# construct in the wrong place has to have a wrong place to write it in.
ELSEWHERE = ["zz-added/src/main/java/zz/New%02d.java#go" % i for i in range(1, 25)]

# --- the golden's own shape ---------------------------------------------------
# Predicted, not measured: item 4.3's calibration run has not happened yet, and
# when it does its report is reconciled against this and the TABLE re-transcribed.
# What is NOT a prediction is the set membership -- which buckets depart and
# which arrive -- because those are the frozen sets above, and they are what the
# gates actually score.
GOLDEN_FACTORY = {
    "impl-base/src/main/java/org/jboss/arquillian/impl/ArquillianProfile.java#getClientProfile":
        ("java.util.List.of", 17),
    "impl-base/src/main/java/org/jboss/arquillian/impl/ArquillianProfile.java#getContainerProfile":
        ("java.util.List.of", 14),
    "junit/src/main/java/org/jboss/arquillian/junit/Arquillian.java#<init>":
        ("java.util.List.of", 1),
    "junit/src/main/java/org/jboss/arquillian/junit/JUnitTestRunner.java#getRunListeners":
        ("java.util.List.of", 0),
    "spi/src/main/java/org/jboss/arquillian/spi/client/deployment/DeploymentScenario.java#getStartupDeploymentsFor":
        ("java.util.List.copyOf", 1),
    "testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/MethodParameterInjectionPoint.java#getAnnotations":
        ("java.util.Set.of", 1),
    "testng/src/main/java/org/jboss/arquillian/testng/TestNGTestRunner.java#execute":
        ("java.util.List.of", 1),
}
assert sorted(GOLDEN_FACTORY) == sorted(BUILDS)
# One Java-9 consumption per isPresent site the branch replaced, which conserves
# the tree's Optional-use count across the step: 25 uses before, 25 after, none
# of them get(). The two-site method becomes two ifPresentOrElse calls and the
# three-site one an or()-chain.
GOLDEN_OPT9 = tuple(
    [(OPTOPS[0], "ifPresentOrElse")] * OPTOP_SITES[OPTOPS[0]]
    + [(OPTOPS[1], "or")] * OPTOP_SITES[OPTOPS[1]]
    + [(OPTOPS[2], "or")] * OPTOP_SITES[OPTOPS[2]])
assert len(GOLDEN_OPT9) == 6

# The class-file scan the build stage writes. 757 classes at major 52 is the
# figure the step-05 oracle run measured on this very tree (gate.classes=757,
# gate.major.52=757, gate.majors.distinct=1 in the m04-s05b-oracle facts); the
# golden's is the same population recompiled at --release 10.
SCAN_CLASSES = 757
SCAN_IN = (SCAN_CLASSES, {52: SCAN_CLASSES})
SCAN_OUT = (SCAN_CLASSES, {WANT_MAJOR: SCAN_CLASSES})

# --- the two measurements -----------------------------------------------------
PINNED = dict(
    # counters nothing at this step derives from a record
    files=int(BASE["files"]), suppress_sites=BASE_SUP,
    anonfn=int(BASE["anonymousfunctional.size"]),
    conv=int(BASE["anonymousfunctional.convertible"]),
    forward=int(BASE["anonymousfunctional.forward"]),
    sam=int(BASE["iface.sam"]),
    foreach=int(BASE["foreach.size"]), collect=int(BASE["foreach.collect"]),
    loop=int(BASE["loop.size"]),
    explicit=int(BASE["newgeneric.explicit"]),
    explicitanon=int(BASE["newgeneric.explicitanon"]),
    diamond=int(BASE["newgeneric.diamond"]),
    diamondfiles=int(BASE["newgeneric.diamondfiles"]),
    raw=BASE_RAW,
    resources=int(BASE["try.resources"]),
    resourcevars=int(BASE["try.resourcevars"]),
    close=int(BASE["close.manual"]),
    ionew=int(BASE["iofile.new"]), iodecl=int(BASE["iofile.decl"]),
    nioref=int(BASE["niofile.ref"]), niostatic=int(BASE["niofile.staticref"]),
    getpkg=int(BASE["getpackage.size"]),
    # step 03's and step 04's constructs, as records
    lam=tuple(R_LAMBDA), mref=tuple(R_METHODREF),
    strm=tuple(R_STREAM), strmop=tuple(R_STREAMOP),
    iface_annotated=tuple(SAMPAIRS),
    # step 05's four, ALL OF THEM ALREADY DONE: this is the step-05 golden.
    retnull_left=(), optret_at=tuple(OPTBUCKETS),
    accum_left=(), collect_at=dict(BASE_COLLECT),
    objects_at=BASE_OBJECTS,
    millis_left=(), jtime_at=tuple(TIMESITES),
    b12jars=(10, 13, 3, 4), jars="full",
    # this step's seven, NONE of them done
    ni_leg=tuple(NI_LEGACY), ni_ctor=tuple(NI_CTOR),
    acc_guard=tuple(ACC_GUARD), acc_setacc=tuple(ACC_SETACC), canacc=(),
    boxed=tuple(BOXED),
    lc_drop=frozenset(), lc_viewf=tuple(VIEWS), factory_at=(),
    opt_makes=tuple(OPT_MAKES), opt_uses=tuple(OPT_USES_OTHER),
    isp_left=dict((b, OPTOP_SITES[b]) for b in OPTOPS), opt9_at=(),
    var_n=int(BASE["var.size"]), varcand_n=int(BASE["var.candidates"]),
    varnew_n=int(BASE["var.candidatenew"]), varmulti=int(BASE["var.multideclarator"]),
    scan=SCAN_IN,
    no_in=False, broke_nio=False,
    ok10="yes", ok8="yes", okdef="yes", e_err=0,
)
GOLDEN = dict(
    PINNED,
    # C-1: the same 757 classes, emitted at major 54.
    scan=SCAN_OUT,
    # C-2: both Class.newInstance sites become Constructor.newInstance, so the
    # ctor population rises from 11 to 13 -- which is the floor, exactly.
    ni_leg=(), ni_ctor=tuple(NI_CTOR) + tuple(NI_LEGACY),
    # C-3: the twelve guards deleted, all fourteen setAccessible calls kept.
    acc_guard=(),
    # C-4: the twelve wrapper constructors become valueOf.
    boxed=(),
    # C-5: the seven constructions become factories, the seven views stay views.
    lc_drop=frozenset(BUILDS), factory_at=tuple(sorted(GOLDEN_FACTORY.items())),
    # C-6: the six isPresent branches become or / ifPresentOrElse.
    isp_left={}, opt9_at=GOLDEN_OPT9,
    # C-7: eighty of the eighty-one redundant declarations become var.
    var_n=C7_WANT, varnew_n=C7_CAND - C7_WANT,
    varcand_n=int(BASE["var.candidates"]) - C7_WANT,
    # what the step's own work does to the counters around it: the one diamond
    # in the 81 is the exempt declaration, which the golden leaves alone, so
    # newgeneric.diamond falls by one only because MethodParameterInjectionPoint's
    # `new HashSet<>(..)` went with Set.of.
    diamond=T_DIA,
    # ...and the tree no longer compiles at -source 8, which is the point of the
    # rung: `var` is a Java 10 language feature and it is now load-bearing.
    ok8="no", e_err=C7_WANT,
)
assert GOLDEN["explicit"] - GOLDEN["explicitanon"] == T_NONANON
assert PINNED["explicit"] - PINNED["explicitanon"] == T_NONANON
assert PINNED["diamond"] == T_DIA + 1


def variant(base, **kw):
    d = dict(base)
    d.update(kw)
    return d


def retag_use(body, name):
    """One `optop ... use <what> ...` record, re-spelled as a different use."""
    fields = body.split()
    assert fields[1] == "use", body
    fields[2] = name
    return " ".join(fields)


# The one bucket that C-5 and B-12b both score, from opposite ends: it builds a
# collection by hand (`kind=view arg=local` -- an unmodifiableList over a list
# it has just finished filling) AND it is one of step 05's 23 collector sinks.
# The golden has to satisfy both at once, which is why the co-existence control
# below is built on this bucket and not on an easier one.
BUILD_SINK = sorted(set(BUILDS) & set(LC_LOCAL) & set(SINKS))
assert len(BUILD_SINK) == 1, BUILD_SINK
BUILD_SINK = BUILD_SINK[0]


# --- the catalogue ------------------------------------------------------------
# Forty-five K fixtures and the two ends. Each K names the one thing about a tree
# it is asking the scorer to notice; several share a total on purpose, because
# the gates exist to separate trees a total cannot, and c-rehearsal.sh asserts
# the facts that do the separating.
FIX = [
    ("pinned", PINNED,
     "this rung's INPUT -- the step-05 golden: B-10, B-11 and B-12 in full, "
     "family C nothing"),
    ("golden", GOLDEN,
     "the step's solution: all seven family-C gates and all three held gates in "
     "full"),

    # --- C-1: 757 class files at major 54 --------------------------------------
    ("K1", variant(GOLDEN, scan=SCAN_IN),
     "the whole tree recompiled -- but still at --release 8, so every class is "
     "major 52"),
    ("K2", variant(GOLDEN, scan=(SCAN_CLASSES, {52: 57, WANT_MAJOR: 700})),
     "700 of 757 classes at major 54 and 57 left at 52 -- two majors in the "
     "published jars"),
    ("K3", variant(GOLDEN, scan=None),
     "the build published jars and the class-file scan produced nothing -- the "
     "C-1 instrument, not the tree"),
    ("K4", variant(GOLDEN, scan=(SCAN_CLASSES, {WANT_MAJOR: 600})),
     "600 of 757 classes at major 54 and the rest not scanned at all"),

    # --- C-2: Class.newInstance -> Constructor.newInstance ---------------------
    ("K5", variant(GOLDEN, ni_leg=tuple(NI_LEGACY), ni_ctor=tuple(NI_CTOR)),
     "both Class.newInstance() calls left exactly where they were"),
    ("K6", variant(GOLDEN, ni_ctor=tuple(NI_CTOR)),
     "both reflective constructions rewritten to something that is not "
     "Constructor.newInstance"),
    ("K7", variant(GOLDEN, ni_ctor=tuple(NI_LEGACY) + tuple(NI_CTOR[:4])),
     "the two sites migrated and seven other reflective constructions deleted -- "
     "the ctor floor"),
    ("K8", variant(GOLDEN, ni_leg=tuple(NI_LEGACY[:1]),
                   ni_ctor=tuple(NI_LEGACY[1:]) + tuple(NI_CTOR)),
     "one of the two Class.newInstance() calls migrated and the other left"),

    # --- C-3: the isAccessible() guards ---------------------------------------
    ("K9", variant(GOLDEN, acc_guard=tuple(ACC_GUARD)),
     "every isAccessible() guard still in place around every setAccessible()"),
    ("K10", variant(GOLDEN, acc_setacc=()),
     "the guards removed by deleting the setAccessible() calls with them"),
    ("K11", variant(GOLDEN, acc_guard=tuple(ACC_GUARD[:6])),
     "six of the twelve isAccessible() guards removed"),

    # --- C-4: the wrapper constructors ----------------------------------------
    ("K12", variant(GOLDEN, boxed=tuple(BOXED)),
     "every `new Integer(..)` left standing"),
    ("K13", variant(GOLDEN,
                    boxed=tuple(BOXED_BY[sorted(BOXED_BY)[0]][:1]
                                + BOXED_BY[sorted(BOXED_BY)[1]][:1])),
     "two wrapper constructors in two methods left behind"),

    # --- C-5: the collection factories ----------------------------------------
    ("K14", variant(GOLDEN, lc_drop=frozenset()),
     "List.of() written alongside the hand-built collections rather than "
     "instead of them"),
    ("K15", variant(GOLDEN, factory_at=()),
     "the seven hand-built collections removed and no factory call written"),
    ("K16", variant(GOLDEN, lc_viewf=(),
                    factory_at=tuple(sorted(GOLDEN_FACTORY.items()))
                    + tuple(sorted((v, ("java.util.List.copyOf", 1)) for v in VIEWS))),
     "a codemod that converted the seven unmodifiable views over fields as well "
     "as the seven constructions"),
    ("K17", variant(GOLDEN, lc_drop=frozenset(BUILDS[:6]),
                    factory_at=tuple(sorted(GOLDEN_FACTORY.items()))[:6]),
     "six of the seven hand-built collections converted"),

    # --- C-6: the Java 9 Optional idioms --------------------------------------
    ("K18", variant(GOLDEN, isp_left=dict((b, OPTOP_SITES[b]) for b in OPTOPS)),
     "or()/ifPresentOrElse() written and every isPresent() branch left beside it"),
    ("K19", variant(GOLDEN, opt9_at=()),
     "the isPresent() branches rewritten to map/orElse rather than to a Java 9 "
     "idiom"),
    ("K20", variant(GOLDEN, isp_left={OPTOPS[2]: 1},
                    opt9_at=tuple(x for x in GOLDEN_OPT9 if x[0] != OPTOPS[2])
                    + ((OPTOPS[2], "or"),) * 2),
     "one of ExceptionProxy's three isPresent() branches left behind -- C-6 "
     "counts buckets"),

    # --- C-7: var --------------------------------------------------------------
    ("K21", variant(GOLDEN, var_n=0, varnew_n=C7_CAND,
                    varcand_n=int(BASE["var.candidates"])),
     "not one redundant declaration converted to var"),
    ("K22", variant(GOLDEN, var_n=40, varnew_n=C7_CAND - 40,
                    varcand_n=int(BASE["var.candidates"]) - 40),
     "forty of the eighty redundant declarations converted"),
    ("K23", variant(GOLDEN, varnew_n=C7_CAND,
                    varcand_n=int(BASE["var.candidates"])),
     "eighty `var` declarations written somewhere other than the eighty asked "
     "for"),

    # --- the multiplier --------------------------------------------------------
    ("K24", variant(GOLDEN, raw=BASE_RAW + 5),
     "five raw types added to make the tree compile"),
    ("K25", variant(GOLDEN, suppress_sites=BASE_SUP + 6),
     "six new @SuppressWarnings sites"),
    ("K26", variant(GOLDEN, acc_setacc=tuple(b for b in ACC_SETACC
                                             if b != ACC_UNGUARDED[0])),
     "one setAccessible() call outside the twelve guarded ones deleted -- C-3 "
     "reads full and the tree lost a capability"),
    ("K27", variant(GOLDEN, lc_viewf=tuple(VIEWS[:6]),
                    factory_at=tuple(sorted(GOLDEN_FACTORY.items()))
                    + ((VIEWS[6], ("java.util.List.copyOf", 1)),)),
     "one of the seven unmodifiable views over a field replaced by a copy"),

    # --- the instrument --------------------------------------------------------
    ("K28", variant(GOLDEN, ok10="no", ok8="no", okdef="no", e_err=48),
     "nothing compiled: every gate that reads a report must say so, and C-1's "
     "reach must pay nothing"),
    ("K29", variant(GOLDEN, no_in=True),
     "a verifier built from a gate jar too old to write the in= field -- every "
     "record present, none attributable"),

    # --- B-10: step 03's constructs ------------------------------------------
    ("K30", variant(GOLDEN, diamond=0, explicit=int(BASE["newgeneric.explicit"]) + T_DIA),
     "every diamond unwound back to an explicit type argument"),
    ("K31", variant(GOLDEN, resources=0, close=B10_CLOSE),
     "every try-with-resources unwound back to a finally block"),
    ("K32", variant(GOLDEN, nioref=0, niostatic=0, ionew=1 + B10_NIO, iodecl=1),
     "java.nio.file reverted to java.io.File"),
    ("K33", variant(GOLDEN, broke_nio=True),
     "java.nio.file held and the recorded file behaviours no longer match"),

    # --- B-11: step 04's constructs ------------------------------------------
    ("K34", variant(GOLDEN,
                    lam=tuple(b for b in R_LAMBDA
                              if b.split()[0].split(":")[0] not in set(LAMFILES)),
                    mref=tuple(b for b in R_METHODREF
                               if b.split()[0].split(":")[0] not in set(LAMFILES))),
     "every lambda and method reference gone from the twenty files step 04 "
     "converted"),
    ("K35", variant(GOLDEN, iface_annotated=()),
     "@FunctionalInterface stripped from all twenty-four interfaces"),
    ("K36", variant(GOLDEN,
                    strm=tuple(b for b in R_STREAM
                               if bucket_of(b) not in set(COLLECTMETHODS)),
                    strmop=tuple(b for b in R_STREAMOP
                                 if bucket_of(b) not in set(COLLECTMETHODS))),
     "every stream pipeline unwound in the twenty-four methods step 04 "
     "converted"),

    # --- B-12: step 05's four library gates ----------------------------------
    ("K37", variant(GOLDEN, retnull_left=tuple(OPTBUCKETS), optret_at=()),
     "the sixteen Optional-returning helpers reverted to returning null"),
    ("K38", variant(GOLDEN, accum_left=tuple(SINKS), collect_at={}),
     "the twenty-three collectors reverted to hand accumulation"),
    ("K39", variant(GOLDEN, objects_at=()),
     "the three value types' equals/hashCode written out by hand again"),
    ("K40", variant(GOLDEN, millis_left=tuple(TIMESITES), jtime_at=()),
     "java.time reverted to System.currentTimeMillis()"),
    ("K41", variant(GOLDEN, jars="empty", scan=(0, {})),
     "the build published jars carrying no class files -- both artifact "
     "channels read empty"),
    ("K42", variant(GOLDEN, jars="none", scan=None),
     "the build published no jar at all"),
    ("K45", variant(GOLDEN,
                    opt_uses=tuple(retag_use(x, "get") for x in OPT_USES_OTHER)),
     "the sixteen Optionals declared and nineteen of the twenty-five uses are "
     "get() -- the null moved rather than went"),

    # --- the two co-existence rows -------------------------------------------
    ("K43", variant(PINNED, scan=SCAN_OUT),
     "THE CHEAP TREE: nothing migrated, recompiled at --release 10 so every "
     "class is major 54"),
    ("K44", variant(GOLDEN, accum_left=(BUILD_SINK,),
                    collect_at=dict((b, v) for b, v in BASE_COLLECT.items()
                                    if b != BUILD_SINK)),
     "List.copyOf written over getStartupDeploymentsFor and its collector "
     "dropped -- C-5 holds, B-12b does not"),
]


# --- the arithmetic, reimplemented --------------------------------------------
def f6(x):
    return float("%.6f" % x)


def frac(n, d):
    return f6(0.0 if d <= 0 else float(n) / d)


def clamp01(v):
    if v < 0:
        v = 0
    if v > 1:
        v = 1
    return f6(v)


def minf(a, b):
    return f6(a if a < b else b)


def b10_score(f):
    """B-10: how much of step 03's work is still standing."""
    nonanon = max(0, f["explicit"] - f["explicitanon"])
    dia_back = max(0, nonanon - T_NONANON)
    d = minf(clamp01(frac(f["diamond"], T_DIA)),
             clamp01(frac(B10_DIA - dia_back, B10_DIA)))
    r = minf(clamp01(frac(f["resources"], T_RES)),
             clamp01(frac(B10_CLOSE - f["close"], B10_CLOSE)))
    now_io = f["ionew"] + f["iodecl"]
    io_back = max(0, now_io - T_IO)
    n = minf(clamp01(frac(f["nioref"] + f["niostatic"], B10_NIO)),
             clamp01(frac(B10_NIO - io_back, B10_NIO)))
    if f["broke_nio"]:
        n = f6(n / 2)
    return f6(0.4 * d + 0.3 * r + 0.3 * n), dict(
        d=d, r=r, n=n, nonanon=nonanon, dia_back=dia_back, io=now_io,
        io_back=io_back, broken=(len(NIOIDS) if f["broke_nio"] else 0))


def b11_score(f, r):
    """B-11: how much of step 04's work is still standing."""
    paths = set(b.split()[0].split(":")[0] for b in r["lambda"]) \
        | set(b.split()[0].split(":")[0] for b in r["methodref"])
    covl = len(paths & set(LAMFILES))
    l = clamp01(frac(covl, B11_LAMFILES))
    sam = len(set(f["iface_annotated"]) & set(SAMPAIRS))
    s = clamp01(frac(sam, B11_SAM))
    # No special case for the unattributed report: group() drops a record with
    # no in= exactly as buckets() does, so the intersection empties itself.
    covc = len(set(group(r["stream"])) & set(group(r["streamop"]))
               & set(COLLECTMETHODS))
    c = clamp01(frac(covc, B11_CM))
    return f6(0.20 * l + 0.20 * s + 0.60 * c), dict(
        l=l, s=s, c=c, covl=covl, sam=sam, covc=covc)


def cpcount(f):
    """B-12e's four binary greps, as class-file counts per package."""
    if f["jars"] in ("none", "empty"):
        return None
    return list(f["b12jars"])


def b12_score(f, r):
    """B-12: step 05's four library gates, held, and the jars that carry them.

    Returns the shell's own SPELLINGS. a..d are inside the measured-report
    guard and publish the literal `0` the block assigns when no compile
    produced a report; e is OUTSIDE it and publishes the literal `0` only when
    there were no jars or no class files in them. A measured 0.000000 and an
    unmeasured 0 are the same number and not the same fact.
    """
    d = {}
    if f["ok10"] != "yes" and f["okdef"] != "yes":
        a = b = c = jt = 0.0
        sp = ["0", "0", "0", "0"]
    else:
        # a: the sixteen null-returning helpers.
        left = set(bucket_of(x) for x in r["retnull"])
        got = set(bucket_of(x) for x in r["optret"])
        opt_dep = len([b_ for b_ in OPTBUCKETS if b_ not in left])
        opt_arr = len(got & set(OPTBUCKETS))
        uses = [x for x in r["optop"] if x.split()[1] == "use"]
        getn = len([x for x in uses if x.split()[2] == "get"])
        optcons = clamp01(frac(len(uses) - getn, len(uses)))
        a = minf(minf(clamp01(frac(opt_dep, B12_OPT)),
                      clamp01(frac(opt_arr, B12_OPT))), optcons)
        # b: the twenty-three hand-rolled sinks, and the collector each wants.
        aleft = set(bucket_of(x) for x in r["accum"])
        cgot = {}
        for x in r["collect"]:
            b_ = bucket_of(x)
            if b_ is not None and _field(x, "to=") is not None:
                cgot.setdefault(b_, set()).add(_field(x, "to="))
        sink_dep = len([b_ for b_ in SINKS if b_ not in aleft])
        sink_arr = len(set(cgot) & set(SINKS))
        sink_fid = 0
        for b_ in SINKS:
            w = SINKWANT[b_]
            have = cgot.get(b_, set())
            if w in have or (w in ("toList", "toSet") and "toCollection" in have):
                sink_fid += 1
        b = minf(clamp01(frac(sink_dep, B12_SINKS)),
                 clamp01(frac(sink_fid, B12_SINKS)))
        # c: the three value types, at method scope.
        objset = set((bucket_of(x), x.split()[1]) for x in r["objects"])
        vt_hit = len([vt for vt in VALUETYPES
                      if (vt + "#equals", "equals") in objset
                      and (vt + "#hashCode", "hash") in objset])
        c = clamp01(frac(vt_hit, B12_VT))
        # d: the four timing sites, as buckets.
        mleft = set(bucket_of(x) for x in r["millis"])
        jgot = set(bucket_of(x) for x in r["jtime"])
        time_dep = len([b_ for b_ in TIMESITES if b_ not in mleft])
        time_arr = len(jgot & set(TIMESITES))
        jt = minf(clamp01(frac(time_dep, B12_TIME)),
                  clamp01(frac(time_arr, B12_TIME)))
        sp = ["%.6f" % v for v in (a, b, c, jt)]
        d.update(opt_dep=opt_dep, opt_arr=opt_arr, uses=len(uses), get=getn,
                 cons=optcons, sink_dep=sink_dep, sink_arr=sink_arr,
                 sink_fid=sink_fid, vt=vt_hit, time_dep=time_dep,
                 time_arr=time_arr)
    # e, outside the guard: the published jars' constant pools, scaled by the
    # four source legs above. A tree the scorer could not compile has all four
    # at 0, so however fully its jars reach the library, e pays nothing.
    cp = cpcount(f)
    if cp is None:
        esp, e = "0", 0.0
        d.update(jars=(0 if f["jars"] == "none" else 2),
                 cp=(None if f["jars"] == "none" else [0, 0, 0, 0]))
    else:
        legs = [clamp01(frac(n, fl)) for n, fl in zip(cp, B12_FLOORS)]
        e = f6((legs[0] * a + legs[1] * b + legs[2] * c + legs[3] * jt) / 4)
        esp = "%.6f" % e
        d.update(jars=2, cp=cp, legs=legs)
    tot = f6(0.30 * a + 0.35 * b + 0.10 * c + 0.10 * jt + 0.15 * e)
    return "%.6f" % tot, sp + [esp], d


def c_score(f, r):
    """Family C: the Java 9/10 migration."""
    # C-1 first and OUTSIDE the guard: it reads the class-file scan, which a
    # tree the scorer could not compile against its own classpath may perfectly
    # well have produced.
    if f["scan"] is None:
        reach_sp, reach = "0", 0.0
        cd = dict(scan=None)
    else:
        classes, majors = f["scan"]
        cd = dict(scan=(classes, majors))
        if len(majors) == 1:
            reach = frac(majors.get(WANT_MAJOR, 0), classes)
            reach_sp = "%.6f" % reach
        else:
            reach_sp, reach = "0", 0.0
    measured = f["ok10"] == "yes" or f["okdef"] == "yes"
    if not measured:
        c2 = c3 = c4 = c5 = c6 = c7 = 0.0
        pen = 1.0
        sp = dict(c2="0", c3="0", c4="0", c5="0", c6="0", c7="0", pen=None)
    else:
        nib = dict()
        for x in r["newinstance"]:
            nib.setdefault(_field(x, "form="), set()).add(bucket_of(x))
        c2dep = len([b for b in NI_LEGACY if b not in nib.get("legacy", set())])
        c2arr = len([b for b in NI_LEGACY if b in nib.get("ctor", set())])
        now_nictor = len([x for x in r["newinstance"]
                          if _field(x, "form=") == "ctor"])
        c2 = minf(minf(clamp01(frac(c2dep, C2_LEGACY)),
                       clamp01(frac(c2arr, C2_LEGACY))),
                  clamp01(frac(now_nictor, C2_CTORFLOOR)))
        accb = dict()
        for x in r["accessible"]:
            accb.setdefault(_field(x, "call="), set()).add(bucket_of(x))
        c3dep = len([b for b in ACC_GUARD if b not in accb.get("isAccessible", set())])
        c3arr = len([b for b in ACC_GUARD if b in accb.get("setAccessible", set())])
        now_setacc = len([x for x in r["accessible"]
                          if _field(x, "call=") == "setAccessible"])
        c3 = minf(clamp01(frac(c3dep, C3_GUARDS)), clamp01(frac(c3arr, C3_GUARDS)))
        bxb = set(bucket_of(x) for x in r["boxedctor"])
        c4dep = len([b for b in BOXED_BY if b not in bxb])
        now_boxed = len(r["boxedctor"])
        c4 = minf(clamp01(frac(c4dep, len(BOXED_BY))),
                  clamp01(frac(C4_BOXED - now_boxed, C4_BOXED)))
        lcb = set()
        for x in r["legacycoll"]:
            if _field(x, "kind=") == "build" or \
               (_field(x, "kind=") == "view" and _field(x, "arg=") == "local"):
                lcb.add(bucket_of(x))
        lcvf = set(bucket_of(x) for x in r["legacycoll"]
                   if _field(x, "kind=") == "view" and _field(x, "arg=") == "field")
        facb = set(bucket_of(x) for x in r["factory"])
        c5dep = len([b for b in BUILDS if b not in lcb])
        c5arr = len([b for b in BUILDS if b in facb])
        c5 = minf(clamp01(frac(c5dep, C5_BUILDS)), clamp01(frac(c5arr, C5_BUILDS)))
        c5held = len([b for b in VIEWS if b in lcvf])
        ispb = set(bucket_of(x) for x in r["optop"]
                   if x.split()[1] == "use" and x.split()[2] == "isPresent")
        op9b = set(bucket_of(x) for x in r["optop"]
                   if x.split()[1] == "use"
                   and x.split()[2] in ("or", "ifPresentOrElse", "stream"))
        c6dep = len([b for b in OPTOPS if b not in ispb])
        c6arr = len([b for b in OPTOPS if b in op9b])
        c6 = minf(clamp01(frac(c6dep, C6_METHODS)), clamp01(frac(c6arr, C6_METHODS)))
        c7dep = clamp01(frac(C7_CAND - f["varnew_n"], C7_WANT))
        c7arr = clamp01(frac(f["var_n"], C7_WANT))
        c7 = minf(c7dep, c7arr)
        pen = 1.0
        penalised = False
        raw_added = max(0, f["raw"] - BASE_RAW)
        if raw_added > 0:
            pen = f6(max(0.0, 1 - raw_added / 10.0))
            penalised = True
        if f["suppress_sites"] > BASE_SUP:
            pen = f6(pen / 2)
            penalised = True
        if now_setacc < C3_SETACC:
            pen = f6(pen / 2)
            penalised = True
        if c5held < C5_VIEWS:
            pen = f6(pen / 2)
            penalised = True
        sp = dict(c2="%.6f" % c2, c3="%.6f" % c3, c4="%.6f" % c4,
                  c5="%.6f" % c5, c6="%.6f" % c6, c7="%.6f" % c7,
                  pen=("%.6f" % pen) if penalised else "1")
        cd.update(c2dep=c2dep, c2arr=c2arr, nictor=now_nictor, c3dep=c3dep,
                  c3arr=c3arr, setacc=now_setacc, c4dep=c4dep, boxed=now_boxed,
                  c5dep=c5dep, c5arr=c5arr, c5held=c5held, c6dep=c6dep,
                  c6arr=c6arr, c7dep=c7dep, c7arr=c7arr, raw=raw_added)
    # C-1, paid: the reading is scaled by the share of the other six gates that
    # corroborate it, renormalised over their 0.90 of family C. A tree that only
    # recompiled publishes its reach in full and is paid nothing for it.
    c1 = f6(reach * ((0.12 * c2 + 0.18 * c3 + 0.03 * c4 + 0.26 * c5
                      + 0.23 * c6 + 0.08 * c7) / 0.90))
    tot = f6(pen * (0.10 * c1 + 0.12 * c2 + 0.18 * c3 + 0.03 * c4
                    + 0.26 * c5 + 0.23 * c6 + 0.08 * c7))
    sp["reach"] = reach_sp
    sp["c1"] = "%.6f" % c1
    return "%.6f" % tot, sp, cd


# --- write them out -----------------------------------------------------------
# The report's counter lines, in two groups. PASSTHROUGH is the counters no
# block at this step derives from a record it also reads -- the source AST reads
# them and B-10, C-3's penalty and C-7 spend them. DERIVED is everything the
# records themselves determine, computed from those records rather than stated,
# because a report whose optop.size disagreed with its optop[] records is not a
# report any tree produces.
#
# Only the counters the five blocks read are written. That makes each fixture a
# report EXCERPT in exactly the shape the blocks consume: `var.candidates=496`
# appears with no varcand[] records under it, because C-7 is counters only and
# 496 unread records per fixture would be noise in every one of them.
PASSTHROUGH = [
    ("files", "files"),
    ("suppress.sites", "suppress_sites"),
    ("anonymousfunctional.size", "anonfn"),
    ("anonymousfunctional.convertible", "conv"),
    ("anonymousfunctional.forward", "forward"),
    ("iface.sam", "sam"),
    ("foreach.size", "foreach"),
    ("foreach.collect", "collect"),
    ("loop.size", "loop"),
    ("newgeneric.explicit", "explicit"),
    ("newgeneric.explicitanon", "explicitanon"),
    ("newgeneric.diamond", "diamond"),
    ("newgeneric.diamondfiles", "diamondfiles"),
    ("newgeneric.raw", "raw"),
    ("try.resources", "resources"),
    ("try.resourcevars", "resourcevars"),
    ("close.manual", "close"),
    ("iofile.new", "ionew"),
    ("iofile.decl", "iodecl"),
    ("niofile.ref", "nioref"),
    ("niofile.staticref", "niostatic"),
    ("var.size", "var_n"),
    ("var.candidates", "varcand_n"),
    ("var.candidatenew", "varnew_n"),
    ("var.multideclarator", "varmulti"),
]

KINDS = ["lambda", "methodref", "interfacedecl", "stream", "streamop",
         "retnull", "optret", "optop", "accum", "collect", "objects", "jtime",
         "millis", "newinstance", "accessible", "boxedctor", "legacycoll",
         "factory", "getpackage"]

CLASSES_PER_FIXTURE = 24


def _sortkey(body):
    loc = body.split()[0]
    path, _, ln = loc.rpartition(":")
    if not path or not ln.isdigit():
        return (loc, 0)
    return (path, int(ln))


def paths_of(bodies):
    return set(b.split()[0].split(":")[0] for b in bodies)


def iface_records(f):
    """The tree's sixty-five interface declarations, with the @FunctionalInterface
    flag set on the ones this fixture marks.

    All sixty-five, and not only the twenty-four: B-11's second third is an
    intersection, and a fixture that carried only the answer would agree with a
    gate pointed at any set of interfaces at all.
    """
    keep = set(f["iface_annotated"])
    out = []
    for body in R_IFACE:
        fl = body.split()
        pair = fl[0].split(":")[0] + " " + fl[1]
        tag = "annotated=yes" if pair in keep else "annotated=no"
        out.append(" ".join(tag if x.startswith("annotated=") else x for x in fl))
    return out


def retag_field(body, pref, value):
    return " ".join((pref + value) if x.startswith(pref) else x
                    for x in body.split())


def build_records(f):
    """Every per-site record this fixture's report holds, as SourceScan writes
    them.

    The input tree's own records are LIFTED -- from baseline-step06-source.txt
    for everything standing on it, and from baseline-step05-source.txt for the
    B-12 sites this step's input already migrated and a fixture wants back. Only
    the arrivals are synthesised, at deterministic line numbers, because they
    are the constructs no tree in the workspace has written yet.
    """
    r = {}
    r["lambda"] = list(f["lam"])
    r["methodref"] = list(f["mref"])
    r["interfacedecl"] = iface_records(f)
    r["stream"] = list(f["strm"])
    r["streamop"] = list(f["strmop"])
    r["getpackage"] = list(R_GETPKG)

    r["retnull"] = [x for k in OPTBUCKETS if k in set(f["retnull_left"])
                    for x in UNDO_RETNULL[k]] + list(RETNULL_REST)
    r["optret"] = [x for k in OPTBUCKETS if k in set(f["optret_at"])
                   for x in OPTRET_BY[k]]
    ops = list(f["opt_makes"]) + list(f["opt_uses"])
    for b, n in sorted(f["isp_left"].items()):
        ops += OPT_ISPRESENT[b][:n]
    for i, (b, name) in enumerate(f["opt9_at"]):
        path, m = split_bucket(b)
        ops.append("%s:%d use %s in=%s" % (path, 700 + i, name, m))
    r["optop"] = ops
    r["accum"] = [x for k in SINKS if k in set(f["accum_left"])
                  for x in UNDO_ACCUM[k]] + list(ACCUM_REST)
    coll = []
    for b in sorted(f["collect_at"]):
        to = f["collect_at"][b]
        for body in COLLECT_BY[b]:
            coll.append(body if _field(body, "to=") == to
                        else retag_field(body, "to=", to))
    r["collect"] = coll
    want = set(f["objects_at"])
    r["objects"] = [x for x in R_OBJECTS if (bucket_of(x), x.split()[1]) in want]
    r["jtime"] = [x for x in R_JTIME if bucket_of(x) in set(f["jtime_at"])]
    r["millis"] = [x for k in TIMESITES if k in set(f["millis_left"])
                   for x in UNDO_MILLIS[k]] + list(MILLIS_REST)

    ni = [NEWINST_BY[(b, "legacy")] for b in f["ni_leg"]]
    for i, b in enumerate(f["ni_ctor"]):
        if (b, "ctor") in NEWINST_BY:
            ni.append(NEWINST_BY[(b, "ctor")])
        else:
            path, m = split_bucket(b)
            ni.append("%s:%d form=ctor in=%s" % (path, 800 + i, m))
    r["newinstance"] = ni
    acc = [ACCESS_BY[(b, "isAccessible")] for b in f["acc_guard"]]
    acc += [ACCESS_BY[(b, "setAccessible")] for b in f["acc_setacc"]]
    for i, b in enumerate(f["canacc"]):
        path, m = split_bucket(b)
        acc.append("%s:%d call=canAccess recv=AccessibleObject in=%s"
                   % (path, 900 + i, m))
    r["accessible"] = acc
    r["boxedctor"] = list(f["boxed"])
    lc = []
    for b in sorted(set(LC_BUILD) | set(LC_LOCAL)):
        if b in f["lc_drop"]:
            continue
        lc += LC_BUILD.get(b, []) + LC_LOCAL.get(b, [])
    for b in f["lc_viewf"]:
        lc += LC_VIEWF[b]
    r["legacycoll"] = lc
    r["factory"] = ["%s:%d %s args=%d in=%s"
                    % (split_bucket(b)[0], 1000 + i, name, nargs,
                       split_bucket(b)[1])
                    for i, (b, (name, nargs)) in enumerate(f["factory_at"])]
    # The in= field is dropped HERE and not at write time, because the scorers
    # below read this dict: a fixture built from a gate jar too old to attribute
    # a site to a method has to be scored on what its report actually says, and
    # every bucket-based gate at this step reads nothing else.
    for k in r:
        r[k] = sorted(r[k], key=_sortkey)
        if f["no_in"]:
            r[k] = [strip_in(x) for x in r[k]]
    return r


# Four byte strings that a CONSTANT_Utf8 entry naming each package would hold.
# B-12e greps for exactly these, so exactly these are what a fixture's class
# files carry; `java/time/` is the prefix form the gate uses, and Instant is
# written after it so that the fixture matches the same way a real class does.
B12_MARKERS = ("java/util/Optional", "java/util/stream/Collectors",
               "java/util/Objects", "java/time/Instant")


def write_jars(d, f):
    """Two real jars holding real .class entries, for B-12e to unzip and grep.

    Two rather than one so that the `while read` over modulejars.txt is
    exercised with more than one iteration, and the classes are interleaved
    between them so that no package's whole count can be collected from one
    archive. The bytes are a class-file magic number and a run of Utf8-shaped
    entries: B-12e is a binary grep over constant pools, not a class parser, and
    a javac-produced fixture would only be a slower way to write the same bytes.

    NOTE the major version written into these headers is not read by anything.
    C-1 does not look at the jars -- it reads ${WORK}/scan.txt, which the build
    stage writes from arqgate.Scan -- and keeping the two channels separate is
    the point: a fixture can carry jars and no scan, or a scan and no jars, and
    the catalogue above exercises each on its own.
    """
    if f["jars"] == "none":
        return
    jd = os.path.join(d, "jars")
    if not os.path.isdir(jd):
        os.makedirs(jd)

    # Every entry is written at a FROZEN timestamp. zipfile defaults each
    # ZipInfo to the wall clock, which at step 05 made two runs of the generator
    # produce jars with identical contents and different bytes -- so a
    # regeneration looked like a changed fixture, and the duplicate-tree check
    # that hashes these files could not tell a real edit from the time of day.
    # Trap 10, in a place it is easy not to look for: the artifact, not the
    # expectation.
    def put(z, name, data):
        zi = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        z.writestr(zi, data)

    zips = [zipfile.ZipFile(os.path.join(jd, "mod%d.jar" % n), "w") for n in (1, 2)]
    try:
        for z in zips:
            put(z, "META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n")
        if f["jars"] == "empty":
            return
        for i in range(CLASSES_PER_FIXTURE):
            blob = bytearray(b"\xca\xfe\xba\xbe\x00\x00\x00\x36")
            blob += b"\x01\x00\x20org/jboss/arquillian/zz/C%02d" % i
            for m, n in zip(B12_MARKERS, f["b12jars"]):
                if i < n:
                    blob += b"\x01" + bytes([0, len(m)]) + m.encode()
            blob += b"\x00" * 8
            put(zips[i % 2], "org/jboss/arquillian/zz/C%02d.class" % i, bytes(blob))
    finally:
        for z in zips:
            z.close()
    for m, n in zip(B12_MARKERS, f["b12jars"]):
        assert n <= CLASSES_PER_FIXTURE, (m, n)


def write_scan(d, f):
    """The class-file census C-1 reads, in arqgate.Scan's own three key families.

    Scan writes a package, native-method and type-reference census as well;
    none of it is read at this rung, and a fixture carrying an invented package
    list would be stating facts about a tree rather than feeding a gate. So the
    file holds exactly what C-1 consumes, in the order and spelling Scan emits
    it: classes, then one major.<N> line per version ascending, then the count
    of distinct versions.
    """
    if f["scan"] is None:
        return
    classes, majors = f["scan"]
    with open(os.path.join(d, "scan.txt"), "w") as fh:
        fh.write("classes=%d\n" % classes)
        for m in sorted(majors):
            fh.write("major.%d=%d\n" % (m, majors[m]))
        fh.write("majors.distinct=%d\n" % len(majors))


def write(name, f, why, r):
    d = os.path.join(OUT, name)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(os.path.join(d, "why.txt"), "w") as fh:
        fh.write(why + "\n")

    def n(kind):
        return len(r[kind])

    with open(os.path.join(d, "report.txt"), "w") as fh:
        for key, attr in PASSTHROUGH:
            fh.write("%s=%s\n" % (key, f[attr]))
        fh.write("lambda.size=%d\n" % n("lambda"))
        fh.write("lambda.files=%d\n" % len(paths_of(r["lambda"])))
        fh.write("methodref.size=%d\n" % n("methodref"))
        fh.write("iface.decl=%d\n" % n("interfacedecl"))
        fh.write("iface.samannotated=%d\n"
                 % len([x for x in r["interfacedecl"] if "annotated=yes" in x]))
        fh.write("stream.producers=%d\n" % n("stream"))
        fh.write("stream.ops=%d\n" % n("streamop"))
        fh.write("stream.files=%d\n" % len(paths_of(r["stream"])))
        fh.write("stream.opfiles=%d\n" % len(paths_of(r["streamop"])))
        # `stream.opmethods` is the one counter in the report that cannot be
        # computed without attributing a site to a method, so the fixture built
        # from a gate jar that emits no in= field does not carry it either. The
        # scorer initialises it empty above its own guard and publishes it as
        # `absent`; that it survives a missing counter under `set -u` is a
        # property worth rehearsing rather than assuming. `loop.size` needs no
        # attribution and stays.
        if not f["no_in"]:
            fh.write("stream.opmethods=%d\n" % len(group(r["streamop"])))
        uses = [x for x in r["optop"] if x.split()[1] == "use"]
        for kind in ("retnull", "optret", "optop", "accum", "collect",
                     "objects", "jtime", "millis"):
            fh.write("%s.size=%d\n" % (kind, n(kind)))
        fh.write("optop.get=%d\n" % len([x for x in uses if x.split()[2] == "get"]))
        fh.write("optop.ispresent=%d\n"
                 % len([x for x in uses if x.split()[2] == "isPresent"]))
        # The collector census, for the facts rather than for a gate: B-12b
        # reads the per-site to= fields and never these three.
        for key, spell in (("tolist", "toList"), ("toset", "toSet"),
                           ("tocollection", "toCollection")):
            fh.write("collect.%s=%d\n"
                     % (key, len([x for x in r["collect"]
                                  if _field(x, "to=") == spell])))
        forms = [_field(x, "form=") for x in r["newinstance"]]
        fh.write("newinstance.size=%d\n" % n("newinstance"))
        fh.write("newinstance.legacy=%d\n" % forms.count("legacy"))
        fh.write("newinstance.ctor=%d\n" % forms.count("ctor"))
        calls = [_field(x, "call=") for x in r["accessible"]]
        fh.write("accessible.size=%d\n" % n("accessible"))
        fh.write("accessible.isaccessible=%d\n" % calls.count("isAccessible"))
        fh.write("accessible.canaccess=%d\n" % calls.count("canAccess"))
        fh.write("accessible.setaccessible=%d\n" % calls.count("setAccessible"))
        fh.write("boxedctor.size=%d\n" % n("boxedctor"))
        kinds = [(_field(x, "kind="), _field(x, "arg=")) for x in r["legacycoll"]]
        fh.write("legacycoll.size=%d\n" % n("legacycoll"))
        fh.write("legacycoll.view=%d\n" % len([1 for k, a in kinds if k == "view"]))
        fh.write("legacycoll.viewfield=%d\n"
                 % len([1 for k, a in kinds if k == "view" and a == "field"]))
        fh.write("legacycoll.build=%d\n" % len([1 for k, a in kinds if k == "build"]))
        called = [x.split()[1].rsplit(".", 1)[-1] for x in r["factory"]]
        fh.write("factory.size=%d\n" % n("factory"))
        fh.write("factory.of=%d\n" % len([c for c in called if c != "copyOf"]))
        fh.write("factory.copyof=%d\n" % called.count("copyOf"))
        fh.write("getpackage.size=%d\n" % n("getpackage"))
        for kind in KINDS:
            for i, body in enumerate(r[kind]):
                fh.write("%s[%d]=%s\n" % (kind, i, body))

    with open(os.path.join(d, "answers.txt"), "w") as fh:
        for k in ("ok10", "ok8", "okdef"):
            fh.write("%s=%s\n" % (k, f[k]))
        fh.write("errors=%d\n" % f["e_err"])

    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        if f["broke_nio"]:
            for i, nid in enumerate(NIOIDS):
                fh.write("behavior.differ[%d]=%s\n" % (i, nid))

    write_jars(d, f)
    write_scan(d, f)


def check_pinned(path):
    """The pinned fixture is this rung's INPUT, and its report is that input's
    own measured report. Not `resembles`: every counter the five scoring blocks
    read has to come out at the value baseline-step06-source.txt already states,
    because the verifier compares the two directly and a fixture that disagreed
    with the baseline would be rehearsing a tree that does not exist.

    The golden end is checked the same way against the report the calibration
    run measures on the solution tree; that file lives outside the task package,
    so the agreement is recorded in NOTES-authoring.md rather than asserted here.
    """
    bad = []
    seen = 0
    for line in open(path):
        line = line.strip()
        if not line or "[" in line or "=" not in line:
            continue
        k, v = line.split("=", 1)
        seen += 1
        if k not in BASE:
            bad.append("%s: not a counter baseline-step06-source.txt carries" % k)
        elif BASE[k] != v:
            bad.append("%s: fixture=%s baseline=%s" % (k, v, BASE[k]))
    assert not bad, "pinned disagrees with baseline-step06-source.txt:\n  " \
        + "\n  ".join(bad)
    return seen


def check_pinned_records(path):
    """...and its records are that input's own records, kind by kind.

    The counters alone would not catch a fixture that emitted the right totals
    over the wrong sites, which is precisely the tree every set-based gate at
    this step exists to distinguish.
    """
    bad = []
    for kind in KINDS:
        want = read_records(S06, kind)
        got = read_records(path, kind)
        if kind == "interfacedecl":
            # Lifted verbatim, modulo the annotated= flag this fixture sets, and
            # the pinned fixture sets it on exactly the baseline's own 24.
            pass
        if sorted(want) != sorted(got):
            bad.append("%s: %d records, baseline has %d"
                       % (kind, len(got), len(want)))
    assert not bad, "pinned's records are not the baseline's:\n  " \
        + "\n  ".join(bad)


# --- generate -----------------------------------------------------------------
def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    rows = []
    for name, f, why in FIX:
        r = build_records(f)
        write(name, f, why, r)
        # B-10 and B-11 sit INSIDE the measured guard and B-12's e sits outside
        # it, so an unmeasured tree publishes three different-looking zeros in
        # one facts file. The spellings are carried through the table rather
        # than normalised, because the rehearsal compares them as strings.
        measured = f["ok10"] == "yes" or f["okdef"] == "yes"
        b10, b10d = b10_score(f)
        b11, b11d = b11_score(f, r)
        b12, b12sp, b12d = b12_score(f, r)
        cs, csp, cd = c_score(f, r)
        b10sp = ("%.6f" % b10) if measured else "0"
        b11sp = ("%.6f" % b11) if measured else "0"
        rows.append(dict(name=name, why=why, f=f, r=r, c=cs, csp=csp, cd=cd,
                         b10=b10sp, b10d=b10d, b11=b11sp, b11d=b11d,
                         b12=b12, b12sp=b12sp, b12d=b12d))

    pin = os.path.join(OUT, "pinned", "report.txt")
    n = check_pinned(pin)
    check_pinned_records(pin)
    print("pinned: %d counters and every record agree with %s"
          % (n, os.path.basename(S06)))

    print("\n=== family C, gate by gate "
          + "=" * 52)
    print("%-7s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s"
          % ("fixture", "reach", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "pen"))
    for w in rows:
        s = w["csp"]
        print("%-7s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s %-8s   %s"
              % (w["name"], s["reach"], s["c1"], s["c2"], s["c3"], s["c4"],
                 s["c5"], s["c6"], s["c7"], s["pen"] or "-", w["c"]))

    print("\n=== the three held gates, leg by leg "
          + "=" * 42)
    print("%-7s | %-8s %-8s %-8s | %-8s %-8s %-8s | %s"
          % ("fixture", "b10.d", "b10.r", "b10.n", "b11.l", "b11.s", "b11.c",
             "b12 a / b / c / d / e"))
    for w in rows:
        d, e = w["b10d"], w["b11d"]
        print("%-7s | %-8.6f %-8.6f %-8.6f | %-8.6f %-8.6f %-8.6f | %s"
              % (w["name"], d["d"], d["r"], d["n"], e["l"], e["s"], e["c"],
                 " / ".join(w["b12sp"])))

    print("\n=== the facts that separate fixtures sharing a total "
          + "=" * 26)
    for w in rows:
        c, d = w["cd"], w["b12d"]
        bits = []
        if c.get("scan") is None:
            bits.append("gate.c1=no-scan")
        else:
            bits.append("c1.classes=%d/%s" % (c["scan"][0],
                        ",".join("%d:%d" % kv for kv in sorted(c["scan"][1].items()))))
        for k in ("c2dep", "c2arr", "nictor", "c3dep", "c3arr", "setacc",
                  "c4dep", "boxed", "c5dep", "c5arr", "c5held", "c6dep",
                  "c6arr", "raw"):
            if k in c:
                bits.append("%s=%s" % (k, c[k]))
        bits.append("sup=%d" % w["f"]["suppress_sites"])
        bits.append("var=%d/%d" % (w["f"]["var_n"], w["f"]["varnew_n"]))
        bits.append("dia=%d res=%d nio=%d"
                    % (w["f"]["diamond"], w["f"]["resources"],
                       w["f"]["nioref"] + w["f"]["niostatic"]))
        bits.append("covl=%d sam=%d covc=%d"
                    % (w["b11d"]["covl"], w["b11d"]["sam"], w["b11d"]["covc"]))
        for k in ("opt_dep", "opt_arr", "uses", "get", "sink_dep", "sink_arr",
                  "sink_fid", "vt", "time_dep", "time_arr", "jars"):
            if k in d:
                bits.append("%s=%s" % (k, d[k]))
        if d.get("cp") is not None:
            bits.append("cp=%s" % ",".join(str(x) for x in d["cp"]))
        bits.append("nio.broken=%d" % w["b10d"]["broken"])
        bits.append("src=%s/%s/%s" % (w["f"]["ok10"], w["f"]["ok8"], w["f"]["okdef"]))
        print("%-7s %s" % (w["name"], " ".join(bits)))

    # Which totals collide, and therefore which rows the rehearsal has to
    # separate with a fact assertion rather than with a number. A shared total
    # is not a defect -- two different trees CAN be worth the same -- but a
    # rehearsal that only checked totals would pass with the two swapped.
    by_tot = {}
    for w in rows:
        by_tot.setdefault((w["c"], w["b10"], w["b11"], w["b12"]), []).append(w["name"])
    print("\n=== fixtures sharing all four column values "
          + "=" * 35)
    for k in sorted(by_tot):
        if len(by_tot[k]) > 1:
            print("  %s  <- %s" % (" ".join(k), " ".join(by_tot[k])))

    # The table to transcribe, BY HAND, into c-rehearsal.sh. The verifier image
    # has no python3 in it -- the rehearsal is a shell script running against
    # the real blocks lifted out of test.sh -- so this generator cannot hand its
    # expectations across. Transcribing them is the point: the rehearsal's
    # numbers and the scorer's numbers then come from two separate derivations,
    # and if the blocks and this reimplementation disagree the rehearsal fails
    # at image build rather than at grading time.
    print("\n=== transcribe into c-rehearsal.sh "
          + "=" * 43)
    print("TABLE")
    for w in rows:
        print(("%-7s %-9s %-9s %-9s %s"
               % (w["name"], w["c"], w["b10"], w["b11"], w["b12"])).rstrip())

    files = sum(len(fs) for _, _, fs in os.walk(OUT))
    dirs = len([x for x in os.listdir(OUT) if os.path.isdir(os.path.join(OUT, x))])
    print("\n%d fixtures, %d files under %s" % (dirs, files, OUT))
    assert dirs == len(FIX), (dirs, len(FIX))


if __name__ == "__main__":
    main()
