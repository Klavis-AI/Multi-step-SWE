#!/usr/bin/env python3
"""Turn one scored run of step 1 into a reward in [0, 1].

test.sh performs the run -- it builds, probes, gates, replays and tests -- and
writes down what happened. This file reads only what was written down, which is
what makes the arithmetic reviewable after the container is gone and testable
here against fixtures (--selftest) rather than against a thirty-minute run.

    score.py --tests <payload> --run <rundir>

<rundir> is what test.sh filled in:

    preflight.tsv             ok|fail <what> <detail>, one line per precondition
    gate/findings.tsv         gate-run.sh's own verdicts, as the unprivileged half left them
    gate/findings.root.tsv    the same rules re-evaluated as root over a root-owned
                              copy of the same facts
    suite/summary.tsv         job, class, rc, seconds, bytes, wantrc
    suite/<job>.out           one recording per planned job
    surefire/TEST-*.xml       the repository's own suite, run from a pristine copy

<payload> is /tests: weights.tsv, and suite/ with the plan, the reference
recordings, the normaliser, the projection, the input census and the one-line
list of jobs whose exit status is recorded but not compared.

THE SHAPE OF THE ANSWER

    reward = T1 x T2   (user decision 2026-10-13: product scoring)
    T1 = (sum over m1..m6 of its weight) / 0.70        migrated-ness
    T2 = (0.22 * replay + 0.08 * tests) / 0.30         preservation

with one condition on the first two terms: they are graded only on a build that
left the Spark line this step starts from. See spark_line_moved.

and every one of those eight numbers is printed with its own evidence, because
a reward is a summary and the thing a reviewer needs is the summary's parts.

Three ways the run can be void rather than bad, and they are not the same thing
as a candidate scoring zero:

  * a precondition failed -- the mirror is down, the payload is short a file,
    an instrument failed its own selftest. Nothing measured after that means
    anything (trap 5, trap 6).
  * a standing rule of rules/common.tsv is red. Those ten are preconditions
    for reading the other twenty-five: the untouched tree passes all ten, so a
    red one says the reading is void, not that the candidate is wrong.
  * the two evaluations of the gate disagree. Same rules, same facts, one run
    as nobody and one as root over a copy -- rules.py is deterministic, so a
    difference means the facts moved between being written and being read.

The first voids everything and is a verifier failure, reported as such. The
second and third void the migrated-ness family only: the behavior family is
measured from recordings, not from facts, and a tree that does not build has
already lost the replay on its own merits.

And one way the behavior family is not graded at all rather than graded badly:
a build that still resolves Spark 2.4 and nothing else has not produced the
artifact that family asks its question about. That is spark_line_moved, and it
is the only thing standing between this scorer and paying 0.30 for a tree that
was never touched (migration_prompt.md section 8: nop reports 0.0).
"""
import argparse
import collections
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


# --- is there a migrated artifact for the behavior family to ask about? -------

SPARK_START = '2.4'                  # the Spark line this step starts from


def spark_line_moved(run, log):
    """True when the resolved graph names org.apache.spark off the 2.4 line.

    The behavior family asks whether the *migrated* artifact still does what
    the original did. A tree whose build resolves Spark 2.4 and nothing else
    has not produced one: the question is unasked rather than answered, and
    crediting it for the recordings it happens to reproduce would pay 0.30 of
    this step's reward for changing nothing.

    This is the weakest condition that separates a tree which did not move from
    every tree that did. One org.apache.spark artifact resolving at any version
    other than 2.4.x is enough, so a candidate who landed on the wrong 3.x, or
    who left a stray 2.4 artifact behind in a mixed graph, keeps full
    proportional behavior credit and loses only the m1 rules that measure those
    things. There is no cliff here for anybody who tried.

    Read out of gate/pom.tsv -- the same file the m1 rules are read from, so the
    two cannot disagree about what the build resolved.
    """
    path = os.path.join(run, 'gate', 'pom.tsv')
    if not os.path.exists(path):
        log('behav  NO       the resolved graph was never written: %s' % path)
        return False
    spark = []
    for line in open(path, errors='replace'):
        f = line.rstrip('\n').split('\t')
        if len(f) >= 3 and f[0] == 'resolved' \
                and f[1].startswith('org.apache.spark:'):
            spark.append((f[1], f[2]))
    if not spark:
        log('behav  NO       no org.apache.spark artifact in the resolved graph')
        return False
    moved = sorted({v for _ga, v in spark
                    if v != SPARK_START and not v.startswith(SPARK_START + '.')})
    if not moved:
        log('behav  NO       all %d org.apache.spark artifact(s) resolve at Spark %s'
            % (len(spark), SPARK_START))
        return False
    log('behav  YES      %d org.apache.spark artifact(s) resolved, off %s at %s'
        % (len(spark), SPARK_START, ' '.join(moved[:4])))
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
        # Counted off the report rather than written out: this sentence said
        # nine and eighteen long after the gate had ten and twenty-five, and a
        # verifier that miscounts its own gate in the log a candidate reads is
        # an instrument disagreeing with itself. Step 3's copy of this log was
        # rewritten the same way for the same reason.
        n_all = len(mine)
        n_req = len([r for r in mine if re.match(r'^m\d+-', r[1])])
        log('The %d rules of rules/common.tsv are preconditions for reading the'
            % (n_all - n_req))
        log('%d above them, and the untouched tree passes all %d. A red one'
            % (n_req, n_all - n_req))
        log('says the reading is void, not that the candidate is wrong -- so this')
        log('family scores 0 and the behavior family below still stands.')

    # Both behavior instruments are run and logged whatever the answer below
    # is: the evidence for "this tree reproduces 131 of 140 recordings" is
    # worth having even when it earns nothing, and a reader who cannot see it
    # cannot tell a zero that was measured from a zero that was assumed.
    replay, _counts = replay_score(tests, run, log)
    tscore = tests_score(run, expect_classes, expect_tests, log)
    graded = spark_line_moved(run, log)
    parts['replay'] = weights['behavior']['replay'][0] * (replay if graded else 0.0)
    parts['tests'] = weights['behavior']['tests'][0] * (tscore if graded else 0.0)
    if not graded:
        log('')
        log('BEHAVIOR NOT GRADED -- the build never left Spark %s, so there is no'
            % SPARK_START)
        log('migrated artifact for this family to ask its question about. The two')
        log('measurements above stand as evidence and score 0: replay %.6f, tests'
            % replay)
        log('%.6f would have been worth %.6f together on a build that moved.'
            % (tscore,
               weights['behavior']['replay'][0] * replay
               + weights['behavior']['tests'][0] * tscore))

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

    def want(got, expected, why):
        if got != expected:
            bad.append('%s: got %r want %r' % (why, got, expected))

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
        try:
            load_weights(p)
            bad.append('weights summing to 0.9 were accepted')
        except ValueError:
            pass

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

    try:
        gate_scores([('PASS', 'm3-a', 0, 'r')], w, lambda s: None)
        bad.append('a requirement with no weight was accepted')
    except ValueError:
        pass

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
            try:
                load_unscored_status(p, {'JobA', 'JobB'})
                bad.append('unscored-status accepted %s' % why)
            except ValueError:
                pass

    # spark_line_moved. The case that matters is the pin: three artifacts at
    # 2.4.0 and a graph full of transitives at 2.4.0 must come back False, or
    # the nop scores 0.30. The mixed graph must come back True, because a
    # candidate who left one 2.4 artifact behind has still moved and is already
    # failing m1-spark-uniform for it.
    with tempfile.TemporaryDirectory() as d:
        g = os.path.join(d, 'gate')
        os.makedirs(g)
        p = os.path.join(g, 'pom.tsv')
        quiet = lambda s: None
        want(spark_line_moved(d, quiet), False, 'no pom.tsv is not a moved build')
        open(p, 'w').write('resolved\torg.apache.spark:spark-core_2.11\t2.4.0\tprovided\n'
                           'resolved\torg.apache.spark:spark-sql_2.11\t2.4.0\tprovided\n')
        want(spark_line_moved(d, quiet), False, 'the pin has not moved')
        open(p, 'w').write('resolved\torg.apache.spark:spark-core_2.12\t3.0.3\tprovided\n')
        want(spark_line_moved(d, quiet), True, 'a 3.0.3 graph has moved')
        open(p, 'w').write('resolved\torg.apache.spark:spark-core_2.12\t3.0.3\tprovided\n'
                           'resolved\torg.apache.spark:spark-sql_2.11\t2.4.0\tprovided\n')
        want(spark_line_moved(d, quiet), True, 'a mixed graph has still moved')
        open(p, 'w').write('resolved\torg.apache.spark:spark-core_2.12\t2.4.8\tprovided\n')
        want(spark_line_moved(d, quiet), False, '2.4.8 is still the 2.4 line')
        open(p, 'w').write('resolved\torg.biojava:biojava-structure\t5.0.2\tcompile\n')
        want(spark_line_moved(d, quiet), False, 'a graph with no Spark at all')
        open(p, 'w').write('dep\torg.apache.spark:spark-core_2.12\t3.0.3\tprovided\n')
        want(spark_line_moved(d, quiet), False,
             'a declaration is not a resolution')

    for line in bad:
        sys.stderr.write('SELFTEST FAIL %s\n' % line)
    if bad:
        raise SystemExit('score selftest: %d case(s) wrong' % len(bad))
    print('score selftest: 25 case(s) behaved')
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
