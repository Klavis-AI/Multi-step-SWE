#!/usr/bin/env python3
"""Build step 07's rehearsal fixtures for family C, B-10, B-11, B-12 and B-13,
and compute what each one must score.

    python3 authoring/fixtures-step07/mkfix07.py

Writes one directory per fixture under fixtures-step07/c/, each holding the
source report the shipped scoring blocks will read, the compile answers they
would have got from javac, a behaviour summary, the `jars/` directory B-12e
unzips, and the `scan.txt` C-1 reads. Prints the score table that
c-rehearsal.sh asserts.

SIX blocks, not five. Step 06 rehearsed the source-AST block, B-10, B-11, B-12
and family C; this step adds B-13, which holds step 06's own seven gates in
place and reads the same report. All six are extracted from the shipped test.sh
and all six are scored here.

THE TWO ENDS ARE MEASURED REPORTS AND NOT SYNTHESISED ONES, and this is the
architectural change from step 06's generator. That one built every fixture's
records from a feature dict -- the arrival side of each gate had to be
synthesised, because at authoring time the migrated tree did not exist. At this
rung BOTH ends exist and both have been compiled by the shipped source gate:

    pinned   baseline-step07-source.txt, the input tree's own measured report,
             which is also what the verifier stages as baseline/source.txt
    golden   baseline-step07-golden.txt, the golden tree's own measured report,
             /work/scratch/out07/src-golden.txt as the gate wrote it

Every fixture below is one of those two with an EXPLICIT, NAMED mutation applied
-- a record moved, a counter bumped, a bucket reverted. Nothing is invented from
whole cloth. That buys two things step 06's generator could not have:

  * a fixture claiming "one of the six buckets did not move" restores that
    bucket's REAL javax records, at their real line numbers, with the real
    qualified names, because it takes them out of the report the unmigrated
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

S08 = os.path.join(AUTH, "baseline-step08-source.txt")
G08 = os.path.join(AUTH, "baseline-step08-golden.txt")


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
BASE = Rep(S08)
GOLD = Rep(G08)
TGT = read_env(os.path.join(AUTH, "baseline-step08-target.txt"))
LEVEL = read_env(os.path.join(AUTH, "baseline-step08-level.txt"))


def T(k):
    return int(TGT[k])


# WANT_MAJOR is still read, and B-4 is the only gate that spends it. There is
# no major 56 at this rung: the module system arrived in Java 9 and this tree
# has compiled at 55 since the rung below, so the level is a HELD gate and the
# first leg of family C is a question about manifests instead. It is read here
# rather than dropped because the rehearsal asserts that the SHIPPED file still
# derives it from baseline/level.txt and never from a literal.
WANT_MAJOR = int(LEVEL["major"])                    # 55, held
BASE_SUP = BASE.num("suppress.sites")               # 48
BASE_RAW = BASE.num("newgeneric.raw")               # 0
CLASSES = 757                                       # what arqgate.Scan counted

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
# Family C's own, this step's.
C2_BUCKETS, C2_REFS = T("target.c2.buckets"), T("target.c2.refs")
C3_BUCKETS, C3_REFS = T("target.c3.buckets"), T("target.c3.refs")
C4_BUCKETS, C4_SIZED = T("target.c4.buckets"), T("target.c4.sized")
C5_FILE, C5_LEGACY = TGT["target.c5.file"], T("target.c5.legacy")
C5_TYPES, C5_STUB = T("target.c5.types"), T("target.c5.cdistub")
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

# This rung's own three population files. nsmove and nshold are keyed
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

# --- and family C's own three, which are this rung's and nobody else's --------
#
# MODNAMES is read as an ORDERED LIST OF PAIRS and compared pairwise, because
# that is what the shell does: two sorted `<artifact> <name>` files and a
# `comm -12` between them. A set of names on its own would score a tree that
# gave arquillian-api the spi's name and the spi the api's as perfect.
MODNAMES = [(l.split()[0], l.split()[1]) for l in
            read_set(os.path.join(AUTH, "baseline-step08-modnames.txt"))]
# SVC is one file with a tag column, for the reason step 06 gave when it put
# accessible.txt and newinstance.txt into that shape: keeping the three
# populations in one file is what makes it impossible to stage half of a pair.
_SVC = [l.split(None, 1) for l in
        read_set(os.path.join(AUTH, "baseline-step08-svc.txt"))]
SVC_LEGACY = [b for t, b in _SVC if t == "legacy"]
SVC_ARRIVE = [b for t, b in _SVC if t == "arrive"]
SVC_KEEP = [b for t, b in _SVC if t == "keep"]
SVCIDS = [l for l in read_set(os.path.join(AUTH, "baseline-step08-svcids.txt"))
          if l and not l.startswith("#")]

MOD_TOTAL = T("target.mod.total")                   # 12
MOD_DERIV = T("target.mod.derivable")               # 11, and see below
T_SVC_LEGACY = T("target.svc.legacy")               # 2
T_SVC_ARRIVE = T("target.svc.arrive")               # 2
T_SVC_KEEP = T("target.svc.keep")                   # 11

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
# Every nsmove package really is one of the two this rung moves, and no nshold
# package is. A population file that named javax.enterprise on the move side
# would ask for the CDI migration this step explicitly defers.
assert set(p for p, _ in NSMOVE) == set(["javax.annotation", "javax.ejb"])
assert not (set(NSHOLD_PKGS) & set(["javax.annotation", "javax.ejb"]))

# Family C's three, the same way. The two that must move are the two that must
# arrive -- ONE pair of methods stated twice under two tags -- and neither of
# them may also be in the hold, which would ask the candidate to delete a
# method and keep it at once.
assert len(MODNAMES) == MOD_TOTAL
assert len(set(a for a, _ in MODNAMES)) == MOD_TOTAL, "an artifact named twice"
assert len(set(n for _, n in MODNAMES)) == MOD_TOTAL, "a module name claimed twice"
assert len(SVC_LEGACY) == T_SVC_LEGACY and len(SVC_ARRIVE) == T_SVC_ARRIVE
assert len(SVC_KEEP) == T_SVC_KEEP
assert set(SVC_LEGACY) == set(SVC_ARRIVE), "the departure and arrival sets differ"
assert not (set(SVC_KEEP) & set(SVC_LEGACY)), "a bucket is both held and moved"
assert len(SVCIDS) == 69 and len(set(SVCIDS)) == 69
# MOD_DERIV is ELEVEN and not twelve, and the arithmetic below depends on
# knowing why: arquillian-impl-base ships a META-INF/services file whose lines
# are `implClass=` properties rather than binary class names, which
# ModuleFinder.deriveModuleDescriptor refuses, and the file belongs to
# shrinkwrap-descriptors-api's format rather than to Arquillian. C-1 folds the
# readable count in as a min() term against this eleven, so the input tree pays
# nothing for the state it is handed and a candidate who breaks a TWELFTH jar
# pays for that.
assert MOD_DERIV == MOD_TOTAL - 1


class Mod(object):
    """One published artifact, as the jar writer will lay it down and as
    arqgate.Modules will read it back.

    Three independent properties, because the gate asks three questions and a
    tree can answer one and not the next:

      artifact  the coordinate the jar file name carries, which is what C-2
                keys its pairwise comparison on. The file is written as
                <artifact>-1.0.0-SNAPSHOT.jar because Modules.artifactOf cuts
                at exactly that string.
      name      the Automatic-Module-Name in the manifest, or "" for a jar that
                declares nothing. This is what mod.declared counts.
      poison    an illegal META-INF/services entry -- a line that is not the
                binary name of a class this jar carries. ModuleFinder refuses
                the whole artifact over it, which is what mod.derivable counts,
                AND IT REFUSES EVEN WHEN THE MANIFEST NAMES THE MODULE. That
                last clause is the one worth a fixture: it is the state the
                input tree's arquillian-impl-base is in, it is why this rung's
                C-1 divides its readable half by eleven, and a reader who
                assumed the manifest settles the question would get it wrong.
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


# The golden's twelve, which is also the shape the rehearsal's `golden` fixture
# publishes: every artifact declaring the name baseline/modnames.txt asks for,
# and arquillian-impl-base carrying the descriptor service file that the
# platform will not read a module descriptor through.
POISONED = "arquillian-impl-base"


def goldmods():
    return [Mod(a, n, poison=(a == POISONED)) for a, n in MODNAMES]


def seedmods():
    """The input tree's twelve: the same jars, published by a build that has
    never heard of Automatic-Module-Name. declared is 0, derivable is still 11
    -- nothing about the descriptor file changed -- and every name the platform
    hands back is derived from the file name, which matches no row of
    baseline/modnames.txt because every row is a dotted org.jboss. prefix and
    every derived name is `arquillian.something`."""
    return [Mod(a, "", poison=(a == POISONED)) for a, _ in MODNAMES]



GOLDMODS = goldmods()
SEEDMODS = seedmods()


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


def fc_score(rep, mods, measured, execbroken=()):
    """Family C: the module graph and the platform's service lookup.

    `mods` is the fixture's jar plan -- the list write_jars() was handed -- or
    None for a tree that published nothing. It is the SAME object the jars were
    written from, so what is predicted here is the plan and what the rehearsal
    measures is java.lang.module's reading of the bytes that plan produced. The
    two agreeing is the assertion; a ModuleFinder that stopped rejecting an
    illegal META-INF/services entry would show up as a failed row rather than
    as a silently weaker gate.

    Returns (familyC, [C1, C2, C3], gate facts).

    C-1 AND C-2 SIT OUTSIDE THE MEASURED GUARD and C-3's three halves sit
    inside it. That is the inversion this rung introduces and it is not an
    oversight: the first two legs read the PUBLISHED JARS, and a tree whose
    sources the scorer could not compile against its own classpath may
    perfectly well have published twelve correctly named artifacts. C-3 reads
    the source AST, and `gone()` over an empty bucket file reads as "both
    departed" -- the seventh trap -- so it needs the guard and says so.
    """
    g = {}
    if mods is None:
        total = declared = derivable = 0
        got = set()
        g["mod"] = "no-module-jars"
    else:
        total = len(mods)
        declared = len([m for m in mods if m.name])
        derivable = len([m for m in mods if not m.poison])
        # `source=manifest` wins over the derived name whenever the manifest
        # carries one, exactly as arqgate.Modules writes it; a jar with neither
        # publishes `name=-`, which matches no baseline row.
        got = set()
        for m in mods:
            got.add("%s %s" % (m.artifact, m.name if m.name
                               else (m.derived if not m.poison else "-")))
    g["mod.total"] = total
    g["mod.declared"] = declared
    g["mod.derivable"] = derivable

    # THE PAIRWISE NAME COUNT IS READ ABOVE C-1 and not inside C-2, because from
    # round 1 of this step's review both of the first two legs use it: C-1 as a
    # third min() term and C-2 as its fraction. test.sh hoists the same block
    # above its own C-1 banner, for the same reason and in the same order.
    want = set("%s %s" % (a, n) for a, n in MODNAMES)
    names = 0 if mods is None else len(want & got)
    g["c2.names"] = names

    # C-1 is min(declared/12, derivable/11, right/12). The third term is what
    # makes a name that is WRONG cost something in C-1 as well as in C-2: with
    # only the first two, twelve arbitrary legal names read C-1 = 1.000000 and
    # took the whole 0.20 -- reward 0.571016 against the rung's 0.466016 floor.
    c1decl = clamp01(frac(declared, MOD_TOTAL))
    c1read = clamp01(frac(derivable, MOD_DERIV))
    c1right = clamp01(frac(names, MOD_TOTAL))
    g["c1.declared"] = "%.6f" % c1decl
    g["c1.readable"] = "%.6f" % c1read
    g["c1.right"] = "%.6f" % c1right
    c1 = minf(minf(c1decl, c1read), c1right)

    c2 = f6(c1right * c1)

    if measured:
        legacy = fbuckets(rep, "svcload", "form=legacyscan") \
            | fbuckets(rep, "svcload", "form=loadclass")
        arrive = fbuckets(rep, "svcload", "form=platformload")
        c3dep = clamp01(frac(len([x for x in SVC_LEGACY if x not in legacy]),
                             T_SVC_LEGACY))
        c3arr = clamp01(frac(len([x for x in SVC_ARRIVE if x in arrive]),
                             T_SVC_ARRIVE))
        c3keep = clamp01(frac(len([x for x in SVC_KEEP if x in legacy]),
                              T_SVC_KEEP))
        g["c3.departed"] = "%.6f" % c3dep
        g["c3.arrived"] = "%.6f" % c3arr
        g["c3.held"] = "%.6f" % c3keep
    else:
        # THE SECOND SPELLING OF ZERO, modelled rather than normalised away.
        # test.sh initialises `c3dep=0; c3arr=0; c3keep=0` above the guard and
        # the else branch only echoes, so a tree nobody could compile publishes
        # the shell LITERAL `0` here and not the `%.6f` a clamp01 would return.
        # Predicting "0.000000" would have made the rehearsal's transcription
        # disagree with the scorer on K17 and on nothing else, which is the
        # hardest kind of disagreement to read.
        c3dep = c3arr = c3keep = 0.0
        g["c3.departed"] = g["c3.arrived"] = g["c3.held"] = "0"

    # And scaled by the 69 recorded behaviours that go through a service
    # lookup. `execbroken is None` is the unreadable summary and takes the
    # whole leg, for B-10's reason: a proviso that cannot be checked is not
    # satisfied.
    if execbroken is None:
        c3exec, broken = 0.0, len(SVCIDS)
    else:
        broken = len(execbroken)
        c3exec = clamp01(frac(len(SVCIDS) - broken, len(SVCIDS)))
    g["c3.exec.want"] = len(SVCIDS)
    g["c3.exec.broken"] = broken
    g["c3.exec"] = "%.6f" % c3exec
    c3 = f6(min(c3dep, c3arr, c3keep) * c3exec)

    tot = f6(0.20 * c1 + 0.35 * c2 + 0.45 * c3)
    return "%.6f" % tot, ["%.6f" % c1, "%.6f" % c2, "%.6f" % c3], g


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


# --- the catalogue ------------------------------------------------------------
# Eighteen fixtures and the two ends. Each names the ONE thing it moves, and the
# table at the foot of this file is what c-rehearsal.sh asserts. Read the `why`
# text as the fixture's claim and the row as what the shipped blocks must say
# about it.
#
# TWO POPULATIONS OF FIXTURE AND ONE TABLE. K1..K3 and M1..M6 move the
# PUBLISHED JARS and the service-lookup records -- this rung's family C. K4..K18
# move the source report's namespace, toArray and HttpClient records -- step
# 07's family C, inherited whole as B-14. They share a table because they share
# a run: every fixture is scored by all seven blocks, and the column that must
# NOT move is as much of the assertion as the one that must. A B-14 that
# noticed a module name, or a family C that noticed a jakarta import, would
# show up here as a moved cell in a column the fixture never touched.
#
# THREE FIXTURES SHARE A familyC OF 0.000000 AND ARE NOT REDUNDANT (K2, M5, M6
# and the pinned end). They differ in the gate fact that says WHY -- nothing
# declared, the lookup never moved, the behaviour gone, the summary unreadable
# -- and a block that read one channel wrongly would collapse them onto one
# message while keeping the score. The rehearsal asserts the messages as well
# as the numbers, which is the only way that particular defect is visible from
# outside.
CAT = []


def fixture(name, base, why, **env):
    def deco(fn):
        CAT.append((name, base, why, fn, env))
        return fn
    return deco


@fixture("K1", "golden", "eleven artifacts declare the name the packages "
         "imply and the twelfth declares nothing -- a build file that listed "
         "the modules by hand and missed one. C-1 is 11/12 on its declared "
         "half and 1 on its readable half, and the min() takes the smaller; "
         "C-2 is 11/12 scaled by that C-1, which is the compounding step 05's "
         "R5 finding asked for and NOT 11/12 flat.",
         mods=[Mod(a, "" if a == "arquillian-testng" else n,
                   poison=(a == POISONED)) for a, n in MODNAMES])
def _k1(rep, env):
    return "arquillian-testng publishes no Automatic-Module-Name"


@fixture("K2", "golden", "the input tree's own jars under the golden's source "
         "report: nothing declares a module name at all. C-1 and C-2 are both "
         "0.000000 and mod.derivable is STILL 11, which is the fact that says "
         "the descriptor file was never the candidate's doing. Family C keeps "
         "only C-3, and B-14 is untouched -- the two families read two "
         "artefacts.",
         mods=SEEDMODS)
def _k2(rep, env):
    return "twelve jars, no Automatic-Module-Name in any of them"


@fixture("K3", "golden", "every artifact declares a name and two of them are "
         "the names the FILE NAME implies rather than the ones the packages "
         "do: arquillian.impl.base for a project whose sources are all under "
         "org.jboss.arquillian.impl, and arquillian.protocol.jmx.osgi for one "
         "whose sources are all under org.jboss.arquillian.packager.osgi. C-1 "
         "is a clean 1.000000 -- twelve declared, eleven readable -- and C-2 "
         "is 10/12. This is the exact failure an agent that derived the names "
         "from the artifact ids produces, and the two it gets wrong are the "
         "two whose name would be wrong forever once published.",
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
         "javax.enterprise. C-3 loses a bucket and the references it held.")
def _k7(rep, env):
    pkg, bucket = NSHOLD[0]
    n = drop_ns(rep, lambda b: b.split()[1][4:] == pkg and bucket_of(b) == bucket)
    return "%s dropped, %d reference(s)" % (pkg, n)


@fixture("K8", "golden", "javax.management was migrated to jakarta.management "
         "-- the broad package rename this step's C-3 exists to price. The tree "
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


@fixture("K9", "golden", "family C's toArray leg fully reverted: all eight "
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
         "added over the baseline's zero. The first penalty term takes family C "
         "to 0.8 of what it earned.")
def _k14(rep, env):
    rep.set("newgeneric.raw", BASE_RAW + 2)
    return "newgeneric.raw=%d" % rep.num("newgeneric.raw")


@fixture("K15", "golden", "one @SuppressWarnings site more than the baseline's "
         "48 -- a warning silenced rather than fixed. The second penalty term "
         "halves family C.")
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
         "literal 0 their blocks assign; B-12e, B-12, B-13, B-14 and familyC "
         "publish 0.000000, because their totals are computed by awk outside "
         "the guard; and B-13.2..7 and B-14.2..5 publish NOTHING AT ALL, "
         "because their fact lines are inside it. "
         "FAMILY C IS THE ROW THAT MAKES THIS FIXTURE WORTH MORE THAN IT WAS "
         "AT STEP 07. C-1 and C-2 are FULL -- 1.000000 each -- because they "
         "read the jars the build published and this build published twelve "
         "correctly named ones; only C-3 falls, and it falls to 0 because "
         "`gone()` over a bucket file that does not exist would otherwise read "
         "as 'both departed' and pay a tree that deleted the lookup. Family C "
         "is 0.20 + 0.35 = 0.550000, and a rung whose family C sat entirely "
         "inside the guard could not produce that row.",
         ok11="no", ok10="no", okdef="no")
def _k17(rep, env):
    return "no report; the twelve published jars intact"


@fixture("K18", "golden", "step 06's Class.newInstance work is undone: both "
         "frozen buckets call the legacy form again. B-13's first leg falls and "
         "family C is untouched, which is the separation the ninth trap asks "
         "for.")
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



# THE TWO ARTIFACT CHANNELS ARE NOW ONE FILE (K19, K20). B-12e unzips the jars
# the build published; family C's first two legs hand the same list to
# java.lang.module. At step 07 these were two files and three fixtures existed
# to keep them apart. Here there is one -- ${WORK}/modulejars.txt -- so the
# pair below says the other half of that: K19 publishes twelve perfectly named
# jars that carry none of the four Java 8 package names, and K20 publishes
# nothing at all. K19 is the separation that survives (B-12e falls, family C
# does not); K20 is the absence that no longer separates, and the `why` says so
# rather than pretending otherwise.
@fixture("K19", "golden", "the build published its twelve named jars and not "
         "one class in them names java.util.Optional, "
         "java.util.stream.Collectors, java.util.Objects or java.time. "
         "B-12e's four legs read zero against a source tree whose four AST "
         "legs are full -- the module that was dropped from the build, which "
         "is the failure B-12e was written for. The manifests are untouched, "
         "so family C is unmoved: one jar set, two readings, and only one of "
         "them falls.",
         jars=(0, 0, 0, 0))
def _k19(rep, env):
    return "nothing in the report; the jars carry no Java 8 library names"


@fixture("K20", "golden", "the build published nothing. ONE ABSENCE WITH TWO "
         "CONSEQUENCES, and that is the honest shape at this rung: B-12e is "
         "the literal 0 under gate.cp=no-jars and family C reads "
         "gate.mod=no-module-jars with C-1 and C-2 both 0.000000. C-3 is "
         "untouched -- it reads the source AST, which is intact -- so family C "
         "is 0.45 exactly and the fixture proves the three legs are three.",
         jars=None, mods=None)
def _k20(rep, env):
    return "nothing in the report; the build published no jars"


# --- family C's own six -------------------------------------------------------
# C-3 is 0.45 of family C and it is the leg no build file can reach. Six
# fixtures, one per way of getting it wrong, and every one of them leaves the
# module channel at the golden so that the row's C-1 and C-2 stay at 1.000000
# and the reader can see which leg moved.
@fixture("M1", "golden", "a thirteenth malformed provider file: the candidate "
         "moved the DynamicServiceLoader work and, on the way, wrote a "
         "META-INF/services entry into arquillian-spi whose line is not the "
         "binary name of a class the jar carries. mod.declared is still 12 and "
         "mod.derivable falls to 10 of the 11 that were readable. C-1 is the "
         "min() of 1.000000 and 10/11 -- 0.909091 -- and C-2 falls WITH it, "
         "because a name is worth what the naming behind it earned.",
         mods=[Mod(a, n, poison=(a in (POISONED, "arquillian-spi")))
               for a, n in MODNAMES])
def _m1(rep, env):
    return "arquillian-spi carries an illegal META-INF/services entry"


@fixture("M2", "golden", "half the service migration: DynamicServiceLoader#load "
         "calls java.util.ServiceLoader.load and spi ServiceLoader#reload still "
         "reads META-INF/services off the class loader by hand. The departure "
         "half is 1/2 and the arrival half is 1/2, and C-3 is the min of those "
         "and the hold -- half the leg, not three quarters of it.")
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
         "and the arrival half is 0, so C-3 is 0 and the tree has deleted its "
         "service lookup rather than migrated it. form=platformtype is not "
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
         "and C-3 is 0 -- a tree that rewrote the lookup into something that "
         "does not resolve a provider. Family C keeps C-1 and C-2 and nothing "
         "else, and B-14 is untouched because its own eleven ids are a "
         "different population.",
         svcbroken="all")
def _m5(rep, env):
    return "nothing in the report; all 69 recorded behaviours differ"


@fixture("M6", "golden", "nine of the 69 differ -- the partial regression a "
         "candidate who moved the lookup but lost the package-private provider "
         "produces. C-3 is scaled by 60/69 and every source half stays full, "
         "which is the shape that tells a behaviour regression from a source "
         "one.",
         svcbroken=9)
def _m6(rep, env):
    return "nothing in the report; 9 of 69 recorded behaviours differ"


# --- writing a fixture out ----------------------------------------------------
CLASSES_PER_FIXTURE = 24
B12_MARKERS = ("java/util/Optional", "java/util/stream/Collectors",
               "java/util/Objects", "java/time/Instant")


CLASSES_PER_JAR = 2


def write_jars(d, counts, mods):
    """The published artifacts: real jars, read by two blocks on two channels.

    B-12e unzips them and binary-greps their class files for four package
    names -- step 05's must-flip, held at this rung as one leg of B-12.
    Family C's first two legs hand the same list to java.lang.module and ask
    what each one calls itself.

    THE TWO CHANNELS NOW SHARE AN ARTEFACT, which is a change from step 07 and
    is worth stating rather than discovering. At that rung C-1 read
    ${WORK}/scan.txt and B-12e read the jars, so a fixture could carry one and
    not the other and three of them did. Here both read ${WORK}/modulejars.txt,
    so `mods=None` is ONE absence with two consequences -- gate.cp=no-jars for
    B-12e and gate.mod=no-module-jars for family C -- and there is no fixture
    that can separate them, because no tree can.

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
    """The fixture's C-3 behaviour breakage, in the three spellings the
    catalogue uses: `"all"` for every id, an int for the first n, and the
    default empty tuple for a tree whose recorded behaviour is intact."""
    v = env.get("svcbroken", ())
    if v == "all":
        return list(SVCIDS)
    if isinstance(v, int):
        return list(SVCIDS[:v])
    return list(v)


def write_fixture(name, rep, why, env, note):
    d = os.path.join(OUT, name)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    rep.write(os.path.join(d, "report.txt"))
    with open(os.path.join(d, "answers.txt"), "w") as fh:
        for tag in ("11", "10", "def"):
            fh.write("ok%s=%s\n" % (tag, env.get("ok" + tag, "yes")))
        fh.write("errors=0\n")
    with open(os.path.join(d, "behavior.summary"), "w") as fh:
        fh.write("behavior.build=ok\n")
        # C-3's run-time scaling, for the two fixtures that move it. Written
        # into the FIXTURE rather than doctored by the rehearsal the way
        # B-14.5's BEHAV* runs are, because these two are catalogue rows with a
        # report of their own: M5 and M6 are trees whose source shape is
        # perfect and whose behaviour is not, and that pairing is the finding.
        for i, sid in enumerate(svcbroken_of(env)):
            fh.write("behavior.differ[%d]=%s\n" % (i, sid))
    with open(os.path.join(d, "why.txt"), "w") as fh:
        fh.write(why + "\n")
        fh.write("\nwhat this fixture moves: " + note + "\n")
    write_jars(d, env.get("jars", B12_FLOORS), env.get("mods", GOLDMODS))
    return d


_UNSET = object()


def score(rep, env, execbroken=(), svcbroken=_UNSET):
    """Every gate the seven lifted blocks publish, for one fixture.

    `execbroken` is B-14.5's list -- step 07's eleven http exchange ids -- and
    `svcbroken` is family C's, the 69 service-lookup ids. They are separate
    arguments and not one, because they are separate populations scaled into
    separate families and a rehearsal that moved them together could not tell
    a B-14 regression from a C-3 one.
    """
    measured = env.get("ok11", "yes") == "yes" or env.get("okdef", "yes") == "yes"
    jars = env.get("jars", B12_FLOORS)
    mods = env.get("mods", GOLDMODS)
    if measured:
        b10, _ = b10_score(rep)
        b11, _ = b11_score(rep)
        b10s, b11s = "%.6f" % b10, "%.6f" % b11
    else:
        b10s = b11s = "0"
    b12s, b12p = b12_score(rep, None if mods is None else jars, measured)
    b13s, b13p = b13_score(rep, measured)
    b14s, b14p, gb = b14_score(rep, measured, execbroken)
    # A SENTINEL AND NOT A FALSY DEFAULT. `None` is C-3's unreadable-summary
    # case and an empty list is its intact case, and `svcbroken or ...` reads
    # both as "the caller said nothing" -- which silently turned the SVCGONE
    # row into a copy of the golden the first time this was written.
    cs, cp, gc = fc_score(rep, mods, measured,
                          svcbroken_of(env) if svcbroken is _UNSET else svcbroken)
    g = dict(gb)
    g.update(gc)
    return dict(b10=b10s, b11=b11s, b12=b12s, b12p=b12p, b13=b13s, b13p=b13p,
                b14=b14s, b14p=b14p, c=cs, cp=cp, gates=g)


def check_pinned():
    """The pinned fixture is the input tree's own report, byte for byte.

    Not a resemblance check. `pinned` is written by copying every pair out of
    baseline-step08-source.txt and writing them back, so if Rep's round trip
    ever stopped being the identity -- a dropped blank line, a renumbered
    record, a key reordered -- this catches it here rather than three fixtures
    downstream where the number would look like a finding.
    """
    p = os.path.join(OUT, "pinned", "report.txt")
    with open(p) as a, open(S08) as b:
        got, want = a.read(), b.read()
    assert got == want, "pinned/report.txt is not baseline-step08-source.txt"
    g = os.path.join(OUT, "golden", "report.txt")
    with open(g) as a, open(G08) as b:
        assert a.read() == b.read(), "golden/report.txt is not the golden's report"


# --- generate -----------------------------------------------------------------
def main():
    # Wipe and rebuild. The generator is the only author of this directory, so
    # a fixture that used to be in the catalogue and no longer is has to leave
    # the tree with it: c-rehearsal.sh iterates the DIRECTORY, not the table
    # below, and a step-07 fixture left behind here would be scored by step
    # 08's blocks against step 07's report and read as a finding.
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    rows = []

    for name, src, why, env in (
            ("pinned", BASE, "the input tree's own measured report and the jars "
             "its build publishes, counter for counter and record for record. "
             "Family C must read 0.000000 on it and family B must read "
             "1.000000: that is migration_prompt item 6's nop run, asserted at "
             "image build time.", {"mods": SEEDMODS}),
            ("golden", GOLD, "the golden tree's own measured report and its "
             "twelve named jars. Every family-C leg full, every family-B gate "
             "held: item 6's oracle run.", {})):
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
    print("%-8s %-8s %-8s %-8s %-8s %-8s | %-8s %-8s %-8s | %-8s" %
          ("fixture", "B10", "B11", "B12", "B13", "B14",
           "C1", "C2", "C3", "familyC"))
    print("-" * 100)
    for name, st, note in rows:
        c = [cell(x) for x in st["cp"]]
        print("%-8s %-8s %-8s %-8s %-8s %-8s | %-8s %-8s %-8s | %-8s" %
              (name, st["b10"], st["b11"], st["b12"], st["b13"], st["b14"],
               c[0], c[1], c[2], st["c"]))
    print()
    print("B-12 parts (a b c d e), B-13 parts (2..7) and B-14 parts (2..5):")
    for name, st, note in rows:
        print("  %-8s b12: %s" % (name, " ".join(cell(x) for x in st["b12p"])))
        print("  %-8s b13: %s" % ("", " ".join(cell(x) for x in st["b13p"])))
        print("  %-8s b14: %s" % ("", " ".join(cell(x) for x in st["b14p"])))

    # --- the BEHAV* runs, which are not source-tree fixtures
    #
    # c-rehearsal.sh builds these by copying golden/ and appending to nothing
    # but behavior.summary, so the report is the golden's and only the run-time
    # factors move. Their `want` figures are computed here for the same reason
    # every other want is: so the rehearsal asserts a number this file worked
    # out rather than one the scorer handed it.
    #
    # TWO SETS OF THEM AT THIS RUNG, because there are two run-time scalings in
    # two different families: B-14.5's eleven http ids, inherited, and family
    # C-3's 69 service-lookup ids, new. A tree can break either without
    # touching the other, and the pair of tables is what says so.
    print()
    print("BEHAV* (golden's report, doctored behavior.summary) -- B-14.5's ids:")
    print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s" %
          ("run", "exec.want", "broken", "red.hon", "B14.5", "B14", "familyC"))
    for name, broken in (
            ("BEHAVALL", C5_HTTPIDS),
            ("BEHAVHALF", C5_HTTPIDS[:9]),
            ("BEHAVRED", [C5_REDID]),
            ("BEHAVONE", [i for i in C5_HTTPIDS if i != C5_REDID][:1])):
        st = score(GOLD.clone(), {}, broken)
        g = st["gates"]
        print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s" %
              (name, g["b14.c5.exec.want"], g["b14.c5.exec.broken"],
               g["b14.c5.redirects.honoured"], cell(st["b14p"][3]), st["b14"],
               st["c"]))
        print("           ids: %s" %
              ("<no behavior.summary at all>" if broken is None
               else (" ".join(broken) if len(broken) <= 2
                     else "%s ... %s (%d)" % (broken[0], broken[-1], len(broken)))))

    print()
    print("SVC* (golden's report, doctored behavior.summary) -- C-3's 69 ids:")
    print("%-10s %-9s %-9s | %-8s %-8s | %-8s" %
          ("run", "exec.want", "broken", "C3", "familyC", "B14"))
    for name, broken in (
            ("SVCALL", SVCIDS),
            ("SVCHALF", SVCIDS[:35]),
            ("SVCONE", SVCIDS[:1])):
        st = score(GOLD.clone(), {}, (), broken)
        g = st["gates"]
        print("%-10s %-9s %-9s | %-8s %-8s | %-8s" %
              (name, g["c3.exec.want"], g["c3.exec.broken"],
               st["cp"][2], st["c"], st["b14"]))

    # --- GONE: one absence, two consequences
    #
    # behavior.summary is not B-14's file or C-3's file. It is ONE file that
    # both provisos read, against two different id lists, and deleting it is
    # one event with two results. The first draft of this generator printed it
    # twice -- a BEHAVGONE row in the table above and an SVCGONE row in the
    # table below -- and each row reported its own gate at zero and the other
    # gate at 1.000000, because each was computed by handing one channel a
    # missing summary and the other channel the golden's. There is no run in
    # which the file is absent for one list and present for the other. So it is
    # one run and it is printed once, and c-rehearsal.sh asserts both halves of
    # it off the same deletion.
    print()
    print("GONE (golden's report, no behavior.summary at all) -- both provisos, one absence:")
    st = score(GOLD.clone(), {}, None, None)
    g = st["gates"]
    print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s" %
          ("run", "b14.want", "b14.brk", "red.hon", "B14.5", "B14", "familyC"))
    print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s" %
          ("GONE", g["b14.c5.exec.want"], g["b14.c5.exec.broken"],
           g["b14.c5.redirects.honoured"], cell(st["b14p"][3]), st["b14"], st["c"]))
    print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s" %
          ("", "c3.want", "c3.brk", "", g["c3.exec"], st["cp"][2], st["c"]))
    print("           (c3.exec.want=%s c3.exec.broken=%s)" %
          (g["c3.exec.want"], g["c3.exec.broken"]))

    print()
    print("gate facts, per fixture:")
    for name, st, note in rows:
        print("  %s  (%s)" % (name, note))
        g = st["gates"]
        for k in sorted(g):
            print("      gate.%s=%s" % (k, g[k]))


if __name__ == "__main__":
    main()
