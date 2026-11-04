"""The shape every migrated-ness gate reports in.

A gate is a list of named checks, each of which passed or did not, each
carrying enough detail for a reader to see why. The score is the fraction that
passed -- flat, one point per check -- so the weight a gate gives to some part
of the migration is expressed by how many checks it makes there, not by a
multiplier chosen after seeing the results.

Two rules keep the fraction honest.

A check that could not be evaluated is a *failure*, never a skip. If a table is
missing, every column check underneath it fails; if the database will not
answer, every check in the gate fails. The alternative -- dropping what cannot
be evaluated -- means deleting a table raises the score, which is the exact
shape of a gate that can be gamed by destroying things.

The denominator is fixed before the deployment is looked at, by `expect()`.
A gate whose denominator is built from what it finds is measuring the
candidate's tree against itself.
"""

import json


class Checks(object):
    def __init__(self, family):
        self.family = family
        self.results = []          # [(id, ok, detail)]
        self._ids = set()
        self.error = None
        self.notes = []

    # ------------------------------------------------------------- adding --
    def add(self, check_id, ok, detail=""):
        if check_id in self._ids:
            raise ValueError("duplicate check id %r in %s"
                             % (check_id, self.family))
        self._ids.add(check_id)
        self.results.append((check_id, bool(ok), detail))
        return bool(ok)

    def fail_all(self, check_ids, detail):
        """Every check under something that could not be evaluated."""
        for check_id in check_ids:
            self.add(check_id, False, detail)

    def note(self, text):
        """Something a reader should see that is not itself scored."""
        self.notes.append(text)

    # ------------------------------------------------------------ scoring --
    @property
    def passed(self):
        return sum(1 for _, ok, _ in self.results if ok)

    @property
    def total(self):
        return len(self.results)

    @property
    def score(self):
        return (self.passed / float(self.total)) if self.total else 0.0

    def failures(self, limit=200):
        return [{"check": i, "detail": d}
                for i, ok, d in self.results if not ok][:limit]

    def as_dict(self, limit=200):
        return {"family": self.family,
                "score": round(self.score, 6),
                "passed": self.passed,
                "total": self.total,
                "error": self.error,
                "notes": self.notes,
                "failed": self.failures(limit)}

    def __repr__(self):
        return "<%s %d/%d>" % (self.family, self.passed, self.total)


def crashed(family, reason, total=1):
    """A gate that could not run at all: zero over a non-empty denominator."""
    c = Checks(family)
    c.error = reason
    for i in range(total):
        c.add("%s#%d" % (family, i), False, reason)
    return c


# Product scoring (user decision 2026-10-13). The earned families are split by
# what they measure, and the reward is the product of the two halves:
#
#   T1  migrated-ness -- every earned family that is not `behavior:*`
#       (`migrated:*`: the step's own migration: the strict configuration and
#       its repairs, the install files, the sequences, the dialect, the wiring,
#       the encoding repair);
#   T2  preservation -- every earned `behavior:*` family (the application and
#       the database still answer what the MySQL recording answered).
#
# Inside each factor the plan's weights keep their relative sizes, renormalised
# to that factor, so T1 and T2 are each a weighted mean in [0, 1]. A tree that
# keeps every recorded behaviour and migrates nothing scores T1 = 0 and so 0,
# and so does one that migrates and stops behaving. The guards stay what they
# were: multiplicative caps, so every hard zero is still a zero.
PRESERVATION_PREFIX = "behavior:"


def factor_of(family):
    """'T2' for a preservation family, 'T1' for every other earned family."""
    return "T2" if family.startswith(PRESERVATION_PREFIX) else "T1"


def combine(parts, weights=None, guards=()):
    """The step's reward: T1 x T2 over the earned families (see `factor_of`),
    capped by the guards.

    Weights default to equal. A part that contributed no checks contributes no
    weight either, so a gate that is not applicable to a step (there is no
    PostgreSQL yet) is left out by the step rather than silently scoring zero.

    A family is *earned* or it is a *guard*, and which one it is was decided
    before the tree was looked at.

    An earned family asks questions this step's work answers: they are false on
    the tree the candidate was handed and true on the tree the step asks for.
    Its score is the fraction of them it earned, and it pays.

    A guard family asks questions that are already true before the step begins
    -- the part of the recorded corpus this step does not touch, the schema it
    must not reshape, the shortcuts it must not take. Passing one is not
    evidence of work, so it must not pay, or every step would hand out most of
    its reward for arriving. A guard cannot be dropped either: the questions
    are exactly the ones a wrong answer gets wrong. So a guard enters as a
    multiplier, not as a term. Breaking things costs; leaving them alone earns
    nothing.

    Guards compose by *product*, not by mean. Each one is a separate cap, and a
    mean would dilute whichever cap a given wrong answer trips: a candidate who
    leaves 404 recorded responses alone and fails all three must-flip probes
    should be capped by the probes, and averaging the two guards would hand
    back most of that penalty for work it did not do.

    That is what keeps the nop at zero without the arithmetic having to be
    tuned: every earned family is preconditioned on the step's own deliverable
    (`gate.py` treats an unanswerable check as a failure), so on an untouched
    tree the earned mean is 0 and no guard can lift it.
    """
    if weights is None:
        weights = {}
    guards = set(guards)
    detail = {p.family: p.as_dict() for p in parts}

    def mean(group):
        live = [p for p in group if p.total]
        if not live:
            return None
        total_w = sum(weights.get(p.family, 1.0) for p in live)
        return sum(weights.get(p.family, 1.0) * p.score
                   for p in live) / total_w

    earned_parts = [p for p in parts if p.family not in guards]
    factors = [mean([p for p in earned_parts if factor_of(p.family) == f])
               for f in ("T1", "T2")]
    factors = [f for f in factors if f is not None]
    if not factors:
        # A step with nothing but guards has nothing to earn, and scoring it
        # by them would pay for arriving. That is a malformed plan, and the
        # honest reward for one is zero.
        return 0.0, detail
    earned = 1.0
    for f in factors:
        earned *= f
    held = 1.0
    for p in parts:
        if p.family in guards and p.total:
            held *= p.score
    return round(earned * held, 6), detail


def dumps(obj):
    return json.dumps(obj, sort_keys=True, indent=1, ensure_ascii=False,
                      default=str)
