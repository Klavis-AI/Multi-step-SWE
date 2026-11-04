#!/usr/bin/env python3
"""Turn one scored run of step 5 into a reward in [0, 1].

test.sh performs the run -- it builds, replays, probes, gates and tests -- and
writes down what happened. This file reads only what was written down, which is
what makes the arithmetic reviewable after the container is gone and testable
here against fixtures (--selftest) rather than against a thirty-minute run.

    score.py --tests <payload> --run <rundir>

<rundir> is what test.sh filled in:

    preflight.tsv             ok|fail <what> <detail>, one line per precondition
    suite/summary.tsv         job, class, rc, seconds, bytes, wantrc
    suite/<job>.out           one recording per planned job
    suite/probe/<job>.tsv     what gate.GateProbe saw while that job ran: one
                              row per Spark job, naming the SQL execution it
                              ran inside, plus the session's version, the
                              fourteen watched configs as the engine actually
                              resolved them, and the counters
    gate/probe.tsv            those rows and the session probe's, concatenated
    gateroot/probe.tsv        root's own copy of that file, made before the
                              repository's suite runs as the uid that wrote it
    gate/findings.tsv         gate-run.sh's own verdicts, as the unprivileged half left them
    gate/findings.root.tsv    the same rules re-evaluated as root over a root-owned
                              copy of the same facts
    surefire/TEST-*.xml       the repository's own suite, run from a pristine copy

<payload> is /tests: weights.tsv, gates/ (the scanners, the rules and their
counterexample fixtures), and suite/ with the plan, the reference recordings,
the normaliser, the projection, the input census and the one-line list of jobs
whose exit status is recorded but not compared.

One thing about this step's run is worth reading before the arithmetic: the
replay happens *before* the gate, as it has since step 2, and at this step
none of the fourteen requirement rules depends on that order -- but all five
standing engine facts do: rung3.tsv's three, and the two this step adds in
rung5.tsv. `rung5-session-344` asks whether any job the replay ran came up in
a Spark 3.4.4 session, and `rung5-session-344-uniform` asks whether any came
up in something else. The second is the one the order is load bearing for:
it is an `absent` rule over `probe.probe`, and an `absent` rule is green over
zero rows, so a gate that ran first would not read it as "no session
reported a version other than 3.4.4" but as "no session reported anything",
which is green on a tree that never ran a line (NOTES 6.4.4, and the same trap
step 4 recorded for m3).

THE SHAPE OF THE ANSWER

    reward = T1 x T2   (user decision 2026-10-13: product scoring)
    T1 = (0.12 * m1 + 0.18 * m2 + 0.36 * m3) / 0.66    migrated-ness
    T2 = (0.24 * replay + 0.10 * tests) / 0.34         preservation

with one condition on the first two terms: they are graded only on a tree that
made this step's move, which is the rung and the reads together. That condition
is read in two parts -- the build resolves Spark 3.4.4 for Scala 2.13 and
nothing else and the jar's classes are class-file major 61, *and* no csv read
under src/main asks the engine to infer a schema without saying how date-shaped
columns are to be inferred, with at least six reads still both asking and
pinning, the seven sites still there, and the statement compiled into the jar. The first part is what the replay is a replay *of*; the
second is the work. See `moved`.

and every one of those five numbers is printed with its own evidence, because
a reward is a summary and the thing a reviewer needs is the summary's parts.

Four ways the run can be void rather than bad, and they are not the same thing
as a candidate scoring zero:

  * a precondition failed -- the mirror is down, the payload is short a file,
    an instrument failed its own selftest. Nothing measured after that means
    anything (trap 5, trap 6).
  * a standing rule is red. A hundred and sixty-six of this gate's hundred and
    eighty-one rules are standing: common.tsv's ten as floor.tsv tightens two
    of them, the hundred and fifteen that `hold` renamed with a `held-` prefix
    out of step 1's m01.tsv, step 2's m02.tsv, step 3's m03.tsv and step 4's
    m04.tsv, the thirty-eight prohibitions of cheats.tsv, cheats3.tsv,
    cheats4.tsv and cheats5.tsv, and rung3.tsv's three engine facts. Twelve of
    the hundred and fifteen are not held as the earlier steps wrote them:
    m05-inherit.tsv widens every fence those steps put around a Spark version
    or a Scala suffix, so that they accept 3.4.4 and _2.13 as well as the
    versions the earlier steps landed on, because this step is the one that
    moves off 3.3.4 and _2.12 and the rules step 4 left behind would otherwise
    void a perfect tree. The widening is forward-only and it does not let a
    candidate regress the pin: a held rule is a precondition, and m05.tsv's own
    `m1-spark-344` and `m1-scala-213` are what forbid going back. The tree
    Harbor hands this step -- steps 1 to 4 finished, none of step 5 done --
    passes all hundred and sixty-six, rung3.tsv's included: those three read the
    engine the replay ran on, and that engine is this image's whatever the
    candidate's pom says. So a red one says the reading is void, not that the
    candidate is wrong: the mirror fell over, or an instrument did not run, or
    an earlier step's work was undone, or something switched off a default the
    reading assumes.
  * one of the thirty-eight prohibitions is red. Those are standing rules too,
    and by the arithmetic above they would void the migrated family and leave
    the behavior family standing. They do not: an opaque encoder, a hand-set
    execution id, a forged one, the reflective spellings of those, the two ways
    a jar reaches for the instrument that reads it, the `spark.sql.legacy.*`
    hatches, a hand on adaptive execution or on the optimiser, the jobs going
    quiet, a reading the tree arranged to fail, and -- new at this step -- the
    two ways a pom can tell surefire not to run the suite at all are the ways
    this step is made to look done without being done. Skipping the tests
    belongs here rather than among the requirements for the same reason the
    others do: it buys the *appearance* of the milestone. Moving to JDK 17
    breaks the forked test JVM until the pom opens `java.base/sun.nio.ch`, and
    the cheapest way to make that breakage go away is to stop running the
    tests, which turns a red suite into a green build and moves nothing. The
    ids are read out of gates/rules/cheats.tsv, cheats3.tsv, cheats4.tsv and
    cheats5.tsv rather than listed here, so the files and the arithmetic cannot
    drift apart.
  * the two evaluations of the gate disagree. Same rules, same facts, one run
    as nobody and one as root over a copy -- rules.py is deterministic, so a
    difference means the facts moved between being written and being read.

The first voids everything and is a verifier failure, reported as such. The
second and third void the migrated-ness family only: the behavior family is
measured from recordings, not from facts, and a tree that does not build has
already lost the replay on its own merits. The fourth is the one exception, and
it is deliberate: it zeroes the reward outright, behavior included.

And one way the behavior family is not graded at all rather than graded badly,
which at this step is sharper than at any before it. `spark-core` is
`provided`. The replay runs on the verifier image's Spark and on its JVM
whatever the candidate's pom resolves, so every one of the rung's recorded
behaviour changes -- the JDK-17 helpful-NPE text in HarnessDriver-rowtypes, the
status DatasetClassifier exits with -- is produced by the image and not by the
candidate. A tree that edits nothing at all replays 140 of 141, losing only
`HarnessDriver-csvdates`, which is the job that reads the inferred type back.
Its suite is another matter -- without the test JVM opened, 13 of the 140
graded tests are all that get through -- but 0.248643 of the family's 0.34 is
still collectable by a tree nobody touched. That is not 140/141 of the replay:
each job is weighted by its scored input count, 249 across the census, and
credited with the fraction of its recorded blocks that match, and the one job
that moves still matches two of its three, so the replay side of it is
0.239357 of the 0.24. Not one recorded difference at this rung is a must-flip
a candidate could earn, and paying a quarter of the step for recordings
reproduced by not having been touched would make the nop non-zero
(migration_prompt.md section 8: nop reports 0.0). `moved` is what
stops that, and what it leaves the one-line edit is nothing: a tree that sets
`<spark.version>` to 3.4.4 and stops there still resolves `spark-core_2.12`, so
m1 is red, m2 is red at class-file 52, m3 is red on six sites, and the
behaviour family is not graded. That tree scores 0.000000 (item 4.3). A tree
that moves the whole pom -- 3.4.4, _2.13, Java 17, the test JVM opened -- and
edits no source scores 0.30, which is m1 and m2 and no more. Rubric R4 calls a
step answerable by editing pom.xml alone an automatic Blocker; this is the
arithmetic that keeps this one from being it. The reads are the half a pom
cannot reach, and they carry 0.36 of the reward directly and 0.34 more through
this gate. See `moved`.
"""
import argparse
import collections
import contextlib
import importlib.util
import os
import re
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True            # trap 8: no __pycache__ beside /tests


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --- the weights -------------------------------------------------------------

def load_weights(path):
    """family -> {key: (weight, description)}, and the total must be exactly 1.

    Exactly, not approximately. These are four literals in a file a human
    edits, and a set that sums to 0.98 is a step whose perfect candidate scores
    0.98 -- which would look like a hard step rather than like a typo.
    """
    fams = collections.OrderedDict()
    total = 0.0
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if len(f) != 4:
                raise ValueError('%s:%d: want 4 fields, got %d' % (path, n, len(f)))
            fam, key, w, what = f
            if key in fams.setdefault(fam, collections.OrderedDict()):
                raise ValueError('%s:%d: %s/%s twice' % (path, n, fam, key))
            fams[fam][key] = (float(w), what)
            total += float(w)
    if abs(total - 1.0) > 1e-9:
        raise ValueError('%s: weights sum to %.6f, not 1' % (path, total))
    return fams


# --- the gate ----------------------------------------------------------------

FINDING = re.compile(r'^(PASS|FAIL|ERROR)\t')
REQUIREMENT = re.compile(r'^(m\d+)-')


def read_findings(path):
    """[(status, id, count, reason)] from a rules.py report; evidence dropped."""
    out = []
    with open(path, errors='replace') as fh:
        for line in fh:
            if not FINDING.match(line):
                continue                     # an evidence line, indented
            f = line.rstrip('\n').split('\t')
            out.append((f[0], f[1], int(f[2]), f[3] if len(f) > 3 else ''))
    return out


def gate_scores(findings, weights, log):
    """Per-requirement credit, all or nothing, and the standing-rule verdict."""
    standing = [r for r in findings if not REQUIREMENT.match(r[1])]
    red = [r for r in standing if r[0] != 'PASS']
    for status, rid, count, reason in red:
        log('gate   VOID   %-28s %s (%s, %d match(es))' % (rid, reason, status, count))

    seen = collections.defaultdict(list)
    for status, rid, count, reason in findings:
        m = REQUIREMENT.match(rid)
        if m:
            seen[m.group(1)].append((status, rid, count, reason))

    # Both directions, because the two files are edited months apart: a rule
    # added to step01.tsv under a prefix nothing weights would be scored at
    # zero and never noticed, and a weight for a requirement whose rules were
    # renamed would be paid out for nothing (rule 4, in the verifier's own
    # terms).
    if set(seen) != set(weights):
        raise ValueError('the findings carry requirements %s but weights.tsv '
                         'names %s' % (sorted(seen), sorted(weights)))

    credit = {}
    for key in weights:
        rows = seen[key]
        bad = [r for r in rows if r[0] != 'PASS']
        credit[key] = 0.0 if bad else 1.0
        log('gate   %-6s %-4s %d of %d rule(s) pass%s'
            % ('PASS' if not bad else 'FAIL', key, len(rows) - len(bad), len(rows),
               '' if not bad else '   red: ' + ' '.join(r[1] for r in bad)))
        for status, rid, count, reason in bad:
            log('       %-8s %-28s %s (%d match(es))' % (status, rid, reason, count))
    return credit, red


# --- the thirty-eight ways to fake this step ---------------------------------

# All four prohibition files, by name. cheats.tsv carries the twelve this
# ladder has had since step 2 -- the opaque encoder, the forged execution id,
# the tampered listener, a lookup by name, a call through java.lang.reflect,
# and a class of the candidate's own named for the observer; cheats3.tsv the
# fifteen step 3 added, which refuse the `spark.sql.legacy.*` hatches three
# ways, the two adaptive-execution switches two ways each, any other adaptive
# sub-setting put there by hand, a `describe` spelled only in the constant
# pool, an optimiser rule excluded by hand, the two paying jobs going quiet,
# and a conf read the tree arranged to fail; cheats4.tsv the nine step 4 added;
# and cheats5.tsv the two this step adds, which refuse a pom that tells
# surefire to skip the suite -- once as a plugin configuration and once as the
# property that does the same thing without naming the plugin.
#
# They are four files rather than one because each step's instruction is
# compared against its own rule file in both directions (item 4.4), and a rule
# in a file an earlier step's instruction is read against would have to be
# mentioned in an instruction written before the thing it prohibits existed:
# step 2's instruction cannot mention a legacy config and step 4's cannot
# mention a test JVM that JDK 17 has not yet broken. The split is a
# documentation boundary and nothing else: step05.tsv includes all four, this
# function reads all four, and the price of a rule in any of them is identical.
CHEAT_FILES = ('cheats.tsv', 'cheats3.tsv', 'cheats4.tsv', 'cheats5.tsv')


def cheat_rule_ids(tests):
    """The ids in the four gates/rules/cheats*.tsv files, read out by name.

    Every standing rule costs the migrated family. These thirty-eight cost the
    whole reward, behavior included, and the difference between the two prices is not
    a judgement this file makes -- it is which file the rule was written in. A
    list kept here as well would be a second copy of it, and on the day a
    thirty-ninth prohibition is written the copy is the one that would not
    have it (rule 4, in the verifier's own terms).

    An id appearing in two of them is refused rather than deduplicated. rules.py
    raises on a duplicate id when it composes the gate, so a tree could never
    be scored against such a payload anyway; catching it here means the payload
    that could not be scored is reported as a broken payload rather than as a
    candidate who failed a prohibition.
    """
    ids = []
    for name in CHEAT_FILES:
        path = os.path.join(tests, 'gates', 'rules', name)
        for n, line in enumerate(open(path), 1):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if len(f) != 5:
                raise ValueError('%s:%d: want 5 fields, got %d'
                                 % (path, n, len(f)))
            if REQUIREMENT.match(f[0]):
                raise ValueError('%s:%d: %s carries a requirement prefix, so it '
                                 'would be paid for by the migrated family and '
                                 'void the whole reward at the same time'
                                 % (path, n, f[0]))
            if f[0] in ids:
                raise ValueError('%s:%d: %s is named in more than one of %s; '
                                 'rules.py refuses to compose a gate with a '
                                 'duplicate id, so no tree could be scored '
                                 'against this payload'
                                 % (path, n, f[0], ', '.join(CHEAT_FILES)))
            ids.append(f[0])
    if not ids:
        raise ValueError('%s name no rule, so nothing would void the reward and '
                         'the instruction\'s "a faked tree is worth nothing, '
                         'behaviour included" would be false of the arithmetic'
                         % ' and '.join(CHEAT_FILES))
    return ids


def cheat_red(findings, ids):
    """The rows of `findings` that are one of `ids` and did not pass.

    A rule the report does not mention is counted red, not absent. rules.py
    reports every rule it evaluates, so a missing id means the gate did not
    evaluate it -- and a prohibition that did not run is exactly as much
    evidence as a prohibition that failed.
    """
    by_id = dict((r[1], r) for r in findings)
    red = []
    for rid in ids:
        row = by_id.get(rid)
        if row is None:
            red.append(('MISSING', rid, 0, 'the gate report does not mention '
                        'this rule, so it was never evaluated'))
        elif row[0] != 'PASS':
            red.append(row)
    return red


# --- the behavior replay ------------------------------------------------------

def blocks(text, patterns):
    """Split a normalised recording at its labeled lines.

    One block is one scored input's answer: the label line and everything
    printed under it until the next label. Anything before the first label is
    the subcommand's own preamble and is a block too -- it is real output and a
    candidate that broke it should not be paid for the cases below it.

    A recording with no labels at all is one block, which is what a CLI recipe
    is: one input, one answer, all of it.
    """
    out, key, buf = [], '(head)', []
    for line in text.splitlines():
        if any(p.match(line) for p in patterns):
            if buf or out:
                out.append((key, '\n'.join(buf)))
            key, buf = line.split(':', 1)[0], [line]
        else:
            buf.append(line)
    out.append((key, '\n'.join(buf)))
    return out


def keyed(pairs):
    """Blocks by label, disambiguated by occurrence.

    By label rather than by position on purpose. The labels come from the
    harness sources in /tests, which are compiled fresh in the verifier and
    which the candidate cannot reach, so the set of labels and their order is
    fixed by the author's code. Only the answers are the candidate's. Keying on
    the label means a candidate that breaks one case loses one case, where a
    positional comparison would lose every case after it as well.
    """
    seen, out = collections.Counter(), collections.OrderedDict()
    for k, t in pairs:
        seen[k] += 1
        out['%s#%d' % (k, seen[k]) if seen[k] > 1 else k] = t
    return out


def load_unscored_status(path, planned):
    """{job: reason} for jobs whose rc is recorded but not compared.

    An exemption that names a job nobody runs is a line of prose, and an
    exemption list nobody can see is worse than none at all: both leave a hole
    in the behavior family that reads as a clean green. So every row is
    asserted against the plan here, and every row is printed by the caller on
    every run, whether or not the status it exempts actually moved.
    """
    out = {}
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if len(f) != 2 or not f[1].strip():
                raise ValueError('%s:%d: want <job>\\t<reason>, got %d field(s)'
                                 % (path, n, len(f)))
            if f[0] in out:
                raise ValueError('%s:%d: %s listed twice' % (path, n, f[0]))
            if f[0] not in planned:
                raise ValueError('%s:%d: %s is not a planned job; an exemption '
                                 'for a job that does not run hides nothing and '
                                 'proves nothing' % (path, n, f[0]))
            out[f[0]] = f[1].strip()
    return out


def replay_score(tests, run, log):
    suite = os.path.join(tests, 'suite')
    norm = load_module('norm', os.path.join(suite, 'norm.py'))
    proj = load_module('projection', os.path.join(suite, 'projection.py'))
    inputs = load_module('inputs', os.path.join(suite, 'inputs.py'))
    table = proj.load(os.path.join(suite, 'projection.tsv'))
    patterns = (inputs.ADVERSARIAL_LABEL, inputs.ORDINARY_LABEL)

    rows = [l.rstrip('\n').split('\t')
            for l in open(os.path.join(suite, 'jobs.tsv')) if l.strip()]
    refs, missing = {}, []
    for _phase, job, _cls, _rc, _args in rows:
        p = os.path.join(suite, 'ref', job + '.norm')
        if not os.path.exists(p):
            missing.append(job)
        else:
            refs[job] = open(p, encoding='utf-8', errors='replace').read()
    if missing:
        raise ValueError('the payload is short %d reference recording(s): %s'
                         % (len(missing), ' '.join(missing[:5])))

    # The weights are rule 7's scored inputs, counted over the *reference*
    # recordings. Never over the candidate's: classify() counts labeled lines,
    # so a candidate whose tree printed nothing but `adv x: ` lines would be
    # weighting its own answers.
    _n, per_job = inputs.classify(rows, refs)

    # And the same count, taken when the payload was staged, is on disk beside
    # the recordings. Two derivations of one number: a ref/ directory that lost
    # a file, or a norm.py that stopped matching, moves one and not the other.
    staged = {}
    with open(os.path.join(suite, 'inputs.tsv')) as fh:
        for line in fh:
            if line.startswith('#') or not line.strip():
                continue
            job, adv, ord_ = line.rstrip('\n').split('\t')
            staged[job] = (int(adv), int(ord_))
    if staged != per_job:
        diff = [j for j in set(staged) | set(per_job)
                if staged.get(j) != per_job.get(j)]
        raise ValueError('the staged input census and the one derived here '
                         'disagree on %d job(s): %s' % (len(diff), ' '.join(sorted(diff)[:5])))

    # Jobs whose status is recorded but not compared, asserted against the plan
    # and named in the log every run -- see suite/unscored-status.tsv for what
    # earns a row and NOTES 6.1.13 for the one that has one.
    unscored = load_unscored_status(os.path.join(suite, 'unscored-status.tsv'),
                                    {job for _p, job, _c, _r, _a in rows})
    for job in sorted(unscored):
        log('replay UNSCORED-STATUS %s: %s' % (job, unscored[job]))

    summary = {}
    spath = os.path.join(run, 'suite', 'summary.tsv')
    with open(spath) as fh:
        for line in fh:
            if not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            summary[f[0]] = f
    # Trap 3, one level down: a job that vanished from the run vanishes from
    # the mean unless something counts it. run-suite.sh already exits 6 on
    # this; it is asserted again here because the exit status of a script that
    # ran half an hour ago is not evidence a scorer can cite.
    absent = [job for _p, job, _c, _r, _a in rows if job not in summary]
    if absent:
        log('replay MISSING %d planned job(s) left no summary row: %s'
            % (len(absent), ' '.join(absent[:8])))

    earned = total = 0.0
    scored = collections.Counter()
    detail = []
    for _phase, job, _cls, wantrc, _args in rows:
        weight = sum(per_job[job])
        total += weight
        row = summary.get(job)
        outp = os.path.join(run, 'suite', job + '.out')
        if row is None or not os.path.exists(outp):
            detail.append((job, weight, 0.0, 'no recording'))
            scored['absent'] += 1
            continue
        if row[2] != wantrc and job not in unscored:
            detail.append((job, weight, 0.0, 'rc=%s want %s' % (row[2], wantrc)))
            scored['rc'] += 1
            continue
        if row[2] != wantrc:
            # Exempt, so it costs nothing -- but it is still reported, with the
            # status it actually exited with, so that "this job's rc moved" is a
            # sentence in the log of every run and not a silent zero cost.
            scored['rcfree'] += 1
            detail.append((job, weight, -1.0,
                           'rc=%s want %s, not compared (unscored status)'
                           % (row[2], wantrc)))
        got = proj.project(job, norm.normalize(
            open(outp, encoding='utf-8', errors='replace').read()), table)
        want = proj.project(job, refs[job], table)
        gb, wb = keyed(blocks(got, patterns)), keyed(blocks(want, patterns))
        hit = sum(1 for k, v in wb.items() if gb.get(k) == v)
        credit = hit / float(len(wb))
        earned += weight * credit
        if credit == 1.0:
            scored['same'] += 1
        else:
            scored['diff'] += 1
            bad = [k for k, v in wb.items() if gb.get(k) != v]
            detail.append((job, weight, credit,
                           '%d of %d block(s) differ: %s'
                           % (len(bad), len(wb), ' | '.join(bad[:3]))))
    for job, weight, credit, why in detail:
        if credit < 0:                    # an exempt status: no cost to report
            log('replay %-44s %20s   %s' % (job, '-', why))
            continue
        log('replay %-44s %5.3f of %2d input(s)   %s' % (job, credit * weight, weight, why))
    log('replay %d same, %d different, %d wrong status, %d absent, %d status not '
        'compared (of %d job(s), %d scored input(s))'
        % (scored['same'], scored['diff'], scored['rc'], scored['absent'],
           scored['rcfree'], len(rows), int(total)))
    return (earned / total if total else 0.0), scored


# --- the repository's own suite ----------------------------------------------

# The one class no tree at this rung can pass, and the control it becomes.
#
# `StructType.toString()` dropped the space after the comma between 3.2.4 and
# 3.3.4 and has not put it back at 3.4.4. CustomReportServiceTest hardcodes the
# old rendering as a string literal at :42 and :50, so both of its two tests are
# red here -- for every tree, the golden solution included, because test.sh
# deletes /app/src/test and copies the commit's own pristine copy over it before
# the suite runs, which deletes a candidate's correction along with everything
# else they did to that directory. Measured at this rung on two byte-identical
# trees one `--add-opens` apart: red either way, with the failure text differing
# only in which of the two ways it is red (below), and the cause reproduced with
# no data, no mirror and no web service in it by building a StructType in memory
# at each rung (NOTES 6.4.2).
#
# Scoring it would cost the oracle 2/142 and hand out an unpriced shortcut:
# with the pair unpassable, deleting the file scores exactly what leaving it in
# place scores, so the gate would pay the same for a suite that was repaired
# and one that was vandalised. So the class leaves both the numerator and the
# denominator -- 46 classes, 140 tests -- rather than the baseline becoming
# "two failures allowed anywhere", which would let a candidate break two other
# tests for free.
#
# And the exemption is turned around into an instrument -- with two more shapes
# to it at this step than at the last. On JDK 17 the forked test JVM reaches
# `sun.nio.ch.DirectBuffer` through `StorageUtils` while the session starts,
# and under JPMS that is an `IllegalAccessError` unless the pom opens
# `java.base/sun.nio.ch`. So this class has three candidate-independent
# readings at this rung, the first two measured on two trees one `--add-opens`
# apart (NOTES 6.4.2) and the third on the tree a candidate who changed nothing
# arrives with (NOTES 6.5.10):
#
#     opened      tests=2  failures=2  errors=0  skipped=0
#     not opened  tests=4  failures=0  errors=4  skipped=0
#     the 2.4 pin tests=2  failures=0  errors=2  skipped=0
#
# -- the four being each of the two tests reported twice, once for the
# `IllegalAccessError` its `@Before` threw and once for the NPE its `@After`
# threw on a `JavaSparkContext` that was never assigned. The third is the same
# denial arriving one layer later: on the 2.4 pin `StorageUtils` never reaches
# `sun.nio.ch`, so the `@Before` succeeds and the test bodies themselves take
# an `InaccessibleObjectException` on `java.net.URI.scheme`. All three are
# facts about the overlay and the rung; none is a fact about the tree beyond
# whether the pom opened the module, and that is already priced twice over --
# directly by `m2-test-jvm-opens` inside m2's 0.18 and proportionally by this
# family, whose score falls from 1.000000 to 0.092857 when the other 46 classes
# run without the open: the same access error kills 252 of the suite's readings
# and leaves 13 of the 140 graded tests passing, so the 0.10 pays out 0.009286
# (measured over the same four arms, test.sh's overlay-control block).
#
# Which is why all three shapes are exempt here rather than one. Refusing the
# second would make an omitted `--add-opens` fail a *precondition* in test.sh,
# and a failed precondition is a 0.0 for the whole run -- a third price for one
# omission, and a candidate reading a verifier bug where the rubric says they
# should read m2's 0.18 gone and almost all of the 0.10 with it. Refusing the
# third voided the nop outright: the tree that did nothing is the one
# calibration that has to come back 0.00 for reasons, and a failed precondition
# says nothing at all instead. test.sh accepts all three and requires each
# erroring one to name the access it was denied -- `sun.nio.ch` for the
# four-entry shape, any JPMS denial for the two-error one -- so "the candidate
# did not open the module" stays distinguishable from "something else broke
# before the test bodies ran". Here the arithmetic is done and every reading is
# logged, so whoever reads the log can compare them.
EXEMPT_CLASS = 'edu.sdsc.mmtf.spark.webservices.CustomReportServiceTest'
# (tests, failures, errors, skipped), in the order tests_score reads them, and
# the sentence each one earns in the log.
EXEMPT_READINGS = (
    ((2, 2, 0, 0), 'the test JVM was opened onto java.base/sun.nio.ch'),
    ((4, 0, 4, 0), 'the test JVM was not opened onto java.base/sun.nio.ch'),
    ((2, 0, 2, 0), 'the test JVM was not opened, and this tree is far enough '
                   'behind that the denial lands in the test bodies'),
)
EXEMPT_SHAPES = tuple(shape for shape, _ in EXEMPT_READINGS)


def tests_score(run, expect_classes, expect_tests, log):
    d = os.path.join(run, 'surefire')
    tot = collections.Counter()
    classes = 0
    red = []
    if os.path.isdir(d):
        for name in sorted(os.listdir(d)):
            if not (name.startswith('TEST-') and name.endswith('.xml')):
                continue
            try:
                a = ET.parse(os.path.join(d, name)).getroot().attrib
            except ET.ParseError as exc:
                log('tests  UNREADABLE %s: %s' % (name, exc))
                continue
            n = int(a.get('tests', 0) or 0)
            f = int(a.get('failures', 0) or 0)
            e = int(a.get('errors', 0) or 0)
            s = int(a.get('skipped', 0) or 0)
            if a.get('name') == EXEMPT_CLASS:
                said = dict(EXEMPT_READINGS).get((n, f, e, s))
                log('tests  EXEMPT  %s: %d test(s), %d failure(s), %d error(s), '
                    '%d skipped -- outside both baselines at this rung%s'
                    % (EXEMPT_CLASS, n, f, e, s,
                       ' (%s)' % said if said is not None
                       else '; test.sh overlay-control is what refuses this run'))
                continue
            classes += 1
            tot.update(tests=n, failures=f, errors=e, skipped=s)
            if f or e or s:
                red.append('%s (%d test(s), %d failure(s), %d error(s), %d skipped)'
                           % (a.get('name'), n, f, e, s))
    passed = tot['tests'] - tot['failures'] - tot['errors'] - tot['skipped']
    # Denominator is what the baseline runs, not what this run happened to run.
    # A suite that shrank is not a suite that passed: surefire reports 39
    # classes and 124 tests the moment the pom's own `<excludes>` take effect
    # again, and every one of them green (run-inherited-tests.sh).
    score = max(0.0, min(1.0, passed / float(expect_tests)))
    log('tests  %d class(es) (baseline %d), %d test(s) (baseline %d), '
        '%d passed, %d failed, %d error(s), %d skipped'
        % (classes, expect_classes, tot['tests'], expect_tests, passed,
           tot['failures'], tot['errors'], tot['skipped']))
    if classes != expect_classes:
        log('tests  SHRANK  %d class(es) ran where the baseline runs %d'
            % (classes, expect_classes))
    for line in red[:12]:
        log('tests  RED     %s' % line)
    return score


# --- did this tree make the move, for behavior to be worth asking about? -----

# The ten ids the milestone is composed from, matched against `m05.tsv` rather
# than listed here. Two conjunctions, and the names say which work each does.
#
# `rung_adopted` is the rung -- the build resolves Spark 3.4.4 for Scala 2.13
# and no org.apache.spark artifact resolves at anything else, and the classes
# of this repository in the built jar are class-file major 61 and none of them
# is anything else. `reads_stated` is the work -- no csv read under src/main
# asks the engine to infer a schema without saying how date-shaped columns are
# to be inferred, six reads do pin it off, the seven csv sites are still there,
# and the statement reached the jar.
#
# All ten are read out of the same gate report m1, m2 and m3 are scored from,
# so the milestone and the requirements cannot disagree about what the tree
# did, and none of them is a rule this file invents: each is matched by pattern
# against the step's own rule file and then shape-checked, because a rule file
# is a thing a human edits and every shape refused below is green on the tree
# the candidate is handed.
RUNG_SPARK = re.compile(r'^m\d+-spark-344$')
RUNG_SPARK_ONLY = re.compile(r'^m\d+-spark-344-uniform$')
RUNG_SCALA = re.compile(r'^m\d+-scala-213$')
RUNG_SCALA_ONLY = re.compile(r'^m\d+-scala-213-only$')
RUNG_CLASSES = re.compile(r'^m\d+-classes-17$')
RUNG_CLASSES_ONLY = re.compile(r'^m\d+-classes-17-uniform$')
READ_DATES = re.compile(r'^m\d+-csv-dates-stated$')
READ_PINNED = re.compile(r'^m\d+-csv-dates-pinned$')
READ_SITES = re.compile(r'^m\d+-csv-sites-kept$')
READ_JAR = re.compile(r'^m\d+-prefer-date-jar$')

# The two constants below are the only piece of rules.py's selector grammar
# this file repeats: fields are separated by a literal backslash-t in the
# escaped text a rule line carries, and a field of `*` matches anything
# (rules.py, `load`). Running rules.py's own loader over the selector would be
# a second copy of that loader to keep in step; checking two terms of its
# grammar by hand is the smaller duplication.
SELECTOR_SEP = '\\t'
SELECTOR_ANY = '*'

# What the handed tree answers, measured by the scanners that score it rather
# than by grep (NOTES 6.5.2). These are what make the shape checks below mean
# something: every refused shape is a rule that is green on *these* numbers, so
# weakening the rule file alone cannot lower the floor.
#
#   seven csv reads under src/main, six of them asking the engine to infer
#   thirty-three dependencies carrying a Scala suffix, all of them _2.12
#   every org.apache.spark artifact resolved at 3.3.4
#   two hundred and one classes in the jar, all of them class-file major 52
#   no `preferDate` string constant anywhere in the jar, and `inferSchema`
#     and `header` both present in it
HANDED_CSV_READS = 7
HANDED_INFERRING_READS = 6
HANDED_SPARK = '3.3.4'
HANDED_SCALA = '2.12'
HANDED_CLASS_MAJOR = '52'

# The sample values each selector term is tried against. The paths carry the
# `/app/` prefix the scanners actually emit (gate-run.sh hands ast-scan.sh
# `$TREE/src`, and `$TREE` is `/app`), because a path term that matches nothing
# is not a strict rule -- it is a vacuous one, and an `absent` rule that
# selects no row is green on every tree there is.
#
# Two samples on the accepting side of each path and name term rather than one,
# from two different packages. Six of the seven csv reads are under
# `datasets/` and one under `applications/`, and the 201 classes are spread
# over nineteen packages, so a term scoped to one package accepts the first
# sample and grades a fraction of the tree -- which is the same vacuity as
# selecting nothing, just harder to see.
MAIN_READ = '/app/src/main/java/edu/sdsc/mmtf/spark/datasets/PdbToUniProt.java'
ALT_READ = '/app/src/main/java/edu/sdsc/mmtf/spark/applications/DatasetFileConverter.java'
TEST_READ = '/app/src/test/java/edu/sdsc/mmtf/spark/datasets/PdbToUniProtTest.java'
MAIN_CLASS = 'edu/sdsc/mmtf/spark/datasets/PdbToUniProt'
ALT_CLASS = 'edu/sdsc/mmtf/spark/io/MmtfReader'
# A class in a package this repository does not have. `--only` is derived from
# the tree it scans (gates/only-prefixes.sh), so a candidate who adds one file
# in a new package adds that package to the scan's scope.
OTHER_CLASS = 'com/example/Helper'
# One inferring csv read as the handed tree writes it, the same read once date
# inference is pinned off at the site, the same read with 3.4's new default
# adopted instead, and a read that asks for nothing.
OPTS_INFERRING = 'header=true,inferSchema=true'
OPTS_STATED = 'header=true,inferSchema=true,preferDate=false'
OPTS_ADOPTED = 'header=true,inferSchema=true,preferDate=true'
OPTS_NONE = '-'


def selector_field(term, value):
    """Does one selector field accept `value`? `*` is rules.py's match-anything.

    A term that will not compile is a rule rules.py refuses to load at all, so
    it is reported here as accepting nothing rather than raised: the caller is
    naming a shape, and "this field matches no value" is the shape it has.
    """
    if term == SELECTOR_ANY:
        return True
    try:
        return re.compile(term).fullmatch(value) is not None
    except re.error:
        return False


# One rule's shape, as the milestone reads it.
#
#   fact      the scanner fact the rule must select, `<scanner>.<kind>`
#   fields    that kind's field names in order (gates/schema.tsv). Only the
#             count is enforced, but the names are what the refusal message
#             says, and a selector off by one column is the failure this
#             catches -- it loads, it runs, and it asks about the wrong thing.
#   mode      `absent`, or `atleast` with `least` as the smallest N allowed
#   least_why why that N is the floor rather than a number somebody picked, as
#             a format string over (the threshold the rule asks for, `least`).
#             `absent` shapes carry None, and each `atleast` shape needs its
#             own: for `-csv-sites-kept` the floor is a count the handed tree
#             already answers, so anything under it is met before the candidate
#             starts, while for `-csv-dates-pinned` the handed tree answers
#             zero and the floor is the number of reads that have to end up
#             pinned. One sentence for both would be false about one of them.
#   selects   (field, value, why) the term must accept
#   rejects   (field, value, why) the term must not accept
#
# `selects` and `rejects` are both needed and they are not two spellings of one
# check. A term that accepts nothing makes an `absent` rule vacuous and an
# `atleast` rule unsatisfiable; a term that accepts everything makes an
# `absent` rule fire on the golden tree and an `atleast` rule countable out of
# rows nobody asked about. Each direction has its own `why`, because the
# message a payload author reads is the whole value of failing here.
Shape = collections.namedtuple('Shape',
                               'fact fields mode least least_why '
                               'selects rejects')

SHAPES = {
    'spark-344': Shape(
        'pom.resolved', ('ga', 'version', 'scope'), 'atleast', 1,
        'asks for %d where %d is the floor: a threshold of nothing is a rule '
        'no pom can fail, and one resolved row is the whole of what this '
        'clause claims',
        ((0, 'org.apache.spark:spark-core_2.13',
          'the artifact whose resolution is what "this build is on 3.4.4 for '
          'Scala 2.13" means'),
         (1, '3.4.4', 'the version this step moves to')),
        ((0, 'org.apache.spark:spark-core_2.12',
          'the Scala 2.12 build of the same artifact, which resolves at 3.4.4 '
          'perfectly well and is not the move'),
         (1, HANDED_SPARK,
          'the version the tree is handed, which every tree already has'))),
    'spark-344-uniform': Shape(
        'pom.resolved', ('ga', 'version', 'scope'), 'absent', None, None,
        ((0, 'org.apache.spark:spark-sql_2.13',
          'the other org.apache.spark artifacts, which are what "and nothing '
          'else" is about'),
         (1, HANDED_SPARK,
          'a Spark version that is not 3.4.4, which is the thing this rule '
          'exists to find')),
        ((1, '3.4.4',
          'the version this step moves to; a rule that fires on it is red on '
          'the golden tree and on nothing else'),)),
    'scala-213': Shape(
        'pom.scalasuffix', ('suffix', 'count'), 'atleast', 1,
        'asks for %d where %d is the floor: a threshold of nothing is a rule '
        'no pom can fail, and one coordinate carrying the suffix is the whole '
        'of what this clause claims',
        ((0, '2.13', 'the Scala binary version this step moves to'),),
        ((0, HANDED_SCALA,
          'the suffix the tree is handed, which every tree already carries'),)),
    'scala-213-only': Shape(
        'pom.scalasuffix', ('suffix', 'count'), 'absent', None, None,
        ((0, HANDED_SCALA,
          'the suffix a half-moved pom is left with, which is the thing this '
          'rule exists to find'),),
        ((0, '2.13',
          'the suffix this step moves to; a rule that fires on it is red on '
          'the golden tree'),)),
    'classes-17': Shape(
        'bytecode.class', ('name', 'major'), 'atleast', 1,
        'asks for %d where %d is the floor: a threshold of nothing is met by '
        'a jar with no class of this repository in it at all',
        ((0, MAIN_CLASS, 'the classes of this repository in the built jar'),
         (0, ALT_CLASS,
          'a class of this repository in another of its nineteen packages: a '
          'name term scoped to one package asks about a fraction of the jar'),
         (1, '61', 'the class-file major version Java 17 produces')),
        ((0, OTHER_CLASS,
          'a class in a package this repository does not have, which --only '
          'admits as soon as a candidate adds one; a threshold that accepts it '
          'is cleared by compiling a single new file at 17 while every '
          'edu/sdsc/mmtf class stays at 52'),
         (1, HANDED_CLASS_MAJOR,
          'the major version the handed jar already carries at every one of '
          'its 201 classes'))),
    'classes-17-uniform': Shape(
        'bytecode.class', ('name', 'major'), 'absent', None, None,
        ((0, MAIN_CLASS, 'the classes of this repository in the built jar'),
         (0, ALT_CLASS,
          'a class of this repository in another of its nineteen packages: a '
          'name term scoped to one package leaves the rest of the jar free to '
          'stay at 52'),
         (1, HANDED_CLASS_MAJOR,
          'a major version that is not 61, which is the thing this rule exists '
          'to find')),
        ((1, '61',
          'the major version this step moves to; a rule that fires on it is '
          'red on the golden tree'),)),
    'csv-dates-stated': Shape(
        'ast.read', ('path', 'line', 'format', 'schema', 'options'),
        'absent', None, None,
        ((0, MAIN_READ, 'the reads under src/main, which are the ones this '
          'step is about'),
         (0, ALT_READ,
          'a read in another of this tree\'s source packages: the seventh csv '
          'read is under applications/, and a rule that cannot see a source '
          'package cannot say anything about what that package infers'),
         (2, 'csv', 'the format whose date inference changed at this rung'),
         (4, OPTS_INFERRING,
          'an inferring read as the handed tree writes it, which is what this '
          'rule has to fire on'),
         (4, OPTS_ADOPTED,
          'an inferring read that adopts 3.4\'s new default instead of '
          'pinning it off: it states the option and still lets '
          'depositionDate come back a date, which is the type change this '
          'step exists to prevent, so a rule that lets it through has let '
          'the work through')),
        ((0, TEST_READ,
          'the test tree, whose reads this step does not grade and whose files '
          'test.sh replaces with the commit\'s own before the suite runs'),
         (4, OPTS_STATED,
          'the same read once it pins date inference off, which is the answer '
          'this rule is asking for'),
         (4, OPTS_NONE,
          'a read that asks the engine for nothing at all, which infers '
          'nothing and has nothing to state'))),
    'csv-dates-pinned': Shape(
        'ast.read', ('path', 'line', 'format', 'schema', 'options'),
        'atleast', HANDED_INFERRING_READS,
        'asks for %d where %d is the floor: unlike the site count beside it '
        'this is not a number the handed tree already answers -- it pins none '
        'of them -- so the floor is the count of reads that have to end up '
        'pinned, and anything under it lets a tree pin all but one and still '
        'be read as having pinned them all',
        ((0, MAIN_READ, 'the reads under src/main, which are the ones this '
          'step is about'),
         (0, ALT_READ,
          'a read in another of this tree\'s source packages: the seventh csv '
          'read is under applications/, and a rule that cannot see a source '
          'package cannot count what that package pinned'),
         (2, 'csv', 'the format whose date inference changed at this rung'),
         (3, 'no',
          'a read that declares no schema, which is what each of the six '
          'inferring reads is and what each of them stays once pinned'),
         (4, OPTS_STATED,
          'an inferring read once it pins date inference off, which is the '
          'answer this rule is counting')),
        ((0, TEST_READ,
          'the test tree, whose own pinned reads would count toward a '
          'threshold about src/main, and whose files test.sh replaces with '
          'the commit\'s own before the suite runs'),
         (3, 'yes',
          'a read handed a declared StructType. The scanner sets `schema` to '
          '`yes` for any read whose chain carries a `.schema(` call, and '
          '`-csv-dates-stated` selects only reads that declare none, so such '
          'a read has already dropped out of the twin; a `-pinned` rule that '
          'counts it as pinned lets the declared-schema route meet this '
          'threshold too, and then both halves of the pair are green on a '
          'tree that pinned five reads and declared the sixth'),
         (4, OPTS_INFERRING,
          'an inferring read as the handed tree writes it: counting it would '
          'make this threshold the handed tree\'s own answer, met before the '
          'candidate does anything'),
         (4, OPTS_ADOPTED,
          'an inferring read that adopts 3.4\'s new default instead of '
          'pinning it off, which states the new behaviour rather than holding '
          'the old one and is what this step exists to refuse'),
         (4, OPTS_NONE,
          'a read that asks the engine for nothing at all, which infers '
          'nothing and has nothing to pin'))),
    'csv-sites-kept': Shape(
        'ast.read', ('path', 'line', 'format', 'schema', 'options'),
        'atleast', HANDED_CSV_READS,
        'asks for %d where the handed tree already answers %d, so it is met '
        'before the candidate does anything',
        ((0, MAIN_READ, 'the reads under src/main'),
         (0, ALT_READ, 'and the ones in its other source packages'),
         (2, 'csv', 'the format the clause above is about')),
        ((0, TEST_READ,
          'the test tree, whose own csv reads would count toward a threshold '
          'about src/main'),
         (2, 'parquet',
          'the other formats this tree reads, which are not the sites the '
          'clause above can be satisfied by deleting'),
         (2, 'json',
          'the other formats this tree reads, three of which would make up a '
          'threshold the csv reads no longer meet'))),
    'prefer-date-jar': Shape(
        'bytecode.strconst', ('value', 'count'), 'atleast', 1,
        'asks for %d where %d is the floor: a threshold of nothing is met by '
        'a jar whose constant pool never carries the option name at all',
        ((0, 'preferDate',
          'the option name whose presence in the constant pool is what "the '
          'statement reached the jar" means'),),
        ((0, 'inferSchema',
          'a string constant the handed jar already carries, so a rule that '
          'accepts it is met before the candidate does anything'),
         (0, 'header',
          'another string constant the handed jar already carries'))),
}

# Which conjunction each rule belongs to, and the order both are logged in.
RUNG_PARTS = (('spark-344', RUNG_SPARK),
              ('spark-344-uniform', RUNG_SPARK_ONLY),
              ('scala-213', RUNG_SCALA),
              ('scala-213-only', RUNG_SCALA_ONLY),
              ('classes-17', RUNG_CLASSES),
              ('classes-17-uniform', RUNG_CLASSES_ONLY))
READ_PARTS = (('csv-dates-stated', READ_DATES),
              ('csv-dates-pinned', READ_PINNED),
              ('csv-sites-kept', READ_SITES),
              ('prefer-date-jar', READ_JAR))

# What each conjunction would be if the rule file stopped naming one of its
# parts: the tree that would then open the milestone, in one line each. These
# are the messages `read_rule_ids` raises with, kept here so that the reason a
# part is required sits beside the part rather than inside a format string.
PART_WHY = {
    'spark-344':
        'a tree that never resolved Spark 3.4.4 for Scala 2.13 would be read '
        'as having moved to it',
    'spark-344-uniform':
        'a pom that resolves two Spark releases at once would open it, and '
        'which one the classpath orders first is not a property of the pom',
    'scala-213':
        'a tree that never adopted the _2.13 suffix would open it',
    'scala-213-only':
        'a pom carrying two Scala binary versions would open it, which is a '
        'link error waiting for the first call that crosses between them',
    'classes-17':
        'a pom that asks for Java 17 and a jar that was compiled at 52 anyway '
        'would open it, and the jar is the thing the replay runs',
    'classes-17-uniform':
        'a jar half at 61 and half at 52 would open it, which is a build that '
        'compiled part of the tree with something else',
    'csv-dates-stated':
        'a tree that left every inferring csv read silent about dates would '
        'open it, which is the whole of the work this step asks for',
    'csv-dates-pinned':
        'a tree that answered the clause above by taking the question away '
        'from a read -- declaring a StructType at the site, or dropping the '
        'inferSchema option -- would open it. `-csv-dates-stated` cannot see '
        'that read: the scanner marks a chain carrying a `.schema(` call as '
        'declaring one, and an `absent` rule does not fire on a row it no '
        'longer selects, so the count is the only half of the pair that '
        'notices',
    'csv-sites-kept':
        'deleting the %d inferring reads would satisfy the clause above for '
        'free, and a deleted read is the failure this task exists to catch'
        % HANDED_INFERRING_READS,
    'prefer-date-jar':
        'a source edit that never compiled -- or that was made in a file the '
        'build does not reach -- would count as one, and the replay runs the '
        'jar',
}


def shape_why(shape, mode, sel):
    """Why `sel`/`mode` cannot be this rule, or None when they can.

    The mode first, then the fact, then the arity, then each sample value in
    the order the shape lists them. The order matters only for the message: a
    reader wants the first thing wrong, and the first thing wrong should be the
    coarsest.
    """
    if shape.mode == 'absent':
        if mode != 'absent':
            return ('is `%s`, not `absent`; the milestone reads it as "no row '
                    'like this exists", and a counted rule says something '
                    'weaker' % mode)
    else:
        if not mode.startswith('atleast:'):
            return ('is `%s`, not `atleast:N`; the milestone reads it as "at '
                    'least this many rows like this exist"' % mode)
        try:
            n = int(mode.split(':', 1)[1])
        except ValueError:
            return 'is `%s`, whose threshold is not a number' % mode
        if n < shape.least:
            return shape.least_why % (n, shape.least)
    fields = sel.split(SELECTOR_SEP)
    if fields[0] != shape.fact:
        return ('reads `%s` rather than `%s`, which is the fact this part of '
                'the milestone is about' % (fields[0], shape.fact))
    terms = fields[1:]
    if len(terms) != len(shape.fields):
        return ('gives %d field term(s) where a `%s` row has %d -- %s'
                % (len(terms), shape.fact, len(shape.fields),
                   ', '.join(shape.fields)))
    for i, value, why in shape.selects:
        if not selector_field(terms[i], value):
            return ('its `%s` term %r does not select %r: %s'
                    % (shape.fields[i], terms[i], value, why))
    for i, value, why in shape.rejects:
        if selector_field(terms[i], value):
            return ('its `%s` term %r also selects %r: %s'
                    % (shape.fields[i], terms[i], value, why))
    return None


def read_rule_ids(tests):
    """(rung, reads): the m05.tsv ids the milestone reads, by name.

    Not two lists of ids kept in this file. The milestone and the m1, m2 and m3
    requirements have to mean the same thing by "this tree moved to Spark 3.4.4
    on Scala 2.13 and Java 17 and said what its csv reads want inferred", and
    two lists in two files mean the same thing right up until one of them is
    edited. So all ten are read out of the rule file the gate is composed
    from, matched by the patterns above, and every one of them is required:
    `PART_WHY` says, part by part, which tree would open the milestone if the
    file stopped naming it.

    Each is then checked for the shape that makes it say what the milestone
    reads it as saying, and the reason for checking rather than trusting is the
    same in all ten cases: every refused shape is a rule that loads, runs, and
    is green on a tree that did none of this step's work. A `-csv-dates-stated`
    rule that reads a different fact, or that is scoped to a tree the candidate
    does not write in, is an `absent` rule over no rows, and an `absent` rule
    over no rows passes. A `-csv-sites-kept` rule whose threshold is below the
    seven sites the handed tree already has is met by doing nothing. A
    `-classes-17` rule whose major term accepts 52 is green on a jar compiled
    by JDK 8. A `-prefer-date-jar` rule whose value term accepts `inferSchema`
    is green on a jar that carries no `preferDate` at all. A
    `-csv-dates-pinned` rule whose options term accepts a read that only asks
    for inference counts the handed tree's own six; one whose threshold is
    under six is green on a tree that pinned all but one; and one whose
    `schema` term also accepts `yes` is green on a tree that pinned five and
    gave the sixth a declared StructType, which is exactly the tree its
    `absent` twin cannot see. In each case the gate would still run, and would
    open on a tree that did not do this step's work. Failing here instead is
    the whole point of reading the file rather than trusting it.

    What is *not* checked is spelling. `(.*/)?src/main/.*` and
    `/app/src/main/.*` are the same rule as far as this is concerned, and so
    are `3\\.4\\.4` and `3[.]4[.]4`: the checks are sample values tried against
    the term, so a rule written another way passes as long as it selects what
    the milestone reads it as selecting and does not select what it reads it as
    excluding. The selftest has a case for exactly that, because a shape check
    that only accepts the rule its author happened to write is a copy of that
    rule with extra steps.
    """
    path = os.path.join(tests, 'gates', 'rules', 'm05.tsv')
    found = {}
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if len(f) != 5:
                raise ValueError('%s:%d: want 5 fields, got %d'
                                 % (path, n, len(f)))
            rid, mode, sel = f[0], f[1], f[2]
            for label, pattern in RUNG_PARTS + READ_PARTS:
                if not pattern.match(rid):
                    continue
                why = shape_why(SHAPES[label], mode, sel)
                if why:
                    raise ValueError('%s:%d: %s %s -- and a rule that does is '
                                     'green on the tree this step is handed, '
                                     'so the milestone would open on a tree '
                                     'that did none of this step\'s work'
                                     % (path, n, rid, why))
                found[label] = rid
    for label, _pattern in RUNG_PARTS + READ_PARTS:
        if label not in found:
            raise ValueError('%s names no `m<digit>-%s` rule, so that part of '
                             'the milestone is vacuously true: %s'
                             % (path, label, PART_WHY[label]))
    return ([found[label] for label, _p in RUNG_PARTS],
            [found[label] for label, _p in READ_PARTS])


def moved(run, tests, findings, log):
    """True when the tree made this step's move: the rung and the reads.

    Two conjunctions, and the conjunction of the two is the point.

    `rung_adopted` is what the replay is a replay *of*. `spark-core` is
    `provided` in this pom, which means the replay runs on the verifier image's
    Spark and on its JVM whatever the candidate's pom resolves. Every recorded
    behaviour change at this rung is therefore produced by the image: the
    JDK-17 helpful-NPE text HarnessDriver-rowtypes prints, the status
    DatasetClassifier exits with. A tree that edits nothing reproduces 140 of
    the 141 recordings, and if the behaviour family were ungraded it would be
    paid 0.248643 -- 0.239357 of the 0.24 of replay and the 0.009286 its
    unopened test JVM leaves of the 0.10 -- for having been handed a newer
    image. That replay figure is not 140/141 of the 0.24: a job is weighted by
    how many scored inputs it has, 249 across the census, and credited with the
    fraction of its recorded blocks that match, and the job that does move
    still matches two of its three blocks, so the untouched tree's share is
    (247 + 2 x 2/3)/249. So the replay is read as evidence only about a tree
    that moved to
    the runtime the recordings were made on -- and the rung half is checked in
    the pom *and* in the jar, because a pom can ask for Java 17 and a plugin
    can override it, and a build that resolves 3.4.4 for the wrong Scala suffix
    resolves perfectly well.

    `reads_stated` is the work. It is four rules rather than its first one
    alone for a reason that is easy to miss: `m3-csv-dates-stated` is an
    `absent` rule, and an `absent` rule is green over zero rows. A candidate who
    deletes the six inferring reads passes it, loses the replay, and would
    otherwise have been read as having done the work. Deleting is not the only
    way to vacate it, either: the scanner marks any read whose chain carries a
    `.schema(` call as declaring a schema and the rule selects only reads that
    declare none, so handing a read a declared StructType drops it out of the
    selection exactly as deleting it would, and so does dropping its
    `inferSchema` option. `m3-csv-dates-pinned` is the half that notices --
    it counts the reads that both ask the engine to infer and pin date
    inference off, and requires six of them. Say exactly what that is, because
    it is easy to read as more: a floor over a count, not a bind on an
    identity. The floor closes the vacancy -- a tree that declares its way out
    and plants nothing leaves this count at zero and goes red here, which is
    what round 2 found missing. What it does not do is fix which six reads are
    counted, and it cannot: AstScan.java records `schema=yes` for any
    `.schema(` call whatever the argument, so "is this one of the six the tree
    was handed" is not a fact this gate has. Six declared originals standing
    beside six planted reads that ask and pin would meet this count. That hole
    is open and written down (NOTES 6.5.9) rather than repaired in the last
    fix round; what still answers it is the replay, which runs the handed
    paths and not the plant. Neither half says the whole sentence
    alone: `-stated` says no inferring read is left unpinned, which is true of
    a tree with no inferring reads left, and `-pinned` says six reads pin it
    off, which is true of a tree that pins six and leaves a seventh silent
    beside them. Together they say six pinned and none left unpinned, which is
    the clause. `m3-csv-sites-kept` is what stops "no unstated read" from
    meaning "no read", and `m3-prefer-date-jar` is what stops a source edit
    that never compiled -- or that was made in a file the build does not reach
    -- from counting as one. The replay's 141 jobs run the jar, not the
    sources.

    What is deliberately *not* in this gate is `m2-test-jvm-opens`. Omitting
    `--add-opens=java.base/sun.nio.ch=ALL-UNNAMED` is already priced twice: it
    is a rule inside m2, which is all-or-nothing and worth 0.18, and it costs
    the `tests` family almost all of its 0.10: that family's score falls from
    1.000000 to 0.092857, because only 13 of the 140 graded tests get through
    without a session. Gating the other 0.24 on it as well would charge a
    third time for one omission. The line between the two halves
    above and that rule is not "how important is it" -- it is whether the
    behaviour measurement means anything without it, and the replay's 141
    recordings are as meaningful with the test JVM closed as with it open.

    What this charges a good solution that is not this one is written down
    rather than discovered. A candidate who moves the rung completely and pins
    five of the six inferring reads scores 0.30: m3 is all-or-nothing so they
    lose 0.36, and `reads_stated` is not met so they lose the behaviour family
    too. That is the number instruction.md promises, and with
    `m3-csv-dates-pinned` in this conjunction it is the number this function
    produces -- the count is read here and not only inside m3, so the sixth
    read cannot be answered by taking the question away from that read. It can
    still be answered by planting a seventh read that asks and pins, which a
    count cannot tell from the sixth; that is the open half above. That is a
    cliff, it is the same cliff step 4 disclosed, and the handoff says so in
    those words -- section 3, "the step is the rung *and* the reads".
    instruction.md states the same cliff in its own words rather than in
    these. A step is made or it is not. There is no cliff for anybody who
    finished: a candidate who pins all six and gets one demo's output wrong
    keeps every point of proportional behaviour credit and loses only whatever
    the replay says about that demo.

    Which answer the reads were stated *for* is part of what this proves, and
    that is why the reads are carried by a pair of rules rather than one --
    and why this gate reads the pair rather than the `absent` half alone.
    `m3-csv-dates-stated` excludes any inferring read that does not pin date
    inference off, and `m3-csv-dates-pinned` requires six that do, so
    `preferDate=false` is the only spelling that satisfies both. `true` is not
    a second defensible answer here: it adopts 3.4's new default, and that
    default is the thing this step is asked to hold constant. Measured on the
    two rung images, `depositionDate` infers TimestampType at 3.3.4 with or
    without the option (mmtf-rung-r05:1), and at 3.4.4 infers DateType by
    default and TimestampType only where the site pins `preferDate=false`
    (mmtf-rung-r09:1). The requirement is that the readers keep producing the
    types they produced before the move, not merely that the file stops being
    silent.

    The replay reads that value back rather than taking it on trust.
    `HarnessDriver-csvdates` prints the inferred type of every field of
    `CustomReportService.getDataset("pmc", "pubmedId", "depositionDate")`, and
    of two more of the repository's own inferring readers, through those
    readers and not through a reader the harness built. A tree that leaves the
    six reads silent -- or that writes `preferDate=true` -- prints `date`
    where the reference prints `timestamp`, and loses that job. It is the one
    recording of the 141 that carries a date-shaped column, and carrying one
    is what it is for: before it was planned, `depositionDate`, `releaseDate`,
    `DateType` and `TimestampType` appeared in zero `.norm` files, because the
    rest of the census reaches `CustomReportService` only for a seven-field
    report schema and the web-service readers do not run with the network off
    (NOTES 6.5.1). The pricing stays where rule 8 asks for it: the gate rules
    price the statement at the site, and the replay prices, proportionally and
    per job, the type the statement actually produces.

    `run` is unused: every part is read out of the gate report in `findings`,
    where the scanners' facts have already been matched against the rules. This
    function reads no file of the replay's own.
    """
    del run  # every part is read out of the gate report; see the docstring
    rung, reads = read_rule_ids(tests)
    by_id = dict((r[1], r) for r in findings)

    every = rung + reads
    missing = [rid for rid in every if rid not in by_id]
    if missing:
        raise ValueError('the gate report does not mention %s, so %s, and '
                         'reading it as passed would open the milestone on a '
                         'tree nothing was measured against'
                         % (' '.join(missing),
                            'part of this milestone never ran'
                            if len(missing) < len(every)
                            else 'no part of this milestone ran at all'))

    short = [rid for rid in rung if by_id[rid][0] != 'PASS']
    if short:
        log('behav  NO       this tree is not on the runtime the recordings '
            'were made on: %d of %d rule(s) that say it is did not pass (%s). '
            'spark-core is `provided`, so the replay ran on the image\'s Spark '
            'and JVM either way -- matching a recording made on a runtime you '
            'did not adopt is evidence about the image, not about the tree'
            % (len(short), len(rung), ' '.join(short)))
        return False
    log('behav  RUNG     all %d rule(s) agree the build resolves Spark 3.4.4 '
        'for Scala 2.13 and the jar is class-file major 61: %s'
        % (len(rung), ' '.join(rung)))

    silent = [rid for rid in reads if by_id[rid][0] != 'PASS']
    if silent:
        log('behav  NO       %d rule(s) ask whether this tree\'s csv reads say '
            'what they want inferred, with the sites still there and the '
            'statement in the jar, and did not pass (%s): the rung moved and '
            'the work did not'
            % (len(silent), ' '.join(silent)))
        return False
    log('behav  READS    every inferring csv read under src/main states its '
        'date handling, the %d sites are there, and the statement reached the '
        'jar: %s'
        % (HANDED_CSV_READS,
           ' '.join('%s (%s row(s))' % (rid, by_id[rid][2]) for rid in reads)))
    return True

# --- putting it together ------------------------------------------------------

def preflight(run, log):
    """ok/fail lines test.sh wrote before it measured anything."""
    path = os.path.join(run, 'preflight.tsv')
    if not os.path.exists(path):
        return ['preflight.tsv was never written']
    bad = []
    for line in open(path, errors='replace'):
        if not line.strip():
            continue
        f = line.rstrip('\n').split('\t')
        log('pre    %-6s %-22s %s' % (f[0], f[1], f[2] if len(f) > 2 else ''))
        if f[0] != 'ok':
            bad.append('%s: %s' % (f[1], f[2] if len(f) > 2 else ''))
    return bad


def stamp(tests, log):
    """Say which step's payload this is, as the first line of every score.log.

    Trap 2: the number a step-scoped verifier produces for the wrong step is
    arithmetically correct and about the wrong tree, and nothing in a job
    directory shows it. The payload carries step.tsv, the manifest covers it,
    and check-job.sh compares this line against the step directory's own name.
    """
    path = os.path.join(tests, 'step.tsv')
    fields = {}
    if os.path.exists(path):
        for line in open(path, errors='replace'):
            f = line.rstrip('\n').split('\t')
            if len(f) >= 2:
                fields[f[0]] = f[1]
    log('payload step %s, census %s'
        % (fields.get('step', '(unstamped)'), fields.get('census', '(unstamped)')))
    return fields


def run_score(tests, run, expect_classes, expect_tests, log):
    stamp(tests, log)
    weights = load_weights(os.path.join(tests, 'weights.tsv'))
    # Read before anything is measured, so a payload whose cheats.tsv is
    # missing or malformed is a crash rather than a run in which nothing
    # happens to be prohibited.
    cheat_ids = cheat_rule_ids(tests)

    broken = preflight(run, log)
    if broken:
        log('')
        log('VERIFIER PRECONDITION FAILED -- %d check(s) did not hold:' % len(broken))
        for b in broken:
            log('    %s' % b)
        log('Nothing measured after a failed precondition is evidence about the')
        log('candidate, so the reward is 0 and this run should be re-read as a')
        log('broken verifier rather than as a red tree.')
        return 0.0, {}

    parts = {}

    mine = read_findings(os.path.join(run, 'gate', 'findings.root.tsv'))
    theirs_path = os.path.join(run, 'gate', 'findings.tsv')
    tamper = None
    if not os.path.exists(theirs_path):
        # gate-run.sh always writes findings.tsv -- a scanner that died becomes
        # an `error` fact and a broken rules file an ERROR row -- so a run with
        # none is a gate that never ran or whose output was removed. Skipping
        # the comparison then would turn the one consistency signal into a
        # no-op, so it voids the family like a disagreement does (2026-10-12
        # reward-hacking audit, F3).
        tamper = 'gate-run.sh left no findings.tsv to compare with the root re-evaluation'
    if os.path.exists(theirs_path):
        theirs = read_findings(theirs_path)
        if theirs != mine:
            only = [r for r in theirs if r not in mine][:4]
            tamper = ('the gate evaluated as nobody and the gate re-evaluated as '
                      'root disagree on %d row(s): %s'
                      % (len([r for r in theirs if r not in mine]),
                         ' '.join('%s/%s' % (r[0], r[1]) for r in only)))
    credit, standing_red = gate_scores(mine, weights['migrated'], log)
    void = None
    if standing_red:
        void = ('%d standing rule(s) red: %s'
                % (len(standing_red), ' '.join(r[1] for r in standing_red)))
    if tamper:
        void = tamper if void is None else void + '; ' + tamper
    for key, (w, _what) in weights['migrated'].items():
        parts[key] = 0.0 if void else w * credit[key]
    if void:
        log('')
        log('MIGRATED-NESS VOID -- %s' % void)
        # Counted off the report rather than written out. The same sentence was
        # wrong twice -- once when round 1 of the item-5 review moved five rules
        # out of the requirements and once when round 2 added two prohibitions
        # -- and a verifier that miscounts its own gate in the log a candidate
        # reads is an instrument disagreeing with itself.
        n_all = len(mine)
        n_req = len([r for r in mine if re.match(r'^m\d+-', r[1])])
        n_held = len([r for r in mine if r[1].startswith('held-')])
        log('%d of this gate\'s %d rules are standing -- common.tsv\'s floor, the'
            % (n_all - n_req, n_all))
        log('%d held from steps 1 to 4, the %d prohibitions in the four cheats'
            % (n_held, len(cheat_ids)))
        log('files, and rung3.tsv\'s engine facts. They are preconditions')
        log('for reading the %d requirement rules above them, and the tree Harbor'
            % n_req)
        log('hands this step passes all of them. A red one says the reading is void, not that the candidate is')
        log('wrong: the mirror fell over, or an instrument did not run, or the')
        log('work of an earlier step was undone. So this family scores 0 and the')
        log('behavior family below still stands -- unless the red one came from a')
        log('cheats file, which is the single exception and is reported under its')
        log('own heading below.')

    # Both behavior instruments are run and logged whatever the answer below
    # is: the evidence for what this tree did to the 141 recordings is worth
    # having even when it earns nothing, and a reader who cannot see it cannot
    # tell a zero that was measured from a zero that was assumed. At this step
    # that is not a nicety -- a tree that moved nothing reproduces 140 of them,
    # the line below the milestone reads is the only place the two zeroes look
    # different.
    replay, _counts = replay_score(tests, run, log)
    tscore = tests_score(run, expect_classes, expect_tests, log)
    graded = moved(run, tests, mine, log)
    parts['replay'] = weights['behavior']['replay'][0] * (replay if graded else 0.0)
    parts['tests'] = weights['behavior']['tests'][0] * (tscore if graded else 0.0)
    if not graded:
        log('')
        log('BEHAVIOR NOT GRADED -- this tree is not on Spark 3.4.4 for Scala')
        log('2.13 with a jar compiled for Java 17, or its csv reads still ask the')
        log('engine to infer a schema without saying how date-shaped columns are')
        log('to be inferred, so it has not made the move this family asks its')
        log('question about. spark-core is `provided`: the replay ran on this')
        log('image\'s Spark and JVM whatever the pom says, so a tree that changed')
        log('nothing reproduces every recording. Matching a recording made on a')
        log('runtime you did not adopt is evidence about the image, not about the')
        log('tree. The two measurements above stand as evidence and')
        log('score 0: replay %.6f, tests %.6f would have been worth %.6f together'
            % (replay, tscore,
               weights['behavior']['replay'][0] * replay
               + weights['behavior']['tests'][0] * tscore))
        log('on a tree that made it.')

    # The one place a standing rule costs more than its own family. Everything
    # above is still measured and still logged, because a reader needs to see
    # what the tree did as well as what it was caught doing.
    cheated = cheat_red(mine, cheat_ids)
    if cheated:
        log('')
        log('FAKED -- %d of the %d rules in gates/rules/%s did not pass:'
            % (len(cheated), len(cheat_ids), ' and '.join(CHEAT_FILES)))
        for status, rid, count, reason in cheated:
            log('    %-8s %-28s %s (%d match(es))' % (status, rid, reason, count))
        log('Those rules are the ways this step can be made to look done without')
        log('being done: an opaque encoder carrying rows the engine never reads as')
        log('columns, an execution id set by hand or forged outright, the')
        log('reflective spellings of both, a spark.sql.legacy.* switch that asks')
        log('the engine to keep answering the way an older one did, and -- new at')
        log('this step -- a pom that tells surefire not to run the suite. JDK 17')
        log('breaks the forked test JVM until the pom opens java.base/sun.nio.ch,')
        log('and skipping the tests makes that breakage disappear without moving')
        log('anything. A tree that did one of these did not migrate anything, and')
        log('its recordings match because nothing moved -- so the reward is 0,')
        log('behavior included, which is what the instruction says in as many')
        log('words.')
        for key in parts:
            parts[key] = 0.0

    return product_reward(weights, parts, log), parts


def product_reward(weights, parts, log):
    """reward = T1 x T2 (user decision 2026-10-13: product scoring).

    T1, migrated-ness, is the `migrated` family and T2, preservation, is the
    `behavior` family (the replay and the repository's own tests). Each factor
    is its family's points over its family's total weight in weights.tsv, so
    the relative weights inside a family stand and each factor is in [0, 1]
    with its partial credit intact. The two families used to be added; now a
    tree that keeps every recording but made none of this step's move scores
    T1 = 0 and so 0, and so does a tree that moved and broke everything.

    Every zero `parts` already carries is a zero factor here: a void migrated
    family (a standing rule red, or the two gate readings disagreeing), a
    behavior family left ungraded by its condition, and a faked step, which
    zeroes every part. So each of those is still a zero reward.
    """
    def factor(fam):
        total = sum(w for w, _what in weights[fam].values())
        got = sum(parts.get(key, 0.0) for key in weights[fam])
        return (got / total) if total > 0 else 0.0
    t1, t2 = factor('migrated'), factor('behavior')
    log('')
    log('product T1 migrated %.6f x T2 behavior %.6f = %.6f' % (t1, t2, t1 * t2))
    return t1 * t2


def selftest():
    """The arithmetic against known answers, including the ones that must be 0.

    A scorer that returned 1.0 for everything would pass every positive case
    a run can produce, so the cases that must come back zero -- and the ones
    that must come back partial -- are what this is for (trap 6).
    """
    bad = []
    ran = []

    def want(got, expected, why):
        ran.append(why)
        if got != expected:
            bad.append('%s: got %r want %r' % (why, got, expected))

    @contextlib.contextmanager
    def must_raise(why):
        """A case whose answer is a refusal: the body has to raise ValueError.

        Counted like any other case, because a guard that stopped guarding --
        a rule file this file no longer parses strictly, a cheats.tsv it no
        longer requires to name anything -- would otherwise fail silently.
        """
        ran.append(why)
        try:
            yield
        except ValueError:
            return
        bad.append(why)

    P = re.compile(r'^(adv|expect) [^:]*: '), re.compile(r'^(filter|webfilters) [^:]*: n=')

    # One label, one block; a preamble is its own block.
    b = blocks('corpus=82\nadv a: n=1\n  1AAA\nadv b: n=0\n', P)
    want([k for k, _ in b], ['(head)', 'adv a', 'adv b'], 'blocks split at labels')
    want(b[1][1], 'adv a: n=1\n  1AAA', 'a block carries the lines under its label')
    want(blocks('', P), [('(head)', '')], 'an empty recording is one empty block')
    want(blocks('one\ntwo\n', P), [('(head)', 'one\ntwo')],
         'a recording with no labels is one block')
    want(list(keyed(blocks('adv a: n=1\nadv a: n=2\n', P))), ['adv a', 'adv a#2'],
         'a repeated label is disambiguated by occurrence')

    # Credit is per block: one of three wrong is two thirds.
    ref = blocks('h\nadv a: 1\nadv b: 2\n', P)
    got = blocks('h\nadv a: 1\nadv b: 9\n', P)
    rk, gk = keyed(ref), keyed(got)
    want(sum(1 for k, v in rk.items() if gk.get(k) == v), 2, 'two of three blocks agree')

    # A block the candidate never printed is a miss, not a skip.
    gk2 = keyed(blocks('h\nadv a: 1\n', P))
    want(sum(1 for k, v in rk.items() if gk2.get(k) == v), 2,
         'a missing block scores nothing and does not shift the others')

    # weights.tsv arithmetic.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'w.tsv')
        open(p, 'w').write('a\tx\t0.5\twhat\nb\ty\t0.5\twhat\n')
        want(sorted(load_weights(p)), ['a', 'b'], 'weights load by family')
        open(p, 'w').write('a\tx\t0.5\twhat\nb\ty\t0.4\twhat\n')
        with must_raise('weights summing to 0.9 were accepted'):
            load_weights(p)

    # The gate: all or nothing per requirement, and a standing red is void.
    fnd = [('PASS', 'ast-parses', 0, 'r'), ('PASS', 'm1-a', 0, 'r'),
           ('FAIL', 'm1-b', 3, 'r'), ('PASS', 'm2-a', 1, 'r')]
    w = {'m1': (0.5, ''), 'm2': (0.5, '')}
    credit, red = gate_scores(fnd, w, lambda s: None)
    want((credit['m1'], credit['m2'], red), (0.0, 1.0, []),
         'one red rule zeroes its requirement and leaves the other alone')
    credit, red = gate_scores(
        [('FAIL', 'ast-parses', 1, 'r'), ('PASS', 'm1-a', 0, 'r'), ('PASS', 'm2-a', 0, 'r')],
        w, lambda s: None)
    want([r[1] for r in red], ['ast-parses'], 'a standing red is reported as void')

    with must_raise('a requirement with no weight was accepted'):
        gate_scores([('PASS', 'm9-a', 0, 'r')], w, lambda s: None)

    # The unscored-status list. It is three lines of code and it removes a cost
    # from the behavior family, so the cases that matter are the ones where it
    # must refuse: a job that is not in the plan, a row with no reason, and a
    # duplicate. A list that silently accepted a typo'd job name would exempt
    # nothing and read exactly like a list that exempted the right thing.
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'u.tsv')
        open(p, 'w').write('# c\n\nJobA\tbecause\n')
        want(load_unscored_status(p, {'JobA', 'JobB'}), {'JobA': 'because'},
             'an unscored-status row loads as job -> reason')
        want(load_unscored_status(p, {'JobA'}), {'JobA': 'because'},
             'comments and blank lines are skipped')
        for body, why in (('JobZ\tbecause\n', 'a job outside the plan'),
                          ('JobA\t\n', 'a row with an empty reason'),
                          ('JobA\n', 'a row with one field'),
                          ('JobA\tx\nJobA\ty\n', 'a job listed twice')):
            open(p, 'w').write(body)
            with must_raise('unscored-status accepted %s' % why):
                load_unscored_status(p, {'JobA', 'JobB'})

    # tests_score's exemption, which at this step has two accepted shapes and
    # is arithmetic on a number the oracle's own reward depends on. Trap 6: the
    # exemption is the instrument here, so it is checked before it is trusted,
    # on reports built to be exactly the shapes that matter.
    with tempfile.TemporaryDirectory() as d:
        quiet = lambda s: None
        sf = os.path.join(d, 'surefire')
        os.makedirs(sf)

        def report(name, tests, failures=0, errors=0, skipped=0):
            open(os.path.join(sf, 'TEST-%s.xml' % name), 'w').write(
                '<?xml version="1.0" encoding="UTF-8"?>\n'
                '<testsuite name="%s" tests="%d" errors="%d" skipped="%d" '
                'failures="%d"/>\n' % (name, tests, errors, skipped, failures))

        # Forty-six green classes and a hundred and forty green tests, plus the
        # exempt class red exactly the way this rung leaves it. That is the
        # oracle's shape, and it has to be 1.0 rather than 140/142.
        for i in range(46):
            report('edu.sdsc.mmtf.spark.T%02d' % i, 3 if i < 2 else 3)
        # 46 classes at 3 tests each is 138; two more on one of them makes 140.
        report('edu.sdsc.mmtf.spark.T00', 5)
        report(EXEMPT_CLASS, 2, failures=2)
        want(tests_score(d, 46, 140, quiet), 1.0,
             'the exempt class red 2/2 leaves the oracle at 1.0, not 140/142')

        # The other accepted shape: the same class on a tree whose pom never
        # opened the test JVM. Four entries, none of them a failure, all four
        # errors -- each of the two tests reported once for the
        # IllegalAccessError its @Before threw and once for the NPE its @After
        # threw on a context that was never assigned. It scores the same,
        # because what it says about the tree is already said by
        # m2-test-jvm-opens and by the 13 session-bearing tests that fail in
        # the other 46 classes.
        report(EXEMPT_CLASS, 4, errors=4)
        want(tests_score(d, 46, 140, quiet), 1.0,
             'the unopened-JVM shape of the exempt class is exempt too, and '
             'is not a third price for one omission')
        seen = []
        tests_score(d, 46, 140, seen.append)
        want([l.split('--')[1].strip() for l in seen
              if l.startswith('tests  EXEMPT')],
             ['outside both baselines at this rung (the test JVM was not '
              'opened onto java.base/sun.nio.ch)'],
             'the log says which of the three shapes this run produced')

        # The third accepted shape: the tree a candidate who changed nothing
        # arrives with. It is still the 2.4 pin, the session starts, and the
        # JPMS denial lands in the two test bodies rather than in the @Before
        # -- two tests, two errors. Refusing it made test.sh void the nop, and
        # a void is not the 0.00 the nop exists to demonstrate (NOTES 6.5.10).
        report(EXEMPT_CLASS, 2, errors=2)
        seen = []
        want(tests_score(d, 46, 140, seen.append), 1.0,
             'the 2.4-pin shape of the exempt class is exempt too')
        want(len([l for l in seen if 'overlay-control' in l]), 0,
             'the 2.4-pin shape is recognised rather than sent to the control')
        want([l.split('--')[1].strip() for l in seen
              if l.startswith('tests  EXEMPT')],
             ['outside both baselines at this rung (the test JVM was not '
              'opened, and this tree is far enough behind that the denial '
              'lands in the test bodies)'],
             'the log distinguishes the 2.4-pin shape from the unopened one')

        # A fourth shape is none of them: something other than the open broke
        # this class, the run is not the run the baselines were measured on,
        # and test.sh's overlay-control is what refuses it. The arithmetic
        # still leaves the class out -- scoring it here would be a second lever
        # on one decision -- but the log says the control is what should have
        # caught it.
        report(EXEMPT_CLASS, 2, skipped=2)
        seen = []
        want(tests_score(d, 46, 140, seen.append), 1.0,
             'an unrecognised shape of the exempt class is still not scored')
        want(len([l for l in seen if 'overlay-control' in l]), 1,
             'an unrecognised shape says which precondition refuses the run')
        report(EXEMPT_CLASS, 2, failures=2)

        # And it is an exemption of that class, not of two failures: the same
        # two reds anywhere else still cost.
        report('edu.sdsc.mmtf.spark.T01', 3, failures=2)
        want(tests_score(d, 46, 140, quiet), 138 / 140.0,
             'two reds in a class that is not exempt still cost the tree')
        report('edu.sdsc.mmtf.spark.T01', 3)

        # A green exempt class is not a bonus either -- it is still outside
        # both baselines, so the score does not move. What refuses that run is
        # test.sh's overlay-control precondition, which is a different lever on
        # purpose: an unexpected pass means the overlay or the rung is not the
        # one these numbers were measured on, and that voids rather than pays.
        report(EXEMPT_CLASS, 2)
        want(tests_score(d, 46, 140, quiet), 1.0,
             'the exempt class passing does not pay either')

        # The class count excludes it too, so a correct run does not log SHRANK.
        seen = []
        tests_score(d, 46, 140, seen.append)
        want([l for l in seen if 'SHRANK' in l], [],
             'a correct run does not report its own suite as having shrunk')
        want(len([l for l in seen if l.startswith('tests  EXEMPT')]), 1,
             'the exempt class is reported rather than silently dropped')

    # `moved`, and its half of the pricing. Every case here that must come back
    # False is a way the tree Harbor hands this step could have collected
    # 0.248643 of the reward for changing nothing -- which here is not a
    # hypothetical, because `spark-core` is `provided` and an untouched tree
    # reproduces 140 of the 141 recordings on the image's own Spark. The ones
    # that must raise are ways a later edit to m05.tsv could have made the
    # milestone unconditional without anything going red.
    with tempfile.TemporaryDirectory() as d:
        quiet = lambda s: None
        rules = os.path.join(d, 'gates', 'rules')
        os.makedirs(rules)
        m05 = os.path.join(rules, 'm05.tsv')

        def rule(rid, mode, *terms):
            return '%s\t%s\t%s\tc.tsv\twhy\n' % (rid, mode, '\\t'.join(terms))

        def rules_file(*lines):
            open(m05, 'w').write('# a comment\n\n' + ''.join(lines))

        # The shape m05.tsv actually has: six rules for the rung -- the pom's
        # resolved Spark version and Scala suffix, each with the `and nothing
        # else` half that a half-moved pom fails, and the jar's class-file
        # major with its own -- and four for the reads.
        R = {
            'spark': rule('m1-spark-344', 'atleast:1', 'pom.resolved',
                          'org\\.apache\\.spark:spark-core_2\\.13',
                          '3\\.4\\.4', '.+'),
            'sparkU': rule('m1-spark-344-uniform', 'absent', 'pom.resolved',
                           'org\\.apache\\.spark:.*', '(?!3\\.4\\.4$).*', '*'),
            'scala': rule('m1-scala-213', 'atleast:1', 'pom.scalasuffix',
                          '2\\.13', '.+'),
            'scalaU': rule('m1-scala-213-only', 'absent', 'pom.scalasuffix',
                           '(?!2\\.13$).+', '*'),
            'cls': rule('m2-classes-17', 'atleast:1', 'bytecode.class',
                        'edu/sdsc/mmtf/.*', '61'),
            'clsU': rule('m2-classes-17-uniform', 'absent', 'bytecode.class',
                         'edu/sdsc/mmtf/.*', '(?!61$).*'),
            'dates': rule('m3-csv-dates-stated', 'absent', 'ast.read',
                          '(.*/)?src/main/.*', '*', 'csv', 'no',
                          '(?!.*preferDate=false).*'
                          'inferSchema=(true|\\?).*'),
            'pinned': rule('m3-csv-dates-pinned', 'atleast:6', 'ast.read',
                           '(.*/)?src/main/.*', '*', 'csv', 'no',
                           '(?=.*inferSchema=(true|\\?))'
                           '(?=.*preferDate=false).*'),
            'sites': rule('m3-csv-sites-kept', 'atleast:7', 'ast.read',
                          '(.*/)?src/main/.*', '*', 'csv', '*', '*'),
            'jar': rule('m3-prefer-date-jar', 'atleast:1',
                        'bytecode.strconst', 'preferDate', '*'),
        }
        # Four rules of this step the milestone must ignore. m2-jdk-level-17
        # and m2-test-jvm-opens are m2's other two, and the second of them is
        # deliberately outside the milestone (see `moved`): reading it here
        # would be the third price for one omission. The two m3 rules left say
        # what a read must look like -- its format named at the site, its
        # options passed one at a time -- rather than what any one of them
        # must say about dates.
        #
        # `m3-csv-dates-pinned` was on this list and is not any more. The
        # `absent` twin cannot carry the clause alone: give the sixth read a
        # declared StructType and the scanner marks it `schema=yes`, the rule
        # stops selecting it, and "no inferring csv read is left unpinned" is
        # true of a tree that pinned five. So the milestone reads the pair,
        # and the count is the half that notices.
        #
        # The session rules are not here either, and not because they are
        # ignored: they left m05.tsv for rules/rung5.tsv, because at this rung
        # spark-core is `provided` and no candidate tree can fail them.
        OTHERS = (
            rule('m2-jdk-level-17', 'atleast:1', 'pom.jdklevel',
                 '(source|target|release)', '17', '.+'),
            rule('m2-test-jvm-opens', 'atleast:1', 'pom.testjvmarg',
                 '(--add-opens=)?java\\.base/sun\\.nio\\.ch=ALL-UNNAMED'),
            rule('m3-read-stated-at-the-site', 'absent', 'ast.read',
                 '(.*/)?src/main/.*', '*', '(\\?|-|load)', '*', '*'),
            rule('m3-read-options-visible', 'absent', 'ast.read',
                 '(.*/)?src/main/.*', '*', '(csv|json)', 'no', '.*\\?=\\?.*'),
        )

        def real_rules(**swap):
            r = dict(R)
            r.update(swap)
            rules_file(*([r[k] for k in ('spark', 'sparkU', 'scala', 'scalaU',
                                         'cls', 'clsU', 'dates', 'pinned',
                                         'sites', 'jar')
                         if r[k] is not None] + list(OTHERS)))

        real_rules()
        want(read_rule_ids(d),
             (['m1-spark-344', 'm1-spark-344-uniform', 'm1-scala-213',
               'm1-scala-213-only', 'm2-classes-17', 'm2-classes-17-uniform'],
              ['m3-csv-dates-stated', 'm3-csv-dates-pinned',
               'm3-csv-sites-kept', 'm3-prefer-date-jar']),
             'the milestone is composed of the ten ids the rule file names, '
             'in two conjunctions, and the four m2/m3 rules beside them are '
             'not part of it')

        def f(rid, status):
            return [(status, rid, 1, 'why')]

        rung_ok = sum((f(r, 'PASS') for r in read_rule_ids(d)[0]), [])
        rung_red = sum((f(r, 'FAIL') for r in read_rule_ids(d)[0]), [])
        reads_ok = sum((f(r, 'PASS') for r in read_rule_ids(d)[1]), [])
        reads_red = sum((f(r, 'FAIL') for r in read_rule_ids(d)[1]), [])
        # The rules of this step that are not in the milestone, all green, so
        # that a case which passes cannot be passing because of them.
        other = [('PASS', 'm2-jdk-level-17', 1, ''),
                 ('PASS', 'm2-test-jvm-opens', 1, ''),
                 ('PASS', 'm3-read-stated-at-the-site', 0, '')]

        want(moved(d, d, rung_ok + reads_ok, quiet), True,
             'the rung moved and the reads stated is the move this step is')

        # The before-state: the tree Harbor hands this step is on 3.3.4 for
        # Scala 2.12 with a jar at class-file 52 and six silent inferring
        # reads. Its replay is 140 of 141 -- and its suite 13 of 140, the one
        # part of the family an untouched tree does not get -- because
        # spark-core is `provided` and the image is this step's rung either
        # way, which is the 0.248643 this returning False is what stops.
        want(moved(d, d, rung_red + reads_red, quiet), False,
             'the tree this step is handed has made neither half of the move')

        # Each half on its own, against a tree that satisfies the other
        # completely.
        #
        # The first of these is the pom-only tree, and it is the one this
        # milestone exists for: 3.4.4, _2.13, Java 17, the jar at 61, and not
        # one source file touched. It is a real and substantial piece of work,
        # it scores 0.30, and it is not this step -- the step is the rung and
        # the reads, and half of what the rung changed is what the reads are
        # about.
        want(moved(d, d, rung_ok + reads_red, quiet), False,
             'the rung moved with the reads left silent is the pom-only tree, '
             'which is paid its 0.30 in the migrated family and no behaviour')
        want(moved(d, d, rung_red + reads_ok, quiet), False,
             'the reads stated on a tree still built for 3.3.4 is a change '
             'the recordings at this rung say nothing about')

        # And one rule at a time, because a conjunction that was secretly a
        # disjunction would pass every case above.
        for rid in read_rule_ids(d)[0] + read_rule_ids(d)[1]:
            report = [(('FAIL' if r[1] == rid else 'PASS'), r[1], 1, 'why')
                      for r in rung_ok + reads_ok]
            want(moved(d, d, report + other, quiet), False,
                 'the milestone is a conjunction: %s red is not made green by '
                 'the other nine' % rid)

        # A rule the report never mentions is not a rule that passed. rules.py
        # reports every rule it evaluates, so a missing id means the gate did
        # not run it, and reading it as green is how a milestone opens on a
        # tree nothing was measured against.
        with must_raise('a gate report missing one rung rule was accepted'):
            moved(d, d, rung_ok[1:] + reads_ok, quiet)
        with must_raise('a gate report naming no read rule was accepted; that '
                        'half would have been dropped without anything going '
                        'red'):
            moved(d, d, rung_ok, quiet)
        with must_raise('an empty gate report was accepted'):
            moved(d, d, [], quiet)
        with must_raise('a gate report naming every read rule but '
                        'm3-csv-dates-pinned was accepted; the count is the '
                        'half of the pair a declared schema does not vacate, '
                        'and reading it as green would pay the behaviour '
                        'family for a tree that pinned five'):
            moved(d, d, rung_ok + [r for r in reads_ok
                                   if r[1] != 'm3-csv-dates-pinned'], quiet)

        # The ways m05.tsv itself could stop constraining anything. First, by
        # not naming a part at all: each of the ten is required, and the
        # message names the tree that would otherwise open the milestone.
        for k in ('spark', 'sparkU', 'scala', 'scalaU', 'cls', 'clsU',
                  'dates', 'pinned', 'sites', 'jar'):
            real_rules(**{k: None})
            with must_raise('a rule file with no %s rule was accepted; that '
                            'part of the milestone would have been vacuous'
                            % k):
                read_rule_ids(d)

        # Then by naming it and asking it something weaker. Every rule below is
        # one somebody could write in good faith, every one of them loads and
        # runs, and every one of them is green on the tree this step is handed.
        #
        #   spark   a version term that also accepts 3.3.4 -- the handed tree's
        #           own -- asks nothing; a ga term of `*` accepts
        #           spark-core_2.12 at 3.4.4, which resolves and is not the
        #           move; `absent` inverts the question; `pom.dep` reads what
        #           the pom declares rather than what resolves, and a property
        #           the pom never interpolates declares 3.4.4 without anything
        #           resolving at it
        #   sparkU  a ga term pinned to spark-core leaves spark-sql free to sit
        #           at 3.3.4, which is the mixed classpath this rule is for; a
        #           version term of `.*` fires on 3.4.4 too, so it is red on the
        #           golden tree and green on nothing; `atleast` inverts it
        #   scala   a suffix term of `*` counts the 33 _2.12 rows the handed
        #           tree already has
        #   scalaU  a suffix term that names 2.11 misses the 2.12 rows that are
        #           actually there
        #   cls     a major term of `*` accepts the 52s; a name term of `*`
        #           accepts a class in a package the candidate just added, and
        #           --only takes its scope from the tree; `bytecode.classversion`
        #           is the histogram and has no name field, so one new file
        #           compiled at 17 puts a `61` row in it while every
        #           edu/sdsc/mmtf class stays at 52; a name term scoped to one
        #           package asks about one package
        #   clsU    a major term that accepts 61 is red on the golden tree; a
        #           name term scoped to one package leaves the rest at 52
        #   dates   a path term over src/test grades a tree test.sh replaces
        #           before the suite runs; an options term of `*` fires on a
        #           read that has already been stated, so it is red on the
        #           golden tree; an options term that does not fire on
        #           `header=true,inferSchema=true` is an `absent` rule over no
        #           rows, which passes; `ast.call` is not the fact that carries
        #           what a read asked for
        #   pinned  a threshold of 6 is the number of reads that have to end
        #           up pinned and not a count the handed tree answers, so 5 is
        #           not "one fewer than the tree already has" but a rule met by
        #           a candidate who pinned all but one -- the tree the cliff is
        #           disclosed for; a schema term of `*` counts a read handed a
        #           declared StructType as pinned, and that read has already
        #           dropped out of `-csv-dates-stated`, so both halves of the
        #           pair go green on a tree that pinned five and declared the
        #           sixth; an options term with the `preferDate=false`
        #           lookahead dropped counts the handed tree's own six
        #           inferring reads
        #   sites   a threshold below the seven sites the handed tree has is met
        #           by doing nothing, and 6 is exactly the number that lets a
        #           candidate delete one; a format term of `*` counts the ten
        #           non-csv reads, so six csv reads may be deleted and the count
        #           still met; a path term over src/test counts a tree this step
        #           does not grade
        #   jar     a value term of `*` matches the 1424 string constants the
        #           handed jar already carries
        for k, weak in (
                ('spark', rule('m1-spark-344', 'atleast:1', 'pom.resolved',
                               'org\\.apache\\.spark:spark-core_2\\.1[23]',
                               '3\\.[34]\\.4', '.+')),
                ('spark', rule('m1-spark-344', 'atleast:1', 'pom.resolved',
                               '*', '3\\.4\\.4', '.+')),
                ('spark', rule('m1-spark-344', 'absent', 'pom.resolved',
                               'org\\.apache\\.spark:spark-core_2\\.13',
                               '3\\.4\\.4', '.+')),
                ('spark', rule('m1-spark-344', 'atleast:1', 'pom.dep',
                               'org\\.apache\\.spark:spark-core_2\\.13',
                               '3\\.4\\.4', '.+')),
                ('sparkU', rule('m1-spark-344-uniform', 'absent',
                                'pom.resolved',
                                'org\\.apache\\.spark:spark-core_2\\.13',
                                '(?!3\\.4\\.4$).*', '*')),
                ('sparkU', rule('m1-spark-344-uniform', 'absent',
                                'pom.resolved', 'org\\.apache\\.spark:.*',
                                '.*', '*')),
                ('sparkU', rule('m1-spark-344-uniform', 'atleast:1',
                                'pom.resolved', 'org\\.apache\\.spark:.*',
                                '(?!3\\.4\\.4$).*', '*')),
                ('scala', rule('m1-scala-213', 'atleast:1', 'pom.scalasuffix',
                               '*', '.+')),
                ('scalaU', rule('m1-scala-213-only', 'absent',
                                'pom.scalasuffix', '2\\.11', '*')),
                ('cls', rule('m2-classes-17', 'atleast:1', 'bytecode.class',
                             'edu/sdsc/mmtf/.*', '*')),
                ('cls', rule('m2-classes-17', 'atleast:1', 'bytecode.class',
                             '*', '61')),
                ('cls', rule('m2-classes-17', 'atleast:1',
                             'bytecode.classversion', '61', '.+')),
                ('cls', rule('m2-classes-17', 'atleast:1', 'bytecode.class',
                             'edu/sdsc/mmtf/spark/datasets/.*', '61')),
                ('clsU', rule('m2-classes-17-uniform', 'absent',
                              'bytecode.class', 'edu/sdsc/mmtf/.*', '.*')),
                ('clsU', rule('m2-classes-17-uniform', 'absent',
                              'bytecode.class',
                              'edu/sdsc/mmtf/spark/datasets/.*',
                              '(?!61$).*')),
                ('dates', rule('m3-csv-dates-stated', 'absent', 'ast.read',
                               '(.*/)?src/.*', '*', 'csv', 'no',
                               '(?!.*preferDate=false).*'
                               'inferSchema=(true|\\?).*')),
                ('dates', rule('m3-csv-dates-stated', 'absent', 'ast.read',
                               '(.*/)?src/main/.*', '*', 'csv', 'no', '*')),
                ('dates', rule('m3-csv-dates-stated', 'absent', 'ast.read',
                               '(.*/)?src/main/.*', '*', 'csv', 'no',
                               '.*preferDate=true.*')),
                ('dates', rule('m3-csv-dates-stated', 'absent', 'ast.call',
                               '(.*/)?src/main/.*', '*', 'csv', 'no')),
                ('pinned', rule('m3-csv-dates-pinned', 'atleast:5',
                                'ast.read', '(.*/)?src/main/.*', '*', 'csv',
                                'no', '(?=.*inferSchema=(true|\\?))'
                                '(?=.*preferDate=false).*')),
                ('pinned', rule('m3-csv-dates-pinned', 'atleast:6',
                                'ast.read', '(.*/)?src/main/.*', '*', 'csv',
                                '*', '(?=.*inferSchema=(true|\\?))'
                                '(?=.*preferDate=false).*')),
                ('pinned', rule('m3-csv-dates-pinned', 'atleast:6',
                                'ast.read', '(.*/)?src/main/.*', '*', 'csv',
                                'no', '.*inferSchema=(true|\\?).*')),
                ('sites', rule('m3-csv-sites-kept', 'atleast:6', 'ast.read',
                               '(.*/)?src/main/.*', '*', 'csv', '*', '*')),
                ('sites', rule('m3-csv-sites-kept', 'atleast:7', 'ast.read',
                               '(.*/)?src/main/.*', '*', '*', '*', '*')),
                ('sites', rule('m3-csv-sites-kept', 'atleast:7', 'ast.read',
                               '(.*/)?src/.*', '*', 'csv', '*', '*')),
                ('jar', rule('m3-prefer-date-jar', 'atleast:1',
                             'bytecode.strconst', '*', '*'))):
            real_rules(**{k: weak})
            with must_raise('a weakened %s rule was accepted: %s'
                            % (k, weak.split('\t')[2])):
                read_rule_ids(d)

        # A rule with the wrong number of terms for its fact is not a
        # weakened rule so much as a rule about nothing: one term short and
        # the `major` the rule means to name is read as the `name` term, one
        # term long and the extra is matched against a column the row does not
        # have. rules.py would decline the row, and a rule rules.py declines
        # is a part of the milestone that never fires.
        for terms in (('edu/sdsc/mmtf/.*',),
                      ('edu/sdsc/mmtf/.*', '61', '*')):
            real_rules(cls=rule('m2-classes-17', 'atleast:1',
                                'bytecode.class', *terms))
            with must_raise('a `bytecode.class` rule with %d field term(s) '
                            'was accepted; a `bytecode.class` row has two'
                            % len(terms)):
                read_rule_ids(d)

        # And a rule written another way is not a weakened rule. Spelling is
        # not what the shape checks are about: what they rule out is a rule
        # that reads a different fact, a different tree, a different column or
        # a different count.
        real_rules(
            spark=rule('m1-spark-344', 'atleast:2', 'pom.resolved',
                       'org[.]apache[.]spark:spark-core_2[.]13',
                       '3[.]4[.]4', '(provided|compile)'),
            dates=rule('m3-csv-dates-stated', 'absent', 'ast.read',
                       '/app/src/main/java/.*', '[0-9]+', 'csv', 'no',
                       '(?!.*preferDate=false).*inferSchema=true.*'),
            sites=rule('m3-csv-sites-kept', 'atleast:9', 'ast.read',
                       '/app/src/main/java/.*', '[0-9]+', 'csv', 'no', '.*'))
        want(read_rule_ids(d)[1][:3],
             ['m3-csv-dates-stated', 'm3-csv-dates-pinned',
              'm3-csv-sites-kept'],
             'a rule that selects what the milestone reads it as selecting is '
             'accepted however it is spelled, and a threshold above the '
             'handed count is stricter rather than weaker')

        real_rules()
        open(m05, 'a').write(
            'm3-csv-sites-kept\tatleast:7\tast.read\\ta\\tb\twhy\n')
        with must_raise('a four-field rule line was accepted'):
            read_rule_ids(d)

        # The four prohibition files: the thirty-eight whose price is the whole
        # reward. The list is read out of the files, so what is tested here is
        # the reading and the three ways it could be wrong in the candidate's
        # favour -- files that name nothing, a report that never mentions a
        # rule, and an id in more than one file, which rules.py would refuse to
        # compose at all. There are four files now rather than three, and that
        # is the point of driving the loop off CHEAT_FILES: step 5 adds
        # cheats5.tsv and this test needed no new case to cover it, only a
        # fourth argument and one more pair in the duplicate loop.
        real_rules()
        paths = [os.path.join(rules, f) for f in CHEAT_FILES]

        def cheats_files(*per_file):
            assert len(per_file) == len(paths)
            for path, lines in zip(paths, per_file):
                open(path, 'w').write('# a comment\n\n' + ''.join(lines))

        cheats_files([rule('no-opaque-encoder', 'absent', 'ast.call', '*', '*',
                           'kryo', 'Encoders'),
                      rule('no-forged-execution-id', 'absent', 'probe.job', '*',
                           'forged:.*')],
                     [rule('no-legacy-conf-set', 'absent', 'probe.setconf',
                           'spark.sql.legacy..*', '*')],
                     [rule('no-sparkconf-reflect', 'absent',
                           'bytecode.strconst',
                           'org\\.apache\\.spark\\.SparkConf', '*')],
                     [rule('no-tests-skipped-config', 'absent',
                           'pom.pluginconfig',
                           'org\\.apache\\.maven\\.plugins:'
                           'maven-surefire-plugin', '(skipTests|skip)',
                           'true')])
        want(cheat_rule_ids(d),
             ['no-opaque-encoder', 'no-forged-execution-id',
              'no-legacy-conf-set', 'no-sparkconf-reflect',
              'no-tests-skipped-config'],
             'the ids that void the whole reward are the contents of all four '
             'prohibition files, in file order')
        want(cheat_red([('PASS', 'no-opaque-encoder', 0, ''),
                        ('PASS', 'no-forged-execution-id', 0, ''),
                        ('PASS', 'no-legacy-conf-set', 0, ''),
                        ('PASS', 'no-sparkconf-reflect', 0, ''),
                        ('PASS', 'no-tests-skipped-config', 0, ''),
                        ('FAIL', 'm3-csv-dates-stated', 0, '')],
                       cheat_rule_ids(d)),
             [],
             'a tree that failed a requirement but faked nothing is not a faked '
             'tree')
        want([r[1] for r in cheat_red([('FAIL', 'no-legacy-conf-set', 2, 'x'),
                                       ('PASS', 'no-opaque-encoder', 0, ''),
                                       ('FAIL', 'no-sparkconf-reflect', 1, 'y'),
                                       ('PASS', 'm1-spark-344', 1, '')],
                                      cheat_rule_ids(d))],
             ['no-forged-execution-id', 'no-legacy-conf-set',
              'no-sparkconf-reflect', 'no-tests-skipped-config'],
             'a red prohibition is red wherever it was written, and one the '
             'report never mentions is not one that passed')

        cheats_files([], [], [], [])
        with must_raise('prohibition files naming no rule were accepted; nothing '
                        'would have voided the reward'):
            cheat_rule_ids(d)

        bad_id = rule('m2-x', 'absent', 'ast.call', '*', '*', 'kryo', 'E')
        for n, which in enumerate(CHEAT_FILES):
            cheats_files(*[[bad_id] if i == n else []
                           for i in range(len(CHEAT_FILES))])
            with must_raise('a requirement-prefixed id in %s was accepted; it '
                            'would have been paid for by the migrated family '
                            'and voided the reward at the same time' % which):
                cheat_rule_ids(d)

        one = rule('no-legacy-conf-set', 'absent', 'probe.setconf',
                   'spark.sql.legacy..*', '*')
        for a in range(len(CHEAT_FILES)):
            for b in range(a + 1, len(CHEAT_FILES)):
                cheats_files(*[[one] if i in (a, b) else []
                               for i in range(len(CHEAT_FILES))])
                with must_raise(
                        'an id named in both %s and %s was accepted; rules.py '
                        'would refuse to compose the gate at all, so no tree '
                        'could be scored against this payload'
                        % (CHEAT_FILES[a], CHEAT_FILES[b])):
                    cheat_rule_ids(d)

    for line in bad:
        sys.stderr.write('SELFTEST FAIL %s\n' % line)
    if bad:
        raise SystemExit('score selftest: %d case(s) wrong' % len(bad))
    print('score selftest: %d case(s) behaved' % len(ran))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--tests', default='/tests')
    ap.add_argument('--run')
    # 46 and 140, the same census step 4 graded: CustomReportServiceTest's
    # tests are outside both baselines at this rung too, and at this rung there
    # are two of them or four depending on whether the fork was opened onto
    # java.base/sun.nio.ch and on how far behind the tree still is (tests_score,
    # EXEMPT_READINGS, NOTES 6.5.2 and 6.5.10). Neither count is in these
    # numbers, which is why they did not move when the JVM did. These are the defaults because test.sh passes neither flag -- a flag
    # it does not pass is a number nobody reading test.sh can see, so the
    # number lives where the reasoning does.
    ap.add_argument('--expect-classes', type=int, default=46)
    ap.add_argument('--expect-tests', type=int, default=140)
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.run:
        ap.error('--run is required')

    lines = []

    def log(s):
        lines.append(s)
        print(s)
        sys.stdout.flush()

    try:
        reward, parts = run_score(a.tests, a.run, a.expect_classes,
                                  a.expect_tests, log)
    except Exception as exc:                 # noqa: BLE001 -- a crash is a finding
        # Rule 8: the verifier always exits 0 and always writes a reward. A
        # scorer that died would otherwise leave Harbor with no reward.json,
        # and a step with no reward.json is a step that vanished from the mean
        # (trap 3).
        log('SCORER CRASHED: %s: %s' % (type(exc).__name__, exc))
        reward, parts = 0.0, {}

    log('')
    for key in sorted(parts):
        log('part   %-8s %.6f' % (key, parts[key]))
    log('reward %.6f' % reward)
    with open(os.path.join(a.run, 'reward.txt'), 'w') as fh:
        fh.write('%.6f\n' % reward)
    with open(os.path.join(a.run, 'score.log'), 'w') as fh:
        fh.write('\n'.join(lines) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
