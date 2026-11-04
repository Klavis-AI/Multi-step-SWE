#!/usr/bin/env python3
"""Build family C's rehearsal fixtures, and compute what each one must score.

    python3 authoring/fixtures-step03/mkfix.py

Writes one directory per fixture under fixtures-step03/c/, each holding the
source report the shipped family-C block will read, the two compile answers it
would have got from javac, and -- for the one fixture that needs it -- a
behaviour summary. Prints the score table that c-rehearsal.sh asserts.

Why a report and not a tree. Step 02's fixtures had to be trees, because its
gate reads compiled classes and there is no honest way to fake one. Family C
here reads a report that arqgate.SourceScan wrote from a typed AST, and the
report is a flat list of counts; a fixture can therefore state the counts
directly, and state combinations no real tree would produce -- 93 diamonds with
143 explicit sites still in place, or 15 java.nio.file references where the
tree only has 13 sites to move. Those are the interesting fixtures, and a
generator that had to produce a compilable tree for each could not write them.

What the tree-shaped fixtures cost instead: nothing here proves that
SourceScan's counts are right, only that the arithmetic over them is. The
counts were verified separately, against a tree whose answer was known -- four
controlled compiles of the pinned and golden trees at two language levels, with
the report reproducing the frozen baseline byte for byte. See NOTES 8.3.

The `want` figures are computed here, by an independent implementation of the
same composition, and then written into c-rehearsal.sh BY HAND. The rehearsal
never calls this file: a rehearsal that computes its own expectations at run
time agrees with the scorer by construction and asserts nothing.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "c")

# --- the frozen numbers, restated so the arithmetic below is readable ---------
# Read out of authoring/baseline-step03-source.txt and -target.txt rather than
# retyped; a fixture set calibrated against numbers that have drifted is a
# rehearsal that passes against the wrong question.
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

AUTH = os.path.dirname(HERE)
BASE = read_env(os.path.join(AUTH, "baseline-step03-source.txt"))
TGT = read_env(os.path.join(AUTH, "baseline-step03-target.txt"))

BASE_NONANON = int(BASE["newgeneric.explicit"]) - int(BASE["newgeneric.explicitanon"])
BASE_CLOSE = int(BASE["close.manual"])
BASE_IO = int(BASE["iofile.new"]) + int(BASE["iofile.decl"])
BASE_SUP = int(BASE["suppress.sites"])
BASE_RAW = int(BASE["newgeneric.raw"])
T_DIA = int(TGT["target.diamond"])
T_DFILES = int(TGT["target.diamondfiles"])
T_RES = int(TGT["target.try.resources"])
T_IO = int(TGT["target.iofile"])
T_NIOREF = int(TGT["target.niofile.ref"])

# C-3's arrival half divides by the number of java.io sites that move, which is
# the same number its departure half divides by -- not by T_NIOREF, the count of
# qualified NIO.2 references the golden solution happened to write for those
# sites. Round 1 of the static review found that 13-as-denominator scores the
# golden's spelling: a migration that moved all 11 sites in 11 references read
# 0.846 for complete work. T_NIOREF stays asserted at the Dockerfile layer as
# the golden's measured figure; it is no longer a threshold.
NIO_NEED = BASE_IO - T_IO

# C-4 is scaled by how broadly the tree stops being Java 6: the count of
# language-level errors javac raises at -source 1.6, over this denominator. The
# golden tree raises 55, so full credit has margin; a tree migrated in one place
# raises 1 and reads 0.05. Round 1 of the static review found the binary form
# handed the whole 0.15 to a two-edit tree.
C4_BREADTH = 20

NIOIDS = [l.strip() for l in open(os.path.join(AUTH, "baseline-step03-nioids.txt"))
          if l.strip() and not l.startswith("#")]

# --- the two measurements ----------------------------------------------------
PINNED = dict(
    files=322, suppress_sites=44,
    explicit=143, explicitanon=27, diamond=0, diamondfiles=0, raw=0,
    resources=0, resourcevars=0, close=11,
    ionew=8, iodecl=5, nioref=0, niostatic=0, niofiles=0,
    ok17="yes", ok16="yes", okdef="yes", e16=0, lvl16=0, broke_nio=False,
)
GOLDEN = dict(
    PINNED,
    explicit=50, diamond=93, diamondfiles=47,
    resources=9, resourcevars=12, close=0,
    ionew=1, iodecl=1, nioref=13, niofiles=6,
    ok16="no", e16=55, lvl16=55,
)

def variant(base, **kw):
    d = dict(base)
    d.update(kw)
    return d

# --- the fixture set ---------------------------------------------------------
# Each entry is paired with the clean control it perturbs, and each perturbation
# is a route a candidate could actually take. The comment is what the fixture is
# for, and it is the reason the fixture exists rather than a description of it.
FIX = [
    # the two ends, which make everything between them mean something
    ("pinned", PINNED, "the tree step 03 starts from"),
    ("golden", GOLDEN, "the step-03 golden solution"),

    # C-1: a real half-migration, then the two ways of faking one
    ("G1", variant(GOLDEN, explicit=97, diamond=46, diamondfiles=24,
                   resources=5, resourcevars=6, close=6,
                   ionew=4, iodecl=3, nioref=7, niofiles=3),
     "about half of each of the three constructs"),
    ("G2", variant(GOLDEN, diamond=0, diamondfiles=0),
     "the explicit type arguments deleted, no diamond written"),
    ("G3", variant(GOLDEN, explicit=143),
     "93 new diamond sites written alongside the 116 untouched ones"),
    ("G13", variant(GOLDEN, diamond=0, diamondfiles=0, raw=93),
     "the type arguments dropped to raw types instead of to diamonds"),

    # C-2 and C-3: the departure half earned by deletion
    ("G4", variant(GOLDEN, resources=0, resourcevars=0),
     "the 11 close() calls deleted, no resource clause written"),
    ("G5", variant(GOLDEN, nioref=0, niofiles=0),
     "the java.io.File uses deleted, no java.nio.file reference written"),

    # C-3's proviso: the two sites the migration is required to leave alone
    ("G6", variant(GOLDEN, ionew=0, iodecl=0, nioref=15, niofiles=7,
                   broke_nio=True),
     "every java.io.File site migrated, including the two that change behaviour"),

    # the anti-gaming multiplier, each half on its own
    ("G7", variant(GOLDEN, raw=5), "five raw types added over the baseline's zero"),
    ("G8", variant(GOLDEN, suppress_sites=50), "six @SuppressWarnings sites added"),

    # C-4: the three ways the level channel says no, which share a total
    ("G9", variant(GOLDEN, ok16="yes", e16=0, lvl16=0),
     "the constructs are all there and the tree still compiles at 1.6"),
    ("G10", variant(GOLDEN, e16=7, lvl16=3),
     "the 1.6 compile fails, but not for language-level reasons"),
    ("G11", variant(GOLDEN, ok17="no"),
     "the tree does not compile at 1.7; graded at the default level instead"),
    ("G12", variant(GOLDEN, ok17="no", okdef="no"),
     "no compile of the tree produced a report at all"),

    # C-4's breadth: the flip bought with the smallest edit that buys it
    ("G14", variant(PINNED, ok16="no", e16=1, lvl16=1),
     "the 1.6 flip bought with one construct and nothing else migrated"),

    # C-3's arrival half, in the spellings a correct migration can use
    ("G15", variant(GOLDEN, nioref=8, niostatic=5),
     "eight qualified references and five more through a static import"),
    ("G16", variant(GOLDEN, nioref=8),
     "the same eight, with nothing making up the three that are missing"),
    ("G17", variant(GOLDEN, nioref=11),
     "all 11 sites moved, reached in 11 references rather than the golden's 13"),
]

# --- an independent implementation of the shipped composition ----------------
# Deliberately a reimplementation and not a call into the shell: the point of
# the rehearsal is that two implementations of the same specification agree.
# The rounding is replicated exactly, because the shell rounds to six decimals
# at every step and not only at the end -- frac() and clamp01() and minf() each
# print %.6f -- and a check that rounds once would disagree with the scorer in
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

def score(f):
    # which report the block ends up measuring, and what C-4 can be
    if f["ok17"] == "yes":
        measured, c4_possible = True, (f["ok16"] == "no")
    elif f["okdef"] == "yes":
        measured, c4_possible = True, False
    else:
        return 0.0, dict(c1=0, c2=0, c3=0, c4=0, pen=1.0)

    if not measured:
        return 0.0, dict(c1=0, c2=0, c3=0, c4=0, pen=1.0)

    nonanon = max(0, f["explicit"] - f["explicitanon"])
    conv = max(0, BASE_NONANON - nonanon)
    m = minf(frac(conv, T_DIA), frac(f["diamond"], T_DIA))
    breadth = clamp01(frac(f["diamondfiles"], T_DFILES))
    c1 = f6(min(1.0, m * (0.75 + 0.25 * breadth)))

    c2a = clamp01(f6(0.0 if BASE_CLOSE <= 0 else (BASE_CLOSE - f["close"]) / float(BASE_CLOSE)))
    c2 = minf(c2a, clamp01(frac(f["resources"], T_RES)))

    d = BASE_IO - T_IO
    now_io = f["ionew"] + f["iodecl"]
    c3a = clamp01(f6(0.0 if d <= 0 else (BASE_IO - now_io) / float(d)))
    c3 = minf(c3a, clamp01(frac(f["nioref"] + f["niostatic"], NIO_NEED)))
    if f["broke_nio"]:
        c3 = f6(c3 / 2)

    if c4_possible and f["lvl16"] > 0 and f["lvl16"] == f["e16"]:
        c4 = clamp01(frac(f["lvl16"], C4_BREADTH))
    else:
        c4 = 0

    pen = 1.0
    raw_added = max(0, f["raw"] - BASE_RAW)
    if raw_added > 0:
        pen = f6(max(0.0, 1 - raw_added / 10.0))
    if f["suppress_sites"] > BASE_SUP:
        pen = f6(pen / 2)

    total = f6(pen * (0.35 * c1 + 0.25 * c2 + 0.25 * c3 + 0.15 * c4))
    return total, dict(c1=c1, c2=c2, c3=c3, c4=c4, pen=pen)

# --- write them out ----------------------------------------------------------
REPORT_KEYS = [
    ("files", "files"),
    ("suppress.sites", "suppress_sites"),
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

def write(name, f, why):
    d = os.path.join(OUT, name)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(os.path.join(d, "why.txt"), "w") as fh:
        fh.write(why + "\n")
    with open(os.path.join(d, "report.txt"), "w") as fh:
        for key, attr in REPORT_KEYS:
            fh.write("%s=%s\n" % (key, f[attr]))
    with open(os.path.join(d, "answers.txt"), "w") as fh:
        for k in ("ok17", "ok16", "okdef"):
            fh.write("%s=%s\n" % (k, f[k]))
        fh.write("errors=%d\n" % f["e16"])
        fh.write("levelerrors=%d\n" % f["lvl16"])
    # The 1.6 javac log the block greps for level errors. Written with the
    # exact diagnostic javac emits, because that string is what the block
    # matches and a paraphrase would pass a gate the real compiler fails.
    with open(os.path.join(d, "javac16.log"), "w") as fh:
        for i in range(f["lvl16"]):
            fh.write("/app/arquillian/x/Y%d.java:1: error: diamond operator "
                     "is not supported in -source 1.6\n" % i)
        for i in range(f["e16"] - f["lvl16"]):
            fh.write("/app/arquillian/x/Z%d.java:1: error: cannot find symbol\n" % i)
    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        if f["broke_nio"]:
            for i, nid in enumerate(NIOIDS):
                fh.write("behavior.differ[%d]=%s\n" % (i, nid))

rows = []
for name, f, why in FIX:
    write(name, f, why)
    total, parts = score(f)
    rows.append((name, total, parts, why))

w = max(len(r[0]) for r in rows)
print("# %-*s  want      C1        C2        C3        C4  pen" % (w, "fixture"))
for name, total, p, why in rows:
    print("%-*s  %.6f  %-8s  %-8s  %-8s  %s   %-8s  %s"
          % (w, name, total, p["c1"], p["c2"], p["c3"], p["c4"], p["pen"], why))
print()
print("# the table c-rehearsal.sh asserts:")
for name, total, p, why in rows:
    print("%-8s %.6f" % (name, total))
