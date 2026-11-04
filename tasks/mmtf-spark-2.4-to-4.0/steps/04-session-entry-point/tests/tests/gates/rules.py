#!/usr/bin/env python3
"""Turn scanner facts into gate findings, and refuse to run a rule that cannot fail.

The four scanners (ast-scan, class-scan, pom-scan, session-probe) each answer
"what is true of this tree" in sorted TSV. None of them decides anything. This
module is the deciding half: it reads a rules file, counts the facts each rule
selects, and reports PASS or FAIL per rule with the matched facts as evidence.

Three properties are the whole point of the file, and each exists because the
obvious implementation of a gate is a gate that is green for the wrong reason:

  * A selector must name every field of its kind (schema.tsv). A selector one
    column out of register matches nothing, and "matches nothing" is how an
    `absent` rule passes. Arity is checked at load, not at match time.

  * A rule must supply a counterexample: a fixture under the rules file's
    `controls/` on which that rule's verdict must be FAIL. `--selftest` runs
    every rule against its own counterexample and reports any that stayed
    green. A rule with no way to fail is not a measurement (trap 6).

  * A rule whose scanner supplied no facts is ERROR, never PASS. A verifier that
    forgot to run the probe should lose the gate, not win it.

Rules file, tab separated, five columns:

    <id>  <mode>  <selector>  <counterexample>  <reason>

`mode` is `absent`, `atleast:N`, `atmost:N` or `exactly:N`. `selector` is
`<scanner>.<kind>` followed by one term per field; a term is a regex fullmatched
against that field, or `*` for any value. `counterexample` names a file under
`controls/`, optionally `<scanner>=<file>` when the rule reads a scanner whose
name is not the fixture's; `-` is allowed only for a rule already covered by
another rule's fixture and is reported by --selftest as uncovered.

Output is one line per rule: `<PASS|FAIL|ERROR>\t<id>\t<count>\t<reason>`,
with the first few matched facts indented under anything that is not a PASS.
The reason column always describes the *finding*, so on a PASS line it reads as
the thing that was looked for and not found -- `PASS suppression-budget 10` says
there were ten and ten is within budget.

`include <file>` on a line of its own pulls in another rules file, which is how
every step gets the standing rules in `rules/common.tsv`. Ids are unique across
the whole include tree; a step that has to loosen a standing rule writes its id
as `override:<id>`, which replaces the earlier rule in place and is an error if
there was no earlier rule to replace. A step cannot weaken a gate by accident,
and cannot weaken one silently either -- the word is in the file.

`hold <file>` includes a file the same way but prefixes every id it defines
with `held-`, which is the one thing that changes what a rule *means* to the
scorer. score.py reads a requirement out of an id that starts `m<digits>-`, so
a held rule no longer names a requirement: it becomes a standing rule, and a
standing rule that fails voids the whole reading rather than costing its share.
That is the right shape for an earlier step's requirements at a later one. The
candidate is handed a tree that already passes them -- Harbor carries /app
forward -- so a FAIL is never "not done yet", it is "undone", and a tree that
has undone step 1 is not a tree whose step 7 is worth a number. It also keeps
each step's weights.tsv about that step: scoring the inherited rules instead
would have step 10 apportioning its reward over nine earlier migrations, and
the move the step is actually about would be worth a tenth of it.

A later step whose own move contradicts an inherited rule -- a version rung
past 3.0.3 makes `m1-spark-303` false on a correct tree -- does not drop the
hold. It writes `override:held-m1-spark-303` after the `hold` line, for the
same reason `override:` exists at all: the contradiction is stated in the file
rather than resolved by omission. Held rules keep the counterexamples they were
written with, so `--selftest` still proves each one can fail.

Usage:  rules.py --rules <file> [--rules <file>]... <scanner>=<facts.tsv> ...
        rules.py --selftest [--rules <file>]...
        rules.py --dump <file>
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA = os.path.join(HERE, 'schema.tsv')

MODES = ('absent', 'atleast:', 'atmost:', 'exactly:')


def load_schema(path=SCHEMA):
    """(scanner, kind) -> [field names]."""
    schema = {}
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            parts = line.rstrip('\n').split('\t')
            if len(parts) != 3:
                raise ValueError('%s:%d: want 3 fields, got %d'
                                 % (path, n, len(parts)))
            scanner, kind, fields = parts
            if (scanner, kind) in schema:
                raise ValueError('%s:%d: %s.%s declared twice'
                                 % (path, n, scanner, kind))
            schema[(scanner, kind)] = fields.split(',')
    return schema


class Rule(object):
    def __init__(self, rid, mode, scanner, kind, terms, counter, reason, where):
        self.id, self.mode = rid, mode
        self.scanner, self.kind, self.terms = scanner, kind, terms
        self.counter, self.reason, self.where = counter, reason, where

    def matches(self, fields):
        if len(fields) != len(self.terms):
            return False
        for term, value in zip(self.terms, fields):
            if term is not None and not term.fullmatch(value):
                return False
        return True

    def verdict(self, count):
        if self.mode == 'absent':
            return count == 0
        head, n = self.mode.split(':')
        n = int(n)
        return {'atleast': count >= n,
                'atmost': count <= n,
                'exactly': count == n}[head]


def parse_mode(mode, path, n):
    if mode == 'absent':
        return mode
    for m in MODES[1:]:
        if mode.startswith(m):
            int(mode[len(m):])           # raises if it is not a count
            return mode
    raise ValueError('%s:%d: unknown mode %r (want %s)'
                     % (path, n, mode, ', '.join(MODES)))


def parse_selector(sel, schema, path, n):
    terms = sel.split('\\t')
    head = terms[0]
    if '.' not in head:
        raise ValueError('%s:%d: selector must start <scanner>.<kind>, got %r'
                         % (path, n, head))
    scanner, kind = head.split('.', 1)
    if (scanner, kind) not in schema:
        raise ValueError('%s:%d: no such fact as %s.%s in schema.tsv'
                         % (path, n, scanner, kind))
    fields = schema[(scanner, kind)]
    rest = terms[1:]
    if len(rest) != len(fields):
        raise ValueError('%s:%d: %s.%s has %d field(s) (%s); selector names %d'
                         % (path, n, scanner, kind, len(fields),
                            ','.join(fields), len(rest)))
    return scanner, kind, [None if t == '*' else re.compile(t) for t in rest]


HELD = 'held-'


def _role(prefix):
    return 'included' if not prefix else 'held (%s)' % prefix


def load(path, schema, seen=None, rules=None, prefix=''):
    """Rules in file order, following `include` and `hold` once per file."""
    seen = {} if seen is None else seen
    rules = [] if rules is None else rules
    path = os.path.abspath(path)
    if path in seen:
        # Loading a file twice is ordinary -- two steps including common.tsv --
        # and the second is a no-op. Loading it twice in two different roles is
        # not: `include m01.tsv` beside `hold m01.tsv` would drop whichever came
        # second, and if that was the hold, six requirements a later step meant
        # to make standing would quietly stay scored. Which is the whole of the
        # difference, so it is an error rather than a precedence rule.
        if seen[path] != prefix:
            raise ValueError('%s: already loaded as %s; loading it again as %s'
                             ' would be silently dropped. A file is included or'
                             ' held, not both.'
                             % (path, _role(seen[path]), _role(prefix)))
        return rules
    seen[path] = prefix
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            if line.startswith('#') or not line.strip():
                continue
            line = line.rstrip('\n')
            if line.startswith('include '):
                load(os.path.join(os.path.dirname(path), line[8:].strip()),
                     schema, seen, rules, prefix)
                continue
            if line.startswith('hold '):
                # The prefix accumulates rather than replaces, so a file held
                # by a file that was itself held does not quietly come back as
                # a scored requirement two levels up.
                load(os.path.join(os.path.dirname(path), line[5:].strip()),
                     schema, seen, rules, prefix + HELD)
                continue
            parts = line.split('\t')
            if len(parts) != 5:
                raise ValueError('%s:%d: want 5 tab-separated fields, got %d'
                                 % (path, n, len(parts)))
            rid, mode, sel, counter, reason = parts
            if not reason.strip():
                raise ValueError('%s:%d: %s has no reason' % (path, n, rid))
            replacing = rid.startswith('override:')
            if replacing:
                rid = rid[len('override:'):]
            # Before the lookup, so that an override written inside a held file
            # replaces the held rule and not the scored one it was derived from.
            rid = prefix + rid
            at = next((i for i, r in enumerate(rules) if r.id == rid), None)
            if at is None and replacing:
                raise ValueError('%s:%d: override:%s replaces nothing'
                                 % (path, n, rid))
            if at is not None and not replacing:
                raise ValueError('%s:%d: %s is already defined at %s; write'
                                 ' override:%s to replace it'
                                 % (path, n, rid, rules[at].where, rid))
            scanner, kind, terms = parse_selector(sel, schema, path, n)
            rule = Rule(rid, parse_mode(mode, path, n), scanner, kind,
                        terms, counter, reason, '%s:%d' % (path, n))
            if at is None:
                rules.append(rule)
            else:
                rules[at] = rule
    return rules


def read_facts(path, scanner, schema):
    """[(kind, [fields])], rejecting any fact whose arity the schema denies."""
    out = []
    with open(path, errors='replace') as fh:
        for n, line in enumerate(fh, 1):
            if not line.strip():
                continue
            parts = line.rstrip('\n').split('\t')
            kind, fields = parts[0], parts[1:]
            want = schema.get((scanner, kind))
            if want is None:
                raise ValueError('%s:%d: %s emits no fact kind %r'
                                 % (path, n, scanner, kind))
            if len(fields) != len(want):
                raise ValueError('%s:%d: %s.%s wants %d field(s), got %d'
                                 % (path, n, scanner, kind, len(want),
                                    len(fields)))
            out.append((kind, fields))
    return out


def evaluate(rules, facts, evidence=3):
    """[(status, rule, count, [evidence lines])]; status is PASS/FAIL/ERROR."""
    results = []
    for rule in rules:
        if rule.scanner not in facts:
            results.append(('ERROR', rule, 0,
                            ['no %s facts were supplied' % rule.scanner]))
            continue
        hits = [f for k, f in facts[rule.scanner]
                if k == rule.kind and rule.matches(f)]
        status = 'PASS' if rule.verdict(len(hits)) else 'FAIL'
        results.append((status, rule, len(hits),
                        ['\t'.join([rule.kind] + h) for h in hits[:evidence]]))
    return results


def report(results, stream=sys.stdout, evidence=True):
    bad = 0
    for status, rule, count, ev in results:
        if status != 'PASS':
            bad += 1
        stream.write('%s\t%s\t%d\t%s\n' % (status, rule.id, count, rule.reason))
        if evidence and status != 'PASS':
            for line in ev:
                stream.write('      %s\n' % line[:200])
    return bad


def selftest(rule_files):
    """Every rule must FAIL on its own counterexample.

    This is the only check in the file that can catch a selector that matches
    nothing -- a typo in a field regex makes an `absent` rule pass on every
    input there is, including the fixture built to break it.
    """
    schema = load_schema()
    bad, uncovered, total = 0, [], 0
    if not rule_files:
        sys.stderr.write('rules selftest: no --rules file given; there is'
                         ' nothing to test and this is not a pass\n')
        return 1
    for rf in rule_files:
        rules = load(rf, schema)
        total += len(rules)
        for rule in rules:
            if rule.counter == '-':
                uncovered.append(rule.id)
                continue
            spec = rule.counter
            scanner = rule.scanner
            if '=' in spec:
                scanner, spec = spec.split('=', 1)
            fixture = os.path.join(os.path.dirname(os.path.abspath(rf)),
                                   'controls', spec)
            if not os.path.exists(fixture):
                sys.stderr.write('rules selftest: %s: no counterexample %s\n'
                                 % (rule.id, fixture))
                bad += 1
                continue
            facts = {scanner: read_facts(fixture, scanner, schema)}
            status, _, count, _ = evaluate([rule], facts)[0]
            if status == 'PASS':
                sys.stderr.write('rules selftest: %s passed on its own'
                                 ' counterexample %s (%d match(es)); the rule'
                                 ' has no way to fail\n'
                                 % (rule.id, spec, count))
                bad += 1
    for rid in uncovered:
        sys.stderr.write('rules selftest: %s has no counterexample\n' % rid)
        bad += 1
    if not total:
        sys.stderr.write('rules selftest: %d file(s) declared no rules at all\n'
                         % len(rule_files))
        bad += 1
    print('rules: %d rule(s) over %d file(s), %s'
          % (total, len(rule_files),
             'selftest passes' if not bad else '%d FAILURE(S)' % bad))
    return 1 if bad else 0


def dump(path):
    """The composed rule set, one line per rule, in evaluation order.

    What a step is graded against is the result of following its `include`,
    `hold` and `override:` lines, and that result is in no file. This prints
    it, so a rehearsal can diff two compositions against each other and a
    verifier can keep a copy of the set it actually used.
    """
    for rule in load(path, load_schema()):
        sys.stdout.write('\t'.join([
            rule.id, rule.mode, rule.scanner, rule.kind,
            '|'.join('*' if t is None else t.pattern for t in rule.terms),
            rule.counter, rule.reason]) + '\n')
    return 0


def main(argv):
    rule_files, sources, test = [], {}, False
    while argv and argv[0].startswith('--'):
        if argv[0] == '--rules':
            rule_files.append(argv[1]); argv = argv[2:]
        elif argv[0] == '--selftest':
            test, argv = True, argv[1:]
        elif argv[0] == '--dump':
            return dump(argv[1])
        else:
            sys.stderr.write('unknown option %s\n' % argv[0])
            return 2
    if test:
        return selftest(rule_files)
    for arg in argv:
        if '=' not in arg:
            sys.stderr.write('want <scanner>=<facts.tsv>, got %s\n' % arg)
            return 2
        scanner, path = arg.split('=', 1)
        sources[scanner] = path
    schema = load_schema()
    rules = []
    for rf in rule_files:
        rules += load(rf, schema)
    facts = {s: read_facts(p, s, schema) for s, p in sources.items()}
    # Always 0: a gate reports findings, it does not decide the reward, and a
    # non-zero exit here would be read as "the gate crashed" by the verifier.
    report(evaluate(rules, facts))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except ValueError as exc:
        # A malformed or contradictory rules file, which is always a defect in
        # the task and never in the candidate's tree. Non-zero and one line:
        # the verifier reads a non-zero exit here as "the gate crashed", which
        # is exactly what happened, and a traceback would bury the sentence
        # that says which line of which file is wrong.
        sys.stderr.write('rules: %s\n' % exc)
        sys.exit(2)
