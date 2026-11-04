#!/usr/bin/env python3
"""Build step 05's rehearsal fixtures for family C, B-10 and B-11, and compute
what each one must score.

    python3 authoring/fixtures-step05/mkfix05.py

Writes one directory per fixture under fixtures-step05/c/, each holding the
source report the shipped scoring blocks will read, the compile answers they
would have got from javac, a behaviour summary, and -- new at this step -- a
`jars/` directory holding real zip archives with real .class files in them.
Prints the score table that c-rehearsal.sh asserts.

FOUR blocks, not two. Step 04 rehearsed the source-AST block, B-10 and family C;
this step adds B-11, which holds step 04's own three constructs in place and
reads the same report. All four are extracted from the shipped test.sh and all
four are scored here.

WHY THERE ARE JARS. Step 04's rehearsal was arithmetic over a report and nothing
else, because every gate it scored was. C-5 is not: it unzips the jars the
candidate's build published and binary-greps their class files for four package
names, which is a different instrument asking a different question of a
different artifact -- the point of having it, since B-4 does not move at this
rung and a family C built only out of SourceScan records would be one
instrument's opinion, unchecked. That makes C-5 rehearsable in the same shape as
everything else, as long as the fixture can produce a jar. Python's zipfile can,
and the class files inside it carry the four package strings as the literal byte
sequences a CONSTANT_Utf8 entry would hold. What is being rehearsed is the
counting, the floors and the mean -- not javac's class-file format -- so a
synthetic constant pool is the honest fixture and a compiled one would only be
a slower way to write the same bytes.

THE RECORDS ARE THE REAL ONES WHERE THEY EXIST. Every gate at this step scores
over a frozen SET of <file>#<method> buckets rather than over a count, so a
fixture that invented its own buckets would agree with a gate pointed at any set
at all. The four set files are read -- optbuckets.txt, sinks.txt,
valuetypes.txt, timesites.txt -- and so are step 04's three, which B-11 divides
by. The DEPARTURE side goes further: the retnull, accum and millis records that
this step's input actually holds are lifted verbatim out of
baseline-step05-source.txt, which is that input's own measured report, so the
residual population a fixture leaves standing is the real one and not a
plausible-looking retyping of it. The ARRIVAL side is synthesised, because the
tree that would produce it is the one being rehearsed against.

The `want` figures are computed here, by an independent implementation of the
same composition, and then written into c-rehearsal.sh BY HAND. The rehearsal
never calls this file: a rehearsal that computes its own expectations at run
time agrees with the scorer by construction and asserts nothing. The verifier
image has no python3 either, which makes that separation structural rather than
a matter of discipline.
"""
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
AUTH = os.path.dirname(HERE)
OUT = os.path.join(HERE, "c")


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


# --- the frozen numbers, read rather than retyped -----------------------------
BASE = read_env(os.path.join(AUTH, "baseline-step05-source.txt"))
TGT = read_env(os.path.join(AUTH, "baseline-step05-target.txt"))
SRC_REPORT = os.path.join(AUTH, "baseline-step05-source.txt")

BASE_SUP = int(BASE["suppress.sites"])                       # 44
BASE_RAW = int(BASE["newgeneric.raw"])                       # 0
# B-10's denominators: step 03's work, not this step's figures.
T_DIA = int(TGT["target.diamond"])                           # 74
T_NONANON = int(TGT["target.explicit.nonanon"])              # 23
T_RES = int(TGT["target.try.resources"])                     # 9
T_IO = int(TGT["target.iofile"])                             # 2
B10_DIA = int(TGT["target.b10.diamond"])                     # 93
B10_CLOSE = int(TGT["target.b10.close"])                     # 11
B10_NIO = int(TGT["target.b10.nio"])                         # 11
# B-11's denominators: step 04's work, under its own b11 prefix.
B11_LAMFILES = int(TGT["target.b11.lambdafiles"])            # 20
B11_SAM = int(TGT["target.b11.sam"])                         # 24
B11_CM = int(TGT["target.b11.collectmethods"])               # 24
# Family C's four populations and C-5's four floors.
T_OPT = int(TGT["target.optbuckets"])                        # 16
T_SINKS = int(TGT["target.sinks"])                           # 23
T_VT = int(TGT["target.valuetypes"])                         # 3
T_TIME = int(TGT["target.timesites"])                        # 4
C5_FLOORS = (int(TGT["target.c5.optional.floor"]),           # 5
             int(TGT["target.c5.collectors.floor"]),         # 6
             int(TGT["target.c5.objects.floor"]),            # 2
             int(TGT["target.c5.jtime.floor"]))              # 2

LAMFILES = read_set(os.path.join(AUTH, "baseline-step04-lambdafiles.txt"))
SAMPAIRS = read_set(os.path.join(AUTH, "baseline-step04-samifaces.txt"))
COLLECTMETHODS = [l.split()[0] for l in
                  read_set(os.path.join(AUTH, "baseline-step04-collectmethods.txt"))]
NIOIDS = read_set(os.path.join(AUTH, "baseline-step03-nioids.txt"))

# This step's four frozen sets, with the second column each carries.
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

assert len(LAMFILES) == B11_LAMFILES, len(LAMFILES)
assert len(SAMPAIRS) == B11_SAM, len(SAMPAIRS)
assert len(COLLECTMETHODS) == B11_CM, len(COLLECTMETHODS)
assert len(OPTBUCKETS) == T_OPT, len(OPTBUCKETS)
assert len(SINKS) == T_SINKS, len(SINKS)
assert len(VALUETYPES) == T_VT, len(VALUETYPES)
assert len(TIMESITES) == T_TIME, len(TIMESITES)
# The 23 sinks are a strict subset of step 04's 24 collecting buckets, the 24th
# being the one in src/test. That containment is what makes the B-11 / C-2
# co-existence fixture mean something: the same buckets, read from both ends.
assert set(SINKS) < set(COLLECTMETHODS)
assert len(set(COLLECTMETHODS) - set(SINKS)) == 1

# --- the departure populations, lifted out of the input's own report ----------
# checklist item 63, asserted here as well as in the Dockerfile: every bucket
# the gate will ask about appears in baseline/source.txt's own records, with the
# record type the gate reads.
def by_bucket(kind, wanted):
    """Split a report's records of one kind into {bucket: [body]} over a frozen
    set, and the remainder that is not in the set at all."""
    inset, rest = {}, []
    for body in read_records(SRC_REPORT, kind):
        b = bucket_of(body)
        if b in wanted:
            inset.setdefault(b, []).append(body)
        else:
            rest.append(body)
    return inset, rest


RETNULL_BY, RETNULL_REST = by_bucket("retnull", set(OPTBUCKETS))
ACCUM_BY, ACCUM_REST = by_bucket("accum", set(SINKS))
MILLIS_BY, MILLIS_REST = by_bucket("millis", set(TIMESITES))
assert sorted(RETNULL_BY) == sorted(OPTBUCKETS)
assert sorted(ACCUM_BY) == sorted(SINKS)
assert sorted(MILLIS_BY) == sorted(TIMESITES)
# 42 = 17 private sites in 16 buckets + 25 that are api=yes or in an anonymous
# body; 24 = 23 src/main sinks + the one in src/test; 5 = 4 buckets, one of
# which holds two calls.
assert sum(len(v) for v in RETNULL_BY.values()) == 17
assert len(RETNULL_REST) == 25
assert len(ACCUM_REST) == 1 and not MILLIS_REST
assert sum(len(v) for v in MILLIS_BY.values()) == 5
for line in read_set(os.path.join(AUTH, "baseline-step05-timesites.txt")):
    b, n = line.split()
    assert len(MILLIS_BY[b]) == int(n), (b, n)

# --- what the tree holds BEYOND the frozen sets -------------------------------
# Every gate here scores a SET INTERSECTION, and an intersection is only doing
# work if the fixture holds sites the frozen set does not name. A fixture whose
# stream buckets were exactly the 24 in collectmethods.txt would read the same
# under a gate that intersected and under a gate that counted, so it would not
# tell the two apart -- and telling them apart is the whole of B-11's third
# sub-gate. So the golden carries the residual population the real tree carries:
# real <file>#<method> buckets, lifted out of the baseline's own records, in
# files the frozen set does not name.
CM_FILES = set(b.split("#")[0] for b in COLLECTMETHODS)


def _pool():
    """Real buckets from the baseline's records, outside collectmethods and
    outside every file it names, grouped by file."""
    bys = {}
    for kind in ("retnull", "accum", "millis", "streamop", "stream"):
        for body in read_records(SRC_REPORT, kind):
            b = bucket_of(body)
            if b is None:
                continue
            path = b.split("#")[0]
            if b in set(COLLECTMETHODS) or path in CM_FILES:
                continue
            bys.setdefault(path, set()).add(b)
    return sorted(bys.items(), key=lambda kv: (-len(kv[1]), kv[0]))


POOL = _pool()
# Whole files, largest first, skipping any that would overshoot: the golden's
# measured report has 31 stream-holding methods in 17 files against
# collectmethods' 24 in 14, so the residue is 7 buckets in 3 files and the
# selection has to land on exactly that shape rather than near it.
STREAM_EXTRA = []
_extra_files = 0
for _p, _s in POOL:
    if len(STREAM_EXTRA) + len(_s) <= 7:
        STREAM_EXTRA.extend(sorted(_s))
        _extra_files += 1
assert len(STREAM_EXTRA) == 7 and _extra_files == 3, (len(STREAM_EXTRA), _extra_files)

# The 33 files that held a lambda before this step -- of which exactly 20 are
# the ones step 04 was scored on, so B-11's first third intersects 20 out of 33
# rather than being handed 20 out of 20.
LAM_PATHS = sorted(set(bucket_of(b + " in=x").split("#")[0]
                       for b in read_records(SRC_REPORT, "lambda")))
assert len(LAM_PATHS) == int(BASE["lambda.files"]) == 33, len(LAM_PATHS)
assert set(LAMFILES) < set(LAM_PATHS)
# ...and the 7 further files that hold one after it, for 40.
LAM_EXTRA = [p for p, _ in sorted(POOL) if p not in set(LAM_PATHS)][:7]
assert len(LAM_EXTRA) == 7

# The golden's java.time sites, transcribed from its measured report: 11 calls
# in the 4 buckets, not one per bucket. C-4 scores buckets and would read the
# same either way, but the fixture that is the control for 36 others should be
# the tree and not a sketch of it.
GOLDEN_JTIME = {
    TIMESITES[0]: ("Instant.now", "Instant.toEpochMilli"),
    TIMESITES[1]: ("Instant.now", "Instant.toEpochMilli"),
    TIMESITES[2]: ("Duration.ofSeconds", "Instant.now", "Instant.plus",
                   "Instant.isBefore", "Instant.now"),
    TIMESITES[3]: ("Instant.now", "Instant.toEpochMilli"),
}
assert sum(len(v) for v in GOLDEN_JTIME.values()) == 11

# --- somewhere that is not one of the frozen sets ------------------------------
# The arrival halves are set intersections, so the fixture that writes the right
# construct in the wrong place has to have a wrong place to write it in.
ELSEWHERE = ["zz-added/src/main/java/zz/New%02d.java#go" % i for i in range(1, 25)]
FAKESAM = ["zz-added/src/main/java/zz/Iface%02d.java Iface%02d" % (i, i)
           for i in range(1, 25)]

# The golden's own collector choice, transcribed from its measured report.
# SEVEN of the 23 are toCollection where sinks.txt names toList or toSet, and
# every one of them is accepted: the acceptance rule is load-bearing on the
# CORRECT tree and not only on the fixtures below it. Six collect into a list
# the method then addAll()s into, and the seventh is
# MethodParameterInjectionPoint#getQualifiers, which adds to the set afterwards.
GOLDEN_TOCOLLECTION = [
    "impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java#resolveActiveContexts",
    "impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java#resolveObservers",
    "impl-base/src/main/java/org/jboss/arquillian/impl/core/Reflections.java#getEventPoints",
    "impl-base/src/main/java/org/jboss/arquillian/impl/core/Reflections.java#getFieldInjectionPoints",
    "impl-base/src/main/java/org/jboss/arquillian/impl/core/Reflections.java#getObserverMethods",
    "spi/src/main/java/org/jboss/arquillian/spi/client/deployment/DeploymentScenario.java#getStartupDeploymentsFor",
    "testenrichers/cdi/src/main/java/org/jboss/arquillian/testenricher/cdi/MethodParameterInjectionPoint.java#getQualifiers",
]
assert set(GOLDEN_TOCOLLECTION) < set(SINKS)
GOLDEN_COLLECT = dict((b, "toCollection" if b in GOLDEN_TOCOLLECTION else SINKWANT[b])
                      for b in SINKS)
# The one site the whole leg turns on: a LinkedHashSet whose iteration order is
# in the 935 recordings family A replays, so Collectors.toSet() is wrong there
# and only toCollection is accepted.
ORDERED_SINK = "impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java#createInstances"
assert SINKWANT[ORDERED_SINK] == "toCollection"

# The golden's Optional consumption, transcribed from its measured report: 25
# instance calls on an Optional, not one of them get().
GOLDEN_USES = (["orElse"] * 10 + ["isPresent"] * 6 + ["ifPresent"] * 3
               + ["orElseThrow"] * 2 + ["map"] * 2 + ["orElseGet"] + ["filter"])
assert len(GOLDEN_USES) == 25 and "get" not in GOLDEN_USES
GOLDEN_MAKES = 21


# --- the two measurements -----------------------------------------------------
# PINNED is the step-04 golden, which is this rung's INPUT: the tree the
# candidate is handed, and the tree the step's floor claim is about. GOLDEN is
# the step-05 solution. Both are measurements -- baseline-step05-source.txt and
# the report the calibration run measured on the solution tree -- and every
# other fixture is a stated edit of one of them.
PINNED = dict(
    files=322, suppress_sites=BASE_SUP,
    anonfn=int(BASE["anonymousfunctional.size"]),
    conv=int(BASE["anonymousfunctional.convertible"]),
    forward=int(BASE["anonymousfunctional.forward"]),
    # step 04's three constructs, at the values step 04 left them at
    lam_n=int(BASE["lambda.size"]), lam_paths=tuple(LAM_PATHS),
    mref_n=int(BASE["methodref.size"]), mref_paths=tuple(LAM_PATHS[:12]),
    ifacedecl=65, sam=B11_SAM, sam_annotated=tuple(SAMPAIRS),
    streams=int(BASE["stream.producers"]), streamops=int(BASE["stream.ops"]),
    stream_sites=tuple(COLLECTMETHODS), streamop_sites="same",
    foreach=int(BASE["foreach.size"]), collect=int(BASE["foreach.collect"]),
    loop=int(BASE["loop.size"]),
    # step 03's three constructs, likewise
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
    niofiles=int(BASE["niofile.files"]),
    # THIS step's four constructs, all of them untouched: every one of the 16
    # lookup helpers still returns null, every one of the 23 sinks still
    # accumulates by hand, no value type calls Objects, every timing site still
    # reads the wall clock.
    retnull_left=tuple(OPTBUCKETS), optret_at=(),
    optuses=(), optmakes=0, optbucket_at=(),
    accum_left=tuple(SINKS), collect_at={},
    objects_at=(),
    millis_left=tuple(TIMESITES), jtime_at=(),
    # ...and the jars the build publishes carry none of the four packages. The
    # jars are REAL and are scanned: this zero is measured by the same unzip and
    # the same binary grep the golden's is, which is what makes it a fact about
    # the tree rather than a fact about a missing fixture.
    c5=(0, 0, 0, 0), jars="full",
    no_in=False,
    ok18="yes", ok17="yes", okdef="yes", e17=0,
    broke_nio=False,
)
GOLDEN = dict(
    PINNED,
    # C-1: the 16 helpers return Optional and 25 calls consume one, none of them
    # with get().
    retnull_left=(), optret_at=tuple(OPTBUCKETS),
    optuses=tuple(GOLDEN_USES), optmakes=GOLDEN_MAKES,
    optbucket_at=tuple(OPTBUCKETS),
    # C-2: all 23 sinks collect, 7 of them through toCollection.
    accum_left=(), collect_at=dict(GOLDEN_COLLECT),
    # C-3: equals and hash in all three value types.
    objects_at=tuple([(vt + "#hashCode", "hash") for vt in VALUETYPES]
                     + [(vt + "#equals", "equals") for vt in VALUETYPES]),
    # C-4: the four buckets hold java.time and no millis.
    millis_left=(), jtime_at=tuple(TIMESITES),
    c5=(10, 13, 3, 4),
    # What the step's own work does to the counters around it, measured: the 19
    # sink constructions that were diamonds are gone with the sinks, the loop
    # census falls, the pipelines gain stages, and 15 of step 04's method
    # references were `sink::add` arguments that collect() replaced.
    diamond=74, diamondfiles=40, loop=107, foreach=82,
    streams=55, streamops=64, lam_n=78, mref_n=9,
    lam_paths=tuple(LAM_PATHS + LAM_EXTRA),
    stream_sites=tuple(list(COLLECTMETHODS) + STREAM_EXTRA),
    ok17="no", e17=47,
)
assert GOLDEN["explicit"] - GOLDEN["explicitanon"] == T_NONANON
assert PINNED["explicit"] - PINNED["explicitanon"] == T_NONANON


def variant(base, **kw):
    d = dict(base)
    d.update(kw)
    return d


def collect_all(factory):
    """All 23 sinks collected through one factory."""
    return dict((b, factory) for b in SINKS)


# --- the fixture set ----------------------------------------------------------
# Every entry is paired with the clean control it perturbs -- `golden` for all
# but the two ends -- and every perturbation is either a route a candidate could
# actually take or a demonstration that two channels are independent.
FIX = [
    # the two ends, which are what make everything between them mean something
    ("pinned", PINNED, "the step-04 golden: this rung's input, and the floor"),
    ("golden", GOLDEN, "the step-05 golden solution"),

    # ---- C-1: Optional where the 16 private lookup helpers were
    ("K1", variant(GOLDEN, optret_at=(), optuses=(), optmakes=0, optbucket_at=()),
     "the 16 null returns deleted, no Optional declared anywhere"),
    ("K2", variant(GOLDEN, retnull_left=tuple(OPTBUCKETS)),
     "16 Optional-returning methods added, the null returns left standing"),
    ("K3", variant(GOLDEN, optuses=tuple(["get"] * 25)),
     "every one of the 25 Optionals consumed with get(): the null moved"),
    ("K4", variant(GOLDEN, optuses=tuple(["get"] * 16 + ["isPresent"] * 16)),
     "if (o.isPresent()) return o.get(): the null check moved, not removed"),
    ("K5", variant(GOLDEN, retnull_left=tuple(OPTBUCKETS[8:]),
                   optret_at=tuple(OPTBUCKETS[:8]), optbucket_at=tuple(OPTBUCKETS[:8])),
     "eight of the sixteen lookup helpers migrated"),
    ("K6", variant(GOLDEN, optret_at=tuple(ELSEWHERE[:16]),
                   optbucket_at=tuple(ELSEWHERE[:16])),
     "the nulls gone and 16 Optional methods written, none where a helper was"),

    # ---- C-2: Collectors where step 04 left the accumulate sites standing
    ("K7", variant(GOLDEN, collect_at={}),
     "the 23 forEach(sink::add) sites deleted, no collector written"),
    ("K8", variant(GOLDEN, collect_at=dict(GOLDEN_COLLECT, **{ORDERED_SINK: "toSet"})),
     "22 faithful, the LinkedHashSet collected with toSet: order not guaranteed"),
    ("K9", variant(GOLDEN, collect_at=collect_all("toSet")),
     "every sink collected with toSet, whatever it was constructed as"),
    ("K10", variant(GOLDEN, collect_at=dict((b, "toList") for b in ELSEWHERE[:23])),
     "the sites gone and 23 collectors written, none where a sink was"),
    ("K11", variant(GOLDEN, accum_left=tuple(SINKS[12:]),
                    collect_at=dict((b, "toCollection") for b in SINKS[:12])),
     "twelve of the 23 migrated, all of them through toCollection"),

    # ---- C-3: java.util.Objects in the three value types
    ("K12", variant(GOLDEN, objects_at=tuple(
        [(vt + "#hashCode", "hashCode") for vt in VALUETYPES]
        + [(vt + "#equals", "equals") for vt in VALUETYPES])),
     "Objects.hashCode instead of Objects.hash: the 31*result loop survives"),
    ("K13", variant(GOLDEN, objects_at=tuple(
        [(vt + "#toString", "requireNonNull") for vt in VALUETYPES])),
     "requireNonNull decorating a method C-3 does not bucket on"),
    ("K14", variant(GOLDEN, objects_at=tuple(
        [(vt + "#equals", "equals") for vt in VALUETYPES])),
     "equals migrated in all three, hashCode in none"),
    ("K15", variant(GOLDEN, objects_at=tuple(
        [(vt + "#hashCode", "hash") for vt in VALUETYPES[:2]]
        + [(vt + "#equals", "equals") for vt in VALUETYPES[:2]])),
     "two of the three value types migrated"),

    # ---- C-4: java.time where the epoch-millis calls were
    ("K16", variant(GOLDEN, jtime_at=()),
     "the five currentTimeMillis calls deleted, no java.time written"),
    ("K17", variant(GOLDEN, millis_left=tuple(TIMESITES)),
     "Instant.now added beside every millis call, none of them removed"),
    ("K18", variant(GOLDEN, millis_left=(TIMESITES[2],)),
     "one of the two calls in ServletMethodExecutor#execute left behind"),

    # ---- C-5: the published jars
    ("K19", variant(GOLDEN, c5=(1, 0, 0, 0),
                    retnull_left=tuple(OPTBUCKETS), optret_at=(), optuses=(),
                    optmakes=0, optbucket_at=(), collect_at={},
                    accum_left=tuple(SINKS), objects_at=(),
                    millis_left=tuple(TIMESITES), jtime_at=()),
     "one Optional in one published class and no source work: pays nothing"),
    ("K20", variant(GOLDEN, c5=(0, 0, 3, 0),
                    retnull_left=tuple(OPTBUCKETS), optret_at=(), optuses=(),
                    optmakes=0, optbucket_at=(), collect_at={},
                    accum_left=tuple(SINKS),
                    millis_left=tuple(TIMESITES), jtime_at=()),
     "the automated migrator: C-3 whole and the quarter of C-5 it reaches"),
    ("K21", variant(GOLDEN, jars="empty"),
     "the build published jars with no class files in them"),
    ("K22", variant(GOLDEN, jars="none"),
     "the build published no module jar at all"),
    ("K23", variant(GOLDEN, c5=(0, 0, 0, 0)),
     "every source gate full and the published jars carry none of it"),

    # ---- the anti-gaming multiplier, each half on its own
    ("K24", variant(GOLDEN, raw=5), "five raw types added over the baseline's zero"),
    ("K25", variant(GOLDEN, suppress_sites=BASE_SUP + 6),
     "six @SuppressWarnings sites added"),

    # ---- the instrument, failing
    ("K26", variant(GOLDEN, ok18="no", okdef="no"),
     "no compile of the tree produced a report at all"),
    ("K36", variant(GOLDEN, no_in=True),
     "a verifier built from a gate jar too old to emit the in= field"),

    # ---- B-10: step 03's three constructs, undone one at a time
    ("K27", variant(GOLDEN, explicit=110, diamond=0, diamondfiles=0),
     "the diamonds unwound back to explicit type arguments"),
    ("K28", variant(GOLDEN, resources=0, resourcevars=0, close=B10_CLOSE),
     "the resource clauses unwound back to manual close() calls"),
    ("K29", variant(GOLDEN, ionew=8, iodecl=5, nioref=0, niofiles=0),
     "the java.nio.file migration unwound back to java.io.File"),
    ("K30", variant(GOLDEN, broke_nio=True),
     "step 03's file migration intact but its four recorded behaviours broken"),

    # ---- B-11: step 04's three constructs, undone one at a time
    ("K31", variant(GOLDEN, lam_n=0, lam_paths=(), mref_n=0, mref_paths=()),
     "the lambdas and method references unwound back to anonymous classes"),
    ("K32", variant(GOLDEN, sam_annotated=()),
     "the @FunctionalInterface annotations removed from all 24"),
    ("K33", variant(GOLDEN, streams=0, streamops=0, stream_sites=()),
     "the stream pipelines unwound out of all 24 buckets"),

    # ---- the B-11 / C-2 co-existence pair (checklist item 61)
    ("K34", variant(GOLDEN, collect_at={}, accum_left=(),
                    stream_sites=(COLLECTMETHODS[-1],), streams=2, streamops=2,
                    collect=23, loop=130, foreach=105),
     "the 23 sinks turned back into plain for loops: B-11's control"),
    ("K35", variant(GOLDEN, c5=(0, 13, 0, 0),
                    retnull_left=tuple(OPTBUCKETS), optret_at=(), optuses=(),
                    optmakes=0, optbucket_at=(), objects_at=(),
                    millis_left=tuple(TIMESITES), jtime_at=()),
     "C-2 alone: the converted buckets pay C-2 AND still hold B-11 in full"),

    # ---- the two halves of C-5's formula, one fixture each (review R5)
    #
    # K37 is the cheap tree the static review priced, exactly: THIS RUNG'S
    # INPUT -- the step-04 golden, no migration done, every source leg 0 -- with
    # published jars whose constant pools reach the library as fully as the
    # golden's do. Before the corroboration change its family C was 0.150000
    # and its reward 0.553750, over item 6's bar without a line of the step
    # being done. It is the PINNED fixture and not a stripped GOLDEN, because
    # what makes the attack cheap is that nothing else has to change.
    ("K37", variant(PINNED, c5=(10, 13, 3, 4)),
     "dummy constant-pool references on an unmigrated tree: R5's cheap tree"),
    # ...and its mirror. The floors are still floors: full source work, and
    # jars that reach only 2 of the 5 published classes the Optional floor
    # asks for. K37 holds the scaling at 0 and varies the reach; this holds the
    # reach below the floor and varies nothing else, so one fixture fails if
    # the scaling is dropped and the other if the floors are.
    ("K38", variant(GOLDEN, c5=(2, 13, 3, 4)),
     "the source work done and the Optional reach short of its floor"),
]


# --- an independent implementation of the shipped composition -----------------
# Deliberately a reimplementation and not a call into the shell: the point of
# the rehearsal is that two implementations of the same specification agree.
#
# The rounding is replicated exactly, because the shell rounds to six decimals
# at every step and not only at the end -- frac(), clamp01() and minf() each
# print %.6f -- and a check that rounded once would disagree with the scorer in
# the sixth place and be right about nothing.
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
    return f6(0.4 * d + 0.3 * r + 0.3 * n), dict(d=d, r=r, n=n)


def b11_score(f):
    """B-11: how much of step 04's work is still standing."""
    cov_l = len((set(p for p in f["lam_paths"]) | set(f["mref_paths"])) & set(LAMFILES))
    l = clamp01(frac(cov_l, B11_LAMFILES))
    sam = len(set(f["sam_annotated"]) & set(SAMPAIRS))
    s = clamp01(frac(sam, B11_SAM))
    if f["no_in"]:
        cov_c = 0
    else:
        cov_c = len(set(f["stream_sites"]) & set(f["streamop_sites"])
                    & set(COLLECTMETHODS))
    c = clamp01(frac(cov_c, B11_CM))
    return f6(0.20 * l + 0.20 * s + 0.60 * c), dict(l=l, s=s, c=c,
                                                    covl=cov_l, sam=sam, covc=cov_c)


def c5_score(f, src=(0.0, 0.0, 0.0, 0.0)):
    """C-5: the four packages in the published jars' constant pools.

    Each leg is scaled by the SOURCE leg it corroborates -- optional by C-1,
    collectors by C-2, objects by C-3, jtime by C-4 -- so a constant-pool hit
    pays only what its own source work earned. Static review round 1 (R5)
    priced the alternative: floors of 5/6/2/2 with nothing tying them to the
    source are clearable by a handful of dummy references, and the resulting
    0.553750 is over item 6's bar on a tree that migrated nothing.

    Returns the shell's own SPELLING of the fact. A gate that never ran
    publishes the literal `0` the block assigns at the top, and one that
    computed an answer publishes "%.6f". They are the same number and not the
    same fact: `no jars`, `jars with no classes` and `jars with none of the
    four packages` are three different findings about a tree.
    """
    if f["jars"] == "none" or f["jars"] == "empty":
        return "0", 0.0, dict(jars=(0 if f["jars"] == "none" else 2),
                              classes=(None if f["jars"] == "none" else 0))
    legs = [clamp01(frac(n, d)) for n, d in zip(f["c5"], C5_FLOORS)]
    v = f6(sum(l * c for l, c in zip(legs, src)) / 4)
    return "%.6f" % v, v, dict(jars=2, classes=CLASSES_PER_FIXTURE, legs=legs)


def c_score(f):
    """Family C: the Java 8 LIBRARY migration."""
    measured = f["ok18"] == "yes" or f["okdef"] == "yes"
    if not measured:
        # Four gates that never ran publish the block's own literals. C-5 is
        # OUTSIDE that block and still runs: it reads the published jars, which
        # a tree that would not compile against the scorer's classpath may
        # perfectly well have built. It now runs with c1..c4 still holding the
        # block's literal 0, so it scores a MEASURED zero -- the jars were
        # unzipped and greppped and the references found, and none of them was
        # corroborated by source work the scorer could see.
        c5s, c5v, c5d = c5_score(f)
        total = f6(1.0 * (0.15 * c5v))
        return "%.6f" % total, dict(c1="0", c2="0", c3="0", c4="0", c5=c5s,
                                    pen=None, d=dict())

    # C-1. Three halves: the 16 buckets that must stop returning null, the 16
    # that must declare an Optional, and the share of Optional USES that are not
    # get(). `make` records are not in the denominator.
    left = set(f["retnull_left"])
    opt_dep = len([b for b in OPTBUCKETS if b not in left]) if not f["no_in"] else T_OPT
    opt_arr = 0 if f["no_in"] else len(set(f["optret_at"]) & set(OPTBUCKETS))
    uses = list(f["optuses"])
    getn = len([u for u in uses if u == "get"])
    optcons = clamp01(frac(len(uses) - getn, len(uses)))
    c1 = minf(minf(clamp01(frac(opt_dep, T_OPT)), clamp01(frac(opt_arr, T_OPT))),
              optcons)

    # C-2. Departure, and fidelity against the factory each sink requires.
    aleft = set(f["accum_left"])
    sink_dep = len([b for b in SINKS if b not in aleft]) if not f["no_in"] else T_SINKS
    got = dict() if f["no_in"] else dict(f["collect_at"])
    sink_arr = len(set(got) & set(SINKS))
    sink_fid = 0
    for b in SINKS:
        w = SINKWANT[b]
        g = got.get(b)
        if g == w or (w in ("toList", "toSet") and g == "toCollection"):
            sink_fid += 1
    c2 = minf(clamp01(frac(sink_dep, T_SINKS)), clamp01(frac(sink_fid, T_SINKS)))

    # C-3. Arrival only, at METHOD scope: <file>#equals must hold an `equals`
    # record and <file>#hashCode a `hash` record.
    objset = set() if f["no_in"] else set(f["objects_at"])
    vt_hit = len([vt for vt in VALUETYPES
                  if (vt + "#equals", "equals") in objset
                  and (vt + "#hashCode", "hash") in objset])
    c3 = clamp01(frac(vt_hit, T_VT))

    # C-4. Buckets and not sites: a bucket with a millis record left is a bucket
    # where only one of its calls moved.
    mleft = set(f["millis_left"])
    time_dep = len([b for b in TIMESITES if b not in mleft]) if not f["no_in"] else T_TIME
    time_arr = 0 if f["no_in"] else len(set(f["jtime_at"]) & set(TIMESITES))
    c4 = minf(clamp01(frac(time_dep, T_TIME)), clamp01(frac(time_arr, T_TIME)))

    # C-5 last, and with the four source legs above, because that is the order
    # the shell runs them in and the scaling reads them from.
    c5s, c5v, c5d = c5_score(f, (c1, c2, c3, c4))

    pen = 1.0
    penalised = False
    raw_added = max(0, f["raw"] - BASE_RAW)
    if raw_added > 0:
        pen = f6(max(0.0, 1 - raw_added / 10.0))
        penalised = True
    if f["suppress_sites"] > BASE_SUP:
        pen = f6(pen / 2)
        penalised = True

    total = f6(pen * (0.30 * c1 + 0.35 * c2 + 0.10 * c3 + 0.10 * c4 + 0.15 * c5v))
    return "%.6f" % total, dict(
        c1="%.6f" % c1, c2="%.6f" % c2, c3="%.6f" % c3, c4="%.6f" % c4, c5=c5s,
        pen=("%.6f" % pen) if penalised else "1",
        d=dict(opt_dep=opt_dep, opt_arr=opt_arr, uses=len(uses), get=getn,
               cons=optcons, sink_dep=sink_dep, sink_arr=sink_arr,
               sink_fid=sink_fid, vt=vt_hit, time_dep=time_dep,
               time_arr=time_arr, raw=raw_added, **c5d))


# --- write them out -----------------------------------------------------------
# The report's counter lines. Only the ones the four blocks read are listed; a
# fixture carrying a counter nothing reads would suggest a gate that does not
# exist.
REPORT_KEYS = [
    ("files", "files"),
    ("suppress.sites", "suppress_sites"),
    ("anonymousfunctional.size", "anonfn"),
    ("anonymousfunctional.convertible", "conv"),
    ("anonymousfunctional.forward", "forward"),
    ("iface.decl", "ifacedecl"),
    ("iface.sam", "sam"),
    ("foreach.size", "foreach"),
    ("foreach.collect", "collect"),
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
    ("niofile.files", "niofiles"),
]

CLASSES_PER_FIXTURE = 24


def split_bucket(b):
    path, meth = b.split("#", 1)
    return path, meth


def filecount(buckets):
    return len(set(split_bucket(b)[0] for b in buckets))


def sites(items, total):
    """Distribute `total` records round-robin over `items`.

    What the arrival halves read is a SET, so what matters is which paths and
    buckets appear rather than how the count is spread. Round-robin keeps the
    summary line and the number of records emitted the same number, which is a
    property a real report has and a gate could notice.
    """
    if not items:
        return []
    return [(items[i % len(items)], 10 + i) for i in range(total)]


def rec(fh, kind, bodies, f):
    """Write one record block, dropping the in= field when the fixture is the
    stale-gate one."""
    for i, body in enumerate(bodies):
        fh.write("%s[%d]=%s\n" % (kind, i, strip_in(body) if f["no_in"] else body))


def build_records(f):
    """Every per-site record this fixture's report holds, as SourceScan writes
    them. The departure blocks are the input's own records; the arrival blocks
    are synthesised at deterministic line numbers."""
    r = {}
    r["retnull"] = ([b for k in OPTBUCKETS if k in set(f["retnull_left"])
                     for b in RETNULL_BY[k]] + RETNULL_REST)
    r["optret"] = ["%s:%d ret=Optional<%s> name=%s in=%s"
                   % (split_bucket(b)[0], 100 + i,
                      OPTTYPE.get(b, "Object"), split_bucket(b)[1],
                      split_bucket(b)[1])
                   for i, b in enumerate(f["optret_at"])]
    ops = []
    at = list(f["optbucket_at"]) or list(f["optret_at"]) or list(OPTBUCKETS)
    for i in range(f["optmakes"]):
        b = at[i % len(at)]
        ops.append("%s:%d make %s in=%s" % (split_bucket(b)[0], 200 + i,
                                            ("of", "ofNullable", "empty")[i % 3],
                                            split_bucket(b)[1]))
    for i, name in enumerate(f["optuses"]):
        b = at[i % len(at)]
        ops.append("%s:%d use %s in=%s" % (split_bucket(b)[0], 300 + i, name,
                                           split_bucket(b)[1]))
    r["optop"] = ops
    r["accum"] = ([b for k in SINKS if k in set(f["accum_left"])
                   for b in ACCUM_BY[k]] + ACCUM_REST)
    r["collect"] = ["%s:%d to=%s in=%s" % (split_bucket(b)[0], 400 + i,
                                           f["collect_at"][b], split_bucket(b)[1])
                    for i, b in enumerate(sorted(f["collect_at"]))]
    r["objects"] = ["%s:%d %s in=%s" % (split_bucket(b)[0], 500 + i, name,
                                        split_bucket(b)[1])
                    for i, (b, name) in enumerate(f["objects_at"])]
    jt, i = [], 0
    for b in f["jtime_at"]:
        for verb in GOLDEN_JTIME.get(b, ("Instant.now",)):
            jt.append("%s:%d java.time.%s as=call in=%s"
                      % (split_bucket(b)[0], 600 + i, verb, split_bucket(b)[1]))
            i += 1
    r["jtime"] = jt
    r["millis"] = [b for k in TIMESITES if k in set(f["millis_left"])
                   for b in MILLIS_BY[k]] + MILLIS_REST
    r["lambda"] = ["%s:%d <Runnable> body=expr methodrefable=no" % (p, ln)
                   for p, ln in sites(list(f["lam_paths"]), f["lam_n"])]
    r["methodref"] = ["%s:%d INVOKE handle" % (p, ln)
                      for p, ln in sites(list(f["mref_paths"]), f["mref_n"])]
    r["stream"] = ["%s:%d ENTER stream in=%s" % (split_bucket(b)[0], ln,
                                                 split_bucket(b)[1])
                   for b, ln in sites(list(f["stream_sites"]), f["streams"])]
    r["streamop"] = ["%s:%d ON filter in=%s" % (split_bucket(b)[0], ln,
                                                split_bucket(b)[1])
                     for b, ln in sites(list(f["streamop_sites"]), f["streamops"])]
    return r


# Four byte strings that a CONSTANT_Utf8 entry naming each package would hold.
# The gate greps for exactly these, so exactly these are what a fixture's class
# files carry; `java/time/` is the prefix form the gate uses, and Instant is
# written after it so that the fixture matches the same way a real class does.
C5_MARKERS = ("java/util/Optional", "java/util/stream/Collectors",
              "java/util/Objects", "java/time/Instant")


def write_jars(d, f):
    """Two real jars holding real .class entries, for C-5 to unzip and grep.

    Two rather than one so that the `while read` over modulejars.txt is
    exercised with more than one iteration, and the classes are interleaved
    between them so that no package's whole count can be collected from one
    archive. The bytes are a class-file magic number and a run of Utf8-shaped
    entries: C-5 is a binary grep over constant pools, not a class parser, and a
    javac-produced fixture would only be a slower way to write the same bytes.
    """
    if f["jars"] == "none":
        return
    jd = os.path.join(d, "jars")
    if not os.path.isdir(jd):
        os.makedirs(jd)
    # Every entry is written at a FROZEN timestamp. zipfile defaults each
    # ZipInfo to the wall clock, which made two runs of this generator produce
    # jars with identical contents and different bytes -- so a regeneration
    # looked like a changed fixture, and the duplicate-tree check that hashes
    # these files could not tell a real edit from the time of day. Trap 10, in
    # a place it is easy not to look for: the artifact, not the expectation.
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
            blob = bytearray(b"\xca\xfe\xba\xbe\x00\x00\x00\x34")
            blob += b"\x01\x00\x20org/jboss/arquillian/zz/C%02d" % i
            for m, n in zip(C5_MARKERS, f["c5"]):
                if i < n:
                    blob += b"\x01" + bytes([0, len(m)]) + m.encode()
            blob += b"\x00" * 8
            put(zips[i % 2], "org/jboss/arquillian/zz/C%02d.class" % i, bytes(blob))
    finally:
        for z in zips:
            z.close()
    for m, n in zip(C5_MARKERS, f["c5"]):
        assert n <= CLASSES_PER_FIXTURE, (m, n)


def write(name, f, why):
    d = os.path.join(OUT, name)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(os.path.join(d, "why.txt"), "w") as fh:
        fh.write(why + "\n")

    r = build_records(f)
    with open(os.path.join(d, "report.txt"), "w") as fh:
        for key, attr in REPORT_KEYS:
            fh.write("%s=%s\n" % (key, f[attr]))
        # The derived counts, kept consistent with the records rather than
        # stated: a report whose lambda.files disagreed with its lambda[]
        # records is not a report any tree produces.
        fh.write("lambda.size=%d\n" % len(r["lambda"]))
        fh.write("lambda.files=%d\n" % len(set(f["lam_paths"])))
        fh.write("methodref.size=%d\n" % len(r["methodref"]))
        fh.write("iface.samannotated=%d\n" % len(f["sam_annotated"]))
        fh.write("stream.producers=%d\n" % len(r["stream"]))
        fh.write("stream.ops=%d\n" % len(r["streamop"]))
        fh.write("stream.files=%d\n" % filecount(f["stream_sites"]))
        fh.write("stream.opfiles=%d\n" % filecount(f["streamop_sites"]))
        fh.write("loop.size=%d\n" % f["loop"])
        # `stream.opmethods` is the one counter in the report that cannot be
        # computed without attributing a site to a method, so the fixture built
        # from a gate jar that emits no in= field does not carry it either. The
        # scorer initialises it empty above its own guard and publishes it as
        # `absent`; that it survives a missing counter under `set -u` is a
        # property worth rehearsing rather than assuming. `loop.size` needs no
        # attribution and stays.
        if not f["no_in"]:
            fh.write("stream.opmethods=%d\n" % len(set(f["streamop_sites"])))
        for kind in ("retnull", "optret", "optop", "accum", "collect",
                     "objects", "jtime", "millis"):
            fh.write("%s.size=%d\n" % (kind, len(r[kind])))
        fh.write("optop.get=%d\n"
                 % len([u for u in f["optuses"] if u == "get"]))
        # The factory census, for the facts rather than for a gate: C-2 reads
        # the per-site to= fields and never these three.
        for fac in ("tolist", "toset", "tocollection"):
            spell = {"tolist": "toList", "toset": "toSet",
                     "tocollection": "toCollection"}[fac]
            fh.write("collect.%s=%d\n"
                     % (fac, len([1 for v in f["collect_at"].values() if v == spell])))
        for kind in ("retnull", "optret", "optop", "accum", "collect", "objects",
                     "jtime", "millis", "lambda", "methodref", "stream",
                     "streamop"):
            rec(fh, kind, r[kind], f)
        # Every interface declaration the tree has, with the annotated flag on
        # the ones this fixture marked, so that B-11's intersection is asked to
        # pick 24 out of 65 rather than being handed only the answer.
        annotated = list(f["sam_annotated"])
        rest = [p for p in SAMPAIRS if p not in set(annotated)]
        n = 0
        for pair in annotated:
            path, nm = pair.split(None, 1)
            fh.write("interfacedecl[%d]=%s:%d %s abstract=1 annotated=yes\n"
                     % (n, path, 20 + n, nm))
            n += 1
        for pair in rest:
            path, nm = pair.split(None, 1)
            fh.write("interfacedecl[%d]=%s:%d %s abstract=1 annotated=no\n"
                     % (n, path, 20 + n, nm))
            n += 1
        for i in range(f["ifacedecl"] - n):
            fh.write("interfacedecl[%d]=zz-added/src/main/java/zz/Multi%02d.java:"
                     "%d Multi%02d abstract=3 annotated=no\n"
                     % (n + i, i, 30 + i, i))

    with open(os.path.join(d, "answers.txt"), "w") as fh:
        for k in ("ok18", "ok17", "okdef"):
            fh.write("%s=%s\n" % (k, f[k]))
        fh.write("errors=%d\n" % f["e17"])

    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        if f["broke_nio"]:
            for i, nid in enumerate(NIOIDS):
                fh.write("behavior.differ[%d]=%s\n" % (i, nid))

    write_jars(d, f)


def normalise(f):
    """`streamop_sites` defaults to the producer buckets.

    A stage is a call ON a stream and the producer that made it is in the same
    expression, so a report holding a stage in a bucket with no producer is a
    report no tree produces. The fixtures that need them to differ say so.
    """
    if f["streamop_sites"] == "same":
        return dict(f, streamop_sites=f["stream_sites"])
    return f


def check_pinned(path):
    """The pinned fixture is this rung's INPUT, and its report is that input's
    own measured report. Not `resembles`: every counter the scoring blocks read
    has to come out at the value baseline/source.txt already states, because the
    verifier compares the two directly and a fixture that disagreed with the
    baseline would be rehearsing a tree that does not exist.

    The golden end is checked the same way against the report the calibration
    run measured on the solution tree; that file lives outside the task package,
    so the agreement is recorded in NOTES-authoring.md rather than asserted
    here.
    """
    bad = []
    for line in open(path):
        line = line.strip()
        if not line or "[" in line or "=" not in line:
            continue
        k, v = line.split("=", 1)
        if k in BASE and BASE[k] != v:
            bad.append("%s: fixture=%s baseline=%s" % (k, v, BASE[k]))
    assert not bad, "pinned disagrees with baseline-step05-source.txt:\n  " \
        + "\n  ".join(bad)


rows = []
for name, f, why in FIX:
    f = normalise(f)
    write(name, f, why)
    if name == "pinned":
        check_pinned(os.path.join(OUT, name, "report.txt"))
    ctot, cp = c_score(f)
    if f["ok18"] == "no" and f["okdef"] == "no":
        # The scorer's broken-report branch assigns b10=0 and b11=0 as integers
        # and never reaches the awk that would print them to six places, so both
        # facts read "0". Replicated exactly: the rehearsal compares strings.
        btot, bp = "0", dict(d=0, r=0, n=0)
        b11tot, b11p = "0", dict(l=0, s=0, c=0, covl=0, sam=0, covc=0)
    else:
        btot, bp = b10_score(f)
        btot = "%.6f" % btot
        b11tot, b11p = b11_score(f)
        b11tot = "%.6f" % b11tot
    rows.append((name, ctot, cp, btot, bp, b11tot, b11p, why))

print("# the per-gate reading, for the notes and for reading this by eye")
print("# %-7s %-8s %-8s %-8s %-8s %-8s %-8s %-8s | %-8s | %-8s" % (
    "fixture", "familyC", "C1", "C2", "C3", "C4", "C5", "pen", "B10", "B11"))
for name, ctot, cp, btot, bp, b11tot, b11p, why in rows:
    print("  %-7s %-8s %-8s %-8s %-8s %-8s %-8s %-8s | %-8s | %-8s  %s" % (
        name, ctot, cp["c1"], cp["c2"], cp["c3"], cp["c4"], cp["c5"],
        cp["pen"] if cp["pen"] is not None else "-", btot, b11tot, why))
print()
print("# the gate facts, for the factck stanzas")
for name, ctot, cp, btot, bp, b11tot, b11p, why in rows:
    d = cp["d"]
    if d:
        print("  %-7s opt dep=%s arr=%s uses=%s get=%s cons=%s | sink dep=%s arr=%s fid=%s"
              " | vt=%s | time dep=%s arr=%s | b11 covl=%s sam=%s covc=%s"
              % (name, d["opt_dep"], d["opt_arr"], d["uses"], d["get"], d["cons"],
                 d["sink_dep"], d["sink_arr"], d["sink_fid"], d["vt"],
                 d["time_dep"], d["time_arr"], b11p["covl"], b11p["sam"], b11p["covc"]))
    else:
        print("  %-7s (no report measured) | b11 covl=%s sam=%s covc=%s"
              % (name, b11p["covl"], b11p["sam"], b11p["covc"]))
print()
print("# the TABLE heredoc for c-rehearsal.sh -- transcribe these lines by hand.")
print("# Fields: fixture familyC B10 B11. All three are compared as strings, so")
print("# the shell's own spelling is what is written: a gate that never ran")
print("# publishes the literal 0 and one that scored zero publishes 0.000000.")
for name, ctot, cp, btot, bp, b11tot, b11p, why in rows:
    print("%-8s %-8s %-8s %s" % (name, ctot, btot, b11tot))
