"""Step 04 -- the application runs on PostgreSQL 17, and its SQL is PostgreSQL SQL.

Sixteen families. Five are this step's and pay; eleven cap.

Earned -- false, or mostly false, on the tree the candidate is handed and true
on the tree the step asks for:

    behavior:requests   all 413 recorded responses, replayed against whatever
                        the tree now talks to. The verifier image from this
                        step onward contains no MySQL server, no MySQL client
                        and no MySQL PHP driver (§16), so this family cannot
                        be answered by the tree the candidate was handed: on
                        an untouched tree every request is a database error
                        page and every one of the 413 fails. It is not
                        partitioned -- step 01's nine strict-mode responses are
                        as hard to keep as the other 404 once the server has
                        changed.
    behavior:tables-after
                        the 14 table snapshots taken *after* the corpus has
                        run whose contents the corpus actually changes: what
                        the application wrote while it was being driven, which
                        on the handed tree is nothing at all. The other twelve
                        of the 26 are a guard -- see below.
    migrated:dialect    the 38 statements in the pinned tree that PostgreSQL
                        17 will not run, one check each, plus the five that
                        say the reading was possible at all. 43 checks.
    migrated:runtime-identity
                        who the *running* application was, read out of the
                        server's own log: that it authenticated at all, as a
                        role of its own that exists in the catalogue and is
                        not a superuser, against the application's database
                        and not `postgres`. 4 checks.
    migrated:runtime-sql
                        what that application sent, out of the same log: that
                        statements were observed and that the logging the
                        reading depends on was not switched off underneath it,
                        that every scenario reached the server, that no
                        statement reads a generated key back as
                        `max(<identity column>)`, and that the statements this
                        server refused are the injection lessons' own and
                        nothing else. 5 checks. Split off from identity
                        because the two fail for different reasons and are
                        worth different amounts: connecting as the wrong role
                        is a configuration slip, and swallowing a statement
                        the server refused is a shim.

Guards -- reproduced by work that is not this step's, so they cap rather than
pay:

    behavior:mysql-intact       the MySQL artifact the step says stays: the
                                data directory, the system tablespace and the
                                redo logs, the system schema's files, the
                                `webgoatphp` database and each of its 27
                                tables, `/etc/mysql`, and step 01's strict
                                mode still in the configuration. 35 checks,
                                read off the disk -- there is no `mysqld` in
                                this image to ask.
    behavior:tables-before      the 24 table snapshots taken before the corpus
                                writes anything: step 03's migrated dataset,
                                untouched. Doing double duty here: see "Where
                                the data's authority went".
    behavior:tables-untouched   the eleven of those 24 whose recorded "after"
                                equals their recorded "before" -- tables this
                                corpus reads and never writes. Reproducing
                                them is step 03's work, not this step's, and
                                they were true on the handed tree, so they cap
                                rather than pay. The line between these twelve
                                and the fourteen above is drawn by the
                                recording, not by hand: see `behavior.split_after`.
    migrated:sequences          step 03's 11 identity columns, still past
                                their rows.
    behavior:data-script        step 03's data file, still loading. 38 checks.
    migrated:install-data       both install files at another prefix. 29.
    migrated:schema             step 02's live catalogue reading. 577 checks.
    migrated:install-schema     the same 577 of the install file's database.
    migrated:install-file       step 02's install file. 57 checks.
    behavior:schema-semantics   step 02's 193 behavioural units, live.
    behavior:install-semantics  the same 193 of the installed schema.

Rule 5's two kinds, and where the weight went
---------------------------------------------

Behaviour equivalence is 0.60 and migrated-ness is 0.40, and each is split
across families rather than carried by one:

    behaviour        behavior:requests          0.55   what it answers
                     behavior:tables-after      0.05   what it writes
    migrated-ness    migrated:dialect           0.30   the SQL in the tree
                     migrated:runtime-sql       0.07   the SQL it sends
                     migrated:runtime-identity  0.03   who it sends it as

Every one of those numbers is argued against a measurement rather than against
the rule-4.3 ceiling -- see the comment on `WEIGHTS`, and in particular what
moving the twelve never-written tables out of the earned half did to the
mechanical migrator's score. The second family on each side is there because
the first has a blind spot the other instruments cannot see into.
`behavior:requests` compares response bodies, and a body is the same whether
the row behind it came from the database or from a cache the candidate wired
in front of it; `behavior:tables-after` reads the database. `migrated:dialect`
folds string expressions out of the tree, and a fold is a reconstruction -- a
shim that rewrites statements on their way to the driver looks ported to it;
the two runtime families read what the server was actually sent.

The two kinds are independent in both directions, which is why neither
subsumes the other. A tree can serve all 413 responses with a `REPLACE INTO`
still in `SettingManager::_Save` -- that method is not on any path the corpus
walks -- and a tree can have every statement parse and serve nothing at all,
because parsing is not connecting.

Why the step is where it is: no single SQL text serves both servers
------------------------------------------------------------------

This step and the adapter that carries it are one step and not two. The
framework's database layer is chosen by name -- `config.php` names an adapter,
`_japp/model/lib/db/adapter/<name>.php` is loaded, and `BaseDatabase::GetSQL`
derives the install file names from the same string. Writing a `pdo_pgsql`
adapter without rewriting the queries delivers an application that connects
and then fails on its first statement; rewriting the queries without the
adapter delivers a tree whose SQL no longer runs on the server it is still
pointed at. There is no ordering of the two that leaves a working tree in
between, because the 38 statements are not stylistic: `REPLACE INTO`,
`GROUP_CONCAT(... SEPARATOR ...)`, `LIMIT 0,1`, `ALTER TABLE ... AUTO_INCREMENT`
and backtick quoting are each rejected outright by one of the two servers.
§16 records the measurement.

That also fixes where the MySQL server stops being available. The snapshot
families and every family in `datagate` read a database as the authority, and
until now that database was MySQL. From this step the verifier has no MySQL at
all -- not stopped, absent, asserted absent by the image build -- because a
step whose deliverable is "the application no longer needs MySQL" cannot be
graded in a container where MySQL is one `service start` away.

That is also why `behavior:mysql-intact` reads the data directory instead of
querying it. The step says in as many words that MySQL itself does not move,
and until this family existed nothing scored that sentence: the cheapest way
through the step for a candidate short of disk was to delete the old server.
There is no `mysqld` here to ask, so the artifact is read as bytes -- weaker
than a query, and not nothing, since an `.ibd` that is gone is gone.

Where the data's authority went
-------------------------------

Steps 01-03 compared PostgreSQL against a live MySQL. With MySQL gone, the
recording is the only authority left, and the recording is not a drop-in
replacement for the one it replaces: `runner.db_snapshot` masks
`corpus.random_cells()` and thresholds `corpus.masked_columns()` before
digesting, and `datagate` compared raw rows. They are different digests of the
same table and neither can be derived from the other.

So the authority moves rather than being reconstructed. `datagate` is told,
once, at import time, that the source database is now the one the candidate
delivered (`use_delivered_postgres`), and `behavior:tables-before` is what pins
that database to the recording in the same run: 26 masked snapshots, taken
before the corpus has written anything. The chain is recording ->
behavior:tables-before -> the delivered database -> the three `datagate`
families that still ask a real question of it.

The two halves of that snapshot pair are one pass and two families because
they are two different questions. `tables_before` is step 03's dataset and is
already right on the tree this step hands over -- a guard by the definition
`gate.combine` uses. `tables_after` is what *this* step's application wrote,
and on the handed tree the application writes nothing, so it is earned. Scored
as one family the second question would have been diluted into the first, and
the first would have been paid for arriving.

Two `datagate` families do not survive the move and are not in this plan.
`behavior:data` and `behavior:data-empty` compared MySQL's rows to
PostgreSQL's; with the delivered database on both sides they are tautologies,
and a tautology in a guard is a check that cannot fail, which is worse than
absent -- it reports 24/24 forever and reads like evidence. What they measured
is not lost: `behavior:tables-before` scores the same 24 tables against the
recording, which is a stronger statement than "PostgreSQL agrees with itself".

The three that survive do so because none of them compares the delivered
database to itself. `migrated:sequences` asks whether each identity column will
hand out a value past the rows in its own table; `behavior:data-script` builds
a second database out of the candidate's own two install files and compares
*that* to the delivered one; `migrated:install-data` does it again at a
different table prefix. `datagate._source_high` was rewritten for this: it read
the high-water mark with `SELECT COALESCE(MAX(...))` over a MySQL connection
and now scans the state's own rows, so it means the same thing whichever
database the authority is.

Trap 9, and what happened to strict mode
----------------------------------------

Step 01's three families -- `migrated:strict-mode`, `migrated:repairs`,
`migrated:no-shortcut` -- are absent for the same reason, and this is the trap-9
reading for this step: an inherited test whose own requirements made it
unpassable. Every one of them needs a live MySQL. `strictgate` reads
`@@GLOBAL.sql_mode`, parses `my.cnf`, opens a root socket connection and
re-reads six probes from the server; "strict mode" is a MySQL setting and there
is no PostgreSQL reading of it. Carried forward unchanged they would fail on a
correct tree, and carried forward "adapted" they would be three new families
wearing step 01's names.

Their substance is all still scored, in families that do not need the server:

  * the data strict mode repaired is the 24 snapshots in
    `behavior:tables-before` and the 13 in `behavior:tables-after`, and is
    loaded again from the candidate's own files by `behavior:data-script` and
    `migrated:install-data`;
  * the shape it forced is `migrated:schema` and `behavior:schema-semantics`,
    577 + 193 checks, live and installed;
  * the nine responses and two table snapshots strict mode moved are inside
    `behavior:requests` and the two snapshot families, which score every
    recorded id rather than the partitioned subset steps 01-03 split out.
    Nothing that was scored before has stopped being scored;
  * the setting itself is one check in `behavior:mysql-intact`, read out of
    the configuration text with `strictgate`'s own reading of it, so the two
    steps cannot come to disagree about what counts as strict --
    `STRICT_ALL_TABLES` and `TRADITIONAL` pass here for the same reason they
    passed there.

Order matters and is not alphabetical
-------------------------------------

`behavior:mysql-intact` is first because it reads no database at all, only the
two MySQL directories, so nothing it does can change what any other family
sees, and the report opens with the state of the tree as it was handed over.

The three `datagate` families run next, before the corpus replay, for the
reason step 03 gives -- the replay moves the authority they read. That reason
did not weaken when the authority changed, it got stronger: the authority is
now the database the replay writes to. `collect.collect` snapshots it, drives
413 requests and snapshots it again, and the last scenario submits
`1; DROP TABLE lesson_XSS2_messages`. A sequence family that ran afterwards
would be asked about tables the corpus had just recreated.

`pgstep.connection()` is called by the first of them, which is also what
resolves *which* database is the candidate's before `installgate` creates two
scratch databases of its own -- step 02's defence, unchanged.

The two families that read the delivered schema -- `migrated:schema` and
`behavior:schema-semantics` -- run before the replay for the same reason, and
step 04 is the step where that stopped being a formality. Until now the corpus
was driven against MySQL and could not touch a PostgreSQL table; from here it
can, and four of them it drops and rebuilds itself, because four lessons
recreate their own table at reset. What it rebuilds is not what the schema file
built: MySQL gave a runtime `CREATE TABLE` the *database* default charset, and
`webgoatphp` is utf8/utf8_bin, while the installer's copy of the same four
tables carries an explicit `DEFAULT CHARSET=latin1`. So on MySQL those tables
compared case-sensitively after a reset and case-insensitively before one, and
`schema-baseline.json` recorded the second -- it was taken on the delivered
database, which is the thing these two families are about. Asked after the
replay they would be comparing that recording against tables the application
had built, and 14 of the 193 semantic units would report a case difference that
is MySQL's own behaviour rather than a migration defect. The difference between
the two kinds of table is not lost by moving them: it is `behavior:requests`
that scores it, on `lesson-reset#15`, which posts `LARRY` where the row says
`Larry` and must still find nobody.

Then the corpus: one pass, four families reading it. `behavior:requests` is
listed first of the four because it is the one that drives it, so the report's
timing lands where the time went. `migrated:runtime` reads the same pass --
the per-scenario byte windows the recorder marked in the server log -- and
costs no extra traffic.

`migrated:dialect` runs after all of those and before the three install
families. It is the only family here that executes the candidate's own code:
`sqlgate` runs `sqlextract.php` over the tree, as `nobody` and under a bound,
but a PHP file that decides to `DROP DATABASE` when it is parsed is still the
candidate's file being run. Everything that reads the live database has read it
by then. It is not last, because last is where the install families are, and
they are the only thing left that a tree could still damage.
"""

from wglib import (behavior, behaviorgate, datagate, dbstate, gate, mysqlkeep,
                   pglog, pgnames, pgstep)

STEP = "04"
TITLE = "The application runs on PostgreSQL 17"
WANT_HTTP = True
ROOTS = ("/app",)

# This step's own recordings, beside the shared ones in the same directory.
# `authoring/sync_tests.py` merges `authoring/fixtures/` and
# `authoring/fixtures-steps/04-query-rewrites/` into one `tests/fixtures/`, and
# refuses a name offered by both, so the suffix is what keeps this step's flip
# list from being mistaken for the shared one.
FLIPS = "/tests/fixtures/behavior-flips-04.json"
OVERRIDES = "/tests/fixtures/behavior-overrides-04.json"

# Read once, at import, before any family has read a database. `datagate`
# asserts that nothing has: the authority is a property of the plan, not
# something a family may change while others are running. The reasoning is in
# "Where the data's authority went" above.
datagate.use_delivered_postgres()

# How the single corpus pass is taken -- also the plan's decision and not
# whichever family reaches it first, since the pass is cached and shared.
#
# `strict=False`: a deployment that stops answering partway is graded on what
# it did answer, request by request, instead of the recorder raising and
# taking all four behaviour families down with it.
#
# `logmark`: the recorder notes where the PostgreSQL log stood at each
# scenario boundary, which is what lets `migrated:runtime` attribute a logged
# statement to the scenario that caused it without driving the corpus twice.
behaviorgate.configure(strict=False, logmark=pglog.mark)


def _state():
    """The delivered PostgreSQL, read the way the recording was written.

    The recording's 24 comparable tables are resolved by name against whatever the
    candidate's `search_path` reaches, for the reason `datagate.states()`
    gives: step 02 left a migration free to fold the four mixed-case table
    names or to quote them, and free to put the schema where it likes. A gate
    that looked for `lesson_XSS2_messages` in `public` would fail a tree the
    schema gate passed.

    `binary_columns` is left at its default rather than passed, because that
    is the reading the two golden corpus passes were scored with (§22) and a
    snapshot instrument that is configured differently from the one the answer
    was measured on is not the same instrument.
    """
    conn, note = pgstep.connection()
    if conn is None:
        raise RuntimeError(note)
    expected = behavior.load(behavior.FIXTURE)
    want = sorted(set(expected["tables_before"]) | set(expected["tables_after"]))
    pgnames.widen_search_path(conn)
    return dbstate.PostgresState(conn, resolved=pgnames.resolve_tables(conn, want))


# Step 03's widening of `schemagate.TYPE_MAP`, carried forward unchanged. It
# is not this step's decision and it is not optional: `jf_options.Value` holds
# 96 NUL bytes and step 03 delivered it as `bytea`, so a step 04 that asked the
# schema families the unwidened question would fail four guards on the tree it
# handed the candidate itself. Copied rather than imported because a step's
# `tests/` directory is its whole build context (`sync_tests.py`) and cannot
# reach another step's.
EXTRA_COLUMNS = {
    ("jf_options", "Value"): {"bytea"},
    ("jf__test_options", "Value"): {"bytea"},
}


def _widened(fn):
    """A schema-reading family, asked with this step's column widening."""
    def wrapper(ctx=None):
        return fn(ctx, extra_columns=EXTRA_COLUMNS)
    return wrapper


def _corpus(name, kinds):
    """One behaviour family over every recorded id of those kinds.

    No `subset` and no `precondition`. The partition steps 01-03 carry exists
    because strict mode moved nine responses that the rest of the corpus
    reproduced for free; nothing about this step reproduces anything for free,
    since the server the recording was taken from is not in the image. And the
    precondition those steps pass -- `strictgate.precondition` -- is a MySQL
    reading, which is the same thing this step's plan retires three families
    over.
    """
    def fn(ctx):
        return behaviorgate.checks(ctx, name, _state, kinds=kinds,
                                   subset="all", precondition=None,
                                   flips_path=FLIPS, overrides_path=OVERRIDES)
    return fn


def _mysql_intact(ctx=None):
    """The MySQL artifact, read off the disk. No server, no connection."""
    return mysqlkeep.evaluate(mysqlkeep.load())


_RUNTIME = {}


def _runtime(which):
    """What the application sent to PostgreSQL, out of the server's own log.

    Shares the single corpus pass every behaviour family takes and reads the
    byte windows the recorder marked in it, once for both families. A pass
    that could not be taken at all fails both whole denominators exactly as it
    does there: the questions are fixed before the tree is looked at, and a
    deployment that cannot be driven has answered none of them.
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


FAMILIES = [
    # Two directories and nothing else, so it cannot disturb what follows.
    ("behavior:mysql-intact", _mysql_intact),
    # The five that read the delivered database as the authority, before the
    # corpus replay rewrites it. See "Order matters" above.
    ("migrated:sequences", datagate.sequences_family),
    ("behavior:data-script", datagate.loader_family),
    ("migrated:install-data", datagate.install_data_family),
    ("migrated:schema", _widened(pgstep.schema_family)),
    ("behavior:schema-semantics", pgstep.semantics_family),
    # One corpus pass, four readings of it.
    ("behavior:requests", _corpus("behavior:requests", ("requests",))),
    ("behavior:tables-before",
     _corpus("behavior:tables-before", ("tables_before",))),
    # The after-snapshot, split along the line the recording itself draws.
    # Eleven of the twenty-four tables are ones this corpus never writes -- in
    # the recording their "after" equals their "before" -- so reproducing them
    # is something a tree that does nothing already does, and scoring them in
    # an earned family was trap 9 in the half of `behavior:tables` that
    # survived the round-1 split. They are a guard, where they were always
    # true; the fourteen the corpus writes are the earned family, and they are
    # 0/14 on the tree the candidate is handed.
    ("behavior:tables-untouched",
     _corpus("behavior:tables-untouched", ("tables_after_unwritten",))),
    ("behavior:tables-after",
     _corpus("behavior:tables-after", ("tables_after_written",))),
    ("migrated:runtime-identity", _runtime("migrated:runtime-identity")),
    ("migrated:runtime-sql", _runtime("migrated:runtime-sql")),
    # Runs the candidate's own PHP. Everything that reads the live database
    # has read it by now, and the install families are what is left to protect.
    ("migrated:dialect", pgstep.dialect_family),
    ("migrated:install-file", _widened(pgstep.install_family)),
    ("migrated:install-schema", _widened(pgstep.install_schema_family)),
    ("behavior:install-semantics", _widened(pgstep.install_semantics_family)),
]

# Rule 5's two kinds, 0.60 behaviour and 0.40 migrated-ness, and every number
# here was argued against a measurement of what a mechanical pass collects.
#
# `migrated:dialect` is the cheapest family in the step to earn: 34 of its 38
# scored statements fall to a regex pass over the tree -- back-quote to double
# quote, `auto_increment` to an identity clause, `GROUP_CONCAT ... SEPARATOR`
# to `string_agg`, the `LIMIT` off a DML statement -- because "PostgreSQL 17
# would run this" is a question about grammar and names, and a textual rule
# can satisfy it without the statement meaning what it used to. That is a
# reason to watch it, and for a while it was also the dial this plan turned:
# at 0.50 the mechanical pass read 0.2967 and at 0.35 it read 0.2128, so 0.25
# was chosen -- and the rule-4.3 margin was, in the second static review's
# words, manufactured by the weight rather than earned by a check.
#
# What fixed that was not a weight. It was finding the free subset: eleven of
# the twenty-four tables `behavior:tables-after` scored are ones the corpus
# never writes, so arriving collected 11/24 of an earned family. Moving them
# to `behavior:tables-untouched`, where they were always true, leaves the
# earned half 0/13 on the handed tree -- and with that subset gone the
# mechanical pass reads 0.2455 at dialect 0.30, *below* what it read at 0.25
# with the free twelve. Re-derived at 0.35 it reads 0.2861, still under the
# ceiling: the ceiling is no longer what picks the number.
#
# So dialect is back at 0.30, which is its share of migrated-ness rather than
# a number chosen to clear a bar, and the 0.10 that is left goes to the two
# families a textual pass cannot touch at all -- `migrated:runtime-sql` needs
# the application to actually send SQL this server accepts, and
# `migrated:runtime-identity` needs it to actually connect as a role of its
# own. Splitting the old nine-check runtime family in two is what makes the
# first of those weigh anything: a shim that swallows a failed statement now
# loses 1/5 of 0.07 where it used to lose 1/9 of 0.05.
#
# No check was removed to reach these numbers, no threshold relaxed and no
# fixture shrunk. One check was dropped for a different reason entirely --
# `runtime/no-bypassrls`, because the instruction never mentions row-level
# security and rule 1 does not allow a check to be the only place a
# requirement exists.
WEIGHTS = {
    "behavior:requests": 0.55,
    "behavior:tables-after": 0.05,
    "migrated:dialect": 0.30,
    "migrated:runtime-identity": 0.03,
    "migrated:runtime-sql": 0.07,
}

GUARDS = ("behavior:mysql-intact", "behavior:tables-before",
          "behavior:tables-untouched",
          "migrated:sequences", "behavior:data-script", "migrated:install-data",
          "migrated:schema", "behavior:schema-semantics",
          "migrated:install-file", "migrated:install-schema",
          "behavior:install-semantics")
