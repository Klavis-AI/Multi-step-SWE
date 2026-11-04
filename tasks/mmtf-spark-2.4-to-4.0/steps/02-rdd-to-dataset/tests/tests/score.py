#!/usr/bin/env python3
"""Turn one scored run of step 2 into a reward in [0, 1].

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
                              ran inside, or `-` for none
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
replay happens *before* the gate, which is the reverse of step 1. The question
this step asks -- does the count come back through the SQL engine -- has no
answer in the source tree, because `createDataset` and `createDataFrame` and
`toDF` and `as(Encoders.bean(...))` are all the same migration spelled four
ways. It has an answer in what the tree did when it ran, so the facts the gate
reads are the replay's own probe rows, and a gate that ran first would have
nothing to read.

THE SHAPE OF THE ANSWER

    reward = T1 x T2   (user decision 2026-10-13: product scoring)
    T1 = (sum over m1..m4 of its weight) / 0.70        migrated-ness
    T2 = (0.22 * replay + 0.08 * tests) / 0.30         preservation

with two conditions on the first two terms, and neither of them is the claim on
its own: they are graded only on a tree where at least one of the fifteen scoped
jobs ran inside a SQL execution *and* at least one of the fifteen scoped classes
no longer takes a count or a reduce off an RDD. One of those facts is about the
run and one is about the artifact, which is the point: the first is written by
an instrument inside the candidate's own driver and is therefore forgeable by
the candidate's own code, and the second is not. See dataset_terminal_reached.

and every one of those six numbers is printed with its own evidence, because
a reward is a summary and the thing a reviewer needs is the summary's parts.

Four ways the run can be void rather than bad, and they are not the same thing
as a candidate scoring zero:

  * a precondition failed -- the mirror is down, the payload is short a file,
    an instrument failed its own selftest. Nothing measured after that means
    anything (trap 5, trap 6).
  * a standing rule is red. Fifty-one of this gate's hundred and fifteen
    rules are standing: common.tsv's ten, the twenty-five of step 1's m01.tsv
    that `hold` renamed to `held-m1-...`, cheats.tsv's twelve, and the four in
    m02.tsv whose ids are deliberately not `m<digit>-` -- the two that keep
    MmtfReader's RDD contract, and the two that say the replay ran
    instrumented and that nothing this verifier submitted failed.
    The tree Harbor hands this step -- step 1
    finished, none of step 2 done -- passes all fifty-one, so a red one says
    the reading is void, not that the candidate is wrong: the mirror fell over,
    or an instrument did not run, or step 1's work was undone.
  * one of cheats.tsv's twelve is red. Those are standing rules too, and by
    the arithmetic above they would void the migrated family and leave the
    behavior family standing. They do not: an opaque encoder, a hand-set
    execution id, a forged one, the two reflective spellings of the first two,
    the two ways a jar reaches for the instrument that reads it, and -- added
    by round 3 of the item-5 review -- the two ways a class looks a method up
    by name at all and the one way a jar puts a class of its own where the
    observer's belongs are the
    ways this step is made to look done without being done, and a
    tree that did that is not a tree whose recordings are worth paying for.
    The ids are read out of gates/rules/cheats.tsv rather than listed here, so
    the file and the arithmetic cannot drift apart.
  * the two evaluations of the gate disagree. Same rules, same facts, one run
    as nobody and one as root over a copy -- rules.py is deterministic, so a
    difference means the facts moved between being written and being read.

The first voids everything and is a verifier failure, reported as such. The
second and third void the migrated-ness family only: the behavior family is
measured from recordings, not from facts, and a tree that does not build has
already lost the replay on its own merits. The fourth is the one exception, and
it is deliberate: it zeroes the reward outright, behavior included.

And one way the behavior family is not graded at all rather than graded badly:
a tree that took none of the fifteen scoped jobs through the SQL engine has not
made this step's move, and paying it 0.30 for the recordings it reproduces by
not having been touched would make the nop non-zero (migration_prompt.md
section 8: nop reports 0.0). That risk is real here and not a formality,
because this step does not move the rung: step 1 landed on Spark 3.0.3 and
step 2 stays there, so the untouched 2.4 tree still builds and still answers
most of these 140 recordings the same way. Step 1 asked its version of this
question of the build (spark_line_moved: does the resolved graph still say
2.4?). Step 2 cannot ask it of the build, because step 2 does not change the
build -- `environment/` is byte-identical to step 1's -- so it asks it of the
run instead. See dataset_terminal_reached.
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

    Exactly, not approximately. These are seven literals in a file a human
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


# --- the twelve ways to fake this step ---------------------------------------

def cheat_rule_ids(tests):
    """The ids in gates/rules/cheats.tsv, read out of that file by name.

    Every standing rule costs the migrated family. These twelve cost the whole
    reward, behavior included, and the difference between the two prices is not
    a judgement this file makes -- it is which file the rule was written in. A
    list kept here as well would be a second copy of it, and on the day a
    thirteenth prohibition is written the copy is the one that would not have
    it (rule 4, in the verifier's own terms).
    """
    path = os.path.join(tests, 'gates', 'rules', 'cheats.tsv')
    ids = []
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
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
            ids.append(f[0])
    if not ids:
        raise ValueError('%s names no rule, so nothing would void the reward and '
                         'the instruction\'s "a faked tree is worth nothing, '
                         'behaviour included" would be false of the arithmetic'
                         % path)
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


# --- is there a migrated run for the behavior family to ask about? -----------

EXEC_ID = '[0-9]+'        # the value term of a rule that wants a real execution


def scoped_job_patterns(tests):
    """[(id, label pattern, how many rows that rule wants)] from m02.tsv.

    Not a list of class names kept in this file. The behavior gate and the
    m1..m4 requirements have to mean the same thing by "this tree took a scoped
    count through the SQL engine", and two lists in two files mean the same
    thing right up until one of them is edited. So the patterns are read out of
    the rule file the gate is composed from: every requirement rule (id
    `m<digit>-...`) that asks for at least one probe.job row whose value is an
    execution id contributes its pattern and its threshold, and there are
    sixteen of them at this census.

    The threshold comes along with the pattern because three of the sixteen
    have one that is not 1. AuthorSearchDemo, KeywordSearch and KinaseSearch
    each run two SQL executions before this step begins -- PdbjMineSearch's
    constructor reads its query through the SQL engine -- so their rules ask
    for three, and a gate that ignored that would be green on the untouched
    tree. It was: see NOTES 6.2.6.

    The `absent ... \t-` rules are skipped, and so is anything whose value term
    is not the execution-id pattern: those say where a job must *not* be, and a
    gate built out of them would open on the tree that did nothing.
    """
    path = os.path.join(tests, 'gates', 'rules', 'm02.tsv')
    pats = []
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            f = line.rstrip('\n').split('\t')
            if len(f) != 5:
                raise ValueError('%s:%d: want 5 fields, got %d'
                                 % (path, n, len(f)))
            rid, mode, terms = f[0], f[1], f[2].split('\\t')
            if not REQUIREMENT.match(rid) or not mode.startswith('atleast:'):
                continue
            if len(terms) != 3 or terms[0] != 'probe.job' or terms[2] != EXEC_ID:
                continue
            need = int(mode.split(':', 1)[1])
            if need < 1:
                raise ValueError('%s:%d: %s asks for %d row(s); a threshold of '
                                 'zero is satisfied by a run that did nothing'
                                 % (path, n, rid, need))
            pats.append((rid, re.compile(terms[1]), need))
    if not pats:
        raise ValueError('%s names no requirement rule asking for a probe.job '
                         'row inside an execution, so there is nothing for the '
                         'behavior gate to read and it would be unconditional'
                         % path)
    return pats


# The artifact half of the gate: three rules per scoped class, and the id
# they share is the class. `-rdd-gone` says the old terminal is gone,
# `-dataset-built` that an RDD was handed to the engine, `-dataset-terminal`
# that a result came back off the Dataset. Read as a group rather than a
# list because the claim is about one class doing all three.
ARTIFACT = re.compile(r'^(m\d+-\S+?)-(rdd-gone|dataset-built|dataset-terminal)$')
ARTIFACT_PARTS = ('rdd-gone', 'dataset-built', 'dataset-terminal')


def dataset_terminal_reached(run, tests, findings, log):
    """True when the run shows this step's move and the artifact shows it too.

    Two facts, and the conjunction is the point. One of the fifteen scoped
    classes must show the whole move in the built jar -- it takes no `count`
    or `reduce` off a JavaRDD or a JavaPairRDD (`-rdd-gone`), it hands an RDD
    to `createDataset`/`createDataFrame` (`-dataset-built`), and it reads a
    result back off the `Dataset` it got (`-dataset-terminal`) -- *and* one of
    the sixteen scoped probe rows must have cleared its own rule's threshold.

    Neither fact alone is the claim. The probe rows are written by a listener
    running inside the candidate's own driver JVM, which means the candidate's
    own `main` can produce them without doing the work: one
    `SparkSession.range(1).count()` beside an untouched `pdb.count()` buys a
    job inside an execution, and so does writing the listener's output file
    directly from a shutdown hook, which runs after GateProbe has dumped.
    Nothing a candidate's JVM writes down about itself is unforgeable, so the
    run half is the weaker of the two and the artifact half is what has to
    carry the claim.

    Which is why the artifact half is three rules and not one. An absent RDD
    terminal, alone, is bought by deleting the count and printing the constant
    it used to print: the corpus is fixed, so stdout still matches the
    recordings, and a forged probe row supplies the rest. That tree -- the
    cheapest 1.0 this step has -- fails `-dataset-built` and
    `-dataset-terminal` on all fifteen classes, because it never builds a
    Dataset from anything and never reads one. Together the three are this
    step: the count that used to be an RDD action is gone, the data that used
    to be counted is what went to the engine, and the answer came back from
    there.

    What they do not do is prove dataflow. A candidate who writes
    `spark.createDataset(sc.parallelize(ONE).rdd(), Encoders.INT()).count()`
    beside a hardcoded print satisfies all three from the constant pool, and
    no reference-level fact can tell that Dataset from the reader's. The rules
    do not close that; they price it, and at that price doing the migration is
    the cheaper move. NOTES 6.2.7 records the residual rather than hiding it.

    The behavior family asks whether the *migrated* tree still answers the way
    the original did. A tree that ran every one of these fifteen counts on an
    RDD, the way the 2.4 tree does, has not been asked that question yet: the
    reward for it would be 0.30 for changing nothing, and section 8 of
    migration_prompt.md says the nop reports 0.0.

    Beyond the forgery, it is the weakest condition that separates a tree which
    made the move from one that did not: one class, one row, each judged the
    way its own rule judges it. So a candidate who migrated one of the fifteen
    classes and gave up keeps every point of proportional behavior credit and
    loses only the requirements that measure the other fourteen -- and one
    whose single migrated class is AuthorSearchDemo is measured the same way,
    against the three executions that class has to show rather than the two it
    starts with. There is no cliff here for anybody who tried. Neither half is
    per-class-paired for the same reason: pairing them would close the gate on
    a candidate whose one migrated class is not the one the probe happened to
    clear, which is a cliff for somebody who tried in order to price out a
    forgery that is already worth less than trying.

    Four things the run half is careful about, and each of them is a way a tree
    that did none of this step's work could have opened this gate:

      * `-`, a job outside every execution, is not an execution. That is what
        every one of the thirteen m1/m2/m3 rows looks like before the step.
      * the three m4 rows do *not* look like that before the step, which is
        what the thresholds are for. Two executions is what PdbjMineSearch
        costs; the third is the candidate's count.
      * `forged:<id>`, which GateProbe writes when a job claims an execution id
        no SparkListenerSQLExecutionStart ever announced, is not an execution
        either. One `sc.setLocalProperty("spark.sql.execution.id", "0")` makes
        every job in a run claim one; the probe is what notices, and three of
        cheats.tsv's rules refuse the call outright -- by name, by reference
        and by the string constant a reflective spelling cannot avoid.
      * a job whose label is not one of the sixteen scoped ones does not count,
        however real its execution is. Nineteen of this repository's other
        demos already build Datasets -- AtpInteractionAnalysis, the whole
        datasets/ package -- and their executions are in this same file.

    Read out of gateroot/probe.tsv -- root's copy, taken before the
    repository's own suite runs as the uid that wrote the original -- and out
    of the same gate report the m1..m4 requirements are scored from, so the
    gate and the requirements cannot disagree about what the tree did.
    """
    seen = collections.defaultdict(dict)
    for row in findings:
        m = ARTIFACT.match(row[1])
        if m:
            seen[m.group(1)][m.group(2)] = row[0]
    whole = dict((k, v) for k, v in seen.items()
                 if all(p in v for p in ARTIFACT_PARTS))
    if not whole:
        raise ValueError('the gate report names no class carrying all of %s, so '
                         'the artifact half of this gate is vacuously true and a '
                         'probe row the candidate wrote by hand would open it '
                         'alone'
                         % ', '.join('`m<digit>-...-%s`' % p
                                     for p in ARTIFACT_PARTS))
    moved = sorted(k for k, v in whole.items()
                   if all(v[p] == 'PASS' for p in ARTIFACT_PARTS))
    if not moved:
        log('behav  NO       none of the %d scoped class(es) shows the move in '
            'the jar -- the RDD count is still there, or no RDD was handed to '
            'the engine, or no result was read back off a Dataset -- so '
            'whatever the probe rows say, no terminal moved to the engine'
            % len(whole))
        return False
    log('behav  MOVED    %d of %d scoped class(es) drop the RDD terminal, build '
        'a Dataset from an RDD and read a result back off it: %s'
        % (len(moved), len(whole), ' '.join(moved[:3])))

    path = os.path.join(run, 'gateroot', 'probe.tsv')
    if not os.path.exists(path):
        log('behav  NO       the probe rows were never written: %s' % path)
        return False
    pats = scoped_job_patterns(tests)
    jobs = forged = outside = 0
    inside = collections.Counter()
    for line in open(path, errors='replace'):
        f = line.rstrip('\n').split('\t')
        if len(f) != 3 or f[0] != 'job':
            continue
        jobs += 1
        key, value = f[1], f[2]
        if value.startswith('forged:'):
            forged += 1
            continue
        if not value.isdigit():
            outside += 1
            continue
        for rid, pat, _need in pats:
            if pat.fullmatch(key):
                inside[rid] += 1
    hits = [(rid, inside[rid], need) for rid, _pat, need in pats
            if inside[rid] >= need]
    near = ['%s %d/%d' % (rid, inside[rid], need) for rid, _pat, need in pats
            if 0 < inside[rid] < need]
    if not hits:
        log('behav  NO       %d job(s) probed against %d scoped pattern(s): none '
            'of them cleared its threshold (%d job(s) outside every execution, '
            '%d forged id(s))' % (jobs, len(pats), outside, forged))
        if near:
            log('behav  SHORT   %s' % ' '.join(near))
        return False
    log('behav  YES      %d job(s) probed; %d of %d scoped row(s) cleared their '
        'threshold, e.g. %s'
        % (jobs, len(hits), len(pats),
           ' '.join('%s %d/%d' % h for h in hits[:3])))
    if near:
        log('behav  SHORT   %s' % ' '.join(near))
    if forged:
        log('behav  FORGED  %d job(s) claimed an execution id nothing started; '
            'not counted here, and m02.tsv voids the reading for them' % forged)
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
        # Counted off the report rather than written out. The same sentence
        # went stale twice, and a verifier that miscounts its own gate in the
        # log a candidate reads is an instrument disagreeing with itself.
        n_all = len(mine)
        n_req = len([r for r in mine if re.match(r'^m\d+-', r[1])])
        n_held = len([r for r in mine if r[1].startswith('held-')])
        log('The %d standing rules of this gate -- common.tsv\'s floor,'
            % (n_all - n_req))
        log('the %d held from step 1, cheats.tsv\'s %d prohibitions'
            % (n_held, len(cheat_ids)))
        log('and m02.tsv\'s own contracts -- are preconditions for reading the')
        log('%d requirement rules above them, and the tree Harbor hands' % n_req)
        log('this step passes all %d. A red one says the reading is void,'
            % (n_all - n_req))
        log('not that the candidate is wrong: the mirror fell over, or an')
        log('instrument did not run, or step 1\'s work was undone. So this family')
        log('scores 0 and the behavior family below still stands -- unless the red')
        log('one came from cheats.tsv, which is the single exception and is')
        log('reported under its own heading below.')

    # Both behavior instruments are run and logged whatever the answer below
    # is: the evidence for "this tree reproduces 131 of 140 recordings" is
    # worth having even when it earns nothing, and a reader who cannot see it
    # cannot tell a zero that was measured from a zero that was assumed.
    replay, _counts = replay_score(tests, run, log)
    tscore = tests_score(run, expect_classes, expect_tests, log)
    graded = dataset_terminal_reached(run, tests, mine, log)
    parts['replay'] = weights['behavior']['replay'][0] * (replay if graded else 0.0)
    parts['tests'] = weights['behavior']['tests'][0] * (tscore if graded else 0.0)
    if not graded:
        log('')
        log('BEHAVIOR NOT GRADED -- no job of the fifteen scoped classes ran inside')
        log('a SQL execution, so this tree has not made the move this family asks')
        log('its question about. The two measurements above stand as evidence and')
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
        log('FAKED -- %d of the %d rules in gates/rules/cheats.tsv did not pass:'
            % (len(cheated), len(cheat_ids)))
        for status, rid, count, reason in cheated:
            log('    %-8s %-28s %s (%d match(es))' % (status, rid, reason, count))
        log('Those rules are the ways this step can be made to look done without')
        log('being done: an opaque encoder carrying rows the engine never reads as')
        log('columns, an execution id set by hand or forged outright, and the')
        log('reflective spellings of both. A tree that did one of them did not')
        log('migrate anything, and its recordings match because nothing moved --')
        log('so the reward is 0, behavior included, which is what the instruction')
        log('says in as many words.')
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
        gate_scores([('PASS', 'm3-a', 0, 'r')], w, lambda s: None)

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

    # dataset_terminal_reached, and its half of the pricing. Every case here
    # that must come back False is a way the untouched tree could have
    # collected 0.30 of this step's reward for changing nothing, and the two
    # that must raise are ways a later edit to m02.tsv could have made the gate
    # unconditional without anything going red.
    with tempfile.TemporaryDirectory() as d:
        quiet = lambda s: None
        rules = os.path.join(d, 'gates', 'rules')
        os.makedirs(rules)
        os.makedirs(os.path.join(d, 'gateroot'))
        m02 = os.path.join(rules, 'm02.tsv')
        probe = os.path.join(d, 'gateroot', 'probe.tsv')
        # The artifact half, held fixed while the run half is exercised below
        # and then exercised itself. One class with all three green is the
        # whole of it, which is what keeps the no-cliff property: fourteen
        # classes red and one green still opens.
        def art(cls, *statuses):
            return [(st, '%s-%s' % (cls, p), 1, 'why')
                    for st, p in zip(statuses, ARTIFACT_PARTS)]

        art_ok = art('m1-readmmtffull', 'PASS', 'PASS', 'PASS')
        art_red = (art('m1-readmmtffull', 'FAIL', 'FAIL', 'FAIL')
                   + art('m2-maptobioassembly', 'FAIL', 'FAIL', 'FAIL'))
        # The round-1 R5 blocker in its exact shape: the RDD count is deleted
        # and the number it used to print is printed as a constant, so
        # `-rdd-gone` is green on every class and the other two are red on
        # every class.
        art_hardcode = (art('m1-readmmtffull', 'PASS', 'FAIL', 'FAIL')
                        + art('m2-maptobioassembly', 'PASS', 'FAIL', 'FAIL'))
        # And the near miss: a Dataset was built and then never read.
        art_unused = art('m1-readmmtffull', 'PASS', 'PASS', 'FAIL')

        def rule(rid, mode, *terms):
            return '%s\t%s\t%s\tc.tsv\twhy\n' % (rid, mode, '\\t'.join(terms))

        def rules_file(*lines):
            open(m02, 'w').write('# a comment\n\n' + ''.join(lines))

        def probe_file(*rows):
            open(probe, 'w').write(''.join('job\t%s\t%s\n' % r for r in rows))

        rules_file(
            rule('m1-readmmtffull', 'atleast:1', 'probe.job', 'ReadMmtfFull#[0-9]+', '[0-9]+'),
            rule('m1-reader-count-outside', 'absent', 'probe.job', 'ReadMmtfFull#[0-9]+', '-'),
            rule('m1-mmtfbenchmark', 'atleast:1', 'probe.job', 'MmtfBenchmark#[0-9]+', '[0-9]+'),
            rule('m4-kinasesearch', 'atleast:3', 'probe.job', 'KinaseSearch#[0-9]+', '[0-9]+'),
            rule('no-forged-execution-id', 'absent', 'probe.job', '*', 'forged:.*'),
            rule('m1-spark-303', 'atleast:1', 'pom.resolved', 'org.apache.spark:.*', '3.0.3', '*'))
        want([(rid, need) for rid, _pat, need in scoped_job_patterns(d)],
             [('m1-readmmtffull', 1), ('m1-mmtfbenchmark', 1), ('m4-kinasesearch', 3)],
             'the scope is the requirement rules that ask for a job inside an '
             'execution, each with its own threshold, and nothing else in the file')

        want(dataset_terminal_reached(d, d, art_ok, quiet), False,
             'a run that wrote no probe rows is not a migrated run')

        # The before-state, as build/suite-p02-before/probe actually
        # recorded it: the m1/m2/m3 rows run every job outside every
        # execution, and the three m4 rows already run two real
        # executions apiece. This case is the reason
        # the gate carries thresholds at all -- it came back True without them,
        # which is a gate that pays the untouched tree 0.30 (NOTES 6.2.6).
        probe_file(('ReadMmtfFull#0', '-'), ('MmtfBenchmark#0', '-'),
                   ('KinaseSearch#0', '0'), ('KinaseSearch#1', '-'),
                   ('KinaseSearch#2', '1'), ('KinaseSearch#3', '-'))
        want(dataset_terminal_reached(d, d, art_ok, quiet), False,
             "the unmigrated tree's own probe rows do not open the gate")

        probe_file(('ReadMmtfFull#0', '0'), ('MmtfBenchmark#0', '-'))
        want(dataset_terminal_reached(d, d, art_ok, quiet), True,
             'one scoped job inside one execution, and one scoped class that '
             'stopped counting an RDD, is the whole condition')

        # The forge, priced. A candidate whose code writes the probe file, or
        # runs one `SparkSession.range(1).count()` beside an untouched
        # `pdb.count()`, produces exactly the rows above -- and every scoped
        # class still takes its terminal off an RDD, which is a fact about the
        # jar and not about the run. This case is the round-1 review's R5
        # blocker (NOTES 6.2.7).
        want(dataset_terminal_reached(d, d, art_red, quiet), False,
             'a job inside an execution while every scoped class still counts '
             'an RDD is the forgery, not the move')
        want(dataset_terminal_reached(d, d, art_red + art_ok, quiet), True,
             'one class out of fifteen is enough, as it is on the run half: '
             'there is no cliff for a candidate who migrated one and stopped')
        want(dataset_terminal_reached(d, d, art_hardcode, quiet), False,
             'deleting the RDD count and printing the constant it used to '
             'print is not the move, however many probe rows say otherwise')
        want(dataset_terminal_reached(d, d, art_unused, quiet), False,
             'a Dataset built from an RDD and then never read is not where the '
             'printed answer came from')
        want(dataset_terminal_reached(d, d, art_hardcode + art_ok, quiet), True,
             'and one class that did all three still opens it')
        with must_raise('a gate report naming no artifact rule was accepted; the '
                        'artifact half would have been vacuously true and a '
                        'hand-written probe row would have opened the gate alone'):
            dataset_terminal_reached(d, d, [('PASS', 'm1-readmmtffull', 1, '')],
                                     quiet)
        with must_raise('a gate report naming only -rdd-gone was accepted; the '
                        'other two thirds of the artifact half would have been '
                        'dropped without anything going red'):
            dataset_terminal_reached(
                d, d, [('PASS', 'm1-readmmtffull-rdd-gone', 0, 'why')], quiet)
        with must_raise('a gate report missing -dataset-terminal was accepted'):
            dataset_terminal_reached(
                d, d, [('PASS', 'm1-readmmtffull-rdd-gone', 0, 'why'),
                       ('PASS', 'm1-readmmtffull-dataset-built', 1, 'why')], quiet)

        # The m4 threshold from the other side: the third execution is the
        # candidate's own count, and it opens the gate on its own even though
        # the m4 requirement needs its other two classes too.
        probe_file(('KinaseSearch#0', '0'), ('KinaseSearch#1', '-'),
                   ('KinaseSearch#2', '1'), ('KinaseSearch#3', '2'))
        want(dataset_terminal_reached(d, d, art_ok, quiet), True,
             'the third execution of a web demo is this step\'s work and opens '
             'the gate')

        # And a rule whose threshold is 1 opens it on one row, which is what
        # twelve of the sixteen look like.
        probe_file(('MmtfBenchmark#0', '4'))
        want(dataset_terminal_reached(d, d, art_ok, quiet), True,
             'one row inside one execution clears a threshold of one')

        probe_file(('KinaseSearch#0', 'forged:0'), ('KinaseSearch#1', 'forged:0'),
                   ('KinaseSearch#2', 'forged:0'), ('ReadMmtfFull#0', '-'))
        want(dataset_terminal_reached(d, d, art_ok, quiet), False,
             'an execution id nothing ever started is not an execution')

        # Nineteen other demos in this repository already build Datasets, and
        # their executions are in this same concatenated file.
        probe_file(('AtpInteractionAnalysis#0', '0'), ('SecondaryStructureDemo#0', '1'),
                   ('ReadMmtfFull#0', '-'))
        want(dataset_terminal_reached(d, d, art_ok, quiet), False,
             'an execution in a demo this step does not ask about is not this '
             "step's work")

        probe_file(('MmtfBenchmark-missing#0', '0'), ('ReadMmtfFull#0', '-'))
        want(dataset_terminal_reached(d, d, art_ok, quiet), False,
             'the label is fullmatched: MmtfBenchmark-missing is not MmtfBenchmark')

        probe_file(('ReadMmtfFull#0', '0'))
        rules_file(rule('m1-reader-count-outside', 'absent', 'probe.job',
                        'ReadMmtfFull#[0-9]+', '-'))
        with must_raise('a rule file with no positive job rule was accepted; the '
                        'gate would have been unconditional'):
            dataset_terminal_reached(d, d, art_ok, quiet)

        open(m02, 'w').write('m1-x\tatleast:1\tprobe.job\\tA#[0-9]+\\t[0-9]+\twhy\n')
        with must_raise('a four-field rule line was accepted'):
            dataset_terminal_reached(d, d, art_ok, quiet)

        rules_file(rule('m1-x', 'atleast:0', 'probe.job', 'A#[0-9]+', '[0-9]+'))
        with must_raise('a threshold of zero was accepted; the gate would open '
                        'on a run that probed nothing'):
            dataset_terminal_reached(d, d, art_ok, quiet)

        # cheats.tsv: the twelve whose price is the whole reward. The list is
        # read out of the file, so what is tested here is the reading and the
        # two ways it could be wrong in the candidate's favour -- a file that
        # names nothing, and a report that never mentions a rule.
        cheats = os.path.join(rules, 'cheats.tsv')

        def cheats_file(*lines):
            open(cheats, 'w').write('# a comment\n\n' + ''.join(lines))

        cheats_file(
            rule('no-opaque-encoder', 'absent', 'ast.call', '*', '*', 'kryo',
                 'Encoders'),
            rule('no-forged-execution-id', 'absent', 'probe.job', '*', 'forged:.*'))
        want(cheat_rule_ids(d), ['no-opaque-encoder', 'no-forged-execution-id'],
             'the ids that void the whole reward are the contents of one file')
        want(cheat_red([('PASS', 'no-opaque-encoder', 0, ''),
                        ('PASS', 'no-forged-execution-id', 0, ''),
                        ('FAIL', 'm1-readmmtffull', 0, '')], cheat_rule_ids(d)), [],
             'a tree that failed a requirement but faked nothing is not a faked '
             'tree')
        want([r[1] for r in cheat_red([('FAIL', 'no-opaque-encoder', 2, 'x'),
                                       ('PASS', 'm1-readmmtffull', 1, '')],
                                      cheat_rule_ids(d))],
             ['no-opaque-encoder', 'no-forged-execution-id'],
             'a red prohibition is red, and one the report never mentions is '
             'not one that passed')

        cheats_file()
        with must_raise('a cheats.tsv naming no rule was accepted; nothing would '
                        'have voided the reward'):
            cheat_rule_ids(d)

        cheats_file(rule('m2-x', 'absent', 'ast.call', '*', '*', 'kryo', 'E'))
        with must_raise('a requirement-prefixed id in cheats.tsv was accepted; it '
                        'would have been paid for and voided the reward at once'):
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
