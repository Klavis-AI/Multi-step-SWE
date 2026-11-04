#!/usr/bin/env python3
"""Turn one scored run of step 4 into a reward in [0, 1].

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
replay happens *before* the gate, as it did at steps 2 and 3, and at this step
a *graded requirement* depends on that order rather than only a standing rule.
rung3.tsv's three engine facts still have no answer in the source tree -- they
ask whether adaptive query execution actually re-planned something and what the
effective value of `spark.sql.legacy.statisticalAggregate` is, both answers a
session gives while it runs. m3 is the new one: it asks whether
`spark.sql.ansi.enabled` was set by this tree, on the session it built, and
took effect. Its evidence is `probe.setconf` and `probe.conf` rows, which exist
only because the replay ran first. A gate that ran first would score m3 against
an empty fact kind -- and an `absent` rule over an empty fact kind passes
silently, which is exactly how a requirement turns into free credit (NOTES
6.4.4).

THE SHAPE OF THE ANSWER

    reward = T1 x T2   (user decision 2026-10-13: product scoring)
    T1 = (0.10 * m1 + 0.44 * m2 + 0.12 * m3) / 0.66    migrated-ness
    T2 = (0.24 * replay + 0.10 * tests) / 0.34         preservation

with one condition on the first two terms: they are graded only on a tree that
stopped building its own `SparkConf` at every entry point and started taking
the entry point off a `SparkSession` instead. That condition is read in three
parts -- no SparkConf is constructed under src/main or referenced from the jar;
no file under src/main with a `main` method in it builds no session; and no
program the replay ran came up without reaching one. The first two are artifact
facts and the third, added by the static review's round 2, is the run's, because
the sources containing the idiom is not the program executing it. See
session_adopted.

and every one of those five numbers is printed with its own evidence, because
a reward is a summary and the thing a reviewer needs is the summary's parts.

Four ways the run can be void rather than bad, and they are not the same thing
as a candidate scoring zero:

  * a precondition failed -- the mirror is down, the payload is short a file,
    an instrument failed its own selftest. Nothing measured after that means
    anything (trap 5, trap 6).
  * a standing rule is red. A hundred and forty-five of this gate's hundred
    and fifty-seven rules are standing: common.tsv's ten as floor.tsv tightens
    two of them, the hundred and two that `hold` renamed with a `held-` prefix
    out of step 1's m01.tsv, step 2's m02.tsv and step 3's m03.tsv, the thirty
    prohibitions of cheats.tsv, cheats3.tsv and cheats4.tsv, and rung3.tsv's
    three engine facts. Six of the hundred and two are not held as the earlier
    steps wrote them: m04-inherit.tsv widens the four pom fences and the two
    session fences that pin Spark's version, so that they accept 3.3.4 as well
    as the versions the earlier steps landed on, because this step is the one
    that moves off 3.2.4 and the rules step 3 left behind would otherwise void
    a perfect tree. The widening is forward-only and it does not let a
    candidate regress the pin: a held rule is a precondition, and m04.tsv's own
    `m1-spark-334` is what forbids going back. The tree Harbor hands this step
    -- steps 1 to 3 finished, none of step 4 done -- passes all hundred and
    forty-five, rung3.tsv's included: those three read the engine the replay
    ran on, and that engine is this image's whatever the candidate's pom says.
    Measured rather than asserted (NOTES 6.4.4): run against that tree with all
    five scanners supplying real facts, the gate reports every standing rule
    green but `probe-submit-ok`, which wants a session-probe row a
    hand-assembled fact set cannot carry; item 6's oracle run is what confirms
    that last one. So a red one says the reading is void, not that the
    candidate is wrong: the mirror fell over, or an instrument did not run, or
    an earlier step's work was undone, or something switched off a default the
    reading assumes.
  * one of the thirty prohibitions is red. Those are standing rules too,
    and by the arithmetic above they would void the migrated family and leave
    the behavior family standing. They do not: an opaque encoder, a hand-set
    execution id, a forged one, the reflective spellings of those, the two ways
    a jar reaches for the instrument that reads it, the two that refuse a
    method looked up by name at all and the one that refuses a class of the
    candidate's own standing where the observer's belongs, and -- new at this
    step -- the fifteen reads that catch a `spark.sql.legacy.*` switch, a hand
    on adaptive execution or on the optimiser, a `describe` spelled for a
    reflective lookup, a job whose facts went missing on the way out, and a
    reading the tree arranged to fail are the ways this
    step is made to look done without being done, and a tree that did that is
    not a tree whose recordings are worth paying for. A legacy switch belongs
    here rather than among the requirements for the same reason the others do:
    it buys the *appearance* of the milestone. This step's whole subject is
    that a 3.2 engine answers some questions differently from a 3.0 one, and a
    tree that asks 3.2 to answer them the old way has moved its pom and nothing
    else. The ids are read out of gates/rules/cheats.tsv and cheats3.tsv rather
    than listed here, so the files and the arithmetic cannot drift apart.
  * the two evaluations of the gate disagree. Same rules, same facts, one run
    as nobody and one as root over a copy -- rules.py is deterministic, so a
    difference means the facts moved between being written and being read.

The first voids everything and is a verifier failure, reported as such. The
second and third void the migrated-ness family only: the behavior family is
measured from recordings, not from facts, and a tree that does not build has
already lost the replay on its own merits. The fourth is the one exception, and
it is deliberate: it zeroes the reward outright, behavior included.

And one way the behavior family is not graded at all rather than graded badly:
a tree that still builds its own `SparkConf` at every entry point has not made
this step's move, and paying it 0.34 for the recordings it reproduces by not
having been touched would make the nop non-zero (migration_prompt.md section 8:
nop reports 0.0). That risk is sharper here than at any earlier step, and the
reason is worth stating plainly: rung 04 is behaviourally silent on this
codebase. The census measures c04 -> c05b as SAME on all 140 recordings (NOTES
6.4.1), and the repository's own suite is byte-identical with ANSI forced on,
down to the text of its two pre-existing failures (NOTES 6.4.2). So a candidate
who edits one version in `pom.xml` replays 140/140 and passes 140/140, and
collects m1 as well -- the pom half because they earned it, the session half
because the replay runs the *image's* Spark and reports 3.3.4 whatever the pom
says. Step 3 learned that the expensive way: it priced two requirements at 0.14
for facts the image answers on its own, and item 4.3 caught it (NOTES 6.3.11).

That lesson is why m3 is a requirement here and `spark.sql.storeAssignmentPolicy`
is not. Both looked like facts that arrive with the rung. Measured on the handed
tree, ANSI reads false on all 123 probe rows while storeAssignmentPolicy already
reads ANSI on all 123 -- so one is earnable and the other was free, and the free
one is a cheat rule in cheats4.tsv rather than a share of the reward (NOTES
6.4.4).

What is left is one requirement the pom answers, one the engine cannot be made
to answer for free, and one the pom cannot touch at all, and the gate below is
what stops the first from collecting the others' behaviour marks: without it
the one-line edit would take the replay and the repository tests too, and reach
0.44 for work the automated migrator does unattended. With it, that tree scores
0.10. Rubric R4 calls a step answerable by editing pom.xml alone an automatic
Blocker; this is the arithmetic that keeps this one from being it. See
session_adopted.
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


# --- the thirty ways to fake this step ---------------------------------------

# Both prohibition files, by name. cheats.tsv carries the twelve this ladder
# has had since step 2 -- the opaque encoder, the forged execution id, the
# tampered listener, and the three round 3 added: a lookup by name, a call
# through java.lang.reflect, and a class of the candidate's own named for the
# observer -- and cheats3.tsv the fifteen this step adds, which refuse the
# `spark.sql.legacy.*` hatches three ways, the two adaptive-execution switches
# two ways each, any other adaptive sub-setting put there by hand, a `describe`
# spelled only in the constant pool, an optimiser rule excluded by hand, the two
# paying jobs going quiet, and a conf read the tree arranged to fail. They are two files rather than one
# because step 2's instruction never mentions a legacy config, and a rule in a
# file that step's instruction is compared against in both directions would
# have to be mentioned there (item 4.4). The split is a documentation boundary
# and nothing else: step03.tsv includes both, this function reads both, and the
# price of a rule in either is identical.
CHEAT_FILES = ('cheats.tsv', 'cheats3.tsv', 'cheats4.tsv')


def cheat_rule_ids(tests):
    """The ids in gates/rules/cheats.tsv and cheats3.tsv, read out by name.

    Every standing rule costs the migrated family. These thirty cost the
    whole reward, behavior included, and the difference between the two prices is not
    a judgement this file makes -- it is which file the rule was written in. A
    list kept here as well would be a second copy of it, and on the day a
    thirty-first prohibition is written the copy is the one that would not
    have it (rule 4, in the verifier's own terms).

    An id appearing in both files is refused rather than deduplicated. rules.py
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
# 3.3.4. CustomReportServiceTest hardcodes the old rendering as a string
# literal at :42 and :50, so both of its two tests are red here -- for every
# tree, the golden solution included, because test.sh deletes /app/src/test and
# copies the commit's own pristine copy over it before the suite runs, which
# deletes a candidate's correction along with everything else they did to that
# directory. Measured on two byte-identical trees one `mvn -D` word apart:
# `142 / Failures: 2` on both, failure text byte-identical, and the cause
# reproduced with no data, no mirror and no web service in it by building a
# StructType in memory at each rung (NOTES 6.4.2).
#
# Scoring it would cost the oracle 2/142 and hand out an unpriced shortcut:
# with the pair unpassable, deleting the file scores exactly what leaving it in
# place scores, so the gate would pay the same for a suite that was repaired
# and one that was vandalised. So the class leaves both the numerator and the
# denominator -- 46 classes, 140 tests -- rather than the baseline becoming
# "two failures allowed anywhere", which would let a candidate break two other
# tests for free.
#
# And the exemption is turned around into an instrument. At this rung the class
# is deterministic and candidate-independent, so `tests=2 failures=2` is a fact
# about the overlay and the rung rather than about the tree. test.sh asserts it
# as a precondition after the suite runs; anything else means the run is not
# the run these baselines were measured on. Here the arithmetic is done and the
# reading is logged, so the two halves can be compared by whoever reads the log.
EXEMPT_CLASS = 'edu.sdsc.mmtf.spark.webservices.CustomReportServiceTest'
EXEMPT_TESTS = 2
EXEMPT_FAILURES = 2


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
                log('tests  EXEMPT  %s: %d test(s), %d failure(s), %d error(s), '
                    '%d skipped -- outside both baselines at this rung%s'
                    % (EXEMPT_CLASS, n, f, e, s,
                       '' if (n, f) == (EXEMPT_TESTS, EXEMPT_FAILURES)
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


# --- did this tree adopt the session, for behavior to be worth asking about? -

# The ids the gate is composed from, matched against `m04.tsv` rather than
# listed here. `-no-sparkconf-...` says the old conf is gone; `-session-adopted`
# says the sources of every program build a session of their own instead; and
# `-session-live`, added by the static review's round 2, says the programs
# actually reached one when they ran. All three are read out of the same gate
# report the m1, m2 and m3 requirements are scored from, so the gate and the
# requirements cannot disagree about what the tree did.
SPARKCONF_GONE = re.compile(r'^m\d+-no-sparkconf-\S+$')
SESSION_COUNT = re.compile(r'^m\d+-session-adopted$')
SESSION_LIVE = re.compile(r'^m\d+-session-live$')
# The selector the gate will accept for it, read field by field. What has to
# be true is about *which* of an `ast.unit` row's three counts the rule asks
# about -- the row is `<path>\t<mains>\t<sessions>\t<sessions in main>`
# (gates/schema.tsv) -- and one pattern over the escaped line cannot say that
# without counting separators itself, which is how the review's round 3 found
# the old check passing a rule that asked about the wrong column. The escaped
# text is the form session_rule_ids has in hand; running rules.py's own loader
# over it would be a second copy of that loader to keep in step, so the two
# constants below are the only piece of its grammar this file repeats: fields
# are separated by a literal backslash-t, and a field of `*` matches anything
# (rules.py, `load`).
SELECTOR_SEP = '\\t'
SELECTOR_ANY = '*'

# The two sample paths the path term is checked against: one the rule has to
# select and one it must not. The scanner is handed both halves of src and the
# test tree has main-bearing files of its own, which no requirement of this
# step grades (m04.tsv).
SESSION_UNIT_MAIN = 'src/main/java/edu/sdsc/mmtf/spark/io/demos/ReadMmtfFull.java'
SESSION_UNIT_TEST = 'src/test/java/edu/sdsc/mmtf/spark/io/demos/ReadMmtfFullTest.java'


def selector_field(term, value):
    """Does one selector field accept `value`? `*` is rules.py's match-anything.

    A term that will not compile is a rule rules.py refuses to load at all, so
    it is reported here as accepting nothing rather than raised: the caller is
    naming a shape, and "this field matches no count" is the shape it has.
    """
    if term == SELECTOR_ANY:
        return True
    try:
        return re.compile(term).fullmatch(value) is not None
    except re.error:
        return False


def session_unit_shape(sel):
    """Why `sel` cannot be the `-session-adopted` selector, or None when it can.

    Six ways to write a rule that loads, runs, and is green on the tree this
    step is handed. The last two are the review's round 3: until it, the check
    was one pattern requiring a zero in the selector's last field, and the
    `ast.unit` row had since grown a third count, so a rule pinned to the
    *file-wide* session count went on matching that pattern
    while asking a question a shared factory answers -- move the
    `getOrCreate()` into a helper the main calls and the file has a session,
    the entry point has none, and the rule is silent. What the gate reads this
    rule as saying is "no program under src/main builds its session anywhere
    but in its own entry point", and only the fourth field says that.
    """
    fields = sel.split(SELECTOR_SEP)
    if fields[0] != 'ast.unit':
        return ('reads `%s` rather than the per-file `ast.unit` row, the only '
                'fact that can be negative about a program having no session'
                % fields[0])
    terms = fields[1:]
    if len(terms) != 4:
        return ('gives %d field term(s) where an `ast.unit` row has 4 -- path, '
                'mains, sessions, sessions in main' % len(terms))
    path, mains, sessions, inmain = terms
    if not selector_field(path, SESSION_UNIT_MAIN):
        return 'selects no file under src/main'
    if selector_field(path, SESSION_UNIT_TEST):
        return 'selects the test tree as well as src/main'
    if not selector_field(mains, '1'):
        return 'selects no file that declares a main method'
    if selector_field(mains, '0'):
        return 'selects files with no main method, which have no entry point'
    if not (selector_field(sessions, '0') and selector_field(sessions, '1')):
        return ('pins the file-wide session count rather than leaving it open, '
                'so a session built in a helper of the same file answers it '
                'and the entry point still has none')
    if not selector_field(inmain, '0'):
        return 'does not select the files whose main method builds no session'
    if selector_field(inmain, '1'):
        return 'accepts a main method that does build one, so it asks nothing'
    return None

# And the third half's shape, added by the review's round 2. `-session-live`
# has to be `absent` over the per-program `probe.appsession` rows and it has to
# refuse every value but `true`: a rule that read the sources' idiom and not
# the run would be satisfied by a tree whose programs carry the idiom and never
# reach it, and a rule that accepted any value at all would be satisfied by the
# 29 programs of the handed tree that bring up no session.
SESSION_LIVE_SEL = re.compile(r'^probe\.appsession\\t.*\\t.*\(\?!true\$\)')

# Files under src/main that declare a main method and never build a
# SparkSession, in the tree this step is handed: 48, counted by the AST scanner
# that scores it rather than by grep. Both trees have 78 main-bearing files
# there; the golden one has 0 of these and the handed one has 48, which is the
# 48 conf-only entry points of the 15+48 split (NOTES 6.4.8).
#
# It is here so `session_rule_ids` has something to say about a rule it can no
# longer read a threshold out of. `m2-session-adopted` used to be `atleast:90`
# over `builder` call sites and the guard was "the threshold must exceed the 54
# the handed tree already has". The static review's round 1 broke that rule --
# a single class with thirty-six unused methods clears 90 without touching an
# entry point -- so it is now `absent` over the per-file `ast.unit` row, and
# what has to be guarded is the *shape*: the rule must ask which main-bearing
# files build no session *in the main method itself*, because a rule that asked
# about any other column or any other number would be green on the handed
# tree. The review's round 3 moved that question from the file to the method;
# the count is the same 48 either way, since on this tree a file whose main
# builds nothing builds nothing anywhere (NOTES 6.4.10).
HANDED_MAINS_WITHOUT_SESSION = 48

# And the runtime half of the same measurement: of the 96 programs of this
# repository that the replay runs -- 123 probe files less the 27 the suite's
# own HarnessDriver writes -- 29 bring up no SparkSession at all on the handed
# tree and all 96 do on the golden one (NOTES 6.4.8).
HANDED_PROGRAMS_WITHOUT_SESSION = 29


def session_rule_ids(tests):
    """(gone, adopted, live): the m04.tsv ids the behavior gate reads, by name.

    Not three lists of ids kept in this file. The behavior gate and the m2
    requirement have to mean the same thing by "this tree stopped building its
    own SparkConf and takes its entry point off a session instead", and two
    lists in two files mean the same thing right up until one of them is
    edited. So all of them are read out of the rule file the gate is composed
    from.

    Each part is checked for the shape that makes it say what the gate reads it
    as saying. A `-no-sparkconf-...` rule that was not `absent` would be asking
    for the SparkConf to still be *there*; a `-session-adopted` rule that did
    not ask, of every file under src/main with a main method, whether that
    method builds a session, would be satisfied by a tree that built no
    session at any entry point; and a `-session-live` rule that did not ask, of every
    program the replay ran, whether it brought a session up, would be satisfied
    by the 29 programs of the handed tree that never reach one. In each case
    the gate would still run, and would open on the tree that did none of this
    step's work. Failing here instead is the whole point of reading the file
    rather than trusting it.

    The second part used to be a threshold -- `atleast:90` `builder` call
    sites, guarded here against being set at or below the 54 the handed tree
    already has. The static review's round 1 pointed out what that actually
    measures: one class under src/main with thirty-six unused methods -- 36
    being just what 90 less the 54 the handed tree already has comes to --
    each calling `SparkSession.builder().getOrCreate()`, clears 90 without
    moving a single entry point, and since this gate is what opens the behaviour share,
    that dead code was worth 0.78. A count over call sites cannot be made to
    mean "every program has one"; only a negative can, and a negative needs a
    per-file fact to be negative about. `ast.unit` is that fact and
    `session_unit_shape` is the shape check that replaces the threshold
    check, and it reads the selector one field at a time because the round-3
    review found the one-pattern version of it accepting a rule that asked
    about the file-wide session count instead of the entry point's.

    The measurement that makes the shape check meaningful is
    `HANDED_MAINS_WITHOUT_SESSION`, and that name is where it lives.

    The third part is the review's round 2, and it is a different fact rather
    than a stronger version of the second. `ast.unit` is read off the sources:
    it says the idiom is written in the file with the main method. It cannot
    say the program ran it. A tree whose entry points each build a session
    inside a branch that is never taken, or after an exit, or in a file that no
    longer compiles into the program the replay invokes, satisfies the second
    part completely. `probe.appsession` is one row per program the replay
    actually started, written by a listener that only exists once a
    SparkContext does, and `HANDED_PROGRAMS_WITHOUT_SESSION` is the 29 of 96 it
    reads as having none on the tree the agent is handed.
    """
    path = os.path.join(tests, 'gates', 'rules', 'm04.tsv')
    gone, adopted, live = [], [], []
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if len(f) != 5:
                raise ValueError('%s:%d: want 5 fields, got %d'
                                 % (path, n, len(f)))
            rid, mode = f[0], f[1]
            if SPARKCONF_GONE.match(rid):
                if mode != 'absent':
                    raise ValueError('%s:%d: %s is `%s`, not `absent`, so it '
                                     'asks for a SparkConf to still be built '
                                     'and the gate would read a tree that '
                                     'never touched one as having moved'
                                     % (path, n, rid, mode))
                gone.append(rid)
            elif SESSION_COUNT.match(rid):
                if mode != 'absent':
                    raise ValueError('%s:%d: %s is `%s`, not `absent`; the gate '
                                     'reads it as "no program here is left '
                                     'without a session of its own", and a '
                                     'counted rule says something weaker that '
                                     'dead code in one file can satisfy'
                                     % (path, n, rid, mode))
                why = session_unit_shape(f[2])
                if why:
                    raise ValueError('%s:%d: %s %s (selector %r); the handed '
                                     'tree has %d main-bearing file(s) under '
                                     'src/main whose entry point builds no '
                                     'session, and a rule that asks anything '
                                     'else is green on it'
                                     % (path, n, rid, why, f[2],
                                        HANDED_MAINS_WITHOUT_SESSION))
                adopted.append(rid)
            elif SESSION_LIVE.match(rid):
                if mode != 'absent':
                    raise ValueError('%s:%d: %s is `%s`, not `absent`; the gate '
                                     'reads it as "every program the replay ran '
                                     'brought a session up", and a counted rule '
                                     'says something weaker that one program '
                                     'running many times can satisfy'
                                     % (path, n, rid, mode))
                if not SESSION_LIVE_SEL.match(f[2]):
                    raise ValueError('%s:%d: %s does not select `probe.appsession` '
                                     'rows whose value is anything but `true` '
                                     '(selector %r); %d of the handed tree\'s 96 '
                                     'programs bring up no session, and a '
                                     'selector that accepts a value other than '
                                     '`true` -- or that reads some other fact -- '
                                     'is green on it'
                                     % (path, n, rid, f[2],
                                        HANDED_PROGRAMS_WITHOUT_SESSION))
                live.append(rid)
    if not gone:
        raise ValueError('%s names no `m<digit>-no-sparkconf-...` rule, so the '
                         'first part of this gate is vacuously true and a tree '
                         'that still builds a SparkConf at every entry point '
                         'would be read as having consolidated them' % path)
    if not adopted:
        raise ValueError('%s names no `m<digit>-session-adopted` rule, so the '
                         'second part of this gate is vacuously true and a tree '
                         'that took its context off SparkContext.getOrCreate() '
                         'without ever building a session would open it' % path)
    if not live:
        raise ValueError('%s names no `m<digit>-session-live` rule, so the third '
                         'part of this gate is vacuously true and a tree whose '
                         'sources carry the idiom in every entry point and whose '
                         'programs never reach one would open it' % path)
    return gone, adopted, live


def session_adopted(run, tests, findings, log):
    """True when the tree made this step's move: the conf out, a session in.

    Three facts, and the conjunction is the point. Every `-no-sparkconf-...`
    rule must pass -- no SparkConf is constructed in the sources the AST scanner
    walked under src/main, and none is referenced from the built jar's
    edu/sdsc/mmtf classes -- *and* the `-session-adopted` rule must pass,
    meaning no file under src/main declares a main method without building a
    SparkSession in it, *and* the `-session-live` rule must pass, meaning every
    program the replay actually ran brought a session up.

    Which part does which work was measured at item 4.3 rather than argued,
    and the first draft of this docstring had it wrong. It justified the
    conjunction with two trees that the second part does not catch: a builder
    added *beside* a SparkConf that is still there fails the first part and
    never reaches the second, and the 63 conf lines deleted with nothing in
    their place does not compile, so there is no jar to read, no replay to
    pay for and no test run to open.

    The tree the count uniquely catches is a third one, and it is why the
    count is in this gate:

        JavaSparkContext sc = JavaSparkContext.fromSparkContext(
            SparkContext.getOrCreate());

    No SparkConf is constructed or named, so both `-no-sparkconf-` rules pass.
    Neither `JavaSparkContext.<init>` nor `SparkContext.<init>` appears in the
    candidate's classes -- both of those are static factories -- so
    `m2-ctx-from-session-only` and `m2-no-raw-sparkcontext` pass on an absence
    that is real rather than arranged. It compiles, it runs, and it replays.
    Every artifact fact in m2 except `-session-adopted` is green on a tree that
    moved nothing, and `-session-adopted` is the only thing between it and 0.34
    of behaviour credit.

    Which is why what `-session-adopted` reads had to change. As a count of
    `builder` call sites it caught the tree above and also, as the static
    review's round 1 showed, opened for a tree that added thirty-six of those
    calls in one unused class and moved nothing either. It now reads the
    per-file `ast.unit` row, and asks whether any program here was left
    without a session -- a question dead code in some other file cannot
    answer, because the file with the main is still the file with no builder.

    No part of it can be bought by doing nothing: the handed tree leaves 48 of
    its 78 main-bearing files under src/main without a session and 29 of its 96
    replayed programs without one at run time, and `session_rule_ids` refuses a
    rule that would not see either set.

    The third part is there because the second reads the sources and the
    sources are not the run. `ast.unit` says the file with the `main` also
    contains a `SparkSession.builder()...getOrCreate()`; it cannot say the
    program executed it. Put that call behind a condition that is false, or
    after the work it was supposed to configure, or in a class the assembled
    jar does not invoke, and the second part is green on a tree whose programs
    still come up with whatever session the submit line happens to hand them.
    `probe.appsession` is written by a listener that only exists once a
    SparkContext does, so a program that never reached one has no `true` row to
    show -- it has `<no-session>`, or, since the review's round 2, `dropped` if
    it exited 0 and left no probe file at all (NOTES 6.4.9).

    What this charges a good solution that is not this one is written down
    rather than discovered: a candidate who routes every entry point through
    one shared session factory clears both `-no-sparkconf-` rules and leaves
    every one of those 78 files without a builder of its own, fails m2, loses
    the gate with it, and scores 0.22.
    That is deliberate -- the milestone is one session per program built at
    that program's entry point, a factory is a different design, and
    instruction.md says so in those words. A threshold is not allowed to be
    the only place a requirement is stated.

    That third part does come off the probe, which the first draft of this
    docstring said this gate would never do, and the distinction it was drawing
    is worth keeping rather than deleting. What it refused was gating behaviour
    on a *configuration* read: ANSI is the one configuration fact this step
    grades, it is m3's requirement, it is scored in the migrated family where a
    configuration read belongs, and gating behaviour on it as well would price
    one fact twice -- the mistake step 3 made and item 4.3 caught (NOTES
    6.3.11). `-session-live` reads no configuration. It reads whether the
    program reached a SparkSession at all, which is the same claim the second
    part makes about the sources, asked of the run. `run` is still unused
    because all three parts are read out of the gate report in `findings`,
    which is where the scanners' facts have already been matched against the
    rules; this function reads no file of the replay's own.

    What none of the three parts does is prove the sessions were configured the
    same way the confs were. A candidate who retires a `SparkConf` that set
    `spark.serializer` and does not carry that setting to the builder satisfies
    all three and changes the runtime's behaviour. That is not this gate's
    job: the replay family is what reads the output, and this gate is what
    decides whether the replay family is worth reading. The 63 sites set an app
    name and a master and nothing else in 61 of them, which is why the residual
    is small rather than absent -- NOTES 6.4.3 records it rather than hiding
    it.

    The behavior family asks whether the *migrated* tree still answers the way
    the original did. A tree that still builds its own SparkConf, the way the
    step-3 tree does, has not been asked that question yet, and at this step
    that matters more than at any before it: rung 04 is behaviourally silent on
    this codebase -- 140 of 140 recordings SAME across c04 -> c05b (NOTES 6.4.1)
    and a byte-identical test suite with ANSI forced on (NOTES 6.4.2) -- so on
    this step's weights the reward for bumping one line of pom.xml would be
    0.34 plus m1, and section 8 of migration_prompt.md says the nop reports
    0.0.

    Beyond that, it is the weakest condition that separates a tree which made
    the move from one that did not: the confs gone, the sessions written where
    the entry points are, and the programs reaching them when they run. A candidate who consolidated every entry point but left one demo's
    app name unset keeps every point of proportional behavior credit and loses
    only whatever the replay says about that one demo. There is no cliff here
    for anybody who tried.
    """
    del run  # every part is read out of the gate report; see the docstring
    gone, adopted, live = session_rule_ids(tests)
    by_id = dict((r[1], r) for r in findings)

    every = gone + adopted + live
    missing = [rid for rid in every if rid not in by_id]
    if missing:
        raise ValueError('the gate report does not mention %s, so %s of this '
                         'gate never ran and reading it as passed would open it '
                         'on a tree nothing was measured against'
                         % (' '.join(missing),
                            'part' if len(missing) < len(every)
                            else 'no part'))

    red = [rid for rid in gone if by_id[rid][0] != 'PASS']
    if red:
        log('behav  NO       a SparkConf is still being built: %d of %d rule(s) '
            'that say it is gone did not pass (%s), so whatever sessions the '
            'tree also builds, the entry points this step is about are still '
            'coming off a conf of their own'
            % (len(red), len(gone), ' '.join(red)))
        return False
    log('behav  GONE     all %d rule(s) agree no SparkConf is constructed in '
        'src/main or referenced from the jar: %s' % (len(gone), ' '.join(gone)))

    short = [rid for rid in adopted if by_id[rid][0] != 'PASS']
    if short:
        log('behav  NO       %d rule(s) ask whether the tree builds its own '
            'sessions and did not pass (%s): the confs were deleted, not '
            'replaced, and the entry points this step is about are not being '
            'built at all'
            % (len(short), ' '.join(short)))
        return False
    log('behav  BUILT    the sources build a session at every entry point: %s'
        % ' '.join('%s (%s row(s))' % (rid, by_id[rid][2]) for rid in adopted))

    dead = [rid for rid in live if by_id[rid][0] != 'PASS']
    if dead:
        log('behav  NO       %d rule(s) ask whether the programs the replay ran '
            'actually reached a session and did not pass (%s): the idiom is in '
            'the sources and the running programs did not get there'
            % (len(dead), ' '.join(dead)))
        return False
    log('behav  YES      every replayed program brought a session up: %s'
        % ' '.join('%s (%s row(s))' % (rid, by_id[rid][2]) for rid in live))
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
        log('%d held from steps 1 and 2, the %d prohibitions in cheats.tsv and'
            % (n_held, len(cheat_ids)))
        log('cheats3.tsv, and rung3.tsv\'s engine facts. They are preconditions')
        log('for reading the %d requirement rules above them, and the tree Harbor'
            % n_req)
        log('hands this step passes all of them. A red one says the reading is void, not that the candidate is')
        log('wrong: the mirror fell over, or an instrument did not run, or the')
        log('work of an earlier step was undone. So this family scores 0 and the')
        log('behavior family below still stands -- unless the red one came from a')
        log('cheats file, which is the single exception and is reported under its')
        log('own heading below.')

    # Both behavior instruments are run and logged whatever the answer below
    # is: the evidence for "this tree reproduces 131 of 140 recordings" is
    # worth having even when it earns nothing, and a reader who cannot see it
    # cannot tell a zero that was measured from a zero that was assumed.
    replay, _counts = replay_score(tests, run, log)
    tscore = tests_score(run, expect_classes, expect_tests, log)
    graded = session_adopted(run, tests, mine, log)
    parts['replay'] = weights['behavior']['replay'][0] * (replay if graded else 0.0)
    parts['tests'] = weights['behavior']['tests'][0] * (tscore if graded else 0.0)
    if not graded:
        log('')
        log('BEHAVIOR NOT GRADED -- this tree still builds its own SparkConf at')
        log('the entry point, or it deleted the confs and put no session in their')
        log('place, so it has not made the move this family asks its question')
        log('about. Rung 04 is behaviourally silent on this codebase, which is why')
        log('a tree that changed nothing reproduces every recording: matching a')
        log('recording of the thing you did not change is not evidence that you')
        log('changed it safely. The two measurements above stand as evidence and')
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
        log('reflective spellings of both, and -- new at this step -- a')
        log('spark.sql.legacy.* switch that asks the 3.2 runtime to keep answering')
        log('the way 3.0 did. A tree that did one of them did not migrate anything,')
        log('and its recordings match because nothing moved -- so the reward is 0,')
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

    # tests_score's exemption, which is new at this step and is arithmetic on a
    # number the oracle's own reward depends on. Trap 6: the exemption is the
    # instrument here, so it is checked before it is trusted, on reports built
    # to be exactly the two shapes that matter.
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
        report(EXEMPT_CLASS, EXEMPT_TESTS, failures=EXEMPT_FAILURES)
        want(tests_score(d, 46, 140, quiet), 1.0,
             'the exempt class red 2/2 leaves the oracle at 1.0, not 140/142')

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
        report(EXEMPT_CLASS, EXEMPT_TESTS)
        want(tests_score(d, 46, 140, quiet), 1.0,
             'the exempt class passing does not pay either')

        # The class count excludes it too, so a correct run does not log SHRANK.
        seen = []
        tests_score(d, 46, 140, seen.append)
        want([l for l in seen if 'SHRANK' in l], [],
             'a correct run does not report its own suite as having shrunk')
        want(len([l for l in seen if l.startswith('tests  EXEMPT')]), 1,
             'the exempt class is reported rather than silently dropped')

    # session_adopted, and its half of the pricing. Every case here that must
    # come back False is a way the tree Harbor hands this step could have
    # collected 0.34 of the reward for changing nothing, and the ones that must
    # raise are ways a later edit to m04.tsv could have made the gate
    # unconditional without anything going red.
    with tempfile.TemporaryDirectory() as d:
        quiet = lambda s: None
        rules = os.path.join(d, 'gates', 'rules')
        os.makedirs(rules)
        m04 = os.path.join(rules, 'm04.tsv')

        def rule(rid, mode, *terms):
            return '%s\t%s\t%s\tc.tsv\twhy\n' % (rid, mode, '\\t'.join(terms))

        def rules_file(*lines):
            open(m04, 'w').write('# a comment\n\n' + ''.join(lines))

        # The shape m04.tsv actually has: two rules saying no SparkConf is
        # built -- one over the sources the AST scanner walked under src/main,
        # one over the built jar's reference table -- one saying no program
        # under src/main was left without a session of its own, and one saying
        # no program the replay ran came up without reaching one.
        def real_rules():
            rules_file(
                rule('m2-no-sparkconf-src', 'absent', 'ast.newclass',
                     '(.*/)?src/main/.*', '*',
                     '(org\\.apache\\.spark\\.)?SparkConf', '*'),
                rule('m2-no-sparkconf-jar', 'absent', 'bytecode.ref',
                     'edu/sdsc/mmtf/.*', 'org/apache/spark/SparkConf',
                     '*', '*'),
                rule('m2-session-adopted', 'absent', 'ast.unit',
                     '(.*/)?src/main/.*', '[1-9][0-9]*', '.*', '0'),
                rule('m2-session-live', 'absent', 'probe.appsession',
                     '.+', '(?!true$).*'),
                # Four rules of this step that the gate must ignore. The first
                # is m2's own, and the most interesting of the four:
                # `m2-ctx-from-session-only` says no JavaSparkContext is built
                # from a SparkConf, which is *how* the contexts that exist were
                # made, not *whether* the tree built any session of its own.
                # Reading it as a second way to satisfy the adopted half would
                # open the gate on a tree that kept all 63 confs, because the
                # rule is about a descriptor and 8 sites already use the
                # session spelling (NOTES 6.4.3). `m2-no-raw-sparkcontext` is a
                # prohibition and neither half. The last two are an m1 rule and
                # an m3 rule, which carry the `m<digit>-` prefix and neither
                # half's name.
                rule('m2-ctx-from-session-only', 'absent', 'bytecode.ref',
                     'edu/sdsc/mmtf/.*',
                     'org/apache/spark/api/java/JavaSparkContext',
                     '<init>', '(?!\\(Lorg/apache/spark/SparkContext;\\)V$).*'),
                rule('m2-no-raw-sparkcontext', 'absent', 'bytecode.ref',
                     'edu/sdsc/mmtf/.*', 'org/apache/spark/SparkContext',
                     '<init>', '*'),
                rule('m1-spark-334', 'atleast:1', 'pom.resolved',
                     'org\\.apache\\.spark:spark-core_2\\.12', '3\\.3\\.4', '.+'),
                rule('m3-ansi-set', 'absent', 'probe.appsetconf',
                     'spark\\.sql\\.ansi\\.enabled', '(?!true$).*'))

        real_rules()
        want(session_rule_ids(d),
             (['m2-no-sparkconf-src', 'm2-no-sparkconf-jar'],
              ['m2-session-adopted'], ['m2-session-live']),
             'the gate reads its three parts out of m04.tsv by name, and the '
             'other four rules in the file are none of them')

        def f(rid, status):
            return (status, rid, 1 if status == 'PASS' else 3, 'why')

        gone_ok = [f('m2-no-sparkconf-src', 'PASS'),
                   f('m2-no-sparkconf-jar', 'PASS')]
        gone_red = [f(r[1], 'FAIL') for r in gone_ok]
        adopted_ok = [f('m2-session-adopted', 'PASS')]
        adopted_red = [f('m2-session-adopted', 'FAIL')]
        live_ok = [f('m2-session-live', 'PASS')]
        live_red = [f('m2-session-live', 'FAIL')]
        # Carried through the cases below so that the rows the gate must ignore
        # are present in the report the way they will be in a real one.
        other = [f('m2-ctx-from-session-only', 'PASS'),
                 f('m2-no-raw-sparkcontext', 'PASS'),
                 f('m1-spark-334', 'PASS'), f('m3-ansi-set', 'PASS')]

        want(session_adopted(d, d, gone_ok + adopted_ok + live_ok, quiet), True,
             'the confs gone, the sessions built in their place, and the '
             'programs reaching them is the whole condition')

        # The before-state: the tree Harbor hands this step builds a SparkConf
        # at 63 entry points and leaves 48 of its 78 main-bearing files under
        # src/main with no session in them at all.
        # This is the case the whole gate exists for -- without it the
        # untouched tree collects the replay and the repository tests in full,
        # because rung 04 is behaviourally silent and the c05b recordings answer
        # the same as the c04 ones.
        want(session_adopted(d, d, gone_red + adopted_red + live_red, quiet),
             False, "the untouched tree's own facts do not open the gate")

        # The pom-only tree, which is this step's cheapest near-miss: one
        # version bumped in pom.xml buys m1 outright, and buys it twice over
        # because the replay's spark-submit is the image's whatever the pom
        # says (NOTES 6.3.11). Nothing else about the tree has moved.
        want(session_adopted(d, d, gone_red + adopted_ok + live_ok, quiet), False,
             'a tree that edited pom.xml and nothing else has not made this '
             "step's move, whatever its runtime reports")

        # Half the condition, each way round. Deleting the confs and building
        # no session is not consolidating an entry point, it is removing one;
        # and adding a builder beside a SparkConf that is still there is the
        # tree that read the instruction and did not do it.
        want(session_adopted(d, d, gone_ok + adopted_red + live_ok, quiet),
             False,
             'the confs deleted and no session built in their place is not the '
             'move')
        want(session_adopted(d, d, [f('m2-no-sparkconf-src', 'PASS'),
                                    f('m2-no-sparkconf-jar', 'FAIL')]
                             + adopted_ok + live_ok, quiet), False,
             'a SparkConf still referenced from the jar is still a SparkConf, '
             'whatever the sources say')
        want(session_adopted(d, d, [f('m2-no-sparkconf-src', 'FAIL'),
                                    f('m2-no-sparkconf-jar', 'PASS')]
                             + adopted_ok + live_ok, quiet), False,
             'a SparkConf still in the sources is not answered by a jar that '
             'happens not to reference it')

        # The third part, on its own, against a tree that satisfies the other
        # two completely. The sources of all 78 main-bearing files under
        # src/main build a session and no SparkConf survives anywhere -- and
        # not one of the 96 programs the replay ran reached the session its own
        # file declares, because the declaration sits behind a branch that is
        # never taken, or after the work, or in a class the jar does not
        # invoke. On the artifact facts alone this tree is the golden one.
        want(session_adopted(d, d, gone_ok + adopted_ok + live_red, quiet),
             False,
             'sources that contain the idiom and programs that never reach it '
             'is not a tree whose entry points come off a session')

        # And a green `m2-ctx-from-session-only` is not a second way to satisfy
        # the adopted half. On the handed tree that rule is red, but it goes
        # green the moment a candidate deletes the 63 conf-taking constructions
        # without building anything -- so if the gate read it, the tree that
        # removed every entry point would be graded as having consolidated
        # them.
        want(session_adopted(d, d, gone_ok + adopted_red + live_ok + other,
                             quiet), False,
             'how the surviving contexts were made is not whether the tree '
             'built any session of its own')

        # A rule the report never mentions is not a rule that passed. rules.py
        # reports every rule it evaluates, so a missing id means the gate did
        # not run it, and reading it as green is how a gate opens on a tree
        # nothing was measured against.
        with must_raise('a gate report missing the jar half was accepted'):
            session_adopted(d, d, [f('m2-no-sparkconf-src', 'PASS')]
                            + adopted_ok + live_ok, quiet)
        with must_raise('a gate report naming no -session-adopted rule was '
                        'accepted; the second part would have been dropped '
                        'without anything going red'):
            session_adopted(d, d, gone_ok + live_ok, quiet)
        with must_raise('a gate report naming no -session-live rule was '
                        'accepted; the third part would have been dropped '
                        'without anything going red'):
            session_adopted(d, d, gone_ok + adopted_ok, quiet)
        with must_raise('an empty gate report was accepted'):
            session_adopted(d, d, [], quiet)

        # The ways m04.tsv itself could stop constraining anything. Each of
        # these is an edit somebody could make to the rule file in good faith,
        # and each leaves a gate that opens on the untouched tree. Each case
        # carries the parts it is not about, so that what it proves is the
        # omission it names rather than the first thing missing.
        adopted_rule = rule('m2-session-adopted', 'absent', 'ast.unit',
                            '(.*/)?src/main/.*', '[1-9][0-9]*', '.*', '0')
        live_rule = rule('m2-session-live', 'absent', 'probe.appsession',
                         '.+', '(?!true$).*')
        jar_rule = rule('m2-no-sparkconf-jar', 'absent', 'bytecode.ref',
                        'a', 'b', 'c', 'd')

        rules_file(adopted_rule, live_rule)
        with must_raise('a rule file with no -no-sparkconf- rule was accepted; '
                        'the first part of the gate would have been vacuous'):
            session_rule_ids(d)

        rules_file(jar_rule, live_rule)
        with must_raise('a rule file with no -session-adopted rule was '
                        'accepted; deleting the confs would have opened the '
                        'gate'):
            session_rule_ids(d)

        rules_file(jar_rule, adopted_rule)
        with must_raise('a rule file with no -session-live rule was accepted; '
                        'a tree whose sources carry the idiom and whose '
                        'programs never reach it would have opened the gate'):
            session_rule_ids(d)

        rules_file(rule('m2-no-sparkconf-jar', 'atleast:1', 'bytecode.ref',
                        'a', 'b', 'c', 'd'),
                   adopted_rule, live_rule)
        with must_raise('a -no-sparkconf- rule that was not `absent` was '
                        'accepted; it asks for a SparkConf to still be built'):
            session_rule_ids(d)

        # The shape cases, and the ones this step had to measure to write.
        # Every one of these is a rule somebody could put in the file in good
        # faith and every one of them is green on the handed tree, which has
        # 48 main-bearing files under src/main with no session in them.
        #
        #   a count      of `builder` call sites anywhere, which is what this
        #                rule was until the static review's round 1: thirty-six
        #                of them in one unused class clears any threshold
        #   any count    `[0-9]+` instead of `0` in the last field asks nothing
        #                at all: every count matches, including the honest ones
        #   src/test     the scanner is handed both halves of src, and the test
        #                tree has its own main-bearing files this step must not
        #                grade (m04.tsv)
        #   file-wide    `0` in the *third* field is the count of sessions
        #                built anywhere in the file, which one helper method
        #                answers for a main that builds none -- the shape the
        #                static review's round 3 found the old check passing
        #   three terms  the same rule as it stood before round 3, when the
        #                `ast.unit` row had two counts rather than three: the
        #                third field was the file-wide one and the rule read it
        #   any main     `.*` instead of `[1-9][0-9]*` selects the 102 files
        #                under src/main with no main method at all, which build
        #                no session because they have no entry point to put one
        #                in; the rule goes red on the golden tree
        for weak in (rule('m2-session-adopted', 'atleast:90', 'ast.call',
                          '(.*/)?src/main/.*', '*', 'builder', 'SparkSession'),
                     rule('m2-session-adopted', 'atleast:55', 'ast.unit',
                          '(.*/)?src/main/.*', '[1-9][0-9]*', '.*', '0'),
                     rule('m2-session-adopted', 'absent', 'ast.unit',
                          '(.*/)?src/main/.*', '[1-9][0-9]*', '.*', '[0-9]+'),
                     rule('m2-session-adopted', 'absent', 'ast.unit',
                          '(.*/)?src/test/.*', '[1-9][0-9]*', '.*', '0'),
                     rule('m2-session-adopted', 'absent', 'ast.unit',
                          '(.*/)?src/main/.*', '[1-9][0-9]*', '0', '.*'),
                     rule('m2-session-adopted', 'absent', 'ast.unit',
                          '(.*/)?src/main/.*', '[1-9][0-9]*', '0'),
                     rule('m2-session-adopted', 'absent', 'ast.unit',
                          '(.*/)?src/main/.*', '.*', '.*', '0')):
            rules_file(jar_rule, weak, live_rule)
            with must_raise('a -session-adopted rule that does not ask which '
                            'src/main programs have no session was accepted: '
                            '%s' % weak.split('\t')[2]):
                session_rule_ids(d)

        # And the same for the third part. Every one of these is green on the
        # handed tree, which brings up no session in 29 of the 96 programs the
        # replay runs.
        #
        #   a count      of programs that did reach one: 67 of the handed
        #                tree's 96 already do, because an earlier step added a
        #                session to 15 demos (NOTES 6.4.3), so a threshold low
        #                enough to be reachable is already two thirds met, and
        #                any threshold at all is what this step retired
        #   any value    `.*` instead of `(?!true$).*` matches nothing to
        #                complain about: `<no-session>` is as acceptable as
        #                `true`
        #   the wrong    `probe.probe` is the JVM-wide dump, one row per probe
        #   fact         file whatever the program did; only `probe.appsession`
        #                is per-program and only it can be absent
        for weak in (rule('m2-session-live', 'atleast:90', 'probe.appsession',
                          '.+', 'true'),
                     rule('m2-session-live', 'absent', 'probe.appsession',
                          '.+', '.*'),
                     rule('m2-session-live', 'absent', 'probe.probe',
                          'spark\\.version', '(?!true$).*')):
            rules_file(jar_rule, adopted_rule, weak)
            with must_raise('a -session-live rule that does not ask which '
                            'replayed programs reached no session was '
                            'accepted: %s' % weak.split('\t')[2]):
                session_rule_ids(d)

        rules_file(jar_rule,
                   rule('m2-session-adopted', 'absent', 'ast.unit',
                        'src/main/.*', '[1-9][0-9]*', '[0-9]+', '0'),
                   rule('m2-session-live', 'absent', 'probe.appsession',
                        '[A-Za-z].*', '(?!true$).+'))
        want(session_rule_ids(d),
             (['m2-no-sparkconf-jar'], ['m2-session-adopted'],
              ['m2-session-live']),
             'the path and label terms may be spelled other ways: what the '
             'shape checks rule out is a rule that reads a different fact, a '
             'different tree or a different count, not one written a '
             'different way')

        open(m04, 'w').write(
            'm2-x\tabsent\tast.newclass\\ta\\tb\\tSparkConf\\tc\twhy\n')
        with must_raise('a four-field rule line was accepted'):
            session_rule_ids(d)

        # cheats.tsv, cheats3.tsv and cheats4.tsv: the thirty whose price is the
        # whole reward. The list is read out of the files, so what is tested
        # here is the reading and the three ways it could be wrong in the
        # candidate's favour -- files that name nothing, a report that never
        # mentions a rule, and an id in more than one file, which rules.py
        # would refuse to compose at all. There are three files now rather than
        # two, and that is the point of driving the loop off CHEAT_FILES: step 4
        # adds cheats4.tsv and this test needed no new case to cover it, only a
        # third argument.
        real_rules()
        cheats = os.path.join(rules, 'cheats.tsv')
        cheats3 = os.path.join(rules, 'cheats3.tsv')
        cheats4 = os.path.join(rules, 'cheats4.tsv')

        def cheats_files(one, three, four=()):
            open(cheats, 'w').write('# a comment\n\n' + ''.join(one))
            open(cheats3, 'w').write('# a comment\n\n' + ''.join(three))
            open(cheats4, 'w').write('# a comment\n\n' + ''.join(four))

        cheats_files([rule('no-opaque-encoder', 'absent', 'ast.call', '*', '*',
                           'kryo', 'Encoders'),
                      rule('no-forged-execution-id', 'absent', 'probe.job', '*',
                           'forged:.*')],
                     [rule('no-legacy-conf-set', 'absent', 'probe.setconf',
                           'spark.sql.legacy..*', '*')],
                     [rule('no-sparkconf-reflect', 'absent',
                           'bytecode.strconst',
                           'org\\.apache\\.spark\\.SparkConf', '*')])
        want(cheat_rule_ids(d),
             ['no-opaque-encoder', 'no-forged-execution-id',
              'no-legacy-conf-set', 'no-sparkconf-reflect'],
             'the ids that void the whole reward are the contents of all three '
             'prohibition files, in file order')
        want(cheat_red([('PASS', 'no-opaque-encoder', 0, ''),
                        ('PASS', 'no-forged-execution-id', 0, ''),
                        ('PASS', 'no-legacy-conf-set', 0, ''),
                        ('PASS', 'no-sparkconf-reflect', 0, ''),
                        ('FAIL', 'm2-session-adopted', 0, '')], cheat_rule_ids(d)),
             [],
             'a tree that failed a requirement but faked nothing is not a faked '
             'tree')
        want([r[1] for r in cheat_red([('FAIL', 'no-legacy-conf-set', 2, 'x'),
                                       ('PASS', 'no-opaque-encoder', 0, ''),
                                       ('FAIL', 'no-sparkconf-reflect', 1, 'y'),
                                       ('PASS', 'm2-no-sparkconf-src', 1, '')],
                                      cheat_rule_ids(d))],
             ['no-forged-execution-id', 'no-legacy-conf-set',
              'no-sparkconf-reflect'],
             'a red prohibition is red wherever it was written, and one the '
             'report never mentions is not one that passed')

        cheats_files([], [], [])
        with must_raise('prohibition files naming no rule were accepted; nothing '
                        'would have voided the reward'):
            cheat_rule_ids(d)

        for which, one, three, four in (
                ('cheats.tsv',
                 [rule('m2-x', 'absent', 'ast.call', '*', '*', 'kryo', 'E')],
                 [], []),
                ('cheats3.tsv', [],
                 [rule('m2-x', 'absent', 'ast.call', '*', '*', 'kryo', 'E')],
                 []),
                ('cheats4.tsv', [], [],
                 [rule('m2-x', 'absent', 'ast.call', '*', '*', 'kryo', 'E')])):
            cheats_files(one, three, four)
            with must_raise('a requirement-prefixed id in %s was accepted; it '
                            'would have been paid for by the migrated family '
                            'and voided the reward at the same time' % which):
                cheat_rule_ids(d)

        one = rule('no-legacy-conf-set', 'absent', 'probe.setconf',
                   'spark.sql.legacy..*', '*')
        for a, b, c in (([one], [one], []), ([one], [], [one]),
                        ([], [one], [one])):
            cheats_files(a, b, c)
            with must_raise('an id named in two prohibition files was accepted; '
                            'rules.py would refuse to compose the gate at all, '
                            'so no tree could be scored against this payload'):
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
    # 46 and 140, not step 3's 47 and 142: CustomReportServiceTest's two tests
    # are outside both baselines at this rung (tests_score, NOTES 6.4.2). These
    # are the defaults because test.sh passes neither flag -- a flag it does not
    # pass is a number nobody reading test.sh can see, so the number lives where
    # the reasoning does.
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
