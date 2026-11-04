#!/usr/bin/env python3
"""Build step 04's rehearsal fixtures for family C and B-10, and compute what
each one must score.

    python3 authoring/fixtures-step04/mkfix04.py

Writes one directory per fixture under fixtures-step04/c/, each holding the
source report the shipped scoring blocks will read, the compile answers they
would have got from javac, a 1.7 javac log, and a behaviour summary. Prints the
score table that c-rehearsal.sh asserts.

Why a report and not a tree. Step 02's fixtures had to be trees, because its
gate reads compiled classes and there is no honest way to fake one. The blocks
rehearsed here read a report that arqgate.SourceScan wrote from a typed AST, and
that report is a list of counts and per-site records; a fixture can therefore
state them directly, and state combinations no real tree would produce -- sixty
lambdas in a tree that still compiles at -source 1.7, or thirty-six converted
bodies whose lambdas landed in files that never held one. Those are the
interesting fixtures, and a generator that had to emit a compilable tree for
each could not write them.

What the tree-shaped fixtures cost instead: nothing here says SourceScan's
counts are right, only that the arithmetic over them is. The counts are verified
separately and by a different instrument -- fixtures/src-rehearsal.sh compiles a
smoke tree with the plugin attached at image build time and asserts every
counter and every record shape against figures measured first and frozen after.

TWO blocks, not one. Step 03 rehearsed family C alone, because family C was the
only thing that read the AST. This step's B-10 reads it too -- it is the gate
that holds step 03's three constructs in place, the ladder's ninth trap made
scorable -- so both are extracted from the shipped test.sh and both are scored
here. The two ends matter most for B-10 and are the first two rows of the table:
the step-03 golden must read 1.000000 on it, because that IS step 03's work and
the floor arithmetic in the scorer depends on the unmigrated tree collecting
B-10 in full; and this step's golden must also read 1.000000, because two of
step 03's figures legitimately move across this step and a gate that read them
raw would fail the correct tree.

The per-site records are generated from the REAL frozen set files --
baseline-step04-lambdafiles.txt, -streamfiles.txt, -samifaces.txt and
-collectmethods.txt -- so the arrival halves are rehearsed against the paths and
the <path>#<method> buckets they will actually be asked about. A fixture that
invented its own paths would agree with a gate pointed at any set at all.

The `want` figures are computed here, by an independent implementation of the
same composition, and then written into c-rehearsal.sh BY HAND. The rehearsal
never calls this file: a rehearsal that computes its own expectations at run
time agrees with the scorer by construction and asserts nothing. The verifier
image has no python3 either, which makes that separation structural rather than
a matter of discipline.
"""
import os

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


# --- the frozen numbers, read rather than retyped -----------------------------
# A fixture set calibrated against figures that have drifted is a rehearsal that
# passes against the wrong question, so every threshold below comes out of the
# same two files the scorer reads.
BASE = read_env(os.path.join(AUTH, "baseline-step04-source.txt"))
TGT = read_env(os.path.join(AUTH, "baseline-step04-target.txt"))

BASE_ANONFN = int(BASE["anonymousfunctional.size"])          # 48
BASE_COLLECT = int(BASE["foreach.collect"])                  # 24
BASE_SUP = int(BASE["suppress.sites"])                       # 44
BASE_RAW = int(BASE["newgeneric.raw"])                       # 0
T_ANONFN = int(TGT["target.anonfn"])                         # 12
ANONFN_NEED = BASE_ANONFN - T_ANONFN                         # 36
T_LAMFILES = int(TGT["target.lambdafiles"])                  # 20
T_SAM = int(TGT["target.sam"])                               # 24
T_STREAMFILES = int(TGT["target.stream.files"])              # 14
T_OPFILES = int(TGT["target.stream.opfiles"])                # 14
T_COLLECTMETHODS = int(TGT["target.collectmethods"])         # 24
T_OPMETHODS = int(TGT["target.stream.opmethods"])            # 24
BASE_LOOP = int(BASE["loop.size"])                           # 139
T_LOOP = int(TGT["target.loop.size"])                        # 115
C4_FLOOR = int(TGT["target.level17.floor"])                  # 20
T_DIA = int(TGT["target.diamond"])                           # 93
T_NONANON = int(TGT["target.explicit.nonanon"])              # 23
T_RES = int(TGT["target.try.resources"])                     # 9
T_IO = int(TGT["target.iofile"])                             # 2
B10_DIA = int(TGT["target.b10.diamond"])                     # 93
B10_CLOSE = int(TGT["target.b10.close"])                     # 11
B10_NIO = int(TGT["target.b10.nio"])                         # 11

LAMFILES = read_set(os.path.join(AUTH, "baseline-step04-lambdafiles.txt"))
STREAMFILES = read_set(os.path.join(AUTH, "baseline-step04-streamfiles.txt"))
SAMPAIRS = read_set(os.path.join(AUTH, "baseline-step04-samifaces.txt"))
NIOIDS = read_set(os.path.join(AUTH, "baseline-step03-nioids.txt"))
# The 24 <path>#<method> buckets holding a collecting loop, which is the scope
# C-3 pairs departure with arrival at since round 2 of the static review. The
# 14 files are these 24 buckets' files and nothing else, which is why the two
# sets cannot be calibrated against each other by accident.
COLLECTMETHODS = read_set(os.path.join(AUTH, "baseline-step04-collectmethods.txt"))

assert len(LAMFILES) == T_LAMFILES, len(LAMFILES)
assert len(STREAMFILES) == T_STREAMFILES, len(STREAMFILES)
assert len(SAMPAIRS) == T_SAM, len(SAMPAIRS)
assert len(COLLECTMETHODS) == T_COLLECTMETHODS, len(COLLECTMETHODS)
assert sorted(set(b.split("#")[0] for b in COLLECTMETHODS)) == sorted(STREAMFILES)

# The 20 lambda-file paths are a strict subset of the 33 the golden holds
# lambdas in; the other 13 are the stream files, whose collecting loops carry
# lambdas of their own. Spelled out here because it is what stops C-1's arrival
# half from being paid by C-3's work.
ELSEWHERE = ["zz-added/src/main/java/zz/New%02d.java" % i for i in range(1, 25)]
ELSEWHERE_BUCKETS = ["%s#go" % p for p in ELSEWHERE]
# Round 2's attack, spelled as a fixture: one dummy pipeline per frozen FILE,
# in a method that never held a loop. Fourteen statements, no conversion, and
# the file-scope conjunction pays them in full.
DUMMY_BUCKETS = ["%s#zzDummy" % p for p in STREAMFILES]
FAKESAM = ["zz-added/src/main/java/zz/Iface%02d.java Iface%02d" % (i, i)
           for i in range(1, 25)]


# --- the two measurements -----------------------------------------------------
# PINNED is the step-03 golden, which is this rung's INPUT: the tree the
# candidate is handed. GOLDEN is the step-04 solution. Both are measurements,
# and every other fixture is a stated edit of one of them.
PINNED = dict(
    files=322, suppress_sites=BASE_SUP,
    anonfn=BASE_ANONFN, conv=int(BASE["anonymousfunctional.convertible"]),
    forward=int(BASE["anonymousfunctional.forward"]),
    lam_n=0, lam_paths=(),
    mref_n=0, mref_paths=(),
    ifacedecl=65, sam=T_SAM, sam_annotated=(),
    foreach=int(BASE["foreach.size"]), collect=BASE_COLLECT,
    streams=0, streamops=0, stream_sites=(), streamop_sites="same",
    no_in=False,
    loop=BASE_LOOP,
    # step 03's gains, at the values step 03 left them at
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
    # javac's two answers. The step-03 golden compiles clean at BOTH levels,
    # which is exactly why C-4 reads 0 on it and why C-4 is the gate that
    # separates this rung from the one below.
    ok18="yes", ok17="yes", okdef="yes", e17=0, lvl17=0, broke_nio=False,
)
GOLDEN = dict(
    PINNED,
    anonfn=T_ANONFN, conv=int(TGT["target.convertible"]),
    lam_n=int(TGT["target.lambda"]), lam_paths=tuple(LAMFILES),
    mref_n=int(TGT["target.methodref"]), mref_paths=tuple(LAMFILES[:12]),
    sam_annotated=tuple(SAMPAIRS),
    foreach=90, collect=int(TGT["target.foreach.collect"]),
    streams=int(TGT["target.stream.producers"]),
    streamops=int(TGT["target.stream.ops"]),
    stream_sites=tuple(COLLECTMETHODS),
    loop=T_LOOP,
    # The two figures that move for an honest reason: 14 explicit-generic sites
    # sit on anonymous classes that become lambdas, so explicit falls 50 -> 36
    # and explicitanon 27 -> 13 by the same 14. The non-anonymous remainder is
    # 23 at both ends, and B-10 reads that remainder rather than the raw count.
    explicit=36, explicitanon=13,
    ok17="no", e17=int(TGT["target.level17.errors"]),
    lvl17=int(TGT["target.level17.errors"]),
)
assert GOLDEN["explicit"] - GOLDEN["explicitanon"] == T_NONANON
assert PINNED["explicit"] - PINNED["explicitanon"] == T_NONANON


def variant(base, **kw):
    d = dict(base)
    d.update(kw)
    return d


# --- the fixture set ----------------------------------------------------------
# Every entry is paired with the clean control it perturbs -- which for all of
# them is `golden`, and for the C-4 breadth row is `pinned` as well -- and every
# perturbation is a route a candidate could actually take, or, where it is not,
# is here to show that two channels are independent.
FIX = [
    # the two ends, which are what make everything between them mean something
    ("pinned", PINNED, "the step-03 golden: this rung's input"),
    ("golden", GOLDEN, "the step-04 golden solution"),

    ("H1", variant(GOLDEN, anonfn=30, lam_n=30, lam_paths=tuple(LAMFILES[:10]),
                   mref_n=12, mref_paths=tuple(LAMFILES[:6]),
                   sam_annotated=tuple(SAMPAIRS[:12]),
                   collect=12, loop=T_LOOP + 12, streams=12, streamops=24,
                   stream_sites=tuple(COLLECTMETHODS[:12]),
                   e17=24, lvl17=24),
     "about half of each of the three constructs"),

    # C-1: the two ways of faking a conversion, and the spelling that is not one
    ("H2", variant(GOLDEN, lam_n=0, lam_paths=(), mref_n=0, mref_paths=()),
     "the 36 anonymous bodies deleted, no lambda or method reference written"),
    ("H3", variant(GOLDEN, anonfn=BASE_ANONFN),
     "60 lambdas written alongside all 48 anonymous bodies, none converted"),
    ("H4", variant(GOLDEN, lam_paths=tuple(ELSEWHERE[:20]), mref_paths=()),
     "the bodies gone and 60 lambdas written, none of them where a body was"),
    ("H5", variant(GOLDEN, anonfn=BASE_ANONFN, conv=0),
     "a member added to each convertible body: convertible falls to 0, size does not"),
    ("H6", variant(GOLDEN, lam_n=0, lam_paths=(), mref_n=36,
                   mref_paths=tuple(LAMFILES)),
     "every one of the 36 migrated to a method reference and not to a lambda"),

    # C-2: the count reachable without touching the tree's own interfaces
    ("H7", variant(GOLDEN, sam_annotated=tuple(FAKESAM)),
     "24 trivial annotated interfaces declared, the tree's own 24 left unmarked"),
    ("H8", variant(GOLDEN, sam_annotated=tuple(SAMPAIRS[:12])),
     "half of the SAM interfaces annotated"),

    # C-3: the same two fakes at the gate no automated migrator reaches
    ("H9", variant(GOLDEN, streams=0, streamops=0, stream_sites=()),
     "the 24 collecting loops deleted, no stream pipeline written"),
    ("H10", variant(GOLDEN, stream_sites=tuple(ELSEWHERE_BUCKETS[:14])),
     "the loops gone and 24 pipelines written, none where a loop was"),
    ("H11", variant(GOLDEN, collect=12, loop=T_LOOP + 12, streams=12,
                    streamops=24, stream_sites=tuple(COLLECTMETHODS[:12])),
     "half the collecting loops migrated"),

    # C-3 again, and these three are round 1 of this step's static review made
    # scorable. The review found the 0.60 of family C that sits on C-3 reachable
    # without a pipeline, by two independent holes, and each is closed by a term
    # that did not exist before; H27 is the instrument failing rather than the
    # tree. All three read 0.400000 -- the same total as H9 and H10, which is
    # why the facts below the table are what tell the five apart.
    ("H25", variant(GOLDEN, loop=BASE_LOOP),
     "the 24 collecting loops respelled as while, pipelines written alongside"),
    ("H26", variant(GOLDEN, streamops=0, streamop_sites=()),
     "a bare xs.stream(); in each of the 14 files: a producer with no stage"),
    ("H27", variant(GOLDEN, loop=None),
     "a verifier built from a gate jar too old to emit the loop census"),

    # C-3, round 2 of the static review. The round-1 gate paired the departed
    # loop with the arrived pipeline at the FILE, and 14 files is a large enough
    # place to write 14 dummy statements in. H28 is that tree; it scores C-3's
    # file-scope coverage in full and its method-scope coverage at zero, which
    # is the whole finding in two facts. H29 is the instrument failing the same
    # way H27 does one term over: a gate jar too old to write the in= field
    # cannot bucket anything, and must score zero rather than read the absence
    # as a clean sweep.
    ("H28", variant(GOLDEN, stream_sites=tuple(DUMMY_BUCKETS)),
     "one dummy pipeline per frozen file, none in a method that held a loop"),
    ("H29", variant(GOLDEN, no_in=True),
     "a verifier built from a gate jar too old to emit the in= field"),

    # C-4: the level channel, in each of the four ways it says no
    ("H12", variant(GOLDEN, ok17="yes", e17=0, lvl17=0),
     "every construct present and the tree still compiles at -source 1.7"),
    ("H13", variant(GOLDEN, e17=7, lvl17=3),
     "the 1.7 compile fails, but not over the language level"),
    ("H14", variant(GOLDEN, ok18="no", okdef="yes"),
     "the tree does not compile at 1.8; graded at the default level instead"),
    ("H15", variant(GOLDEN, ok18="no", okdef="no"),
     "no compile of the tree produced a report at all"),

    # C-4's breadth, at the floor and one construct below it
    ("H16", variant(PINNED, ok17="no", e17=1, lvl17=1),
     "the 1.7 flip bought with one lambda and nothing else migrated"),
    ("H17", variant(GOLDEN, e17=C4_FLOOR, lvl17=C4_FLOOR),
     "the migration spelled in fewer files: exactly the breadth floor"),

    # the anti-gaming multiplier, each half on its own
    ("H18", variant(GOLDEN, raw=5), "five raw types added over the baseline's zero"),
    ("H19", variant(GOLDEN, suppress_sites=BASE_SUP + 6),
     "six @SuppressWarnings sites added"),

    # B-10: step 03's three constructs, undone one at a time
    ("H20", variant(GOLDEN, explicit=129, diamond=0, diamondfiles=0),
     "the diamonds unwound back to explicit type arguments"),
    ("H21", variant(GOLDEN, resources=0, resourcevars=0, close=B10_CLOSE),
     "the resource clauses unwound back to manual close() calls"),
    ("H22", variant(GOLDEN, ionew=8, iodecl=5, nioref=0, niofiles=0),
     "the java.nio.file migration unwound back to java.io.File"),
    ("H23", variant(GOLDEN, explicit=82, diamond=47, diamondfiles=24,
                    resources=4, close=6, ionew=4, iodecl=3, nioref=6),
     "about half of each of step 03's three constructs given back"),
    ("H24", variant(GOLDEN, broke_nio=True),
     "step 03's file migration intact but its four recorded behaviours broken"),
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


def c_score(f):
    """Family C: the Java 8 migration."""
    # which report the block ends up measuring, and what C-4 can be
    if f["ok18"] == "yes":
        measured, c4_possible = True, (f["ok17"] == "no")
    elif f["okdef"] == "yes":
        measured, c4_possible = True, False
    else:
        measured, c4_possible = False, False
    # The gate facts are compared by the rehearsal AS STRINGS, so the shell's
    # own spelling of each is reproduced rather than its value. `c1=0; c2=0;
    # c3=0; c4=0; cpen=1` are shell literals: a gate the block never reaches
    # publishes "0", and one it computes publishes "%.6f". They are the same
    # number and not the same fact, and a rehearsal that normalised them would
    # stop being able to tell a gate that scored zero from one that never ran.
    if not measured:
        return "0.000000", dict(c1="0", c2="0", c3="0", c4="0", pen=None)

    departed = max(0, BASE_ANONFN - f["anonfn"])
    cov1 = len((set(f["lam_paths"]) | set(f["mref_paths"])) & set(LAMFILES))
    c1 = minf(clamp01(frac(departed, ANONFN_NEED)),
              clamp01(frac(cov1, T_LAMFILES)))

    samhit = len(set(f["sam_annotated"]) & set(SAMPAIRS))
    c2 = clamp01(frac(samhit, T_SAM))

    # Three halves. Departure is the collecting loops going; no-reshape is the
    # whole-tree loop census falling by the same number, so that a `while`
    # rewrite of all 24 scores nothing; arrival is a producer AND a stage in the
    # same METHOD one of the 24 loops was in, so that neither a bare
    # `xs.stream();` nor a dummy pipeline one-per-file is a conversion. A report
    # with no loop census at all, or one whose records carry no in= field, is a
    # broken instrument and scores zero rather than reading the absence as a
    # clean sweep.
    collected = max(0, BASE_COLLECT - f["collect"])
    if f["loop"] is None:
        loopterm = 0.0
    else:
        loopterm = clamp01(frac(max(0, BASE_LOOP - f["loop"]), BASE_COLLECT))
    if f["no_in"]:
        cov3 = 0
    else:
        cov3 = len(set(f["stream_sites"]) & set(f["streamop_sites"])
                   & set(COLLECTMETHODS))
    c3 = minf(minf(clamp01(frac(collected, BASE_COLLECT)), loopterm),
              clamp01(frac(cov3, T_COLLECTMETHODS)))

    if c4_possible and f["lvl17"] > 0 and f["lvl17"] == f["e17"]:
        c4 = clamp01(frac(f["lvl17"], C4_FLOOR))
    else:
        c4 = 0

    pen = 1.0
    penalised = False
    raw_added = max(0, f["raw"] - BASE_RAW)
    if raw_added > 0:
        pen = f6(max(0.0, 1 - raw_added / 10.0))
        penalised = True
    if f["suppress_sites"] > BASE_SUP:
        pen = f6(pen / 2)
        penalised = True

    total = f6(pen * (0.15 * c1 + 0.15 * c2 + 0.60 * c3 + 0.10 * c4))
    return "%.6f" % total, dict(
        c1="%.6f" % c1, c2="%.6f" % c2, c3="%.6f" % c3,
        c4=("%.6f" % c4) if c4_possible and c4 else "0",
        pen=("%.6f" % pen) if penalised else "1")


# --- write them out -----------------------------------------------------------
# The report's counter lines, in the order SourceScan emits them. Only the ones
# the two blocks read are listed; a fixture carrying a counter nothing reads
# would suggest a gate that does not exist.
REPORT_KEYS = [
    ("files", "files"),
    ("suppress.sites", "suppress_sites"),
    ("anonymousfunctional.size", "anonfn"),
    ("anonymousfunctional.convertible", "conv"),
    ("anonymousfunctional.forward", "forward"),
    ("lambda.size", "lam_n"),
    ("methodref.size", "mref_n"),
    ("iface.decl", "ifacedecl"),
    ("iface.sam", "sam"),
    ("foreach.size", "foreach"),
    ("foreach.collect", "collect"),
    ("stream.producers", "streams"),
    ("stream.ops", "streamops"),
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


def split_bucket(b):
    """`<path>#<method>` -> the two halves."""
    path, meth = b.split("#", 1)
    return path, meth


def filecount(buckets):
    return len(set(split_bucket(b)[0] for b in buckets))


def sites(paths, total):
    """Distribute `total` records round-robin over `paths`.

    Per-site records are what the arrival halves read, and they are read as a
    SET of paths -- so what matters is which files appear, not how the count is
    spread. Round-robin keeps the two consistent: the count in the report's
    summary line and the number of records emitted are the same number, which
    is a property a real report has and a gate could notice.
    """
    if not paths:
        return []
    return [(paths[i % len(paths)], 10 + i) for i in range(total)]


def write(name, f, why):
    d = os.path.join(OUT, name)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(os.path.join(d, "why.txt"), "w") as fh:
        fh.write(why + "\n")

    with open(os.path.join(d, "report.txt"), "w") as fh:
        for key, attr in REPORT_KEYS:
            fh.write("%s=%s\n" % (key, f[attr]))
        # The two derived file counts, kept consistent with the records rather
        # than stated: a report whose lambda.files disagreed with its lambda[]
        # records is not a report any tree produces.
        fh.write("lambda.files=%d\n" % len(set(f["lam_paths"])))
        fh.write("stream.files=%d\n" % filecount(f["stream_sites"]))
        fh.write("stream.opfiles=%d\n" % filecount(f["streamop_sites"]))
        # Omitted by the stale-jar fixture, for the same reason loop.size is:
        # the scorer publishes `absent` rather than 0, and a fixture has to be
        # able to make the key missing for that to be a distinguishable fact.
        if not f["no_in"]:
            fh.write("stream.opmethods=%d\n" % len(set(f["streamop_sites"])))
        # Omitted entirely when the fixture is the stale-gate one. `${x:-0}`
        # would turn a missing census into a full-credit reading, so the scorer
        # branches on the key being absent and the fixture has to be able to
        # make it absent.
        if f["loop"] is not None:
            fh.write("loop.size=%d\n" % f["loop"])
        fh.write("iface.samannotated=%d\n" % len(f["sam_annotated"]))
        # The per-site records. SourceScan's own emit shapes, which is what the
        # gates match: a paraphrase would pass a gate the real report fails.
        for i, (p, ln) in enumerate(sites(list(f["lam_paths"]), f["lam_n"])):
            fh.write("lambda[%d]=%s:%d <Runnable> body=expr methodrefable=no\n"
                     % (i, p, ln))
        for i, (p, ln) in enumerate(sites(list(f["mref_paths"]), f["mref_n"])):
            fh.write("methodref[%d]=%s:%d INVOKE handle\n" % (i, p, ln))
        for i, (b, ln) in enumerate(sites(list(f["stream_sites"]), f["streams"])):
            path, meth = split_bucket(b)
            fh.write("stream[%d]=%s:%d ENTER stream%s\n"
                     % (i, path, ln, "" if f["no_in"] else " in=" + meth))
        for i, (b, ln) in enumerate(sites(list(f["streamop_sites"]), f["streamops"])):
            path, meth = split_bucket(b)
            fh.write("streamop[%d]=%s:%d ON filter%s\n"
                     % (i, path, ln, "" if f["no_in"] else " in=" + meth))
        # Every interface declaration the tree has, with the annotated flag on
        # the ones this fixture marked. The unannotated remainder is emitted
        # too, so that C-2's intersection is asked to pick 24 out of 65 rather
        # than being handed only the answer.
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
        fh.write("levelerrors=%d\n" % f["lvl17"])

    # The 1.7 javac log C-4 greps for level errors. Written with the exact
    # diagnostics javac emits, because those strings are what the gate matches.
    #
    # BOTH inflections, deliberately. javac agrees the message with the
    # feature's own name -- "lambda expressions ARE not supported", "the diamond
    # operator IS not supported" -- and step 03's pattern carried the `is`,
    # which matched none of this step's errors. The gate now greps
    # `not supported in -source` without the verb, and half of every log below
    # carries the other inflection so that a pattern which regained a verb
    # fails here.
    with open(os.path.join(d, "javac17.log"), "w") as fh:
        for i in range(f["lvl17"]):
            feature = ("lambda expressions are" if i % 2 == 0
                       else "method references are")
            fh.write("/app/arquillian/x/Y%d.java:1: error: %s not supported "
                     "in -source 1.7\n" % (i, feature))
        for i in range(f["e17"] - f["lvl17"]):
            fh.write("/app/arquillian/x/Z%d.java:1: error: cannot find symbol\n" % i)

    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        if f["broke_nio"]:
            for i, nid in enumerate(NIOIDS):
                fh.write("behavior.differ[%d]=%s\n" % (i, nid))


def normalise(f):
    """`streamop_sites` defaults to the producer buckets.

    Stated per fixture it would be twenty-six chances to write a tree where a
    stage lands in a file with no producer -- a shape no report can hold, since
    a stage is a call ON a stream and the producer that made it is in the same
    expression. The two fixtures that need them to differ say so explicitly.
    """
    if f["streamop_sites"] == "same":
        return dict(f, streamop_sites=f["stream_sites"])
    return f


# The loop census is not free to be anything. A collecting loop that was
# migrated is a loop that is gone, so on every fixture except the two that exist
# to break exactly that relation the census must fall by the number of
# collecting loops that left. An arbitrary figure here would make H25 pass for
# the wrong reason: it would be the only fixture whose census disagreed with its
# own `foreach.collect`, rather than the only one where the disagreement is the
# finding.
LOOP_ESCAPES = {"H25", "H27"}
for name, f, why in FIX:
    if name in LOOP_ESCAPES:
        continue
    want = BASE_LOOP - (BASE_COLLECT - f["collect"])
    assert f["loop"] == want, (name, f["loop"], want)

rows = []
for name, f, why in FIX:
    f = normalise(f)
    write(name, f, why)
    ctot, cp = c_score(f)
    if f["ok18"] == "no" and f["okdef"] == "no":
        # The scorer's broken-gate branch assigns b10=0 as an integer and never
        # reaches the awk that would print it to six places, so the fact reads
        # "0". Replicated exactly: the rehearsal compares strings.
        btot, bp = "0", dict(d=0, r=0, n=0)
    else:
        btot, bp = b10_score(f)
        btot = "%.6f" % btot
    rows.append((name, ctot, cp, btot, bp, why))

print("# the per-gate reading, for the notes and for reading this by eye")
print("# %-6s %-8s %-8s %-8s %-8s %-8s %-8s | %-8s %-8s %-8s %-8s"
      % ("fixture", "familyC", "C1", "C2", "C3", "C4", "pen",
         "B10", "diamond", "resource", "nio"))
for name, ctot, cp, btot, bp, why in rows:
    print("  %-6s %-8s %-8s %-8s %-8s %-8s %-8s | %-8s %-8f %-8f %-8f  %s"
          % (name, ctot, cp["c1"], cp["c2"], cp["c3"], cp["c4"],
             cp["pen"] if cp["pen"] is not None else "-",
             btot, bp["d"], bp["r"], bp["n"], why))
print()
print("# the TABLE heredoc for c-rehearsal.sh -- transcribe these lines by hand.")
print("# Fields: fixture familyC B10. Both are compared as strings, so the")
print("# shell's own spelling is what is written: a gate that never ran")
print("# publishes the literal 0 and one that scored zero publishes 0.000000.")
for name, ctot, cp, btot, bp, why in rows:
    print("%-8s %-8s %s" % (name, ctot, btot))
