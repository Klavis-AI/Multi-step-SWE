"""Step 06 -- the text is repaired.

Every step before this one carried bytes without reinterpreting them, and step
02's schema conversion and step 03's data migration were graded on exactly
that: `Caf\xc3\xa9` arrived in PostgreSQL as `Caf\xc3\xa9`, because a
migration that quietly decided what some of the text "meant" is a migration
nobody can audit. The damage in this corpus predates the migration. It was
made by a client that wrote UTF-8 and read it back a byte at a time as a
single-byte charset, and it is now 31 values in 12 columns of a database whose
other 40 non-ASCII values -- 45 counting the five that appear in two columns
each -- are perfectly correct text.

So this is the first step whose requirement is to change what a column holds.
That single fact is what shapes the plan, in three places.

**The encoding family is declared first, and that is not cosmetic.** Four
`lesson_*` tables are dropped and re-seeded with ASCII rows by each lesson's
own `reset()` during the corpus pass -- `challenges/single/AccessControlMatrix
/index.php:104` is one of the four -- so sixteen of the 31 damaged values, in
seven of the 12 damaged columns, are not in the database at all once the corpus
has run. A repair-mode reading taken after the pass would agree with an
unrepaired tree about half of the damage. `behaviorgate` drives the corpus at
most once per verifier run and caches it, so "before the corpus" means
"before any behaviour family runs",
which in a plan means first. NOTES-authoring.md section 27.3 has the
measurement; the oracle run is what proves the ordering holds.

**The behaviour families are split by what the repair moves, not by what the
corpus writes.** Steps 04 and 05 split the "after" snapshot into written and
unwritten halves (section 22) so that the tables the corpus never touches --
free to any tree that breaks nothing -- were a guard rather than earned
weight. Here the free/earned line falls somewhere else: 48 of the 413 recorded
responses render text that the repair changes, and 7 of 24 before-snapshots
and 3 of 24 after-snapshots hold a column it changes. Those 58 ids are what
this step's work moves; the other 380 are what it must not. So the partition
in `behavior-partition-06.json` is that measurement, the earned families score
inside it, and the guards score outside it. The written/unwritten split is
dropped because the partition already separates the same thing more exactly --
`load_partition` cannot express the split kinds anyway, and a family that
asked for one would silently score its whole recording.

**The 58 ids are re-recorded.** Rule 5's exception clause, and the reason this
is the first step to use it for a table snapshot rather than a response: a
step whose requirement is to repair the contents of a column changes the
digest of the table holding it, and an expectation recorded before the change
cannot be the expectation after it. The alternative -- leaving the three
`tables-*` families out of the step -- would drop the only check on the 38
snapshots the step does *not* repair, which is the larger part of what is at
risk here. `behavior-overrides-06.json` carries all 58 with their `why`, each
recorded from the golden repaired deployment by
`authoring/record_step06.py`, two passes in agreement (rule 7). It also
carries step 04's own single re-recording, because the gate reads one override
file and the file this step names has to hold everything this step's
expectation is overridden by.

**Rule 8, and the problem this step has with it.** The nop tree here is a
*finished* tree: step 05's solution, which passes every inherited guard by
construction. Nothing inherited can put it at 0.0. Two things do.

The partition is the first. Without it, `behavior:requests` would score all
413 ids and the nop would hold 365 of them for free -- 0.885 of 0.27, and a
floor over 0.5 with nothing else moving. Scoring only the 48 that move takes
the nop to 0/48 on that family and 0/10 on the snapshots.

`migrated:install-repaired` is the second, and it is what makes the nop
exactly zero rather than merely small. One check per damaged value on
`install/_db/pdo_pgsql.data.sql` (31; two all-or-nothing checks until the
round-1 review split them for partial credit): its damaged spelling is gone
from the file and its repaired one is in it. Every one is false on a tree that
has not done the step, so the guard product is 0 and no earned fraction
survives it. Section
24.14 met the same problem in step 05 and answered it the same way -- one
small binary guard that states the step's headline requirement, named in the
instruction's first paragraph.

It is not a third reading of the same thing. `migrated:encoding` reads the
live database and never opens that file. `migrated:install-data` (inherited,
step 03's) opens the file but only ever compares it *against* the live
database, so a tree that repaired neither passes it and a tree that repaired
both does too. Between the three, repairing one of the two places is caught by
`install-data` and repairing neither is caught here.

**Rule 4.3 and the shape of a mechanical answer.** The mechanical answer here
is a blanket conversion of every text column --
`convert_from(convert_to(col, 'LATIN1'), 'UTF8')` or its PHP equivalent. Run
as one statement per column it aborts on the first value LATIN1 cannot encode
and delivers the nop. Run in the form an attacker would actually reach for --
per value, inside a savepoint, so an unencodable value is skipped rather than
fatal, iterated to a fixed point, with the same substitution table applied to
the installer file -- it repairs 15 of the 31 damaged values with LATIN1 alone
and 27 with `LATIN1` and `WIN1252` tried in turn. The values they cannot reach
are the ones whose repair needs a byte MySQL's latin1 maps and PostgreSQL's two
built-in codecs do not, and those values stay damaged in
`install/_db/pdo_pgsql.data.sql` as well. With the guard all-or-nothing both
passes scored **0.000**; with it per value (round 1) they score **0.169949**
(LATIN1 alone, the platform's own conversion) and **0.700872** (the two-codec
loop), measured in round 2 on 2026-10-05 against the step-05 golden handoff.
The second is partial credit for 27 of 31 repairs, a hand-built loop rather
than the platform's converter; the four values it cannot reach are what stands
between it and 1.0. What neither does is damage correct text: a codec that cannot encode a
value, or whose output is not valid UTF-8, leaves it alone, so the 45 correct
non-ASCII values survive both passes and `behavior:requests` reads 365/365 on
each. The discrimination is in what is *repaired*, not in what is broken.
NOTES section 27.2 has both runs and the per-family numbers.

**Order.** The encoding family first, for the reason above. Then the seven
retirement families that only read the tree off disk, then the five that read
the delivered database as the authority, then the single corpus pass and its
four readings, then the tree-walking families, then the four install families
last -- `migrated:install-repaired` among them, because it reads the same file
the other three do and there is no reason for it to read it at a different
time.
"""

import json
import re

from wglib import (behavior, behaviorgate, datagate, dbstate, encodinggate,
                   gate, mysqlgone, pglog, pgnames, pgstep, retire)
STEP = "06"
TITLE = "the text is repaired"
WANT_HTTP = True
ROOTS = ("/app",)

# Step 04's flip list, carried forward under its own name for the reason step
# 05's plan gives: it is the same recording, and a second copy under a `-06`
# name would be a second thing to keep in step with the first. The overrides
# are this step's own, and they include step 04's -- see "The 58 ids are
# re-recorded" above.
FLIPS = "/tests/fixtures/behavior-flips-04.json"
OVERRIDES = "/tests/fixtures/behavior-overrides-06.json"
PARTITION = "/tests/fixtures/behavior-partition-06.json"

datagate.use_delivered_postgres()
behaviorgate.configure(strict=False, logmark=pglog.mark)


def _state():
    """The delivered PostgreSQL, read the way the recording was written.

    Unchanged from step 05, including `binary_columns` left at its default:
    a snapshot instrument configured differently from the one the answer was
    measured on is not the same instrument, and the answer this step overrides
    58 ids against was measured on this one.
    """
    conn, note = pgstep.connection()
    if conn is None:
        raise RuntimeError(note)
    expected = behavior.load(behavior.FIXTURE)
    want = sorted(set(expected["tables_before"]) | set(expected["tables_after"]))
    pgnames.widen_search_path(conn)
    return dbstate.PostgresState(conn, resolved=pgnames.resolve_tables(conn, want))


EXTRA_COLUMNS = {
    ("jf_options", "Value"): {"bytea"},
    ("jf__test_options", "Value"): {"bytea"},
}


def _widened(fn):
    def wrapper(ctx=None):
        return fn(ctx, extra_columns=EXTRA_COLUMNS)
    return wrapper


def _corpus(name, kinds, subset):
    """One behaviour family over the ids of those kinds on one side of the
    partition.

    No `precondition`: the deliverable this step is preconditioned on is the
    text in the database, and the family that reads the text is
    `migrated:encoding`. Making the behaviour families conditional on it too
    would fail the 380 guard ids for a reason they are not about, which is how
    a guard product turns a partial answer into a zero it did not earn.
    """
    def fn(ctx):
        return behaviorgate.checks(ctx, name, _state, kinds=kinds,
                                   subset=subset, precondition=None,
                                   flips_path=FLIPS, overrides_path=OVERRIDES,
                                   partition_path=PARTITION)
    return fn


# ------------------------------------------------------------- the repair ---

_REPAIRED = "/tests/fixtures/textvalues-repaired.json"
_BASELINE = "/tests/fixtures/textvalues-baseline.json"
_ENCODING = "migrated:encoding"
_ENCODING_INTACT = "migrated:encoding-intact"


def _fixtures():
    return encodinggate.load(_REPAIRED), encodinggate.load(_BASELINE)


# ------------------------------------------- the bookkeeping table's damage --
#
# `jf_logs` is in `dbstate.EXCLUDED_TABLES`, and both textvalues fixtures were
# recorded through that filter, so the five damaged serialized values it holds
# (`Anmeldung fÃ¼r MÃ¼ller`, ids 1-5) were outside every family above: the
# golden tree re-damaged scored 1.000000 (round-1 F6-F1). The table does not
# move -- `jf::Log()` has no caller in the pinned tree, and the recorder found
# the other 1,495 rows and every other cell byte-identical between the
# untouched baseline and the golden step-06 tree -- so the cells the repair
# changes are recorded by row id in `logvalues-06.json`, beside its recorder
# `record_logvalues.py` (round-2 review, 2026-10-05).
#
# One check per recorded cell, earned (must-flip: false on the tree this step
# is handed, which holds every `before`): the row is still there, its value
# carries no double-encoding signature (`wglib.mojibake`, the same detector the
# column survey uses), and it equals the recorded repair with PHP
# `serialize()` string-length prefixes normalised on both sides. The values
# are serialized structures whose `s:26:` was computed over the damaged bytes;
# undoing the round trip in place keeps `s:26:` (the golden tree), recomputing
# the prefix for the repaired text gives `s:22:`, and the instruction accepts
# either, so the comparison does not read the number.

_LOGVALUES = "/tests/fixtures/logvalues-06.json"
_PHP_STRLEN = re.compile(r's:[0-9]+:"')


def _php_norm(value):
    return _PHP_STRLEN.sub('s:#:"', value) if isinstance(value, str) else value


def _log_doc():
    with open(_LOGVALUES, encoding="utf-8") as fh:
        return json.load(fh)


def _log_ids(doc):
    return ["encoding/log/%s/%d/%s" % (doc["table"], c["id"], c["column"])
            for c in doc["cells"]]


def _log_values(conn, earned):
    """Add one check per recorded `jf_logs` cell to the earned half."""
    from wglib import mojibake
    doc = _log_doc()
    ids = _log_ids(doc)
    try:
        where = pgnames.resolve_tables(conn, [doc["table"]])[doc["table"]]
        if not where.found:
            raise LookupError("no relation answers for %s" % doc["table"])
        cur = conn.cursor()
        cur.execute("SELECT * FROM %s" % where.qualified)
        names = [d[0].lower() for d in cur.description]
        rows = {}
        for row in cur.fetchall():
            rec = dict(zip(names, row))
            rows[int(rec[doc["key"].lower()])] = rec
        cur.close()
    except Exception as e:                            # noqa: BLE001
        conn.rollback()
        for check_id in ids:
            earned.add(check_id, False, "could not read %s: %s"
                       % (doc["table"], str(e).strip()[:200]))
        return
    for check_id, cell in zip(ids, doc["cells"]):
        rec = rows.get(int(cell["id"]))
        if rec is None:
            earned.add(check_id, False, "row %s is gone" % cell["id"])
            continue
        got = rec.get(cell["column"].lower())
        if isinstance(got, memoryview):
            got = bytes(got)
        if not isinstance(got, str):
            earned.add(check_id, False, "not text: %r" % (got,)[:120])
        elif mojibake.is_mojibake(got):
            earned.add(check_id, False, "still carries a double-encoding"
                       " signature: %r" % got[:120])
        elif _php_norm(got) != _php_norm(cell["after"]):
            earned.add(check_id, False, "not the repaired text: %r" % got[:120])
        else:
            earned.add(check_id, True, "repaired")


_READING = {}


def _encoding_read(ctx=None):
    """The reading both encoding families are scored from. Taken once.

    `score.py` runs one family function per entry in `FAMILIES`, so the two
    halves of this reading are two entries and the reading itself is cached
    here. It is taken before anything else in the plan: the corpus pass
    re-seeds four of the twelve damaged columns out of existence, and this is
    the family order's whole reason (see `FAMILIES`).
    """
    if _READING:
        return _READING["parts"]
    conn, note = pgstep.connection()
    fixture, baseline = _fixtures()
    if conn is None:
        parts = encodinggate.crashed_pair(note, _ENCODING, _ENCODING_INTACT,
                                          fixture, baseline)
        for check_id in _log_ids(_log_doc()):
            parts[0].add(check_id, False, note)
    else:
        pgnames.widen_search_path(conn)
        whole = encodinggate.evaluate(conn, encodinggate.REPAIR,
                                      fixture, baseline, family=_ENCODING)
        columns = encodinggate.expected_columns(fixture)
        gone = encodinggate.gone_values(baseline, fixture)
        parts = encodinggate.partition(whole, columns, gone,
                                       _ENCODING, _ENCODING_INTACT)
        _log_values(conn, parts[0])
    _READING["parts"] = parts
    return parts


def _half(which):
    def fn(ctx=None):
        earned, intact = _encoding_read(ctx)
        return earned if which == _ENCODING else intact
    return fn


def _encoding(ctx=None):
    """Scored, 0.70, and first. What the repair was for.

    149 of the 239 checks over the 12 columns the two recordings disagree
    about; the other 90 are `migrated:encoding-intact`, the guard half of the
    same reading (`encodinggate.intact_ids`, and section 27.12 for why the
    split exists: 90 of the 239 were true before the step began, and an earned
    family must not pay for those). Per column, the earned half asks: it holds
    no non-ASCII value the golden tree does not, and `mojibake.survey` finds no
    double-encoding signature left in it. Per value the repair removed: the
    damaged spelling is gone, the repaired spelling is present in the right
    number of rows, and no near-miss variant of it was left behind. Plus one
    database-wide sweep: no double-encoding signature survives anywhere in the
    recorded columns.

    The guard half is the same reading's other question -- the database is
    still UTF-8, all 63 text columns still read, the stored octets are still
    UTF-8, the 30 recorded values in those columns that were never damaged are
    still there in the same row counts, no repair arrived Unicode-normalised,
    and the recording's own count of unnormalised values is unchanged. A tree
    that did nothing passes all 90; a tree that repaired the damage by
    flattening a column fails them, and a guard is a product, so it caps.

    `no-nfd` and the database-wide `nfc` count are checks on what this step
    is *not*. The corpus seeds the
    NFC and the NFD spelling of the same name as two separate accounts with
    two separate passwords (`authoring/seed/seedtool.php:194`), and
    `jf_users.Username` is uniquely indexed: normalising that column either
    raises on the index or merges two users on a tie-break this application
    does not have. Repairing an encoding and normalising Unicode are different
    operations and only the first is this step, so the database-wide sweep
    asks for the recording's own count of unnormalised values rather than
    zero. NOTES section 27.1.
    """
    return _half(_ENCODING)(ctx)


def _encoding_intact(ctx=None):
    """Guard, 90 checks, the other half of `_encoding`'s one reading.

    Everything the same reading says that a tree which did nothing already
    satisfies: see `_encoding` and `encodinggate.intact_ids`. It is here as a
    guard for the reason section 24.9 gives -- passing it is not work -- and
    not dropped for the reason `gate.combine` gives: the questions are exactly
    the ones a candidate who repaired the damage destructively gets wrong.
    """
    return _half(_ENCODING_INTACT)(ctx)


def _install_repaired(ctx=None):
    """Guard, one check per damaged value, and the reason the nop is 0.0.

    `install/_db/pdo_pgsql.data.sql` was generated from this database, so it
    carries the same damage; a repair that leaves it behind has left the
    installer able to recreate the deployment it just fixed. Every check is
    false on the tree the step is handed, which is what rule 8 needs from a
    step whose nop tree is a finished one.
    """
    fixture, baseline = _fixtures()
    return encodinggate.install_family(fixture=fixture, baseline=baseline)


# ------------------------------------------------- step 05's, carried on ----

DRIVERS = "/tests/fixtures/drivers-step05.json"
WIRING = "/tests/fixtures/wiring-step05.json"
INVENTORY = "/tests/fixtures/inventory-step05.json"


def _drivers(ctx=None):
    return mysqlgone.evaluate(ROOTS, fixture=DRIVERS,
                              family="migrated:drivers")


def _inventory(ctx=None):
    return retire.inventory_family(ROOTS, fixture=INVENTORY)


def _server(ctx=None):
    return retire.server_family()


def _postgres(ctx=None):
    return retire.postgres_family()


def _wiring(ctx=None):
    """Guard here. It was step 05's one earned family; this step inherits the
    tree that passes it and must not undo it."""
    return retire.wiring_earned(ROOTS, fixture=WIRING)


def _wiring_gone(ctx=None):
    return retire.wiring_gone(ROOTS, fixture=WIRING)


def _wiring_intact(ctx=None):
    return retire.wiring_intact(ROOTS, fixture=WIRING)


def _provisioning(ctx=None):
    return retire.provisioning_family(ROOTS[0])


_RUNTIME = {}


def _runtime(which):
    """What the application sent to PostgreSQL, out of the server's own log.

    Step 05's, unchanged, sharing the same single corpus pass. A repair
    changes what the statements carry, not what they are.
    """
    def fn(ctx):
        if not _RUNTIME:
            obs, error = behaviorgate.observation(ctx, _state)
            if error is not None:
                _RUNTIME.update(zip(
                    pgstep.RUNTIME_FAMILIES,
                    [gate.crashed(pgstep.RUNTIME_FAMILIES[0],
                                  "the corpus could not be replayed: %s"
                                  % error, pgstep.n_runtime_identity()),
                     gate.crashed(pgstep.RUNTIME_FAMILIES[1],
                                  "the corpus could not be replayed: %s"
                                  % error, pgstep.n_runtime_sql())]))
            else:
                _RUNTIME.update(zip(
                    pgstep.RUNTIME_FAMILIES,
                    pgstep.runtime_families(
                        windows=obs.get("log_windows") or ())))
        return _RUNTIME[which]
    return fn


# The order is load-bearing, and one invocation of `/tests/test.sh` per
# container is part of it. Sixteen of the 31 damaged values live in the four
# lesson tables whose pages `reset()` DROPs and re-CREATEs, so the corpus pass
# takes them out of the database on its way through; the encoding reading is
# taken first, off the database as delivered, and is cached for the two
# families that report it. Run a second time in the same container the reading
# would be taken after that re-seed and would fail a correct tree on those
# sixteen. Harbor runs a step's verifier once; `authoring/rescore.py` restores
# all five directories from the artifacts for the same reason. Section 27.12;
# REVIEW-06 W7.
FAMILIES = [
    # First, before the corpus pass re-seeds four of the twelve damaged
    # columns out of existence. See `_encoding`'s docstring and the note above.
    (_ENCODING, _encoding),
    (_ENCODING_INTACT, _encoding_intact),
    # Off the disk, before anything has connected to anything.
    ("migrated:inventory", _inventory),
    ("migrated:drivers", _drivers),
    ("migrated:server-retired", _server),
    ("migrated:postgres-intact", _postgres),
    ("migrated:wiring-gone", _wiring_gone),
    ("migrated:wiring-intact", _wiring_intact),
    ("migrated:provisioning", _provisioning),
    # The delivered database as the authority, before the corpus rewrites it.
    ("migrated:sequences", datagate.sequences_family),
    ("behavior:data-script", datagate.loader_family),
    ("migrated:install-data", datagate.install_data_family),
    ("migrated:schema", _widened(pgstep.schema_family)),
    ("behavior:schema-semantics", pgstep.semantics_family),
    # One corpus pass, five readings of it: the two sides of the partition on
    # the responses, the two sides on the snapshots, and the log.
    ("behavior:repaired-requests",
     _corpus("behavior:repaired-requests", ("requests",), "inside")),
    ("behavior:repaired-tables",
     _corpus("behavior:repaired-tables",
             ("tables_before", "tables_after"), "inside")),
    ("behavior:requests",
     _corpus("behavior:requests", ("requests",), "outside")),
    ("behavior:tables-before",
     _corpus("behavior:tables-before", ("tables_before",), "outside")),
    ("behavior:tables-after",
     _corpus("behavior:tables-after", ("tables_after",), "outside")),
    ("migrated:runtime-identity", _runtime("migrated:runtime-identity")),
    ("migrated:runtime-sql", _runtime("migrated:runtime-sql")),
    # The two that run the candidate's own PHP over the whole tree.
    ("migrated:wiring", _wiring),
    ("migrated:dialect", pgstep.dialect_family),
    # Last, the four that read the checked-in installer.
    ("migrated:install-repaired", _install_repaired),
    ("migrated:install-file", _widened(pgstep.install_family)),
    ("migrated:install-schema", _widened(pgstep.install_schema_family)),
    ("behavior:install-semantics", _widened(pgstep.install_semantics_family)),
]

# Rule 5's two kinds: behaviour 0.30, migrated-ness 0.70. The behaviour side
# keeps step 04's and step 05's proportion -- 0.27 on the responses, 0.03 on
# the snapshots -- because it is the same recording read the same way; what
# changed is which ids it reads, and that is the partition's job rather than a
# weight's.
#
# 0.27 against 0.03 is the same ratio those two steps used and it is kept for
# the same reason: a rendered page is the only place a repair is visible to
# anyone who is not holding a database client, and 48 responses is a wider and
# less forgeable statement than 10 digests. The digests are not decoration --
# they are what catches a tree that fixed the pages by editing a template --
# but they are 10 numbers, and a tree that gets all 48 responses right has
# repaired the data.
#
# The arithmetic, measured (sections 27.8 and 27.12). A tree that does nothing
# scores 0/149 on `migrated:encoding` (0/154 with the five `jf_logs` cells
# added in round 2), 0/48 and 0/10 on the two behaviour families and 0/31 on
# `migrated:install-repaired`, so the earned mean is 0
# before the guard product is applied and the reward is 0. The 90 checks of
# the same reading that such a tree does pass -- the database is still UTF-8,
# the columns still read, the values this step does not touch are still there
# -- are `migrated:encoding-intact`, a guard: they were true before the step
# began, so they cap rather than pay. Before that split they were inside this
# family, and the earned-only score of a do-nothing tree was 0.264.
#
# A tree that repairs the database and forgets the installer file scores the
# earned side nearly whole and is capped to 0 by the two families that ask
# about the file. The two mechanical passes -- a blanket single-codec
# conversion of every text column, and the same thing iterated to a fixed
# point over both server-side single-byte codecs -- score 66/149 and 126/149
# here, 4/48 and 34/48 on the responses, and 0/2 on the file: 0.339 and 0.801
# on the earned side, 0 as a reward. Since the round-1 per-value split of the
# file guard they score 15/31 and 27/31 on the file instead, and the rewards
# measured in round 2 are 0.169949 and 0.700872 (71/154 and 131/154 on
# `migrated:encoding`). Section 27.12 records why
# `migrated:install-repaired` stays a guard rather than moving here at 0.05:
# it is the one artifact where the four spellings no PostgreSQL codec can
# produce are unavoidable, and moving it inverts the ordering -- under that
# weighting the two-codec mechanical pass would score 0.759 and a tree that
# repaired the whole database but forgot one file would score 0.704.
WEIGHTS = {
    _ENCODING: 0.70,
    "behavior:repaired-requests": 0.27,
    "behavior:repaired-tables": 0.03,
}

# Everything else, including step 05's one earned family: this step inherits
# the tree that passes `migrated:wiring` and passing it again is not work.
GUARDS = ("migrated:install-repaired", _ENCODING_INTACT,
          "behavior:requests", "behavior:tables-before",
          "behavior:tables-after",
          "migrated:inventory", "migrated:server-retired",
          "migrated:postgres-intact", "migrated:drivers",
          "migrated:wiring", "migrated:wiring-gone", "migrated:wiring-intact",
          "migrated:provisioning",
          "migrated:sequences", "behavior:data-script", "migrated:install-data",
          "migrated:schema", "behavior:schema-semantics",
          "migrated:runtime-identity", "migrated:runtime-sql",
          "migrated:dialect",
          "migrated:install-file", "migrated:install-schema",
          "behavior:install-semantics")
