#!/usr/bin/env python3
"""Turn two observed builds of one tree into facts about the build itself.

The other four scanners read a tree, a POM, a pile of class files or a running
job. This one reads something none of those can see: what happens when the
build runs *twice*. "Reproducible" is not a property of any one artifact, and
no amount of reading the POM establishes it -- `<project.build.outputTimestamp>`
being set is a claim, and two builds agreeing is the evidence.

So the shell half (build-scan.sh) performs the builds and writes down what it
saw, one observation per line, and this half derives the facts. The split is
there so the comparison logic -- which is the part that can be wrong in a way
nobody notices -- is testable without Maven, a warm repository, or 40 seconds.

Observations (build-scan.sh writes them, `<work>/observed.tsv`):

    round  <n>  <rc>  <seconds>              one per mvn invocation
    jar    <n>  <name>  <sha256>  <entries>  one per jar found after round n
    stray  <path>                            a file the build wrote into the
                                             tree, outside target/
    error  <what>  <detail>                  passed through unchanged

Facts:

    round     <n>      <rc>     <seconds>    the invocations, as observed
    artifact  <round>  <name>   <sha256>  <entries>
    repro     <name>   <verdict>             same | differs | missing
    stray     <path>
    error     <what>   <detail>

`repro` is the derived kind and the reason this file exists. A jar built in both
rounds with one sha256 is `same`; with two, `differs`; found in one round only,
`missing` -- and `missing` is a verdict rather than a silence because a build
that stopped producing an artifact is the loudest possible way for a
reproducibility gate to "pass". Fewer than two successful rounds is an `error`
fact and **no** `repro` facts at all, for the reason pom-scan.py's `unpinned`
has the same rule: `absent build.repro <*> <differs>` would otherwise be
answered most convincingly by a build that never ran.

`stray` is part of the same question, not a separate one. maven-shade-plugin
writes `dependency-reduced-pom.xml` into the source tree on every build; the
content happens to be byte-identical each time, so a content hash of the tree
does not notice, but a build whose inputs include a file the previous build
wrote is not a build anyone can reason about. build-scan.sh detects it by
mtime against a marker, which is why it is an observation and not something
derivable here.
"""
import os
import sys


def parse_observations(text):
    """Split the shell half's notes into (rounds, jars, passthrough facts)."""
    rounds, jars, out = [], [], []
    for lineno, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith('#'):
            continue
        f = line.rstrip('\n').split('\t')
        kind = f[0]
        if kind == 'round' and len(f) == 4:
            rounds.append(tuple(f[1:]))
            out.append(('round',) + tuple(f[1:]))
        elif kind == 'jar' and len(f) == 5:
            jars.append(tuple(f[1:]))
            out.append(('artifact',) + tuple(f[1:]))
        elif kind == 'stray' and len(f) == 2:
            out.append(('stray', f[1]))
        elif kind == 'error' and len(f) == 3:
            out.append(('error', f[1], f[2]))
        else:
            # A line this file cannot read is a broken instrument, and the one
            # thing it must not become is a shorter list of observations. The
            # tabs come out of the quoted text: a fact is one line of
            # tab-separated fields, and an error fact that carried real tabs
            # would arrive at rules.py as a fact of the wrong arity.
            out.append(('error', 'observed.tsv:%d' % lineno,
                        'unreadable: '
                        + line.strip().replace('\t', '\\t')[:120]))
    return rounds, jars, out


def repro_facts(rounds, jars):
    """`same`/`differs`/`missing` per artifact name, across the green rounds.

    Only rounds that exited 0 are compared. A failed round produces whatever
    the previous one left behind or nothing at all, and either way its jars are
    not a second opinion about the same build.
    """
    green = sorted(r[0] for r in rounds if r[1] == '0')
    if len(green) < 2:
        return [('error', 'rounds',
                 '%d round(s) exited 0, need 2 to compare' % len(green))]
    by_name = {}
    for n, name, digest, _entries in jars:
        if n in green:
            by_name.setdefault(name, {})[n] = digest
    if not by_name:
        return [('error', 'artifacts', 'no jar in any green round')]
    out = []
    for name, seen in by_name.items():
        if len(seen) < len(green):
            out.append(('repro', name, 'missing'))
        elif len(set(seen.values())) == 1:
            out.append(('repro', name, 'same'))
        else:
            out.append(('repro', name, 'differs'))
    return out


def scan(path):
    if not os.path.exists(path):
        return [('error', os.path.basename(path), 'absent')]
    with open(path, errors='replace') as fh:
        rounds, jars, facts = parse_observations(fh.read())
    return facts + repro_facts(rounds, jars)


def selftest():
    bad = []

    def want(got, expected, why):
        if got != expected:
            bad.append('%s: got %r want %r' % (why, got, expected))

    def obs(*lines):
        return parse_observations('\n'.join(lines))

    two_same = ['round\t1\t0\t21', 'round\t2\t0\t19',
                'jar\t1\tapp.jar\taaa\t4253', 'jar\t2\tapp.jar\taaa\t4253',
                'jar\t1\toriginal-app.jar\tbbb\t201',
                'jar\t2\toriginal-app.jar\tbbb\t201']
    rounds, jars, facts = obs(*two_same)
    want(sorted(repro_facts(rounds, jars)),
         [('repro', 'app.jar', 'same'),
          ('repro', 'original-app.jar', 'same')],
         'two green rounds, identical digests, both artifacts')
    want(sorted(f[0] for f in facts), ['artifact'] * 4 + ['round'] * 2,
         'observations pass through as round and artifact facts')

    rounds, jars, _ = obs(*[l.replace('jar\t2\tapp.jar\taaa', 'jar\t2\tapp.jar\tzzz')
                            for l in two_same])
    want(sorted(repro_facts(rounds, jars)),
         [('repro', 'app.jar', 'differs'),
          ('repro', 'original-app.jar', 'same')],
         'one digest moving condemns that artifact and only that one')

    # The instrument's own failure modes, each of which would otherwise read as
    # a clean tree (trap 6): the artifact vanishing, the build failing, and the
    # build not running at all.
    rounds, jars, _ = obs(*[l for l in two_same if l != 'jar\t2\tapp.jar\taaa\t4253'])
    want(sorted(repro_facts(rounds, jars)),
         [('repro', 'app.jar', 'missing'),
          ('repro', 'original-app.jar', 'same')],
         'an artifact produced once is missing, not absent from the finding')
    rounds, jars, _ = obs('round\t1\t0\t21', 'round\t2\t1\t19',
                          'jar\t1\tapp.jar\taaa\t4253')
    want([f[0] for f in repro_facts(rounds, jars)], ['error'],
         'one green round cannot establish anything')
    want([f[0] for f in repro_facts([], [])], ['error'],
         'no rounds at all is an error, not silence')
    rounds, jars, _ = obs('round\t1\t0\t21', 'round\t2\t0\t19')
    want(repro_facts(rounds, jars),
         [('error', 'artifacts', 'no jar in any green round')],
         'two green rounds that built nothing is an error too')

    # A failed round's jars are stale leftovers, not a second opinion.
    rounds, jars, _ = obs('round\t1\t0\t21', 'round\t2\t1\t2', 'round\t3\t0\t20',
                          'jar\t1\tapp.jar\taaa\t4253',
                          'jar\t2\tapp.jar\taaa\t4253',
                          'jar\t3\tapp.jar\tzzz\t4253')
    want(sorted(repro_facts(rounds, jars)), [('repro', 'app.jar', 'differs')],
         'rounds 1 and 3 are compared; the failed round 2 is not consulted')

    _, _, facts = obs('stray\tdependency-reduced-pom.xml',
                      'error\tmaven\tnot on PATH', 'round\t1\t0\t3',
                      'garbage', 'jar\t1')
    want(sorted(facts),
         [('error', 'maven', 'not on PATH'),
          ('error', 'observed.tsv:4', 'unreadable: garbage'),
          ('error', 'observed.tsv:5', 'unreadable: jar\\t1'),
          ('round', '1', '0', '3'),
          ('stray', 'dependency-reduced-pom.xml')],
         'stray and error pass through; a malformed line becomes an error'
         ' whose own tabs are escaped')

    for line in bad:
        print('SELFTEST FAIL ' + line, file=sys.stderr)
    if bad:
        raise SystemExit('build-scan selftest: %d case(s) wrong' % len(bad))
    print('build-scan selftest: 9 case(s) behaved')


def main():
    args = sys.argv[1:]
    if args[:1] == ['--selftest']:
        return selftest()
    if len(args) != 1:
        raise SystemExit('usage: build-scan.py <observed.tsv>'
                         '   |   build-scan.py --selftest')
    for f in sorted(scan(args[0])):
        sys.stdout.write('\t'.join(f) + '\n')


if __name__ == '__main__':
    main()
