#!/usr/bin/env python3
"""Normalize a Spark job's stdout down to the lines the job itself printed.

The same normalizer runs at record time (against the untouched baseline) and at
replay time (against the candidate tree), so anything it removes is removed
identically on both sides. It has to straddle Spark 2.4 and Spark 4.0, which
differ in logging framework (log4j 1.x vs log4j2) and in which stream the logs
go to (stderr vs stdout).

Removed: framework log lines, stack traces, JVM/loader warnings, blank lines.
Rewritten: absolute paths, wall-clock durations, the three underscore-anchored
date stamps this tree bakes into output file names, and PDB-file temp names,
none of which are part of a job's behavior.

Usage:  norm.py < raw.out > normalized.txt
        norm.py --selftest          exercise the rules against known answers
"""
import re
import sys

# "19/01/31 12:00:00 INFO SparkContext: ..."  (log4j 1.x and log4j2 both use
# this layout in the stock Spark conf, from 2.4 through 4.0)
LOG_CLASSIC = re.compile(r'^\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} '
                         r'(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\b')
# log4j2's own status logger, emitted before any Spark config is applied, in
# the two layouts log4j2 has defaulted to across the versions Spark ships:
#   "2026-09-16T06:42:15.850162402Z main ERROR Could not determine local host ..."
#   "2026-09-17 01:42:36,851 main ERROR Could not determine local host name ..."
# Spark 3.3 prints the second, Spark 4.0 the first, and the layout is log4j2's
# to choose, not ours -- so the separator and the fraction are matched as a
# shape rather than pinned to the one rung that was in front of us. The first
# version of this rule pinned the "T", and all 276 status lines in the Spark 3.3
# census went into scored recordings (NOTES 4.9.4).
#
# The field between the timestamp and the level is the thread name, and it is
# not one token. Spark 4.0's executor threads are called "Executor task launch
# worker for task 1.0 in stage 6.0 (TID 10)"; with \S+ there, exactly one line
# of 684 in the Spark 4.0 census survived normalization, carrying a wall clock
# and the container's hostname -- which is its container id, new every run --
# into a scored recording (NOTES 4.6). Bounded rather than open-ended, and the
# level has to be followed by a space, so a data line that merely ends in the
# word ERROR is not swallowed.
LOG_STATUS = re.compile(r'^\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}[.,\d]*Z? .{0,160}?'
                        r'\b(TRACE|DEBUG|INFO|WARN|ERROR|FATAL) ')

# A third layout, with no timestamp and no thread name at all. log4j2's
# StatusLogger falls back to "<LEVEL> <listener> <message>" when it has to
# report a problem before any configuration has been read, and Spark 3.5.7
# reaches that path where 3.3 and 4.0 do not -- which is why the comment above
# used to name 3.5 alongside 3.3, and why the 3.5 census (c10) came back with
# all 140 recordings opening on four lines of it. Read that sentence as the
# rule's own warning: the set of layouts is log4j2's to extend, and the only
# reason this one was caught is that rung-diff.py compares whole recordings.
#
# The listener name is required, rather than accepting any word after the
# level, because with no timestamp in front there is nothing else to tell this
# apart from a job line that happens to open with the word ERROR.
LOG_STATUS_BARE = re.compile(
    r'^(TRACE|DEBUG|INFO|WARN|ERROR|FATAL) Status[A-Za-z]*Listener ')

DROP_LINE = re.compile(
    r'^(\s+at\s|\s*\.\.\.\s\d+\smore|Caused by:|Suppressed:|'
    r'\s+[a-z][A-Za-z0-9_]*(\.[A-Za-z0-9_$]+)*\.[A-Z][A-Za-z0-9_$]*'
    r'(Exception|Error):|'
    r'WARNING:|OpenJDK|Picked up |SLF4J:|NOTE: |Using Spark\'s default log4j|'
    r'Setting default log level|To adjust logging level)')

# Paths: the data directories move between recording and replay.
PATH_RE = re.compile(r'(/[^\s"\':,]*?/)(full|reduced|cif)\b')
ABS_TMP = re.compile(r'/tmp/[A-Za-z0-9_.-]+')
# "2.8765578 sec." / "Time: 11 sec." / "in 4.02 seconds"
#
# The trailing period is inside the match on purpose. It was outside, and the
# substitution's own "sec." then landed beside the original one, so every
# *fractional* duration normalized to "<T> sec.." while every integral one
# normalized to "<T> sec." -- two recordings of one job differing on whether its
# duration happened to land on a whole second. The integer form is matched here
# for the same reason: one rule for the shape, not one rule per job that happened
# to be slow enough.
# The sub-second units are matched by the same rule rather than by a second one,
# and the unit is carried through to the replacement instead of being flattened
# to "sec.". Both halves of that were learned from this corpus: DemoAllVsAll and
# DemoQueryVsAll print "Time per alignment: N msec." and nothing else that
# differs, so the two recordings of each disagreed on exactly one line and both
# jobs -- the whole alignments module -- were unusable for a raw-text
# expectation until msec was matched. Flattening the unit would have been the
# other failure: "3 sec." and "3 msec." are three orders of magnitude apart, and
# a normalizer that maps them to one string cannot tell a migration that made an
# alignment 1000x slower from one that did not.
# The lookahead is not decoration. Without it the rule matched the "sec" of
# "10 sector" and "5 sections" and rewrote them to "<T> sec.tor" and
# "<T> sec.tions" -- a normalizer deleting a job's answer, which is the one
# way this file fails. No line in the current corpus is shaped that way, so
# nothing had caught it; it was found by running the rule against strings
# chosen to break it rather than against the corpus it already passes, and
# the three cases are in SELFTEST below so it stays found.
SECS_RE = re.compile(
    r'\b\d+(\.\d+)?(E-?\d+)?\s*(m|u|n)?sec(s|onds?)?\.?(?![A-Za-z-])')


def _secs(m):
    return '<T> %ssec.' % (m.group(3) or '')
# Bare float that is only ever a duration on these lines.
TIME_PREFIX = re.compile(r'^(Time|Elapsed|Duration)\s*:\s*\S+', re.I)
# Four classes stamp the wall clock into a name they then print, in four
# different formats -- CreatePdbToUniProtMappingFile:56 yyyyMMddHHmmss,
# WaterInteractions:44 yyyyMMdd_HHmm, HadoopUpdate:108 yyyyMMdd, HadoopUpdate:177
# and :217 yyyy-MM-dd. Leaving any of them in would make the two recordings of
# the same job differ and every expectation expire the moment it was written
# (trap 10).
#
# Only the three underscore-anchored forms are rewritten, and only in name
# position. yyyy-MM-dd is deliberately NOT rewritten: the .norm corpus carries
# 2017-02-28, 2016-01-28 and 2000-01-28 as real PDB deposition dates, so a
# blanket rule for that shape would delete data rather than a clock. HadoopUpdate
# reaches its yyyy-MM-dd lines only after holdings info the mirror does not
# serve, and PerformHadoopUpdate's recording stops at "Date Info Not Found";
# should that ever change, the screen at the end of run-mains.sh is what says so.
# Longest alternative first, or _yyyyMMdd would claim the front of _yyyyMMdd_HHmm
# and leave the time behind.
STAMP_RE = re.compile(r'_\d{14}(?=[._]|$)|_\d{8}_\d{4}(?=[._]|$)|_\d{8}(?=[._]|$)')


def normalize(text):
    out = []
    for line in text.splitlines():
        line = line.rstrip('\r')
        if not line.strip():
            continue
        if (LOG_CLASSIC.match(line) or LOG_STATUS.match(line)
                or LOG_STATUS_BARE.match(line)):
            continue
        if DROP_LINE.match(line):
            continue
        line = PATH_RE.sub(r'<DATA>/\2', line)
        line = ABS_TMP.sub('<TMP>', line)
        line = SECS_RE.sub(_secs, line)
        line = TIME_PREFIX.sub(lambda m: m.group(0).split(':')[0] + ': <T>', line)
        line = STAMP_RE.sub('_<TS>', line)
        out.append(line)
    return '\n'.join(out) + ('\n' if out else '')


# Each case is a real line one of these jobs printed, or a line it would print
# on the branch the census has not reached, paired with what the normalizer has
# to make of it. The negative cases matter more than the positive ones: a rule
# that rewrites everything passes every positive case there is, and the way this
# normalizer fails silently is by deleting a job's actual answer. Run before any
# recording is taken, so a rule that has stopped discriminating says so against a
# known answer rather than against the corpus it is about to flatten (trap 6).
SELFTEST = [
    # WaterInteractions:44 -- the one this file originally missed. Its format is
    # yyyyMMdd_HHmm, not the yyyyMMddHHmmss the first version of STAMP_RE
    # assumed, so both recordings of mains-r1 carried a live clock.
    ('Saving results to: /tmp/out/w/water_pl_r2_d3_b1.645_i24_20260916_1135.parquet',
     'Saving results to: <TMP>/w/water_pl_r2_d3_b1.645_i24_<TS>.parquet'),
    ('Saving results to: /tmp/out/w/water_pl_r2_d3_b1.645_i24_w_20260916_1136.parquet',
     'Saving results to: <TMP>/w/water_pl_r2_d3_b1.645_i24_w_<TS>.parquet'),
    # CreatePdbToUniProtMappingFile:56
    ('wrote /tmp/out/u/pdb2uniprot_20260916113542.parquet',
     'wrote <TMP>/u/pdb2uniprot_<TS>.parquet'),
    # HadoopUpdate:108
    ('fetching full_20260916.tar', 'fetching full_<TS>.tar'),
    # ... and the bare end-of-token form, which has no extension after it.
    ('dir /srv/full_20260916', 'dir /srv/full_<TS>'),
    # Negatives. Deposition dates are data; so are residue and atom counts that
    # happen to be eight digits, and a chain id that happens to follow one.
    ('| 4MYA | 2017-02-28 | 1|', '| 4MYA | 2017-02-28 | 1|'),
    ('deposited 2000-01-28', 'deposited 2000-01-28'),
    ('total_12345678 atoms', 'total_12345678 atoms'),
    ('chain_A_20260916x', 'chain_A_20260916x'),
    # Durations go, in both of the shapes the tree prints them.
    ('3 records reformatted in 2.8765578 sec.',
     '3 records reformatted in <T> sec.'),
    ('Time: 41.2 sec.', 'Time: <T> sec.'),
    ('Time: 11 sec.', 'Time: <T> sec.'),
    # DemoAllVsAll:64 and DemoQueryVsAll:66. The pair is here rather than one
    # case, because what has to hold is that the unit survives: if these two
    # wants were ever the same string the rule would be hiding a 1000x.
    ('Time per alignment: 3521 msec.', 'Time per alignment: <T> msec.'),
    ('Time per alignment: 355 msec.', 'Time per alignment: <T> msec.'),
    ('3 msec', '<T> msec.'),
    # Framework noise goes; a job line that merely starts with a digit stays.
    ('26/09/16 11:43:54 INFO SparkContext: Successfully stopped', ''),
    ('2026-09-16T06:42:15.850162402Z main ERROR Could not determine', ''),
    # Spark 4.0's executor thread name has spaces and parentheses in it.
    ('2026-09-17T00:14:58.599424223Z Executor task launch worker for task 1.0 '
     'in stage 6.0 (TID 10) ERROR Could not determine local host name', ''),
    # log4j2's other default layout: a space where the T was, a comma before
    # the milliseconds. This is what Spark 3.3 emits, and it is the one the
    # pinned-"T" rule let through into 138 of 140 recordings.
    ('2026-09-17 01:42:36,851 main ERROR Could not determine local host name '
     'java.net.UnknownHostException: 958dc136bd4a: Temporary failure', ''),
    ('2026-09-17 01:42:36,852 Executor task launch worker for task 1.0 in '
     'stage 6.0 (TID 10) ERROR Could not determine local host name', ''),
    # ... and the negatives that keep that rule from eating data: an ISO
    # timestamp a job printed itself, and a line whose last word is a level.
    # The space-separated form needs its own pair, because widening the
    # separator is exactly what puts an ordinary "date time ..." data line in
    # reach of this rule.
    ('2018-03-14T09:00:00Z deposited', '2018-03-14T09:00:00Z deposited'),
    ('2018-03-14T09:00:00Z status ERROR', '2018-03-14T09:00:00Z status ERROR'),
    ('2018-03-14 09:00:00 deposited', '2018-03-14 09:00:00 deposited'),
    ('2018-03-14 09:00:00 status ERROR', '2018-03-14 09:00:00 status ERROR'),
    ('2018-03-14 09:00:00,123 chain A ERRORS 4', '2018-03-14 09:00:00,123 chain A ERRORS 4'),
    # The timestamp-free layout, both lines of it, verbatim from c10.
    ('ERROR StatusConsoleListener Could not determine local host name', ''),
    (' java.net.UnknownHostException: 10f6f2a4e6af: 10f6f2a4e6af: '
     'Temporary failure in name resolution', ''),
    # ... and its negatives. A level is only a level when a status listener
    # follows it, and an indented throwable is only noise when it is indented:
    # HarnessDriver-rowtypes prints two of them as its answer, unindented and
    # prefixed, and this rule must not reach them.
    ('ERROR rate 0.5 over 12 chains', 'ERROR rate 0.5 over 12 chains'),
    ('ERROR StatusConsoleListeners are not configured',
     'ERROR StatusConsoleListeners are not configured'),
    ('ERRORS StatusConsoleListener 4', 'ERRORS StatusConsoleListener 4'),
    ('expect unsupported column type: java.lang.NullPointerException: Cannot '
     'invoke "org.apache.spark.sql.types.StructField.dataType()"',
     'expect unsupported column type: java.lang.NullPointerException: Cannot '
     'invoke "org.apache.spark.sql.types.StructField.dataType()"'),
    ('\tat java.net.InetAddress.getLocalHost(InetAddress.java:1427)', ''),
    ('3 structures written to: /tmp/out/x', '3 structures written to: <TMP>/x'),
    # The data path rewrite, which has to survive both env var spellings.
    ('Hadoop Sequence file path: MMTF_FULL=/tmp/data/full',
     'Hadoop Sequence file path: MMTF_FULL=<DATA>/full'),
    # ... and the negatives for the duration rule, which it used to fail.
    ('10 sector chains', '10 sector chains'),
    ('5 sections', '5 sections'),
    ('chain 3 second-shell atoms', 'chain 3 second-shell atoms'),
    # The path rule's boundary: "full" as the start of a longer word is a name.
    ('/bld/fullerene/x', '/bld/fullerene/x'),
]


def selftest():
    bad = 0
    for raw, want in SELFTEST:
        got = normalize(raw).rstrip('\n')
        if got != want:
            bad += 1
            sys.stderr.write('norm selftest: %r\n  got  %r\n  want %r\n'
                             % (raw, got, want))
    print('norm: %d/%d selftest cases pass' % (len(SELFTEST) - bad, len(SELFTEST)))
    return 1 if bad else 0


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--selftest':
        sys.exit(selftest())
    sys.stdout.write(normalize(sys.stdin.read()))
