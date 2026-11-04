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

S07 = os.path.join(AUTH, "baseline-step07-source.txt")
G07 = os.path.join(AUTH, "baseline-step07-golden.txt")


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
BASE = Rep(S07)
GOLD = Rep(G07)
TGT = read_env(os.path.join(AUTH, "baseline-step07-target.txt"))
LEVEL = read_env(os.path.join(AUTH, "baseline-step07-level.txt"))


def T(k):
    return int(TGT[k])


WANT_MAJOR = int(LEVEL["major"])                    # 55
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


def c_score(rep, scan, measured, execbroken=()):
    """execbroken: the exchange ids family A no longer matches, or None for a
    behaviour summary that could not be read at all. Every source-tree fixture
    leaves it empty -- the summaries those runs produce carry `behavior.build=ok`
    and nothing else -- and the BEHAV* runs in c-rehearsal.sh pass a list."""
    """Family C: the Java 11 migration. Returns (familyC, [C1..C5], gate facts)."""
    g = {}
    # C-1 first and OUTSIDE the guard: it reads the class-file scan, which a
    # tree the scorer could not compile against its own classpath may perfectly
    # well have produced.
    #
    # `reach` is published in the SHELL'S OWN SPELLING and not in a uniform
    # %.6f, because the block's two zeroes are different facts. `c1reach=0` is
    # the block's initialiser and survives untouched when the scan is missing or
    # reports more than one major; `frac` produces `0.000000` when the scan was
    # read and found no class at this rung's level. A rehearsal that asserted
    # `0.000000` for both would pass a block that had stopped reading the scan
    # at all. Same reason for `cpen` below.
    if scan is None:
        reach, reachs = 0.0, "0"
        g["c1"] = "no-scan"
    else:
        classes, majors = scan
        g["c1.classes"] = classes
        g["c1.major%d" % WANT_MAJOR] = majors.get(WANT_MAJOR, 0)
        if len(majors) == 1:
            reach = frac(majors.get(WANT_MAJOR, 0), classes)
            reachs = "%.6f" % reach
        else:
            reach, reachs = 0.0, "0"
    g["c1.reach"] = reachs

    if not measured:
        c2 = c3 = c4 = c5 = 0.0
        cpen = 1.0
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

        # C-2. The want set is keyed pkg|bucket; the arrival side is the same
        # keys with the leading javax. rewritten, exactly as the shell's
        # `sed 's/^javax\\./jakarta./'` rewrites them.
        MOVED = ("javax.annotation", "javax.ejb")
        jxmove = nsbuckets(rep, lambda p: p in MOVED)
        jkmove = nsbuckets(rep, lambda p: p in ("jakarta.annotation", "jakarta.ejb"))
        wantjx = set("%s|%s" % (p, b) for p, b in NSMOVE)
        wantjk = set(re.sub(r"^javax\.", "jakarta.", x) for x in wantjx)
        c2dep, c2arr = len(wantjx - jxmove), len(wantjk & jkmove)
        c2jx, c2jk = jxann + jxejb, jkann + jkejb
        g.update({"c2.want": len(wantjx), "c2.departed": c2dep, "c2.arrived": c2arr,
                  "c2.javax.left": c2jx, "c2.jakarta.refs": c2jk})
        c2 = minf(minf(clamp01(frac(c2dep, C2_BUCKETS)),
                       clamp01(frac(c2arr, C2_BUCKETS))),
                  minf(clamp01(frac(C2_REFS - c2jx, C2_REFS)),
                       clamp01(frac(c2jk, C2_REFS))))

        # C-3, and the reference count is over PACKAGES rather than buckets:
        # the shell reads field 2 of every ns record and strips `pkg=`, so a
        # reference in a bucket nobody froze still counts towards the total.
        hold = nsbuckets(rep, lambda p: p not in MOVED and not p.startswith("jakarta."))
        wanthold = set("%s|%s" % (p, b) for p, b in NSHOLD)
        c3held = len(wanthold & hold)
        c3refs = len([b for b in rep.recs("ns")
                      if b.split()[1][4:] in NSHOLD_PKGS])
        g.update({"c3.want": len(wanthold), "c3.held": c3held, "c3.refs": c3refs})
        c3raw = minf(clamp01(frac(c3held, C3_BUCKETS)),
                     clamp01(frac(c3refs, C3_REFS)))
        g["c3.unscaled"] = "%.6f" % c3raw
        c3 = f6(c3raw * c2)

        # C-4
        sizedb = fbuckets(rep, "toarray", "form=sized")
        ctorb = fbuckets(rep, "toarray", "form=ctorref")
        c4dep, c4arr = len(set(TAWANT) - sizedb), len(ctorb & set(TAWANT))
        g.update({"c4.want": len(TAWANT), "c4.departed": c4dep, "c4.arrived": c4arr})
        c4 = minf(minf(clamp01(frac(c4dep, C4_BUCKETS)),
                       clamp01(frac(c4arr, C4_BUCKETS))),
                  minf(clamp01(frac(C4_SIZED - tasized, C4_SIZED)),
                       clamp01(frac(tactor, C4_SIZED))))

        # C-5, scoped to one file and not to a bucket.
        hf = [b for b in rep.recs("http") if site(b)[0] == C5_FILE]
        c5leg = len([b for b in hf
                     if b.split()[1] in ("form=legacytype", "form=legacycall")])
        names = set()
        for b in hf:
            if b.split()[1] != "form=clienttype":
                continue
            n = field(b, "name=")
            if n is None:
                continue
            q = n.split(".")
            if len(q) >= 4 and q[0] == "java" and q[1] == "net" and q[2] == "http" \
               and q[3] != "":
                n = ".".join(q[:4])
            names.add(n)
        c5red = 1 if [b for b in hf if b.split()[1] == "form=redirects"] else 0
        c5stub = max(0, hlegref + hlegcall - c5leg)
        g.update({"c5.legacy": c5leg, "c5.types": len(names),
                  "c5.redirects": c5red, "c5.cdistub": c5stub})
        c5d = clamp01(frac(C5_LEGACY - c5leg, C5_LEGACY))
        c5a = clamp01(frac(len(names), C5_TYPES))
        # The two run-time factors. `execbroken is None` is the unreadable
        # summary: it takes the whole of C-5, and it takes the redirect quarter
        # with it, because a proviso that cannot be checked is not satisfied.
        if execbroken is None:
            c5exec, redbroken = 0.0, True
        else:
            c5exec = clamp01(frac(C5_EXECN - len(execbroken), C5_EXECN))
            redbroken = C5_REDID in execbroken
        c5redok = 0 if redbroken else c5red
        g.update({"c5.exec.want": C5_EXECN,
                  "c5.exec.broken": C5_EXECN if execbroken is None else len(execbroken),
                  "c5.exec": "%.6f" % c5exec,
                  "c5.redirects.honoured": c5redok})
        c5 = f6(c5exec * minf(c5d, c5a) * (0.75 + 0.25 * c5redok))

        # The three penalty terms, all inside the measured guard.
        raw_added = max(0, rep.num("newgeneric.raw") - BASE_RAW)
        g["src.raw.added"] = raw_added
        cpen, pens = 1.0, "1"
        if raw_added > 0:
            cpen = f6(max(0.0, 1 - raw_added / 10.0))
            pens = "%.6f" % cpen
        if rep.num("suppress.sites") > BASE_SUP:
            cpen = f6(cpen / 2)
            pens = "%.6f" % cpen
        if taraw > 0:
            cpen = f6(cpen / 2)
            pens = "%.6f" % cpen
        g["src.penalty"] = pens

    c1 = f6(reach * ((0.07 * c2 + 0.17 * c3 + 0.12 * c4 + 0.54 * c5) / 0.90))
    tot = f6(cpen * (0.10 * c1 + 0.07 * c2 + 0.17 * c3 + 0.12 * c4 + 0.54 * c5))
    # C-1 is published either way -- it reads the scan, outside the guard --
    # but C-2..C-5 are `None` on a broken gate for the same reason B-13's
    # sub-facts are: the block never reaches their `fact` lines.
    parts = ["%.6f" % c1] + ([None] * 4 if not measured
                             else ["%.6f" % v for v in (c2, c3, c4, c5)])
    return "%.6f" % tot, parts, g


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

    The javax records are lifted out of baseline-step07-source.txt at their real
    lines with their real qualified names, and the jakarta ones that replaced
    them are removed. So this is the report a candidate who migrated five of the
    six buckets would really have produced, and not a plausible retyping of one.
    """
    jk = re.sub(r"^javax\.", "jakarta.", pkg)
    back = ns_in(BASE, pkg, bucket)
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
# THREE FIXTURES SHARE A familyC AND ARE NOT REDUNDANT (K1, K2, K3). All three
# put C-1's reach at zero and all three therefore score 0.900000; they differ in
# the gate fact that says WHY, and a block that read the scan wrongly would
# collapse them onto one message while keeping the score. The rehearsal asserts
# the message as well as the number, which is the only way that particular
# defect is visible from outside.
CAT = []


def fixture(name, base, why, **env):
    def deco(fn):
        CAT.append((name, base, why, fn, env))
        return fn
    return deco


@fixture("K1", "golden", "the whole tree still compiles to major 54: the source "
         "legs are all full and the build file was never touched. C-1's reach "
         "is 0 and family C loses exactly its 0.10.",
         scan=(CLASSES, {54: CLASSES}))
def _k1(rep, env):
    return "scan says 757 classes, all major 54"


@fixture("K2", "golden", "one class at 55 and 756 at 54 -- a build that moved "
         "part of the tree. majors.distinct is 2, so C-1 is a clean 0 rather "
         "than 1/757, and the block must say 'more than one major version'.",
         scan=(CLASSES, {54: CLASSES - 1, 55: 1}))
def _k2(rep, env):
    return "two majors present"


@fixture("K3", "golden", "no class-file scan at all: the build published "
         "nothing for arqgate.Scan to read. C-1 is 0 and the fact is "
         "gate.c1=no-scan, which is a different finding from a scan that read 54.",
         scan=None)
def _k3(rep, env):
    return "no scan.txt"


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
    rep.setrecs("toarray", BASE.recs("toarray"))
    rep.set("toarray.raw", 0)
    resync(rep)
    return "8 sized, 0 ctorref"


@fixture("K10", "golden", "two of the five toArray buckets converted and three "
         "left: the bucket halves and the site totals disagree, and the gate "
         "takes the smaller.")
def _k10(rep, env):
    keep = set(sorted(TAWANT)[:2])
    out = [b for b in rep.recs("toarray") if bucket_of(b) in keep]
    out += [b for b in BASE.recs("toarray") if bucket_of(b) not in keep]
    rep.setrecs("toarray", sorted(out))
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
    back = recs_in_file(BASE, "http", C5_FILE)[:7]
    rep.setrecs("http", sorted(rep.recs("http") + back))
    resync(rep)
    return "%d legacy record(s) survive" % len(back)


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


@fixture("K17", "golden", "nothing compiles: all three probes refused, so there "
         "is no measured report. B-10, B-11 and B-12a..d publish the literal 0 "
         "their blocks assign; B-12e, B-12, B-13 and familyC publish 0.000000, "
         "because their totals are computed by an awk that sits outside the "
         "guard; and B-13.2..7 and C-2..C-5 publish NOTHING AT ALL, because "
         "their fact lines are inside it. gate.c1.reach is STILL 1.000000, "
         "because C-1 reads the scan and sits outside the measured guard -- and "
         "C-1 still pays nothing, because the mean it is scaled by is zero.",
         ok11="no", ok10="no", okdef="no")
def _k17(rep, env):
    return "no report; scan intact"


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



# THE TWO ARTIFACT CHANNELS ARE TWO CHANNELS (K19, K20). B-12e unzips the jars
# the build published; C-1 reads the class-file scan that ran over those same
# jars. They are read by different blocks on different sides of the measured
# guard, from different files, and the pair below is what says so: K19 keeps a
# full scan and publishes jars that carry none of the four package names, K20
# publishes no jars and no scan at all. Every fixture above this point carries
# both files, so without these two the `[ -s modulejars.txt ]` else-arm never
# runs at build time and `gate.cp=no-jars` is a line no rehearsal has executed.
@fixture("K19", "golden", "the build published its jars and not one class in "
         "them names java.util.Optional, java.util.stream.Collectors, "
         "java.util.Objects or java.time. B-12e's four legs read zero against a "
         "source tree whose four AST legs are full -- the module that was "
         "dropped from the build, which is the failure B-12e was written for. "
         "The scan is untouched, so family C is unmoved.",
         jars=(0, 0, 0, 0))
def _k19(rep, env):
    return "nothing in the report; the jars carry no Java 8 library names"


@fixture("K20", "golden", "neither artifact file exists: no jars for B-12e to "
         "unzip and no scan for C-1 to read. B-12e is the literal 0 under "
         "gate.cp=no-jars and C-1's reach is the literal 0 under "
         "gate.c1=no-scan -- two different blocks, two different absences, and "
         "the source legs of both families still full.",
         jars=None, scan=None)
def _k20(rep, env):
    return "nothing in the report; both published artifacts are gone"


# --- writing a fixture out ----------------------------------------------------
CLASSES_PER_FIXTURE = 24
B12_MARKERS = ("java/util/Optional", "java/util/stream/Collectors",
               "java/util/Objects", "java/time/Instant")


def write_jars(d, counts):
    """Two real jars holding real .class entries, for B-12e to unzip and grep.

    Two rather than one so that the `while read` over modulejars.txt is
    exercised with more than one iteration, and the classes are interleaved
    between them so that no package's whole count can be collected from one
    archive. The bytes are a class-file magic number and a run of Utf8-shaped
    entries: B-12e is a binary grep over constant pools, not a class parser.

    NOTE the major version written into these headers is not read by anything.
    C-1 does not look at the jars -- it reads ${WORK}/scan.txt -- and keeping
    the two channels separate is the point: K3 carries jars and no scan, and
    reads gate.c1=no-scan while B-12e still pays.

    Every entry is written at a FROZEN timestamp. zipfile defaults each ZipInfo
    to the wall clock, which at step 05 made two runs of the generator produce
    jars with identical contents and different bytes -- trap 10, in the
    artefact rather than the expectation.
    """
    if counts is None:
        return
    jd = os.path.join(d, "jars")
    if not os.path.isdir(jd):
        os.makedirs(jd)

    def put(z, name, data):
        zi = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        z.writestr(zi, data)

    zips = [zipfile.ZipFile(os.path.join(jd, "mod%d.jar" % n), "w") for n in (1, 2)]
    try:
        for z in zips:
            put(z, "META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n")
        for i in range(CLASSES_PER_FIXTURE):
            blob = bytearray(b"\xca\xfe\xba\xbe\x00\x00\x00\x37")
            blob += b"\x01\x00\x20org/jboss/arquillian/zz/C%02d" % i
            for m, n in zip(B12_MARKERS, counts):
                if i < n:
                    blob += b"\x01" + bytes([0, len(m)]) + m.encode()
            blob += b"\x00" * 8
            put(zips[i % 2], "org/jboss/arquillian/zz/C%02d.class" % i, bytes(blob))
    finally:
        for z in zips:
            z.close()
    for n in counts:
        assert n <= CLASSES_PER_FIXTURE, n


def write_scan(d, scan):
    """The class-file census C-1 reads, in arqgate.Scan's own three key families.

    Scan writes a package, native-method and type-reference census as well;
    none of it is read at this rung, and a fixture carrying an invented package
    list would be stating facts about a tree rather than feeding a gate.
    """
    if scan is None:
        return
    classes, majors = scan
    with open(os.path.join(d, "scan.txt"), "w") as fh:
        fh.write("classes=%d\n" % classes)
        for m in sorted(majors):
            fh.write("major.%d=%d\n" % (m, majors[m]))
        fh.write("majors.distinct=%d\n" % len(majors))


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
    with open(os.path.join(d, "why.txt"), "w") as fh:
        fh.write(why + "\n")
        fh.write("\nwhat this fixture moves: " + note + "\n")
    write_scan(d, env.get("scan", (CLASSES, {WANT_MAJOR: CLASSES})))
    write_jars(d, env.get("jars", B12_FLOORS))
    return d


def score(rep, env, execbroken=()):
    measured = env.get("ok11", "yes") == "yes" or env.get("okdef", "yes") == "yes"
    scan = env.get("scan", (CLASSES, {WANT_MAJOR: CLASSES}))
    jars = env.get("jars", B12_FLOORS)
    if measured:
        b10, _ = b10_score(rep)
        b11, _ = b11_score(rep)
        b10s, b11s = "%.6f" % b10, "%.6f" % b11
    else:
        b10s = b11s = "0"
    b12s, b12p = b12_score(rep, jars, measured)
    b13s, b13p = b13_score(rep, measured)
    cs, cp, g = c_score(rep, scan, measured, execbroken)
    return dict(b10=b10s, b11=b11s, b12=b12s, b12p=b12p, b13=b13s, b13p=b13p,
                c=cs, cp=cp, gates=g)


def check_pinned():
    """The pinned fixture is the input tree's own report, byte for byte.

    Not a resemblance check. `pinned` is written by copying every pair out of
    baseline-step07-source.txt and writing them back, so if Rep's round trip
    ever stopped being the identity -- a dropped blank line, a renumbered
    record, a key reordered -- this catches it here rather than three fixtures
    downstream where the number would look like a finding.
    """
    p = os.path.join(OUT, "pinned", "report.txt")
    with open(p) as a, open(S07) as b:
        got, want = a.read(), b.read()
    assert got == want, "pinned/report.txt is not baseline-step07-source.txt"
    g = os.path.join(OUT, "golden", "report.txt")
    with open(g) as a, open(G07) as b:
        assert a.read() == b.read(), "golden/report.txt is not the golden's report"


# --- generate -----------------------------------------------------------------
def main():
    # Wipe and rebuild. The generator is the only author of this directory, so
    # a fixture that used to be in the catalogue and no longer is has to leave
    # the tree with it: c-rehearsal.sh iterates the DIRECTORY, not the table
    # below, and a step-06 fixture left behind here would be scored by step
    # 07's blocks against step 06's report and read as a finding.
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    rows = []

    for name, src, why, env in (
            ("pinned", BASE, "the input tree's own measured report, counter for "
             "counter and record for record. Family C must read 0.000000 on it "
             "and family B must read 1.000000: that is migration_prompt item "
             "6's nop run, asserted at image build time.", {}),
            ("golden", GOLD, "the golden tree's own measured report. Every "
             "family-C leg full, every family-B gate held: item 6's oracle run.",
             {})):
        rep = src.clone()
        d = write_fixture(name, rep, why, env, "nothing -- this is the report as "
                          "the source gate wrote it")
        rows.append((name, score(rep, env), "-"))

    check_pinned()

    for name, base, why, fn, env in CAT:
        rep = (BASE if base == "pinned" else GOLD).clone()
        note = fn(rep, env)
        write_fixture(name, rep, why, env, note)
        rows.append((name, score(rep, env), note))

    # --- the table, for transcription into c-rehearsal.sh by hand
    print("%-8s %-8s %-8s %-8s %-8s | %-8s %-8s %-8s %-8s %-8s | %-8s" %
          ("fixture", "B10", "B11", "B12", "B13",
           "C1", "C2", "C3", "C4", "C5", "familyC"))
    print("-" * 104)
    for name, s, note in rows:
        c = [cell(x) for x in s["cp"]]
        print("%-8s %-8s %-8s %-8s %-8s | %-8s %-8s %-8s %-8s %-8s | %-8s" %
              (name, s["b10"], s["b11"], s["b12"], s["b13"],
               c[0], c[1], c[2], c[3], c[4], s["c"]))
    print()
    print("B-12 parts (a b c d e) and B-13 parts (2 3 4 5 6 7):")
    for name, s, note in rows:
        print("  %-8s b12: %s" % (name, " ".join(cell(x) for x in s["b12p"])))
        print("  %-8s b13: %s" % ("", " ".join(cell(x) for x in s["b13p"])))
    # --- the BEHAV* runs, which are not source-tree fixtures
    #
    # c-rehearsal.sh builds these by copying golden/ and appending to nothing
    # but behavior.summary, so the report is the golden's and only the two
    # run-time factors move. Their `want` figures are computed here for the
    # same reason every other want is: so the rehearsal asserts a number this
    # file worked out rather than one the scorer handed it.
    print()
    print("BEHAV* (golden's report, doctored behavior.summary):")
    print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s" %
          ("run", "exec.want", "broken", "red.hon", "C1", "C5", "familyC"))
    behavs = [
        ("BEHAVALL", C5_HTTPIDS),
        ("BEHAVHALF", C5_HTTPIDS[:9]),
        ("BEHAVGONE", None),
        ("BEHAVRED", [C5_REDID]),
        ("BEHAVONE", [i for i in C5_HTTPIDS if i != C5_REDID][:1]),
    ]
    for name, broken in behavs:
        st = score(GOLD.clone(), {}, broken)
        g = st["gates"]
        print("%-10s %-9s %-9s %-9s | %-8s %-8s | %-8s" %
              (name, g["c5.exec.want"], g["c5.exec.broken"],
               g["c5.redirects.honoured"], st["cp"][0], st["cp"][4], st["c"]))
        print("           ids: %s" %
              ("<no behavior.summary at all>" if broken is None
               else (" ".join(broken) if len(broken) <= 2
                     else "%s ... %s (%d)" % (broken[0], broken[-1], len(broken)))))

    print()
    print("gate facts, per fixture:")
    for name, s, note in rows:
        print("  %s  (%s)" % (name, note))
        g = s["gates"]
        for k in sorted(g):
            print("      gate.%s=%s" % (k, g[k]))


if __name__ == "__main__":
    main()
