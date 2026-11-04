#!/usr/bin/env python3
"""Apply the per-job scoring projection to a normalized recording.

A `.norm` file is what the baseline printed and stays whole (NOTES 4.5.4). Five
of the 140 jobs print a line the scorer must not compare, for one of two
reasons, and `projection.tsv` carries the reason as a class:

  RUN   it moves between two runs of the same build -- two unseeded `sample`
        draws and one aligner that superposes into a shared array. Comparing
        those as text is scoring a coin.
  RUNG  it is repeatable within a rung and moves between Spark versions for a
        mechanism no migration guide documents, so rule 5 forbids both scoring
        it as text and re-recording it as a must-flip.

So the scorer projects first: this module reads `projection.tsv`, and for a job
named there returns the subset of lines that file says may be compared. For
every other job it returns the text unchanged, which is the point -- the default
is byte equality and the exceptions are enumerated.

Three rules, in the order they were needed:

  head:N     keep the first N lines.
  drop:RE    keep every line the pattern does not find.
  cut:A;;B   drop the block that starts at the first line matching A and ends
             at the next line matching B, which is kept; repeatable, and runs
             to end of file if B never matches. For a block of output that is
             one draw from a random variable, where no line-level pattern picks
             the block out without also hitting identical lines elsewhere in
             the recording.
  sort       sort each maximal run of consecutive lines that start with `|`.
             `show()` prints border/header/border/rows/border, so a header is
             always a run of one and sorting is identity on it; only the data
             rows of a grid move. Gives up row order and nothing else: every
             cell of every row still compares, a row that vanished still fails,
             and a value that moved still fails. For a `groupBy` whose result
             has ties and no total order, where the tie breaks one way at one
             Spark version and the other way at the next.
  vec        canonicalize a `show()` grid and redact its numeric vector cells:
             drop the `+---+` rules, right-strip every cell, and rewrite a cell
             that is a bracketed list of numbers as `[<vec:N>]` -- N being the
             element count, or `...` when `show()` truncated the cell so that no
             count is visible. Column names, row order, row count, every string
             cell and (where it is printed) the vector arity all still compare;
             only the float values stop comparing. `head` and `drop` would each
             cost far more here than the drift is worth: a Word2Vec table is
             printed by `show(truncate=false)`, which sizes every column to its
             widest cell, so one drifting vector moves the `+---+` rules and the
             header line too, and a line-class rule that catches those catches
             the neighbouring stable tables with them (4.10.3).

Shipped into every step's tests/ beside `projection.tsv`, the recordings and
`run-suite.sh`, and used by `check-suite.sh` to make the second of its two
comparisons.

Usage:  projection.py --selftest
        projection.py <job> [projection.tsv] < in.norm > out.norm
"""
import os
import re
import sys


CLASSES = ('RUN', 'RUNG')


def load(path):
    """job -> (cls, rule, reason). Malformed rows are an error, not a skip."""
    table = {}
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            # rstrip('\n') only: a `drop:` pattern may legitimately end in a
            # space, and this corpus has one that does ("^l: ").
            parts = line.rstrip('\n').split('\t')
            if len(parts) != 4:
                raise ValueError('%s:%d: want 4 tab-separated fields, got %d'
                                 % (path, n, len(parts)))
            job, cls, rule, reason = parts
            if job in table:
                raise ValueError('%s:%d: %s is projected twice' % (path, n, job))
            if cls not in CLASSES:
                raise ValueError('%s:%d: %s has class %r, want one of %s'
                                 % (path, n, job, cls, ' '.join(CLASSES)))
            if not reason.strip():
                raise ValueError('%s:%d: %s has no reason' % (path, n, job))
            if rule.startswith('head:'):
                int(rule[5:])            # raises if it is not a count
            elif rule.startswith('drop:'):
                re.compile(rule[5:])     # raises if it is not a pattern
            elif rule.startswith('cut:'):
                a, sep, b = rule[4:].partition(';;')
                if not sep or ';;' in b:
                    raise ValueError('%s:%d: cut: wants exactly one ";;" '
                                     'separator, got %r' % (path, n, rule))
                if not a or not b:
                    raise ValueError('%s:%d: cut: needs both a start and an '
                                     'end pattern, got %r' % (path, n, rule))
                re.compile(a), re.compile(b)   # raise if either is not one
            elif rule not in ('vec', 'sort'):
                raise ValueError('%s:%d: unknown rule %r' % (path, n, rule))
            table[job] = (cls, rule, reason)
    return table


BORDER = re.compile(r'^\+[-+]*\+$')
# A vector cell as `show()` prints one: `[0.001,-0.003,...]`, or the same thing
# cut off by the default truncate=20 with no closing bracket. Anchored whole-cell
# and required to start with a digit, so it cannot reach the ngram column, whose
# cells are bracketed lists too -- `[AE, ET, TA]` -- but start with a letter.
NUMCELL = re.compile(r'^\[-?\d[-\d.,E]*(\]|\.\.\.)$')


def redact_cell(cell):
    stripped = cell.strip()
    if not NUMCELL.match(stripped):
        return cell.rstrip()
    if stripped.endswith(']'):
        return '[<vec:%d>]' % (stripped.count(',') + 1)
    return '[<vec:...>]'


def devec(lines):
    out = []
    for line in lines:
        if BORDER.match(line):
            continue
        if line.startswith('|') and line.endswith('|'):
            line = '|'.join(redact_cell(c) for c in line.split('|'))
        out.append(line)
    return out


def cut(lines, start, end):
    """Drop [start, end), keeping the line `end` matched.

    `start` is tested before `end`, so a line matching both opens the block and
    is dropped; the block then closes on the next `end`. There is no nesting:
    once open, the first `end` closes it.
    """
    s, e = re.compile(start), re.compile(end)
    out, dropping = [], False
    for line in lines:
        if not dropping and s.search(line):
            dropping = True
        if dropping and e.search(line):
            dropping = False
        if not dropping:
            out.append(line)
    return out


def sortrows(lines):
    out = list(lines)
    i = 0
    while i < len(lines):
        if lines[i].startswith('|'):
            j = i
            while j < len(lines) and lines[j].startswith('|'):
                j += 1
            out[i:j] = sorted(lines[i:j])
            i = j
        else:
            i += 1
    return out


def project(job, text, table):
    rule = table.get(job, (None, None, None))[1]
    if rule is None:
        return text
    lines = text.splitlines()
    if rule == 'vec':
        lines = devec(lines)
    elif rule == 'sort':
        lines = sortrows(lines)
    elif rule.startswith('cut:'):
        a, _, b = rule[4:].partition(';;')
        lines = cut(lines, a, b)
    elif rule.startswith('head:'):
        lines = lines[:int(rule[5:])]
    else:
        pat = re.compile(rule[5:])
        lines = [l for l in lines if not pat.search(l)]
    return '\n'.join(lines) + ('\n' if lines else '')


HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, 'projection.tsv')


def selftest():
    """Against known answers, including the ones that must NOT be projected.

    A projection that returned '' for everything would satisfy every positive
    case there is and would score every job as passing, so the negatives carry
    the weight here (trap 6).
    """
    bad = 0

    def check(label, got, want):
        nonlocal bad
        if got != want:
            bad += 1
            sys.stderr.write('projection selftest: %s\n  got  %r\n  want %r\n'
                             % (label, got, want))

    table = load(DEFAULT)

    # head:N keeps N lines and drops the rest ...
    check('head keeps 1',
          project('ReadLocalMmtfHadoopFile', '# structures: 82\na\nb\n', table),
          '# structures: 82\n')
    # ... and is a no-op on a recording that is already that short, which is the
    # -rosetta case: the projection must not require the tail to be there.
    check('head on a short file',
          project('ReadLocalMmtfHadoopFile-rosetta', '# structures: 4\n', table),
          '# structures: 4\n')
    # drop:<regex> removes the matching lines and keeps everything else, in
    # order, including a line that merely contains the pattern elsewhere.
    check('drop removes matches',
          project('HarnessDriver-exhaustive',
                  'root\nl: 56 c1: 100\nRows: 3\nl: 47 c1: 99\ntail: l: x\n',
                  table),
          'root\nRows: 3\ntail: l: x\n')
    # The default, and the whole reason this file is short: a job with no row is
    # returned byte for byte.
    check('unprojected job is untouched',
          project('AnalyzeWaterInteractions4', 'a\nb\nl: 1\n', table),
          'a\nb\nl: 1\n')
    check('empty input stays empty',
          project('ReadLocalMmtfHadoopFile', '', table), '')

    # A RUNG row is a no-op inside a single rung, so it has to be exercised
    # like one: the projection must still delete the line it names.
    check('RUNG head keeps the stable prefix',
          project('DatasetClassifier',
                  'Feature count            : 50\n' * 9
                  + 'Dataset size (balanced)  : 3\n', table),
          'Feature count            : 50\n' * 9)
    check('RUNG drop removes the echo, keeps the table',
          project('PdbToUniProtMappingDemo',
                  '4HHB\n1STP\n|          4HHB.A|        1|\n', table),
          '|          4HHB.A|        1|\n')
    # ... and does not reach a line that merely starts with a PDB id, which is
    # every row of the tables these jobs exist to print.
    check('RUNG drop spares a data row',
          project('PdbToUniProtMappingDemo', '4HHB.A 1 VAL\n', table),
          '4HHB.A 1 VAL\n')

    # `sort` must reach the data rows of a grid and nothing else. The header is
    # the case to get wrong: it starts with `|` like the rows do, and it sorts
    # before most of them, so a rule that swept the whole grid into one run
    # would leave the column names buried in the middle of the table.
    GRID2 = ('+----+-----+\n|name|count|\n+----+-----+\n'
             '|zeta|1    |\n|alpha|2   |\n+----+-----+\nRows: 2\n')
    check('sort orders the data rows, not the header',
          project('AtpInteractionAnalysis', GRID2, table),
          '+----+-----+\n|name|count|\n+----+-----+\n'
          '|alpha|2   |\n|zeta|1    |\n+----+-----+\nRows: 2\n')
    check('sort leaves non-grid lines where they are',
          project('AtpInteractionAnalysis', 'b\na\n', table), 'b\na\n')
    # printSchema() prints " |-- field: type", with a leading space, and those
    # lines are ordered by the schema rather than by chance. Sorting them would
    # stop the suite noticing a column that moved in the schema.
    SCHEMA = 'root\n |-- b: string\n |-- a: string\n'
    check('sort spares printSchema, which starts with a space',
          project('AtpInteractionAnalysis', SCHEMA, table), SCHEMA)
    # Two identical rows are two rows. Sorting through a set() would reconcile
    # a recording that lost one of them, which is a row this suite must still
    # be able to miss: groupBy tables in this corpus do repeat a row.
    check('sort keeps duplicate rows',
          project('AtpInteractionAnalysis',
                  '+-+\n|h|\n+-+\n|z|\n|a|\n|z|\n+-+\n', table),
          '+-+\n|h|\n+-+\n|a|\n|z|\n|z|\n+-+\n')
    for label, mutant in (
            ('a row that vanished', '+-+\n|h|\n+-+\n|z|\n+-+\n'),
            ('a cell that moved',   '+-+\n|h|\n+-+\n|z|\n|b|\n+-+\n')):
        check('sort still shows %s' % label,
              project('AtpInteractionAnalysis', mutant, table)
              != project('AtpInteractionAnalysis',
                         '+-+\n|h|\n+-+\n|z|\n|a|\n+-+\n', table), True)

    # `cut` is the only rule whose effect on a line depends on the lines around
    # it, so the cases that matter are the boundaries: the end line is kept,
    # identical text outside the block is kept, and an unterminated block does
    # not silently swallow the file's tail without saying so.
    BAL = ('balance input: 82 row(s)\n'
           '|X-RAY DIFFRACTION       |59   |\n'
           'balance downsampled: 10 row(s)\n'
           '|X-RAY DIFFRACTION       |2    |\n'
           'balance upsampled: 441 row(s)\n'
           '|X-RAY DIFFRACTION       |59   |\n')
    check('cut drops the block and keeps its end line',
          project('HarnessDriver-balance', BAL, table),
          'balance input: 82 row(s)\n'
          '|X-RAY DIFFRACTION       |59   |\n'
          'balance upsampled: 441 row(s)\n'
          '|X-RAY DIFFRACTION       |59   |\n')
    # The two |X-RAY DIFFRACTION|59| lines are byte-identical to each other and
    # one of them sits inside the block. A line rule cannot tell them apart;
    # this is the case that rules `drop:` out for this job.
    check('cut keeps a line identical to one it dropped',
          project('HarnessDriver-balance', BAL, table).count(
              '|X-RAY DIFFRACTION       |59   |'),
          2)
    check('cut runs to EOF when the end never matches',
          project('HarnessDriver-balance',
                  'balance input: 1\nbalance downsampled: 0\n|a|\n', table),
          'balance input: 1\n')
    check('cut is a no-op when the start never matches',
          project('HarnessDriver-balance', 'balance input: 82 row(s)\n', table),
          'balance input: 82 row(s)\n')
    # Malformed cut rules are load errors, not silently-ignored rows.
    for badrule in ('cut:^a', 'cut:^a;;', 'cut:;;^b', 'cut:^a;;^b;;^c'):
        try:
            load_bad = badrule
            import io
            tmp = os.path.join(HERE, '.selftest-cut.tsv')
            with open(tmp, 'w') as fh:
                fh.write('J\tRUNG\t%s\treason\n' % badrule)
            try:
                load(tmp)
                check('cut rejects %r' % badrule, 'accepted', 'ValueError')
            except ValueError:
                pass
            finally:
                os.unlink(tmp)
        except OSError:
            pass

    # `vec` is the only rule that rewrites a line rather than deleting it, so
    # it needs both directions: what it must redact, and what it must leave
    # alone. A rule that redacted the whole grid would pass every positive here
    # and would stop scoring the columns these five jobs exist to produce.
    GRID = ('+----------------+--------+-----+\n'
            '|structureChainId|ngram   |f    |\n'
            '+----------------+--------+-----+\n'
            '|1STP.A          |[AB, BC]|[0.5,-0.25,3.0E-4]|\n'
            '+----------------+--------+-----+\n')
    check('vec drops the rules, rstrips cells, redacts the vector',
          project('SecondaryStructureWord2VecEncoder', GRID, table),
          '|structureChainId|ngram|f|\n'
          '|1STP.A|[AB, BC]|[<vec:3>]|\n')
    # show(truncate=20) cuts the cell before its closing bracket, so no element
    # count survives to compare. Saying so in the token is the honest form; the
    # alternative, guessing a count from the visible prefix, would invent one.
    check('vec marks a truncated vector as countless',
          project('ProteinFoldDatasetCreator',
                  '|1STP.A|[0.001234567890123...|\n', table),
          '|1STP.A|[<vec:...>]|\n')
    # The ngram column is a bracketed list too. It is the input Word2Vec is fed
    # and it does not drift, so redacting it would give up the one column that
    # proves the windowing still works.
    check('vec spares a bracketed list of non-numbers',
          project('SecondaryStructureWord2VecEncoder', '|x|[AE, ET, TA]|\n',
                  table),
          '|x|[AE, ET, TA]|\n')
    check('vec leaves a non-grid line alone',
          project('SecondaryStructureWord2VecEncoder',
                  'Hadoop Sequence file path: MMTF_FULL=<DATA>/full\n', table),
          'Hadoop Sequence file path: MMTF_FULL=<DATA>/full\n')
    # The point of the rule, stated as the pair of differences it must and must
    # not survive.
    vary = GRID.replace('[0.5,-0.25,3.0E-4]', '[0.9,0.125,-7.0E-5]')
    check('vec hides a float that moved',
          project('SecondaryStructureWord2VecEncoder', vary, table),
          project('SecondaryStructureWord2VecEncoder', GRID, table))
    for label, mutant in (
            ('an arity that moved', GRID.replace('3.0E-4]', '3.0E-4,1.0]')),
            ('a string cell that moved', GRID.replace('1STP.A', '1STP.B')),
            ('a column that was renamed', GRID.replace('ngram   ', 'ngrams  ')),
            ('a row that vanished', GRID.replace('|1STP.A          |[AB, BC]|'
                                                 '[0.5,-0.25,3.0E-4]|\n', ''))):
        got = project('SecondaryStructureWord2VecEncoder', mutant, table)
        if mutant == GRID:                       # the edit has to land (trap 7)
            check('vec selftest mutant %s is a no-op' % label, mutant, 'edited')
        check('vec still shows %s' % label,
              got != project('SecondaryStructureWord2VecEncoder', GRID, table),
              True)

    # The table and the declaration have to agree, and they agree differently
    # per class. A RUN row says the job is loose inside a rung, so it must be
    # declared loose; a RUNG row says the opposite, so it must be declared
    # STABLE. Collapsing these two into one check is what makes it tempting to
    # relabel a stable job VALUE to keep the check quiet, which is a lie about
    # within-run repeatability that every later step would inherit.
    # Two real layouts, and the file is looked for in both: it sits two levels
    # up in the authoring tree (build/determinism.tsv) and beside this file in
    # a step's shipped payload, where mk-step-tests.sh puts both into
    # /tests/suite. Looking only two up passed here and failed in the verifier,
    # where /tests/suite/../.. is /, which is exactly the shape of failure the
    # `bad += 1` below exists to make loud rather than quiet.
    decl = next((p for p in (os.path.join(HERE, 'determinism.tsv'),
                             os.path.join(HERE, '..', '..', 'determinism.tsv'))
                 if os.path.exists(p)), None)
    if decl:
        loose, stable = set(), set()
        for line in open(decl):
            line = line.split('#')[0].rstrip('\n')
            parts = line.split('\t')
            if len(parts) >= 2 and parts[0] in ('ORDER', 'VALUE'):
                loose.add(parts[1])
            elif len(parts) >= 2 and parts[0] == 'STABLE':
                stable.add(parts[1])
        run = sorted(j for j, v in table.items() if v[0] == 'RUN')
        rung = sorted(j for j, v in table.items() if v[0] == 'RUNG')
        check('RUN rows == declared loose', run, sorted(loose))
        check('RUNG rows are declared STABLE',
              [j for j in rung if j not in stable], [])
    else:
        sys.stderr.write('projection selftest: no determinism.tsv beside this'
                         ' tree; the cross-check did not run\n')
        bad += 1

    print('projection: %d row(s) (%d RUN, %d RUNG), %s'
          % (len(table),
             sum(1 for v in table.values() if v[0] == 'RUN'),
             sum(1 for v in table.values() if v[0] == 'RUNG'),
             'selftest passes' if not bad else '%d FAILURE(S)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--selftest':
        sys.exit(selftest())
    job = sys.argv[1]
    path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT
    sys.stdout.write(project(job, sys.stdin.read(), load(path)))
