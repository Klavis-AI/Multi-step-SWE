"""Step 02 -- the schema exists in PostgreSQL, and answers what MySQL answers.

Twelve families. Three are this step's and pay; nine cap.

Earned -- false on the tree the candidate is handed, true on the tree the step
asks for:

    behavior:schema-semantics   the 544 probes MySQL was asked and answered at
                                authoring time -- case folding, uniqueness,
                                defaults, identity, over-length refusal,
                                integer range at both ends, orphan refusal --
                                replayed against the live schema and scored at
                                the grain the instruction states them: 193
                                checks, one per column, table, unique index and
                                foreign key, each passing only if every probe
                                about it agrees. `semantics.unit_id` has the
                                measurement that forced that grain.
    behavior:install-semantics  the same 193 units, against a database built
                                from `install/_db/pdo_pgsql.schema.sql` and
                                nothing else, on a `C`-locale database the gate
                                creates.
    migrated:install-file       that the file is there, applies without a
                                single psql error, and still substitutes
                                `PREFIX_` -- 57 checks, 27 of which come from
                                installing it a second time under a different
                                prefix.

Guards -- reproduced by work that is not this step's, so they cap rather than
pay:

    migrated:schema             the live cluster's 27 tables read column by
                                column against the recording. 577 checks.
    migrated:install-schema     the same 577 questions asked of the database
                                the install file builds.
    behavior:requests           404 recorded responses.
    behavior:tables             46 recorded table snapshots.
    behavior:strict-requests    the 9 responses strict mode moved.
    behavior:strict-tables      the 2 table snapshots it moved.
    migrated:strict-mode        step 01's deliverable.
    migrated:repairs            step 01's six measured readings.
    migrated:no-shortcut        step 01's ten guard readings.

The last seven of those are the whole of step 01, re-run unweighted. This is trap 9 in
`INSTRUCTIONS.md` read forwards rather than backwards: the usual failure is a
later step making an earlier step's test unpassable and the step-local mean
hiding it. Here the reverse is also live -- a candidate building a PostgreSQL
schema has every reason to touch the MySQL deployment while they are in there,
and nothing in step 02's own families would notice if strict mode came back off
or a repair was reverted. They multiply, so leaving step 01 alone earns nothing
and breaking it costs everything.

The two structural families are guards for a reason that was measured rather
than assumed, and the measurement changed this file twice.
`authoring/calibrate_pgloader.py` runs the platform's own automated migrator --
`pgloader ... WITH schema only`, one command, in the image the candidate has --
and scores what it builds with these gates. It gets **577 of 577** on
`migrated:schema`: every table, every column, every type, every length, every
nullability, every default, every index, every foreign key. Then it runs the
two commands anyone would reach for next, `pg_dump --schema-only` of what
pgloader built with `jf_` sed to `PREFIX_`, saves that as the install file, and
scores it again: **577 of 577** on `migrated:install-schema` and **57 of 57** on
`migrated:install-file`. So the second structural family is the first one read
through a file, and a `pg_dump` supplies the file.

A family an automated tool reproduces completely is the definition of a cap,
not a payment -- the same reading that put step 01's `behavior:requests` in the
guards -- so `migrated:install-schema` joined `migrated:schema` here.

Neither structural family can be tightened into a discriminator, and that is
deliberate rather than an oversight. `schemagate.TYPE_MAP` accepts `bigint` for
a MySQL `int` because `bigint` plus a `CHECK` is a correct answer, and
`schemagate` refuses to read collation at all because §20.3 accepts `citext`
and a nondeterministic ICU collation equally. Structure cannot enumerate the
correct forms, so it is a floor and behaviour is the judge. That is why what
pgloader gets wrong -- all 87 `int` columns widened to `bigint`, the one
`tinyint` turned into `boolean`, every case-insensitive column made
case-sensitive -- is invisible to both structural families and costs it 96 of
153 columns in the two behaviour families. §20.6 has the full arithmetic.

Why the two semantics families are *behaviour* and not structure. They are not
this scorer's opinion about PostgreSQL: `authoring/record_semantics.py` put
each of the 544 questions to the pinned MySQL 5.7.44 and kept what it said, and
the family asks PostgreSQL the same question and compares. That is rule 5's
first kind -- recorded inputs replayed -- with the database as the interface
instead of HTTP. The recorder ships beside the recording.

Why they nonetheless score 0 on the untouched tree, when rule 5 says the
behaviour family "passes the first family completely". The same reading
`behaviorgate.py` already argues for step 01: the question is not "does the
recording reproduce" but "does it reproduce on the platform this step was
supposed to deliver", and on a machine with no cluster it has no answer --
which `gate.py` counts as failed, never skipped. The family that literally
reproduces itself on an untouched tree is `behavior:requests`, and it is here,
in the guards, worth nothing.

Order matters and is not alphabetical, for two independent reasons.

The four corpus families run first so `behaviorgate` takes its one pass before
anything else writes to MySQL, exactly as in step 01.

`migrated:schema` and `behavior:schema-semantics` run before the three install
families because `installgate` builds two scratch databases of its own, and
`deploy.pick_database` falls back to name order over whatever non-system
databases it finds. A candidate whose database sorts after `wg_` would
otherwise be scored on the grader's own copy of their install file -- a green
for a tree that was never deployed. `pgstep.live()` resolves and memoises the
candidate's database on the first of those two families; `installgate` drops
its scratch databases afterwards as the other half of the same defence.
"""

from wglib import behaviorgate, dbstate, pgstep, strictgate

STEP = "02"
TITLE = "The schema lives in PostgreSQL 17"
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
    ("migrated:schema", pgstep.schema_family),
    ("behavior:schema-semantics", pgstep.semantics_family),
    ("migrated:install-file", pgstep.install_family),
    ("migrated:install-schema", pgstep.install_schema_family),
    ("behavior:install-semantics", pgstep.install_semantics_family),
]

# Rule 5 wants both scored kinds present and neither below ~30%. Behaviour
# equivalence earns 0.70 across two readings of the same 193 units, and the one
# migrated-ness family that is not a structural duplicate earns 0.30.
#
# `migrated:install-file` is the only migrated-ness reading left that measures
# the *tree* rather than a database: a file at the path `GetSQL()` derives from
# the adapter, applying in a single pass with no psql error, producing all 27
# tables -- the shipped MySQL file produces 26 and says nothing (§20.1) -- and
# still honouring `PREFIX_` under a prefix other than `jf_`. An untouched tree
# scores 0 on it and so does pgloader, which edits no files at all.
#
# It is nonetheless held to 0.30 and not a point more, because it is the one
# earned family a `pg_dump` of an already-built database hands over for free.
# Everything above 0.30 goes to the two behaviour families, which are the only
# instruments in this step that can tell a converted schema from a transliterated
# one.
#
# The live database is read by `behavior:schema-semantics`, which pays, and by
# `migrated:schema`, which caps. The split between the two behaviour families is
# 0.40 live / 0.30 installed rather than even: the live database is what the
# next steps are built on, and the install file is scored structurally by a
# guard as well.
#
# Measured against live containers (`authoring/rehearse_schemagate.py`): a
# cluster with nothing in it scores 0.000, the golden 1.000, and each
# deliberate mutation lands on the exact set of check ids it was declared in
# advance to land on, and nowhere else.
WEIGHTS = {
    "behavior:schema-semantics": 0.40,
    "behavior:install-semantics": 0.30,
    "migrated:install-file": 0.30,
}

GUARDS = ("behavior:requests", "behavior:tables",
          "behavior:strict-requests", "behavior:strict-tables",
          "migrated:strict-mode", "migrated:repairs", "migrated:no-shortcut",
          "migrated:schema", "migrated:install-schema")
