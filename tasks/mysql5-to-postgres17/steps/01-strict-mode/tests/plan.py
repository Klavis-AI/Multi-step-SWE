"""Step 01 -- the source server stops repairing invalid values silently.

Seven families, and which of them *pay* is the whole of the calibration.

Earned -- false on the tree the candidate is handed, true on the tree the step
asks for:

    migrated:strict-mode        the deliverable: strict in the running server,
                                strict in `/etc/mysql/my.cnf`, and nothing in
                                the tree putting its own connections back out
                                of it.
    migrated:repairs            the six readings that were *measured* to
                                change when the mode goes on and nothing else
                                does (`NOTES-authoring.md` §19).
    behavior:strict-requests    the 9 of 413 recorded responses the same
                                measurement moved.
    behavior:strict-tables      the 2 of 24 recorded table snapshots it moved.

Guards -- already true before the step begins, so they cap rather than pay
(`gate.combine`):

    behavior:requests           the other 404 responses.
    behavior:tables             both snapshots of everything outside the
                                partition, plus every `tables_before`: strict
                                mode was measured to move none of those, so a
                                candidate that moves one broke something it
                                was not asked to touch. 50 checks, against the
                                partition's 2.
    migrated:no-shortcut        the rule-7 must-flip probes, the readings the
                                mode does not disturb, and the schema and
                                socket the step must not reshape.

The weights over the earned families put 0.60 on the repairs because that is
the step: the one-line config edit is worth 0.10 and the recorded corpus the
other 0.30. Measured against live containers, that puts a no-op at 0.000, a
tree with `sql_mode` set and the application untouched at 0.100, and the golden
solution at 1.000 (`authoring/rehearse_strictgate.py`).

Order matters and is not alphabetical. The behaviour families run first so that
`behaviorgate` takes its one corpus pass before the probes sign up and post,
because that is the order `strict-probes.json` was recorded in -- the corpus's
last scenario resets four lessons, and each `reset()` recreates its table
without a CHARSET clause.
"""

from wglib import behaviorgate, dbstate, strictgate

STEP = "01"
TITLE = "MySQL stops repairing invalid values silently"
WANT_HTTP = True
ROOTS = ("/app",)

DATABASE = "webgoatphp"


def _state():
    from wglib import deploy
    conn, _note = deploy.connect_mysql(DATABASE)
    return dbstate.MySQLState(conn, DATABASE)


def _corpus(name, kinds, subset, precondition):
    def fn(ctx):
        return behaviorgate.checks(
            ctx, name, _state, kinds=kinds, subset=subset,
            precondition=strictgate.precondition(ctx) if precondition else None)
    return fn


FAMILIES = [
    ("behavior:requests",
     _corpus("behavior:requests", ("requests",), "outside", False)),
    ("behavior:tables",
     _corpus("behavior:tables", ("tables_before", "tables_after"), "outside",
             False)),
    ("behavior:strict-requests",
     _corpus("behavior:strict-requests", ("requests",), "inside", True)),
    ("behavior:strict-tables",
     _corpus("behavior:strict-tables", ("tables_after",), "inside", True)),
    ("migrated:strict-mode", strictgate.mode_family),
    ("migrated:repairs", strictgate.repair_family),
    ("migrated:no-shortcut", strictgate.guard_family),
]

# Rule 5 wants both scored kinds present and neither below ~30% of the
# reward. Behaviour equivalence earns 0.30 and the migrated-ness gates 0.70,
# split 0.10 for the config line and 0.60 for the work it forces. The two
# behaviour weights keep the 4:1 they had, which is roughly the 9:2 the
# measured partition has.
#
# The three guard families carry no weight and are worth everything: they
# multiply, so the 404 responses and 46 table snapshots outside the partition
# can take any of this to zero.
WEIGHTS = {
    "behavior:strict-requests": 0.24,
    "behavior:strict-tables": 0.06,
    "migrated:strict-mode": 0.10,
    "migrated:repairs": 0.60,
}

GUARDS = ("behavior:requests", "behavior:tables", "migrated:no-shortcut")
