#!/usr/bin/env python3
"""Re-emit one probe file's rows under the `app*` kinds, when that file is one
of this repository's own programs.

Read on stdin, written on stdout, one file at a time, by gate-run.sh as it
concatenates. It has to be one file at a time: the judgement is per file and
the concatenation is what destroys it.

A file is one of this repository's programs when the replay labelled it and the
label is neither the gate's own session probe nor the suite's HarnessDriver.
Both of those are the verifier's code, submitted by the verifier, and no
requirement of any step is a statement about them -- a rule that read their
rows would be unpassable by construction, which is how the two counted rules
this pass replaces came to be counted rather than uniform (NOTES 6.4.8).

The label comes from `--label`, which the caller takes from the name of the
file, which root gave it from its own record of which token it issued to which
job (gates/probe-split.py). Not from the file's own `probe app.label` row: the
listener runs in the candidate's JVM and writes that row from an environment
variable, so a program that wanted to answer for another program would write
another program's name there. Where the two disagree the file is reported
`tampered` under the name root has, which is the same reading as a repeated
key and priced the same way. Without `--label` -- the standalone probes and
every authoring replay, which run one program at a time into a file of their
own -- the file's own row is all there is, and is used.

Candidate code can still forge extra rows into its own file, since the listener
runs in its JVM, and that is exactly why every rule over these kinds is
`absent`: a forged row can only add a reading, and adding one to an `absent`
rule cannot help.

Except that a forged row can also *displace* one, and three of the four ways it
could are refused here rather than reasoned about:

  * a repeated key. GateProbe writes each of its own `probe` keys once per JVM,
    and its `setconf` rows come from `conf.getAll()` filtered to `spark.sql.*`
    (GateProbe.java:441), which is a map -- so a second row for either is a row
    the listener did not write. Measured across the 246 recorded probe files of
    the before and gold suites: 0 repeats of either kind (NOTES 6.4.9).
  * two `conf` rows for one key that disagree. GateProbe collects those from a
    list and may legitimately report a key twice, so a repeat is not by itself
    a signature -- but two different answers to one question is not a reading,
    and the value that goes out says so. Measured across the same 246 files: 0.
  * a file that never arrived. A probe file exists for every job that started a
    SparkContext, so a missing one is either a program that exited before it
    did -- which is 17 of this census's 140 jobs, all of them argument-
    validation exits -- or a file something removed. test.sh tells the two
    apart by the job's exit status and seeds a `replay.dropped` row for the
    second, because silence is the one reading an `absent` rule mistakes for
    success.

The fourth is a file whose rows are real and whose label is somebody else's,
which is what `--label` closes: the name is root's, so the worst a relabelling
buys is the `tampered` reading above (round-3 review, Blocker 1; NOTES 6.4.10).
"""
import re
import sys

HARNESS = re.compile(r'^HarnessDriver(-|$)')


def main(given=None):
    probe, conf, setconf = {}, {}, {}
    order, setorder = [], []
    repeated = False
    disagreed = set()
    for line in sys.stdin:
        row = line.rstrip('\n').split('\t')
        if len(row) < 3:
            continue
        kind, key, value = row[0], row[1], '\t'.join(row[2:])
        if kind == 'probe':
            # GateProbe writes each of its own keys exactly once per JVM, so a
            # second one is a row something else put there. That is the only
            # signature this side of the wall can have: the listener runs in
            # the candidate's JVM and the file descriptor it writes on is open
            # in that JVM too, so rows can be appended and the file can be
            # rewritten -- but not without the real ones landing beside the
            # invented ones.
            if key in probe:
                repeated = True
            probe.setdefault(key, value)
        elif kind == 'conf':
            # Not a tamper signature: GateProbe reads the watched keys off a
            # list and one key can legitimately be answered more than once. A
            # repeat that agrees is one reading; a repeat that disagrees is
            # none, and is reported as a value no rule can read as good.
            if key not in conf:
                order.append(key)
                conf[key] = value
            elif conf[key] != value:
                disagreed.add(key)
        elif kind == 'setconf':
            # A tamper signature, unlike `conf`: these come from
            # `conf.getAll()` filtered to spark.sql.* (GateProbe.java:441), a
            # map, so one dump cannot mention a key twice.
            if key in setconf:
                repeated = True
            if key not in setconf:
                setorder.append(key)
            setconf.setdefault(key, value)
    # The union, not the conf table alone: Spark reports the keys the probe
    # watches, and an application can hold a key Spark does not report. A key
    # only one side knows about still gets a row on both, so a rule over either
    # kind sees every key either reading mentions.
    for key in setorder:
        if key not in conf:
            order.append(key)

    infile = probe.get('app.label') or probe.get('app.name') or '-'
    label = given or infile
    # A name root issued and a name the JVM wrote are two answers to one
    # question, and two answers are not a reading -- the same judgement the
    # `conf` rows get, reached here as `tampered` because unlike a conf key
    # this one has an owner and the disagreement says the listener's output was
    # edited. `-` is not a disagreement: a file whose rows were all forged
    # before the listener wrote its own is the repeated-key case below, and a
    # file the listener never got to write in is the dropped case.
    if given and infile != '-' and infile != given:
        repeated = True
    # Before the filter, not after: a forger who wants to be skipped writes
    # `probe\tapp.label\tHarnessDriver-x` as the first row of their own file,
    # and filtering first would take the bait and say nothing. With `--label`
    # the bait no longer reaches the filter at all, and this order is what
    # keeps the two callers reading the same.
    if repeated:
        sys.stdout.write('appsession\t%s\ttampered\n' % label)
        return
    if label == '-' or label == 'session-probe' or HARNESS.match(label):
        return
    # Seeded by test.sh for a job that finished successfully and left no probe
    # file behind. After the HarnessDriver filter, so the one job in this
    # census that legitimately ends that way stays out of the reading.
    if probe.get('replay.dropped') == 'true':
        sys.stdout.write('appsession\t%s\tdropped\n' % label)
        return
    present = probe.get('session.present') == 'true'
    out = ['appsession\t%s\t%s' % (label, 'true' if present else 'false')]
    for key in order:
        if present:
            out.append('appconf\t%s\t%s'
                       % (key, '<disagreement>' if key in disagreed
                          else conf.get(key, '<unset>')))
            out.append('appsetconf\t%s\t%s' % (key, setconf.get(key, '<unset>')))
        else:
            out.append('appconf\t%s\t<no-session>' % key)
            out.append('appsetconf\t%s\t<no-session>' % key)
    sys.stdout.write('\n'.join(out) + '\n')


# usage: probe-derive.py [--label <name>] < file
argv = sys.argv[1:]
name = None
if len(argv) == 2 and argv[0] == '--label':
    name = argv[1]
elif argv:
    sys.stderr.write('probe-derive: want [--label <name>], got %s\n'
                     % ' '.join(argv))
    sys.exit(2)
main(name)
