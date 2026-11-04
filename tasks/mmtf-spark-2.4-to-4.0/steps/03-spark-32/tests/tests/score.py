#!/usr/bin/env python3
"""Turn one scored run of step 3 into a reward in [0, 1].

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
replay happens *before* the gate, as it did at step 2. Five of this gate's
standing rules have no answer in the source tree at all -- rung3.tsv's, which
ask whether adaptive query execution actually re-planned something (an event
the engine emits at runtime or does not) and what the effective value of
`spark.sql.legacy.statisticalAggregate` is (a default nobody writes down until
somebody overrides it). Both are answers the session gives while it runs, so
the facts the gate reads are the replay's own probe rows, and a gate that ran
first would have nothing to read.

THE SHAPE OF THE ANSWER

    reward = T1 x T2   (user decision 2026-10-13: product scoring)
    T1 = (0.16 * m1 + 0.50 * m2) / 0.66                migrated-ness
    T2 = (0.24 * replay + 0.10 * tests) / 0.34         preservation

with one condition on the first two terms: they are graded only on a tree that
stopped calling `Dataset.describe()` and started naming the statistics it wants
instead. Both halves of that condition are artifact facts -- the call is gone
from the sources and from the jar, and one of the two classes that used to make
it now asks the Dataset for `summary` or `agg` instead -- which is a
deliberate difference from step 2, whose condition needed a run half. See describe_replaced.

and every one of those four numbers is printed with its own evidence, because
a reward is a summary and the thing a reviewer needs is the summary's parts.

Four ways the run can be void rather than bad, and they are not the same thing
as a candidate scoring zero:

  * a precondition failed -- the mirror is down, the payload is short a file,
    an instrument failed its own selftest. Nothing measured after that means
    anything (trap 5, trap 6).
  * a standing rule is red. A hundred and thirty-three of this gate's hundred
    and forty-two rules are standing: common.tsv's ten as floor.tsv tightens
    two of them, the ninety-three that `hold` renamed with a `held-` prefix out
    of step 1's m01.tsv and step 2's m02.tsv, the twenty-seven prohibitions of
    cheats.tsv and cheats3.tsv, and rung3.tsv's three engine facts. Two of the
    ninety-three are not held as step 2
    wrote them: m03.tsv overrides `held-m1-spark-303` and
    `held-m1-spark-uniform` so that they accept 3.0.3 *or* 3.2.4, because this
    step is the one that moves off 3.0.3 and the rules step 2 left behind
    would otherwise void a perfect tree. The tree Harbor hands this step --
    steps 1 and 2 finished, none of step 3 done -- passes all hundred and
    thirty-three, rung3.tsv's included: those three read the engine the replay ran
    on,
    and that engine is this image's whatever the candidate's pom says. So a red
    one says the reading is void, not that the candidate is wrong: the mirror
    fell over, or an instrument did not run, or an earlier step's work was
    undone, or something switched off a 3.2 default the reading assumes.
  * one of the twenty-seven prohibitions is red. Those are standing rules too,
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
a tree that still takes its summary off `describe()` has not made this step's
move, and paying it 0.34 for the recordings it reproduces by not having been
touched would make the nop non-zero (migration_prompt.md section 8: nop reports
0.0). That risk is sharper here than at step 2, not softer, and the reason is
worth stating plainly. This step *does* move the rung -- step 2 stayed at Spark
3.0.3 and step 3 lands on 3.2.4 -- so a candidate can satisfy m1 by editing one
version in `pom.xml`. An earlier draft of this file also paid that edit for
adaptive execution and for the 3.1 statistical-aggregate semantics, on the
reasoning that they arrive with the version; item 4.3 then measured what
actually arrives with it, and the answer was worse than "cheap". The replay
runs the *image's* Spark -- spark-core is `provided`, the shaded jar does not
carry it, and spark-submit is /opt/spark's -- so the handed tree, rebuilt
against 3.0.3 and replayed untouched, reported spark.version=3.2.4,
adaptive.enabled=true, a non-zero adaptiveUpdate on four of five sampled jobs
and statisticalAggregate=false. Those were not cheap requirements; they were
requirements nobody could fail. They are standing rules now (gates/rules/
rung3.tsv), and their weight went to m2, which no pom edit can satisfy.

What is left is one requirement the pom answers and one it cannot, and the
gate below is what stops the first from collecting the second's behaviour
marks: without it the one-line edit would take the replay and the repository
tests too, because the c04 recordings were made from a tree whose summaries
still come off `describe()`, and it would reach 0.74 for work the automated
migrator does unattended. Measured, after the repricing: that tree scores 0.16.
Rubric R4 calls a step answerable by editing pom.xml alone an automatic
Blocker; this is the arithmetic that keeps this one from being it. See
describe_replaced.
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


# --- the twenty-seven ways to fake this step ---------------------------------

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
CHEAT_FILES = ('cheats.tsv', 'cheats3.tsv')


def cheat_rule_ids(tests):
    """The ids in gates/rules/cheats.tsv and cheats3.tsv, read out by name.

    Every standing rule costs the migrated family. These twenty-seven cost the
    whole reward, behavior included, and the difference between the two prices is not
    a judgement this file makes -- it is which file the rule was written in. A
    list kept here as well would be a second copy of it, and on the day a
    twenty-eighth prohibition is written the copy is the one that would not
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
            classes += 1
            n = int(a.get('tests', 0) or 0)
            f = int(a.get('failures', 0) or 0)
            e = int(a.get('errors', 0) or 0)
            s = int(a.get('skipped', 0) or 0)
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


# --- did this tree replace describe(), for behavior to be worth asking about? -

# The three ids the gate is composed from, matched against `m03.tsv` rather
# than listed here. `-no-describe-...` says the old call is gone;
# `...-named-stats` says something named took its place. Both halves are
# artifact facts, read out of the same gate report the m1 and m2 requirements are
# scored from, so the gate and the requirements cannot disagree about what the
# tree did.
DESCRIBE_GONE = re.compile(r'^m\d+-no-describe-\S+$')
NAMED_STATS = re.compile(r'^m\d+-\S+-named-stats$')


def describe_rule_ids(tests):
    """(gone, named): the m03.tsv ids the behavior gate reads, by name.

    Not two lists of ids kept in this file. The behavior gate and the m1/m2
    requirements have to mean the same thing by "this tree stopped asking for
    describe() and asked for a named set of statistics instead", and two lists
    in two files mean the same thing right up until one of them is edited. So
    both are read out of the rule file the gate is composed from.

    Each half is checked for the mode that makes it say what the gate reads it
    as saying. A `-no-describe-...` rule that was not `absent` would be asking
    for describe() to be *present*, and a `...-named-stats` rule that was not
    `atleast:N` for some N >= 1 would be satisfied by a tree that named
    nothing; either way the gate would still run, and would open on the tree
    that did none of this step's work. Failing here instead is the whole point
    of reading the file rather than trusting it.
    """
    path = os.path.join(tests, 'gates', 'rules', 'm03.tsv')
    gone, named = [], []
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if len(f) != 5:
                raise ValueError('%s:%d: want 5 fields, got %d'
                                 % (path, n, len(f)))
            rid, mode = f[0], f[1]
            if DESCRIBE_GONE.match(rid):
                if mode != 'absent':
                    raise ValueError('%s:%d: %s is `%s`, not `absent`, so it '
                                     'asks for describe() to still be there '
                                     'and the gate would read a tree that '
                                     'never touched it as having moved'
                                     % (path, n, rid, mode))
                gone.append(rid)
            elif NAMED_STATS.match(rid):
                if not mode.startswith('atleast:'):
                    raise ValueError('%s:%d: %s is `%s`; the gate reads it as '
                                     '"a named statistic set is asked for at '
                                     'least once", which only `atleast:N` says'
                                     % (path, n, rid, mode))
                need = int(mode.split(':', 1)[1])
                if need < 1:
                    raise ValueError('%s:%d: %s asks for %d row(s); a threshold '
                                     'of zero is satisfied by a tree that named '
                                     'no statistic at all'
                                     % (path, n, rid, need))
                named.append(rid)
    if not gone:
        raise ValueError('%s names no `m<digit>-no-describe-...` rule, so the '
                         'first half of this gate is vacuously true and a tree '
                         'that still calls describe() on every column would be '
                         'read as having replaced it' % path)
    if not named:
        raise ValueError('%s names no `m<digit>-...-named-stats` rule, so the '
                         'second half of this gate is vacuously true and '
                         'deleting the calls and printing nothing would open it'
                         % path)
    return gone, named


def describe_replaced(run, tests, findings, log):
    """True when the tree made this step's move: describe() out, a named set in.

    Two facts, and the conjunction is the point. Every `-no-describe-...` rule
    must pass -- the call is gone from the sources the AST scanner walked and
    gone from the built jar's reference table -- *and* at least one
    `...-named-stats` rule must pass, meaning one of the two classes that used
    to call describe() now asks the Dataset for `summary` or `agg` and the
    constant pool carries the statistic names it asked for.

    Neither fact alone is the claim, and each buys a different tree without the
    other. Absence alone is bought by deleting the eleven calls in
    AnalyzeWaterInteractions4 and the one in PdbLigandDemo and printing
    nothing: the step's milestone is that the summary survives the move to a
    runtime where `describe()`'s column set is no longer what the recording
    was made against, and a tree with no summary at all has not moved it, it
    has dropped it. Presence alone is bought by adding a `summary()` call
    beside the `describe()` that is still there, which is the tree that read
    the instruction and did not do it.

    Both halves are artifact facts, which is why `run` is unused here and this
    gate has no probe half -- unlike step 2's, which needed one because its
    artifact evidence was a reference-level absence that a hardcoded print
    could buy. The probe facts at this census say what the session was
    configured to do (rung3.tsv's adaptive execution and statistical-aggregate
    semantics, standing rules since item 4.3 measured that the image answers
    them whatever the pom says), and a configuration read says nothing about
    which statistics a Dataset was asked for. A probe half here would make the gate answer a
    different question than the one it is named for -- and it would answer it
    from rows written by a listener inside the candidate's own driver JVM,
    where this one is answered from a jar the build produced.

    What the two halves do not do is prove the numbers came out the same. A
    candidate who replaces `describe(c)` with `summary("count")` satisfies
    both, and no reference-level fact can tell a five-statistic summary from a
    one-statistic one -- `m2-stats-listed` asks the constant pool for `stddev`
    and closes the cheapest version of that, but a tree that names the five and
    prints them in another order still passes here. That is not the gate's job:
    the replay family is what reads the numbers, and this gate is what decides
    whether the replay family is worth reading. NOTES 6.3.9 records the
    residual rather than hiding it.

    The behavior family asks whether the *migrated* tree still answers the way
    the original did. A tree that still takes its summary off `describe()`, the
    way the step-2 tree does, has not been asked that question yet: on this
    step's weights the reward for changing nothing would be 0.34 -- the replay
    family and the repository tests both pass untouched, because the c04
    recordings were made from that tree -- and section 8 of migration_prompt.md
    says the nop reports 0.0.

    Beyond that, it is the weakest condition that separates a tree which made
    the move from one that did not: the calls gone, and *one* of the two
    classes naming what it asks for. A candidate who converted
    AnalyzeWaterInteractions4's eleven and left PdbLigandDemo's one keeps every
    point of proportional behavior credit and loses only the requirement rule
    that measures the class they left. There is no cliff here for anybody who
    tried.
    """
    del run  # both halves are artifact facts; see the docstring
    gone, named = describe_rule_ids(tests)
    by_id = dict((r[1], r) for r in findings)

    missing = [rid for rid in gone + named if rid not in by_id]
    if missing:
        raise ValueError('the gate report does not mention %s, so %s of this '
                         'gate never ran and reading it as passed would open it '
                         'on a tree nothing was measured against'
                         % (' '.join(missing),
                            'half' if len(missing) < len(gone + named)
                            else 'neither half'))

    red = [rid for rid in gone if by_id[rid][0] != 'PASS']
    if red:
        log('behav  NO       describe() is still there: %d of %d rule(s) that '
            'say it is gone did not pass (%s), so whatever else the tree names, '
            'the call this step is about is still the one taking the summary'
            % (len(red), len(gone), ' '.join(red)))
        return False
    log('behav  GONE     all %d rule(s) agree describe() is gone from the '
        'sources and from the jar: %s' % (len(gone), ' '.join(gone)))

    hits = [rid for rid in named if by_id[rid][0] == 'PASS']
    if not hits:
        log('behav  NO       %d rule(s) look for a named statistic set and none '
            'of them found one (%s): the calls were deleted, not replaced, and '
            'the summary this step is about is not being taken at all'
            % (len(named), ' '.join(named)))
        return False
    log('behav  YES      %d of %d rule(s) find a named statistic set: %s'
        % (len(hits), len(named), ' '.join(hits)))
    short = [rid for rid in named if rid not in hits]
    if short:
        log('behav  PARTIAL %d class(es) still take no named summary: %s'
            % (len(short), ' '.join(short)))
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
    graded = describe_replaced(run, tests, mine, log)
    parts['replay'] = weights['behavior']['replay'][0] * (replay if graded else 0.0)
    parts['tests'] = weights['behavior']['tests'][0] * (tscore if graded else 0.0)
    if not graded:
        log('')
        log('BEHAVIOR NOT GRADED -- this tree still takes its summary off')
        log('describe(), or it deleted the calls and put nothing in their place, so')
        log('it has not made the move this family asks its question about. The c04')
        log('recordings were made from the tree that calls describe(), which is why')
        log('a tree that changed nothing reproduces them: matching a recording of')
        log('the thing you did not change is not evidence that you changed it')
        log('safely. The two measurements above stand as evidence and')
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

    # describe_replaced, and its half of the pricing. Every case here that must
    # come back False is a way the tree Harbor hands this step could have
    # collected 0.34 of the reward for changing nothing, and the ones that must
    # raise are ways a later edit to m03.tsv could have made the gate
    # unconditional without anything going red.
    with tempfile.TemporaryDirectory() as d:
        quiet = lambda s: None
        rules = os.path.join(d, 'gates', 'rules')
        os.makedirs(rules)
        m03 = os.path.join(rules, 'm03.tsv')

        def rule(rid, mode, *terms):
            return '%s\t%s\t%s\tc.tsv\twhy\n' % (rid, mode, '\\t'.join(terms))

        def rules_file(*lines):
            open(m03, 'w').write('# a comment\n\n' + ''.join(lines))

        # The shape m03.tsv actually has: two rules saying the call is gone --
        # one over the sources the AST scanner walked, one over the built jar's
        # reference table -- and three saying something named took its place,
        # two of them naming a class and one naming the constant pool.
        def real_rules():
            rules_file(
                rule('m2-no-describe-src', 'absent', 'ast.call', '*', '*',
                     'describe', '*'),
                rule('m2-no-describe-jar', 'absent', 'bytecode.ref',
                     'edu/sdsc/mmtf/.*', 'org/apache/spark/sql/Dataset',
                     'describe', '*'),
                rule('m2-awi4-named-stats', 'atleast:1', 'bytecode.ref',
                     'edu/sdsc/.*/AnalyzeWaterInteractions4',
                     'org/apache/spark/sql/Dataset', '(summary|agg)', '*'),
                rule('m2-pdbligand-named-stats', 'atleast:1', 'bytecode.ref',
                     'edu/sdsc/.*/PdbLigandDemo',
                     'org/apache/spark/sql/Dataset', '(summary|agg)', '*'),
                # Three rules of this step that the gate must ignore. The
                # first is m2's own, and the most interesting of the three:
                # `m2-stats-listed` says the constant pool carries `stddev`,
                # which is *which* statistics were named, not *whether* any
                # were. Reading it as a third way to satisfy the second half
                # would open the gate on a tree that kept every describe() and
                # named a statistic somewhere else entirely. The other two are
                # an m1 rule, whose name carries the `m<digit>-` prefix and
                # neither half, and a standing rule out of rung3.tsv, which
                # carries no prefix at all.
                rule('m2-stats-listed', 'atleast:1', 'bytecode.strconst',
                     'stddev', '*'),
                rule('m1-spark-324', 'atleast:1', 'pom.prop',
                     'spark.version', '3\\.2\\.4'),
                rule('rung3-stat-aggregate-modern', 'atleast:1', 'probe.conf',
                     'spark.sql.legacy.statisticalAggregate', 'false'))

        real_rules()
        want(describe_rule_ids(d),
             (['m2-no-describe-src', 'm2-no-describe-jar'],
              ['m2-awi4-named-stats', 'm2-pdbligand-named-stats']),
             'the gate reads its two halves out of m03.tsv by name, and the '
             'other three rules in the file are neither half')

        def f(rid, status):
            return (status, rid, 1 if status == 'PASS' else 3, 'why')

        gone_ok = [f('m2-no-describe-src', 'PASS'), f('m2-no-describe-jar', 'PASS')]
        named_ok = [f('m2-awi4-named-stats', 'PASS'),
                    f('m2-pdbligand-named-stats', 'PASS')]
        named_red = [f(r[1], 'FAIL') for r in named_ok]
        # Carried through the cases below so that the rows the gate must ignore
        # are present in the report the way they will be in a real one.
        other = [f('m2-stats-listed', 'PASS'), f('m1-spark-324', 'PASS'),
                 f('rung3-stat-aggregate-modern', 'PASS')]

        want(describe_replaced(d, d, gone_ok + named_ok, quiet), True,
             'the calls gone and a named statistic set in their place is the '
             'whole condition')

        # The before-state: the tree Harbor hands this step calls describe()
        # twelve times and names no statistic anywhere. This is the case the
        # whole gate exists for -- without it the untouched tree collects the
        # replay and the repository tests in full, because the c04 recordings
        # were made from it.
        want(describe_replaced(d, d, [f('m2-no-describe-src', 'FAIL'),
                                      f('m2-no-describe-jar', 'FAIL')] + named_red,
                               quiet), False,
             "the untouched tree's own facts do not open the gate")

        # The pom-only tree, which is this step's cheapest near-miss: one
        # version bumped in pom.xml buys m1 outright -- and, until item 4.3
        # measured it and demoted them, would have bought two more
        # requirements for facts the image answers on its own. Nothing else
        # about the tree has moved.
        want(describe_replaced(d, d, [f('m2-no-describe-src', 'FAIL'),
                                      f('m2-no-describe-jar', 'FAIL')] + named_red,
                               quiet), False,
             'a tree that edited pom.xml and nothing else has not made this '
             "step's move, whatever its runtime reports")

        # Half the condition, each way round. Deleting the calls and printing
        # nothing is not migrating a summary, it is dropping one; and adding a
        # summary() beside a describe() that is still there is the tree that
        # read the instruction and did not do it.
        want(describe_replaced(d, d, gone_ok + named_red, quiet), False,
             'the calls deleted and nothing named in their place is not the '
             'move')
        want(describe_replaced(d, d, [f('m2-no-describe-src', 'PASS'),
                                      f('m2-no-describe-jar', 'FAIL')] + named_ok,
                               quiet), False,
             'a describe() still in the jar is still a describe(), whatever the '
             'sources say')
        want(describe_replaced(d, d, [f('m2-no-describe-src', 'FAIL'),
                                      f('m2-no-describe-jar', 'PASS')] + named_ok,
                               quiet), False,
             'a describe() still in the sources is not answered by a jar that '
             'happens not to carry it')

        # No cliff: one of the two classes is enough, which is the same
        # property step 2's gate has and for the same reason.
        want(describe_replaced(d, d, gone_ok + [f('m2-awi4-named-stats', 'PASS'),
                                                f('m2-pdbligand-named-stats', 'FAIL')]
                               + other, quiet), True,
             'one of the two classes naming its statistics is enough; a '
             'candidate who converted eleven calls and deleted the twelfth '
             'without replacing it keeps the behavior credit')
        want(describe_replaced(d, d, gone_ok + named_red + other, quiet), False,
             'and a green m2-stats-listed is not a third way to satisfy that '
             'half: which statistics were named is not whether any were')

        # A rule the report never mentions is not a rule that passed. rules.py
        # reports every rule it evaluates, so a missing id means the gate did
        # not run it, and reading it as green is how a gate opens on a tree
        # nothing was measured against.
        with must_raise('a gate report missing the jar half was accepted'):
            describe_replaced(d, d, [f('m2-no-describe-src', 'PASS')] + named_ok,
                              quiet)
        with must_raise('a gate report naming no -named-stats rule was accepted; '
                        'the second half would have been dropped without '
                        'anything going red'):
            describe_replaced(d, d, gone_ok, quiet)
        with must_raise('an empty gate report was accepted'):
            describe_replaced(d, d, [], quiet)

        # The four ways m03.tsv itself could stop constraining anything. Each
        # of these is an edit somebody could make to the rule file in good
        # faith, and each leaves a gate that opens on the untouched tree.
        rules_file(rule('m2-awi4-named-stats', 'atleast:1', 'bytecode.ref',
                        'a', 'b', 'c', '*'))
        with must_raise('a rule file with no -no-describe- rule was accepted; '
                        'the first half of the gate would have been vacuous'):
            describe_rule_ids(d)

        rules_file(rule('m2-no-describe-jar', 'absent', 'ast.call', '*', '*',
                        'describe', '*'))
        with must_raise('a rule file with no -named-stats rule was accepted; '
                        'deleting the calls would have opened the gate'):
            describe_rule_ids(d)

        rules_file(rule('m2-no-describe-jar', 'atleast:1', 'ast.call', '*', '*',
                        'describe', '*'),
                   rule('m2-awi4-named-stats', 'atleast:1', 'bytecode.ref',
                        'a', 'b', 'c', '*'))
        with must_raise('a -no-describe- rule that was not `absent` was '
                        'accepted; it asks for describe() to still be there'):
            describe_rule_ids(d)

        rules_file(rule('m2-no-describe-jar', 'absent', 'ast.call', '*', '*',
                        'describe', '*'),
                   rule('m2-awi4-named-stats', 'absent', 'bytecode.ref',
                        'a', 'b', 'c', '*'))
        with must_raise('a -named-stats rule that was not `atleast:N` was '
                        'accepted'):
            describe_rule_ids(d)

        rules_file(rule('m2-no-describe-jar', 'absent', 'ast.call', '*', '*',
                        'describe', '*'),
                   rule('m2-awi4-named-stats', 'atleast:0', 'bytecode.ref',
                        'a', 'b', 'c', '*'))
        with must_raise('a threshold of zero was accepted; the gate would open '
                        'on a tree that named no statistic at all'):
            describe_rule_ids(d)

        open(m03, 'w').write('m2-x\tabsent\tast.call\\ta\\tb\\tdescribe\\tc\twhy\n')
        with must_raise('a four-field rule line was accepted'):
            describe_rule_ids(d)

        # cheats.tsv and cheats3.tsv: the twenty-seven whose price is the whole
        # reward. The list is read out of the files, so what is tested here is
        # the reading and the three ways it could be wrong in the candidate's
        # favour -- files that name nothing, a report that never mentions a
        # rule, and an id in both files, which rules.py would refuse to compose
        # at all.
        real_rules()
        cheats = os.path.join(rules, 'cheats.tsv')
        cheats3 = os.path.join(rules, 'cheats3.tsv')

        def cheats_files(one, three):
            open(cheats, 'w').write('# a comment\n\n' + ''.join(one))
            open(cheats3, 'w').write('# a comment\n\n' + ''.join(three))

        cheats_files([rule('no-opaque-encoder', 'absent', 'ast.call', '*', '*',
                           'kryo', 'Encoders'),
                      rule('no-forged-execution-id', 'absent', 'probe.job', '*',
                           'forged:.*')],
                     [rule('no-legacy-conf-set', 'absent', 'probe.setconf',
                           'spark.sql.legacy..*', '*')])
        want(cheat_rule_ids(d),
             ['no-opaque-encoder', 'no-forged-execution-id', 'no-legacy-conf-set'],
             'the ids that void the whole reward are the contents of both '
             'prohibition files, in file order')
        want(cheat_red([('PASS', 'no-opaque-encoder', 0, ''),
                        ('PASS', 'no-forged-execution-id', 0, ''),
                        ('PASS', 'no-legacy-conf-set', 0, ''),
                        ('FAIL', 'm2-awi4-named-stats', 0, '')], cheat_rule_ids(d)),
             [],
             'a tree that failed a requirement but faked nothing is not a faked '
             'tree')
        want([r[1] for r in cheat_red([('FAIL', 'no-legacy-conf-set', 2, 'x'),
                                       ('PASS', 'no-opaque-encoder', 0, ''),
                                       ('PASS', 'm2-stats-listed', 1, '')],
                                      cheat_rule_ids(d))],
             ['no-forged-execution-id', 'no-legacy-conf-set'],
             'a red prohibition is red wherever it was written, and one the '
             'report never mentions is not one that passed')

        cheats_files([], [])
        with must_raise('prohibition files naming no rule were accepted; nothing '
                        'would have voided the reward'):
            cheat_rule_ids(d)

        cheats_files([rule('m2-x', 'absent', 'ast.call', '*', '*', 'kryo', 'E')], [])
        with must_raise('a requirement-prefixed id in cheats.tsv was accepted; it '
                        'would have been paid for and voided the reward at once'):
            cheat_rule_ids(d)

        cheats_files([], [rule('m2-x', 'absent', 'ast.call', '*', '*', 'kryo', 'E')])
        with must_raise('a requirement-prefixed id in cheats3.tsv was accepted'):
            cheat_rule_ids(d)

        one = rule('no-legacy-conf-set', 'absent', 'probe.setconf',
                   'spark.sql.legacy..*', '*')
        cheats_files([one], [one])
        with must_raise('an id named in both prohibition files was accepted; '
                        'rules.py would refuse to compose the gate at all, so '
                        'no tree could be scored against this payload'):
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
    ap.add_argument('--expect-classes', type=int, default=47)
    ap.add_argument('--expect-tests', type=int, default=142)
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
