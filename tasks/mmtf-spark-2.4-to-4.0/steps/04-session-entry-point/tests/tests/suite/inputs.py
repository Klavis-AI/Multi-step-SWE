#!/usr/bin/env python3
"""Count the recorded suite's scored inputs, and its adversarial share.

migration_prompt.md rule 7 sets two bars this file measures and one it does not:

    Target 100-200+ scored inputs per step, well beyond what the codebase's
    own tests reach -- those count toward neither bar, since the agent has
    them. ... At least a third of the inputs are adversarial: edge values,
    malformed input, boundary conditions, unusual-but-legal usage, error
    paths.

The third bar -- line coverage -- is cov-strata.sh's, and both files agree on
one thing: the codebase's own tests are excluded, so nothing here reads
inherited.exec or the JUnit suite.

WHAT COUNTS AS ONE INPUT

One (code-under-test, argument-tuple) pair whose answer is recorded. A CLI
recipe is one: one class, one argv, one recording. A harness subcommand is a
*batch* of them -- advnumeric asks 19 separate questions of the filter API and
prints 19 separately labeled answers -- and it is counted as 19, not as 1,
because batching is an efficiency decision rather than a semantic one. The same
19 tuples split across 19 spark-submit calls would be 19 inputs carrying exactly
the same evidence, and an accounting that changed its answer depending on how
many JVMs a run happened to start would be measuring the runner, not the suite.

Two alternatives were tried and rejected, both by measurement rather than taste:

  - Counting normalized output lines. Makes the adversarial share *worse*
    (~2%), because an error path prints a short answer and a demonstration
    prints a table -- so the metric rewards verbosity, which is the opposite
    of what rule 7 is asking after.
  - Comparing normalized stderr wholesale, so an exception's text is the
    input's answer. 41 of 122 .err files survive normalization still carrying
    Spark's own prose, with TID and stage numbers in it. That is rule 7's
    stability condition failing, not an accounting unit.

WHAT COUNTS AS ADVERSARIAL

For CLI recipes: every recipe that declares a nonzero exit status, minus the
three DELIBERATE crashes. Those three are not adversarial inputs -- they are
bare runs of a demo that the tree's own bug breaks on this corpus, so the
input is ordinary and it is the code that is broken.

Deliberately conservative in one direction: a recipe that exercises an
unusual-but-legal argument and *succeeds* (-p 2, an empty input directory, a
second compression codec) is rule 7 adversarial by the letter and is counted
here as ordinary, because "declares a nonzero status" is a property of the
plan file that cannot drift, and "unusual but legal" is a judgement that can.
The share below is therefore a floor.

For harness subcommands: the "adv " and "expect " labels, which counted() and
expect()/expectKind() emit and which the adv* subcommands and the reject cases
of five demonstration subcommands are the only callers of. "filter " and
"webfilters " are matched() and hits() -- demonstrations of a filter over the
corpus -- and count as ordinary.

A harness subcommand that prints no labeled line at all is one input, not zero.
It would be convenient to leave those 18 out: they are all ordinary, so
dropping them raises the share from 42.9% to 46.3%. They are in.

Usage:  inputs.py <census-dir>      report, and gate on the >=1/3 bar
        inputs.py --selftest        classifier against a known answer
"""
import os
import re
import sys
from collections import Counter

# Same three as covfile.py's DELIBERATE, by job name rather than by package:
# this file's key is the plan's job column. Kept as a literal in both places
# rather than imported, because they are answering different questions about
# the same three demos -- covfile.py asks which files may sit under the
# coverage bar, this asks which nonzero exits are not adversarial inputs -- and
# a shared constant would invite a later edit that is right for one and wrong
# for the other.
CRASHES = {'MapToBioAssembly2', 'MapToCathDomains', 'MutationToStructureDemo'}

ADVERSARIAL_LABEL = re.compile(r'^(adv|expect) [^:]*: ')
ORDINARY_LABEL = re.compile(r'^(filter|webfilters) [^:]*: n=')


def classify(rows, norms):
    """rows: (phase, job, cls, wantrc, args). norms: {job: normalized text}."""
    n = Counter()
    per_job = {}
    for phase, job, cls, wantrc, args in rows:
        if cls.startswith('harness.'):
            text = norms[job]
            adv = sum(1 for l in text.splitlines() if ADVERSARIAL_LABEL.match(l))
            ord_ = sum(1 for l in text.splitlines() if ORDINARY_LABEL.match(l))
            if adv + ord_ == 0:
                ord_ = 1           # the subcommand itself is the input
                n['harness_unlabeled'] += 1
            n['harness_adv'] += adv
            n['harness_ord'] += ord_
            per_job[job] = (adv, ord_)
        else:
            adv = 1 if (wantrc != '0' and job not in CRASHES) else 0
            n['demo_adv'] += adv
            n['demo_ord'] += 1 - adv
            if job in CRASHES:
                n['demo_crash'] += 1
            per_job[job] = (adv, 1 - adv)
    n['adv'] = n['harness_adv'] + n['demo_adv']
    n['total'] = n['adv'] + n['harness_ord'] + n['demo_ord']
    return n, per_job


def load(census):
    plan = os.path.join(census, 'jobs.tsv')
    rows = [l.rstrip('\n').split('\t')
            for l in open(plan) if l.strip()]
    norms, missing = {}, []
    for phase, job, cls, wantrc, args in rows:
        p = os.path.join(census, job + '.norm')
        # Trap 3's shape one level down: a job that crashed out of the census
        # leaves no recording, and an accounting that skips it silently reports
        # a suite smaller than the one that ran without ever saying so.
        if not os.path.exists(p):
            missing.append(job)
            norms[job] = ''
        else:
            norms[job] = open(p, encoding='utf-8', errors='replace').read()
    return rows, norms, missing


def report(census):
    rows, norms, missing = load(census)
    if missing:
        for j in missing:
            print('inputs: NO RECORDING for planned job %s' % j)
        print('inputs: %d planned job(s) left no .norm; refusing to count'
              % len(missing))
        return 3
    n, per_job = classify(rows, norms)
    share = 100.0 * n['adv'] / n['total'] if n['total'] else 0.0

    print('%-34s %5d' % ('CLI recipes and bare runs', n['demo_adv'] + n['demo_ord']))
    print('%-34s %5d' % ('  of which adversarial', n['demo_adv']))
    print('%-34s %5d' % ('  of which declared crashes', n['demo_crash']))
    print('%-34s %5d' % ('harness labeled cases',
                         n['harness_adv'] + n['harness_ord'] - n['harness_unlabeled']))
    print('%-34s %5d' % ('  of which adversarial', n['harness_adv']))
    print('%-34s %5d' % ('harness subcommands w/o labels', n['harness_unlabeled']))
    print('%-34s %5d' % ('TOTAL scored inputs', n['total']))
    print('%-34s %5d  = %.1f%%' % ('  adversarial', n['adv'], share))

    rc = 0
    if n['total'] < 100:
        print('GATE   %d inputs is under rule 7\'s 100-200+ target' % n['total'])
        rc = 1
    if share < 100.0 / 3:
        print('GATE   %.1f%% adversarial is under rule 7\'s one third' % share)
        rc = 1
    print('inputs: %s' % ('OK' if rc == 0 else 'GATE FAILED'))
    return rc


# Trap 6: the clean control is paired with a fixture built to fail, because a
# gate that has never been seen to fire is not known to be a gate. The second
# case also pins the unlabeled-subcommand rule -- drop it and the share reads
# 33.3%, which passes.
def selftest():
    def rows_for(demo_adv, demo_ord, harness):
        rows = []
        for i in range(demo_adv):
            rows.append(('1', 'A%d' % i, 'x.A%d' % i, '1', ''))
        for i in range(demo_ord):
            rows.append(('1', 'B%d' % i, 'x.B%d' % i, '0', ''))
        for i, (a, o) in enumerate(harness):
            rows.append(('1', 'H%d' % i, 'harness.HarnessDriver', '0', 'h%d' % i))
        return rows

    def norms_for(harness):
        out = {}
        for i, (a, o) in enumerate(harness):
            lines = ['adv case%d: n=1' % k for k in range(a)]
            lines += ['filter case%d: n=1' % k for k in range(o)]
            out['H%d' % i] = '\n'.join(lines) + '\n'
        return out

    bad = 0

    def check(name, got, want):
        nonlocal bad
        if got != want:
            bad += 1
            sys.stderr.write('inputs selftest %s: got %r want %r\n'
                             % (name, got, want))

    # A crash is not an adversarial input, and it is not dropped either.
    rows = rows_for(2, 3, [(4, 5)]) + [('1', 'MapToCathDomains', 'x.M', '1', '')]
    n, _ = classify(rows, norms_for([(4, 5)]))
    check('crash not adversarial', n['demo_adv'], 2)
    check('crash still counted', n['total'], 2 + 3 + 1 + 4 + 5)
    check('crash tallied', n['demo_crash'], 1)

    # An unlabeled harness subcommand is one ordinary input, not zero.
    n, _ = classify(rows_for(0, 0, [(0, 0)]), {'H0': ''})
    check('unlabeled is one', (n['total'], n['harness_unlabeled']), (1, 1))

    # expect counts adversarial; webfilters does not.
    n, _ = classify(rows_for(0, 0, [(0, 0)]),
                    {'H0': 'expect a: NO EXCEPTION\nwebfilters b: n=3\n'})
    check('expect/webfilters split', (n['harness_adv'], n['harness_ord']), (1, 1))

    # An id line under a label must not be counted as a label.
    n, _ = classify(rows_for(0, 0, [(0, 0)]),
                    {'H0': 'adv a: n=2\n  1ABC 2DEF\n'})
    check('id line is not a case', n['harness_adv'], 1)

    # The failing fixture: 33 adversarial in 100 is under a third, and the
    # gate has to say so.
    n, _ = classify(rows_for(33, 67, []), {})
    check('below-bar share', n['adv'], 33)
    check('below-bar total', n['total'], 100)
    check('below bar', n['adv'] * 3 < n['total'], True)
    # ... and one input either way flips it, so the comparison is on the edge
    # it claims to be on rather than somewhere safely past it.
    n, _ = classify(rows_for(34, 66, []), {})
    check('above bar', n['adv'] * 3 >= n['total'], True)

    print('inputs: %d/%d selftest checks pass' % (10 - bad, 10))
    return 1 if bad else 0


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--selftest':
        sys.exit(selftest())
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__.rsplit('Usage:', 1)[0])
        sys.stderr.write('usage: inputs.py <census-dir> | --selftest\n')
        sys.exit(2)
    sys.exit(report(sys.argv[1]))
