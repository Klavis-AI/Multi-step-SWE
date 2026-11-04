"""Step 03 -- the rows are in PostgreSQL, and the installer can put them there.

Seventeen families. Four are this step's and pay; thirteen cap.

Earned -- false on the tree the candidate is handed, true on the tree the step
asks for:

    behavior:data           the 15 tables the recording shows holding rows hold
                            MySQL's rows, compared as information: every value
                            canonicalised by type, every table digested over the
                            sorted set of its rows, so a different row order, a
                            different integer width and a `CHAR` PostgreSQL pads
                            and MySQL does not are all the same information and
                            a changed character is not.
    behavior:data-script    `install/_db/pdo_pgsql.data.sql` -- the file
                            `BaseDatabase::GetSQL("data")` derives from the
                            adapter name, beside the schema file step 02
                            delivered -- fills a database the candidate's *own*
                            schema file has just built, and leaves it holding
                            the same rows once when it is applied to that
                            database a second time. 38 checks, 34 of which are
                            `behavior:data` asked again of a tree the
                            candidate's two files built between them.
    migrated:sequences      the 11 identity columns over those tables will hand
                            out a value past the rows that were just loaded.
    migrated:install-data   both files installed the way a deployment
                            configured with a different table prefix installs
                            them -- `PREFIX_` replaced with `zz_` in each, on a
                            database that had neither -- reproduce the same 17
                            tables and position the same 11 sequences. 29
                            checks.

Guards -- reproduced by work that is not this step's, so they cap rather than
pay:

    behavior:data-empty         the 9 tables MySQL has empty are empty in the
                                database the candidate delivered.
    migrated:schema             step 02's live catalogue reading. 577 checks.
    migrated:install-schema     the same 577 of the install file's database.
    migrated:install-file       step 02's install file. 57 checks.
    behavior:schema-semantics   step 02's 193 behavioural units, live.
    behavior:install-semantics  the same 193 of the installed schema.
    behavior:requests           404 recorded responses.
    behavior:tables             46 recorded table snapshots.
    behavior:strict-requests    the 9 responses strict mode moved.
    behavior:strict-tables      the 2 table snapshots it moved.
    migrated:strict-mode        step 01's deliverable.
    migrated:repairs            step 01's six measured readings.
    migrated:no-shortcut        step 01's ten guard readings.

The last twelve are the whole of steps 01 and 02 re-run unweighted, which is
this task's standing answer to trap 9 and its mirror image. A candidate loading
data has every reason to reach for the schema while they are in there -- to
drop a constraint that is in the way of a bulk load, to widen a column that a
value will not fit, to disable a trigger and forget it -- and none of the four
families above would notice. They multiply, so leaving the first two steps
alone earns nothing and breaking either costs everything.

Why the loaded rows are earned and the empty tables are not
----------------------------------------------------------

The comparable set is 24 tables. On the tree this step hands the candidate --
step 02's golden tree, schema built, no rows -- 9 of those 24 already agree
with MySQL, because the 9 `jf__test_*` tables are empty on both sides. An
empty table's digest matches an empty table's digest. A check that is true
before the step starts cannot be a payment (`gate.combine`), so the earned
family is the 15 tables that hold rows and the 9 are a separate guard. With all
24 in one earned family, arriving is worth 0.375 of it.

The same reading removes 6 of the 17 `AUTO_INCREMENT` columns from
`migrated:sequences`: 5 sit on empty `jf__test_*` tables and one on
`jf_session`, which `dbstate.EXCLUDED_TABLES` does not compare at all.
`jf_logs` and `jf_stats` are excluded from the *content* comparison too
(§24.18) and keep their sequence checks, which is why 11 remain and not 9. A
sequence over an empty table is already past its rows before anything happens.
The 11 that remain are false on the handed tree and false after `pgloader`,
which positions none of them.

Why there are two file families and not one, and why they are weighted the way
they are
-----------------------------------------------------------------------------

`authoring/calibrate_pgloader_data.py` runs the platform's automated migrator
at this step the way §21.1 runs it at step 02: one command, in the image the
candidate has, and with every option that could earn it more --
`pgloader ... WITH data only, truncate, reset sequences`. It is the arithmetic
that fixed these weights.

pgloader moves 16 of the 24 comparable tables and exits 0. It loads none of
`jf_xuser`'s 425 rows -- the table is there, the rows are not, and nothing it
prints says so -- and it positions none of the 11 sequences: `reset sequences`
looks for a sequence a column *defaults* from, and step 02's schema declares
identity columns, so the load prints `Reset sequences` and leaves all 17 of
them at `last_value IS NULL`, still handing out 1 against a `jf_users` whose
largest key is 444. So the single command scores 7 of `behavior:data`'s 15 and
nothing else: 0.117 of the step.

The route that has to be closed deliberately is the second one. A candidate can
run that command and then `pg_dump --data-only --inserts
--on-conflict-do-nothing` the result back out as the required install file. The
dump is a faithful copy of what pgloader built, so it reproduces the same 7 of
15 tables in `behavior:data-script`'s two rounds -- 15 of 34 -- and the total
is 0.227. `migrated:install-data` is what keeps it there: every table name in a
`pg_dump` is spelled out, `PREFIX_` appears nowhere in it, and against a
database holding `zz_options` every statement in it fails. 2 of 27, and both of
those are the `jf_logs` and `jf_stats` sequence checks, which §24.18 redirected
at the loaded database's own rows and which a file that loads neither table
therefore satisfies vacuously (NOTES §24.19, known-open). §21.3 has both readings measured against this gate; both are
under rule 4.3's ~30% ceiling, and the ceiling is held by the families the tool
scores nothing on rather than by discounting the one it scores something on.

`behavior:data` and `behavior:data-script` overlap on purpose and do not double
pay for one thing. The first measures the state the step is delivered in, which
is what the next step is built on. The second measures whether the framework's
own installer could rebuild it -- and, in its second round, whether that file
is a load rather than an append: applied to a database that is already full, an
`INSERT`-only file collides on every primary key and doubles every table that
has none, while one that clears what it is about to write does not. A candidate
who moves the data by hand and writes no file earns the first and not the
second, which is the right shape: the file is a smaller deliverable than the
data.

Neither file family is given a database this gate built from the recording.
Both get one the candidate's own `install/_db/pdo_pgsql.schema.sql` has just
created, which is what holds the two deliverables to each other: a data file
written for a column the schema file does not declare that way lands wrong or
does not land at all. That is how the instruction's "if you change one, change
it in both places" is scored -- by consequence, not by inspection. An earlier
draft truncated the delivered database and reloaded that instead, which left
the delivered state unreadable after the fact and put a candidate whose file
opens with `TRUNCATE ... CASCADE` one failed reload away from zeroing four
guards that had nothing to do with this step.

Both files are applied by `installgate.install`, the same function that applies
the schema file for step 02's three install families -- `PREFIX_` replaced with
the configured prefix, cut into statements by `sqlsplit`, sent one at a time
over a library connection as an unprivileged role, carrying on past a failure,
and the server's error messages counted. So both of this tree's install files
are handed to the database identically, and a difference between them is the
candidate's rather than the gate's. Seven applications in all across this
step's verifier, under one 2,100-second budget at 300 seconds each; a file that
exhausts it fails the checks that needed it rather than the whole step.

Rule 5's two kinds, and the split between them. Behaviour equivalence is
0.25 + 0.20 = 0.45: both families ask whether PostgreSQL holds the information
MySQL holds, one of the delivered tree and one of the tree the candidate's own
files build. Migrated-ness is 0.25 + 0.30 = 0.55, and both halves of it are
state the rows do not carry. `AUTO_INCREMENT` position does not travel with a
row, so a sequence that will hand out a free key is evidence of the move rather
than of equivalence. And an install path that still works at another prefix is
evidence of the same shape as step 02's `migrated:install-file`: `GetSQL()`
substitutes `PREFIX_` on every install, so a file with this deployment's prefix
baked into it is one the framework can run exactly once and never again, which
no amount of correct data produces or repairs.

Order matters and is not alphabetical. This step inverts steps 01 and 02: there
the four corpus families went first, here they go after the five data families.

**The data families run before the corpus replay, because the replay moves the
authority they read.** `collect.collect` snapshots MySQL, drives 413 recorded
requests, and snapshots it again; the recording's own `tables_before` and
`tables_after` differ on 13 of the 15 comparable tables that hold rows, and the last
scenario submits `1; DROP TABLE lesson_XSS2_messages`, which takes four lesson
tables down to a fresh `CREATE`. Live MySQL after that pass is not the database
the candidate migrated. Four of the five families would be measuring the corpus
rather than the migration: `behavior:data` would compare a PostgreSQL holding
432 rows of `jf_users` against a MySQL holding 449, and the other three
would require a sequence, a reloaded table and a freshly installed one to be
past a MAX the replay had raised after the candidate finished.

Run first, they read the state the step was delivered in -- which is exactly
`tables_before`, because that is the snapshot `collect` takes at the same point
and `behavior:tables` scores 46 of 46 on a correct tree. Nothing in `datagate`
writes to MySQL, so the corpus pass that follows sees what it would have seen
anyway; `datagate.release()` runs after every one of the five so that no idle
read transaction is left holding a metadata lock the replay's `DROP TABLE`
would then wait a year for.

The five also run before `migrated:schema`, so `pgstep.live()` resolves and
memoises the candidate's database before `installgate` creates two scratch
databases of its own -- the same defence step 02 documents, moved to the front
because `datagate.states()` is now the first thing to ask. `datagate` creates
two more, under names of its own, and drops each in a `finally`.

They also run before `behavior:schema-semantics`, which writes probe rows into
a clone table and, for the orphan probe, into a real one inside a transaction
it rolls back. That rollback is verified by the family itself, but a data gate
that read the tables after it would be reporting on the scorer's own writes as
well as the candidate's.

Within the five, the three that read the delivered database come first and the
two that run the candidate's SQL come last. Nothing in this step writes to the
database the candidate delivered, so the ordering inside each group is free;
it is written this way so that a crash in the expensive half cannot change what
the cheap half reported.

Why the step widens `schemagate.TYPE_MAP`, and by exactly two columns
---------------------------------------------------------------------

`jf_options.Value` is a MySQL `text` holding PHP `serialize()` output, and 96
of its bytes across the corpus are NUL. PostgreSQL will not store a NUL in a
`text` value at any encoding -- it is not an encoding question, it is that a C
string terminates -- so a faithful move of that column is a `bytea`. Step 02
had no reason to accept one and its `TYPE_MAP` does not.

Rather than loosen the shared map, which would silently relax step 02, this
plan passes the widening explicitly to the four families that read a schema.
The widening is per **column**, and that is the whole of its content: the
recording has nine `text` columns and only one of them holds a NUL, so a
per-type widening -- `{"text": {"bytea"}}`, which is what this was before --
would have let the other eight become `bytea` for free. The instruction says a
binary column that did not need to be binary costs the candidate; a per-type
widening makes that sentence false, and only one of the eight (a
`latin1_swedish_ci` column whose collation `behavior:schema-semantics`
measures) would have been caught by anything else.

The pair is named rather than derived because `jf__test_options` is the empty
twin of `jf_options` -- same column, same declared type, no rows to derive a
NUL from -- and a candidate who converts the one and not the other has done
something defensible either way. `pgstep._install` asserts that the three
install families agree about the widening, so the live schema and the installed
schema are held to the same rule by construction and not only by measurement:
a candidate who makes the column `bytea` in the database and leaves it `text`
in `install/_db/pdo_pgsql.schema.sql` fails `migrated:install-schema`, and the
reverse fails `migrated:schema`. `behavior:data-script` closes it from the
third side, by filling a database that file built.

`behavior:schema-semantics` is not passed anything, because it does not read
types: it asks the column the 544 questions MySQL answered at authoring time
and compares the answers. §21.2 has the measurement showing the two recorded
answers that touch `jf_options.Value` are unchanged by the column's type.
"""

from wglib import behaviorgate, datagate, dbstate, pgstep, strictgate

STEP = "03"
TITLE = "The data lives in PostgreSQL 17"
WANT_HTTP = True
ROOTS = ("/app",)

DATABASE = "webgoatphp"

# The one widening this step makes to the shared type map, and the reason is in
# the docstring above. Two columns, not a type: `jf_options.Value` is the one
# that holds NULs and `jf__test_options.Value` is its empty twin. It is a
# module constant rather than four literals so that the families that must
# agree cannot drift apart in an edit.
EXTRA_COLUMNS = {
    ("jf_options", "Value"): {"bytea"},
    ("jf__test_options", "Value"): {"bytea"},
}


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


def _widened(fn):
    """A schema-reading family, asked with this step's column widening."""
    def wrapper(ctx=None):
        return fn(ctx, extra_columns=EXTRA_COLUMNS)
    return wrapper


FAMILIES = [
    # The five that read live MySQL as the authority, before the corpus replay
    # rewrites it. See "Order matters" above: this is the one place where this
    # step's plan is not steps 01 and 02's with families appended.
    ("behavior:data", datagate.tables_family),
    ("migrated:sequences", datagate.sequences_family),
    ("behavior:data-empty", datagate.empty_tables_family),
    ("behavior:data-script", datagate.loader_family),
    ("migrated:install-data", datagate.install_data_family),
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
    ("migrated:schema", _widened(pgstep.schema_family)),
    ("behavior:schema-semantics", pgstep.semantics_family),
    ("migrated:install-file", _widened(pgstep.install_family)),
    ("migrated:install-schema", _widened(pgstep.install_schema_family)),
    ("behavior:install-semantics", _widened(pgstep.install_semantics_family)),
]

WEIGHTS = {
    "behavior:data": 0.25,
    "behavior:data-script": 0.20,
    "migrated:sequences": 0.25,
    "migrated:install-data": 0.30,
}

GUARDS = ("behavior:requests", "behavior:tables",
          "behavior:strict-requests", "behavior:strict-tables",
          "migrated:strict-mode", "migrated:repairs", "migrated:no-shortcut",
          "behavior:data-empty",
          "migrated:schema", "behavior:schema-semantics",
          "migrated:install-file", "migrated:install-schema",
          "behavior:install-semantics")
