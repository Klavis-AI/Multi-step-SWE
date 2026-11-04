"""The data gate: are MySQL's rows in PostgreSQL, and can the installer rebuild them?

Step 03 delivers two things -- a PostgreSQL database holding the data, and
`install/_db/pdo_pgsql.data.sql`, the file the framework installs that data
from -- and this module is four families because they are four different
questions. Two ask whether PostgreSQL holds the information MySQL holds; two
ask whether the move left behind the state and the artefact a move leaves.

  behavior:data          the fifteen tables the recording shows holding rows
                         hold the same rows in PostgreSQL, compared as
                         information rather than as bytes (`dbstate`).
  behavior:data-script   the data file fills a database the candidate's *own*
                         schema file has just built, and leaves it holding the
                         same rows once rather than twice when it is applied
                         to that database a second time. Thirty-four of its
                         thirty-eight checks are the family above asked again,
                         of a database the candidate's two files built between
                         them -- which is also what holds the two files to
                         each other, since a data file written for a column
                         the schema file does not declare that way lands wrong
                         or not at all.
  migrated:sequences     the eleven identity columns on those tables will hand
                         out a value past the rows that were just loaded, so
                         the first insert the application makes does not
                         collide. `AUTO_INCREMENT` state is not data and does
                         not travel with it; a sequence is the platform's own
                         replacement for it, which is what makes this
                         migrated-ness rather than equivalence.
  migrated:install-data  the same two files installed the way a deployment
                         configured with a different table prefix installs
                         them: `PREFIX_` replaced with `zz_` in both, on a
                         database that had neither. `GetSQL()` does that
                         substitution on every install, so a file with the
                         deployment's own prefix baked into it is a file the
                         framework can install exactly once and never again --
                         and the fresh database's sequences have to be
                         positioned by the file itself, because on a fresh
                         install there is nothing else to do it.

and one guard:

  behavior:data-empty    the nine tables MySQL has empty are still empty.

Three decisions here were forced by measuring the platform's own automated
migrator before the step was designed (`authoring/calibrate_pgloader_data.py`,
NOTES §21.1), and each of them is the difference between scoring the migration
and scoring the driver.

*The nine empty tables are a guard, not a payment.* `pgloader ... WITH data
only` reproduces sixteen of the twenty-four comparable tables, and nine of
those sixteen are the `jf__test_*` tables, which are empty in MySQL and empty
in an untouched PostgreSQL. A check that is true on the tree the candidate was
handed cannot pay (`gate.combine`), so the earned family is the fifteen
tables that hold rows and the nine are a separate cap. That is not a
technicality: with all twenty-four in one earned family the tree the candidate
starts from scores 9/24 for arriving.

The comparable set is twenty-four rather than twenty-six because
`dbstate.EXCLUDED_TABLES` drops `jf_session`, `jf_logs` and `jf_stats`; the
last two joined it in §24.18, when a blind run was docked for browsing the
application it had just migrated.

*Six of the seventeen identity columns are excluded for the same reason.* The
recording has seventeen `AUTO_INCREMENT` columns; five sit on empty
`jf__test_*` tables and one on `jf_session`, which is not comparable state at
all. A sequence over an empty table is already past its rows before anything
is loaded. The eleven that remain are all false on the handed tree and all
false after `pgloader`, which positions none of them -- the first insert into
`jf_stats` after its load fails with `duplicate key value violates unique
constraint "jf_stats_pkey"`. Two of the eleven -- `jf_logs` and `jf_stats` --
ask that question of the candidate's own rows rather than of the authority's,
because their extent is a record of traffic and not of the migration
(`_sequence_checks`, §24.18); the other nine keep the stronger comparison.

*Nothing in this module writes to either of the candidate's databases.* The
authority every family here reads is MySQL up to step 03 and the delivered
PostgreSQL database from the cutover at step 04, where the verifier has no
MySQL (see "authority" below); either way it is also what the inherited corpus
guards compare against the recording, and the candidate's PostgreSQL database
is what three of the guards that follow read. So the two files are
exercised on scratch databases this module creates and drops, and the only
statements the candidate's own cluster sees are the `CREATE DATABASE` and
`DROP DATABASE` that bracket them. An earlier draft truncated the live tables
and reloaded them from the candidate's file; that made the delivered state
unreadable after the fact and put a candidate's `TRUNCATE ... CASCADE` one
failed reload away from zeroing four guards it had nothing to do with.
"""

import functools
import json
import re

from . import behavior, dbstate, deploy, gate, installgate, pgstep
from . import schemagate, sqlsplit

# The framework's own name for this file, and not a path this gate invented.
# `BaseDatabase::GetSQL($Type)` builds `install/_db/{$Adapter}.{$Type}.sql` from
# the adapter class name, `InitializeData()` asks it for `"data"` exactly as
# `InitializeSchema()` asks it for `"schema"`, and the tree ships
# `mysqli.data.sql`, `pdo_mysql.data.sql`, `pdo_sqlite.data.sql` and
# `mariadb.data.sql` beside their four schema files. Step 02 delivered the
# `pdo_pgsql` half of the second pair; this step delivers the first.
DATA_FILE = "install/_db/pdo_pgsql.data.sql"

# The configured table prefix of this deployment, which is what `GetSQL()`
# substitutes for `PREFIX_`. Same constant `installgate` applies to the schema
# file, and it is imported from there rather than repeated so the two files
# cannot be installed under different prefixes by a later edit.
PREFIX = installgate.PREFIX

# The other prefix, which is the whole point of the placeholder: a deployment
# configured with `zz_` gets `zz_options` and not `jf_options`. `installgate`
# already installs the schema file twice for the same reason.
FLIP_PREFIX = installgate.FLIP_PREFIX

# The two databases this module builds. Neither is the candidate's --
# `installgate.create_fresh` refuses to recreate that one -- and both are
# dropped in a `finally`. They are named apart from `installgate`'s two so a
# crash in either module cannot leave the other's cleanup with nothing to do.
LOAD_DB = "wg_data_check"
FLIP_DB = "wg_data_prefix"

# Wall-clock spent running SQL the candidate wrote is counted in
# `installgate`, not here, because that module is where every application goes
# through -- the five this one makes (a schema install and two data installs
# at the deployment's own prefix, one of each at `zz_`) and the two
# `installgate` makes for its own three families. Seven in all, one counter,
# and the per-call limit is the number the instruction states.
BUDGET = installgate.BUDGET
CALL_LIMIT = installgate.CALL_LIMIT


# ------------------------------------------------------------- the recording

def recorded_rows(fixture=behavior.FIXTURE):
    """{table: row count} for the recorded tables, excluded ones included.

    `behavior-baseline.json`'s `tables_before` is one pass of `collect()` over
    the untouched deployment, taken before the corpus was replayed, so it is
    the row count the seed image shipped. Reading it here rather than shipping
    a second recording of the same numbers keeps one source of truth: if the
    seed image is ever re-recorded, the split below moves with it.
    """
    return {t: v["rows"] for t, v in behavior.load(fixture)["tables_before"].items()}


def split_tables(fixture=behavior.FIXTURE):
    """(tables with rows, tables without), both sorted, from the recording."""
    rows = {t: n for t, n in recorded_rows(fixture).items()
            if t not in dbstate.EXCLUDED_TABLES}
    return (sorted(t for t in rows if rows[t] > 0),
            sorted(t for t in rows if not rows[t]))


def seed_tables(fixture=behavior.FIXTURE):
    """The excluded-by-volume tables the recording shows holding rows.

    `jf_logs` and `jf_stats`: out of every whole-table comparison because
    their extent can move (`dbstate.VOLATILE_TABLES`), but the rows they were
    handed over with are data like any other and are compared by
    `compare_seed_rows`, in the delivered database and in both scratch
    databases the candidate's two install files build.
    """
    rec = recorded_rows(fixture)
    return sorted(t for t in dbstate.VOLATILE_TABLES if rec.get(t))


def resolvable_tables(fixture=behavior.FIXTURE):
    """Every recorded table, excluded ones included.

    Resolution and comparison are different questions and the exclusion set
    answers only the second. A scratch database still has to be able to *name*
    `jf_logs` -- `migrated:install-data` keeps asking about its sequence -- so
    the map `_build` hands to `PostgresState` is built from the whole
    recording, and the narrowing happens where the tables are compared. §24.18.
    """
    return sorted(recorded_rows(fixture))


def recorded_sequences(baseline=schemagate.FIXTURE, corpus=behavior.FIXTURE):
    """[(table, column)] for each AUTO_INCREMENT column over a table with rows.

    Seventeen columns carry `AUTO_INCREMENT` in the recording. Six are dropped
    here and the docstring at the top of this module says why.
    """
    want = schemagate.load(baseline)
    rows = recorded_rows(corpus)
    out = []
    for table in sorted(want["tables"]):
        if not rows.get(table):
            continue
        for col in want["tables"][table]["columns"]:
            if col.get("auto_increment"):
                out.append((table, col["name"]))
    return out


# --------------------------------------------------------------- authority --

# Which database these five families compare against.
#
# Up to and including step 03 the application is still on MySQL: MySQL is what
# the recording was taken from, MySQL is what the corpus replay drives, and
# MySQL is the authority. From the cutover at step 04 the verifier has no MySQL
# at all -- `Dockerfile.pg` installs no server, no client and no PHP driver,
# and asserts that none of the three is present -- so the authority is the
# PostgreSQL database the candidate delivered.
#
# That is not a weaker reading, and it is worth being precise about why. In the
# same verifier run `behavior:tables` compares 52 snapshots of that database
# against the same `behavior-baseline.json` these families derive their table
# list from, and it is a guard: one wrong row and the step is zero. So the
# chain is unbroken and one link longer -- recording, then the delivered
# database, then the two files that have to rebuild it -- and both ends of it
# are the recording either way. What the two file families actually ask is
# whether `install/_db/pdo_pgsql.*.sql` still reconstruct the deployment, and
# that question needs a correct deployment to compare against, not a MySQL one.
#
# A step declares the reading by calling `use_delivered_postgres()` from its
# plan, at import time, before any family runs. The default is MySQL so that a
# plan which says nothing gets the reading it had.
_AUTHORITY = "mysql"


def use_delivered_postgres():
    """From here on the authority is the database the candidate delivered."""
    global _AUTHORITY
    assert not _STATE, ("the authority was changed after a family had already"
                        " read one: a plan sets it at import time")
    _AUTHORITY = "postgres"


def source_label():
    return ("MySQL" if _AUTHORITY == "mysql"
            else "the delivered PostgreSQL database")


# ------------------------------------------------------------ both databases

_STATE = {}


def states():
    """(mysql state, postgres state, note) or (None, None, why). Never raises.

    The PostgreSQL side is built on `pgstep.live()`'s connection and on its
    `resolve_tables()` map, for two reasons that are both load-bearing.

    The connection, because `pgstep.live()` memoises which database is the
    candidate's and must have done so before `installgate` creates two of its
    own; every family in this module runs before that one, so asking here is
    also what makes the resolution happen early enough.

    The map, because step 02 leaves a migration free to fold the four
    mixed-case table names or to keep and quote them, and free to put the
    schema anywhere its `search_path` reaches. A data gate that looked for
    `lesson_XSS2_messages` in `public` would fail a tree the schema gate
    passed, which is a gate disagreeing with itself about what the step asked
    for.

    `binary_columns` is empty because the recording has no binary column: its
    155 columns are int, varchar, char, text, float and tinyint. It is passed
    explicitly all the same -- see `dbstate.PostgresState`.
    """
    if not _STATE:
        conn, resolved, note = pgstep.live()
        if conn is None:
            _STATE.update(my=None, pg=None, note=note)
            return None, None, note
        pg = dbstate.PostgresState(conn, resolved=resolved,
                                   binary_columns=())
        if _AUTHORITY != "mysql":
            _STATE.update(my=None, pg=pg,
                          note="%s; the authority is %s"
                               % (note, source_label()))
            return _STATE["my"], _STATE["pg"], _STATE["note"]
        try:
            my_conn, my_note = deploy.connect_mysql(deploy.APP_DB)
        except Exception as e:
            why = "no MySQL database to compare against: %s" % e
            _STATE.update(my=None, pg=None, note=why)
            return None, None, why
        _STATE.update(
            my=dbstate.MySQLState(my_conn, deploy.APP_DB),
            pg=pg,
            note="%s; %s" % (note, my_note))
    return _STATE["my"], _STATE["pg"], _STATE["note"]


def release():
    """End the MySQL read transaction, keeping the connection.

    InnoDB is REPEATABLE READ and `collect.open_mysql` does not autocommit, so
    a connection that has read a table is inside a transaction until something
    ends it -- and an open transaction holds a metadata lock on every table it
    touched. A later `DROP TABLE` from another connection then waits for that
    lock, and on this server `lock_wait_timeout` is 31536000 seconds.

    The corpus replay's last scenario is `1; DROP TABLE lesson_XSS2_messages`,
    so with the five families below running before it -- which is this step's
    plan, because they must read MySQL as the authority before the replay
    rewrites it -- the connection this module keeps would hang the replay for
    a year. Committing costs a read-only transaction nothing and removes the
    ordering dependency rather than documenting it.

    Called after each family rather than after the last one, so that
    reordering them stays a free choice.
    """
    my = _STATE.get("my")
    if my is None:
        return
    try:
        my.refresh()
    except Exception:
        pass


def _releasing(fn):
    """A family that leaves no MySQL transaction open behind it. See release()."""
    @functools.wraps(fn)
    def wrapper(*a, **kw):
        try:
            return fn(*a, **kw)
        finally:
            release()
    return wrapper


_SOURCE = {}


def source_state():
    """The state these families read as the authority, or None."""
    my, pg, _note = states()
    return my if _AUTHORITY == "mysql" else pg


def source_digests(tables):
    """{table: (digest, rows)} read from the authority once and kept.

    Once, because every family here compares against the same source and the
    source does not move: nothing in this module writes to either database,
    and the corpus families that do are listed after all of them.
    """
    src = source_state()
    if src is None:
        return {}
    for t in tables:
        if t not in _SOURCE:
            try:
                _SOURCE[t] = dbstate.table_digest(src.rows(t))
            except Exception as e:
                _SOURCE[t] = ("%s: %s" % (source_label(), e), -1)
    return {t: _SOURCE[t] for t in tables}


def _pg_digest(pg, table):
    """(digest, rows) or (None, why)."""
    try:
        return dbstate.table_digest(pg.rows(table)), None
    except Exception as e:
        return None, str(e).strip().splitlines()[0][:200]


def compare_tables(c, pg, tables, prefix):
    """One check per table: this database holds what the authority holds."""
    want = source_digests(tables)
    src = source_label()
    for t in tables:
        check = "%s/%s" % (prefix, t)
        expected, n = want.get(t, (None, -1))
        if n < 0:
            c.add(check, False,
                  "could not read %s's %s: %s" % (src, t, expected))
            continue
        got, why = _pg_digest(pg, t)
        if got is None:
            c.add(check, False, "could not read PostgreSQL's %s: %s" % (t, why))
            continue
        digest, rows = got
        if rows != n:
            c.add(check, False,
                  "%s has %d row(s), this one %d" % (src, n, rows))
        elif digest != expected:
            c.add(check, False,
                  "%d row(s) on both sides, but the contents differ"
                  " (%s %s, here %s)" % (n, src, expected[:16], digest[:16]))
        else:
            c.add(check, True, "%d row(s)" % n)


# --------------------------------------------------------- behavior:data ---

@_releasing
def tables_family(ctx=None, family="behavior:data"):
    """The rows MySQL holds are the rows PostgreSQL holds."""
    with_rows, _empty = split_tables()
    c = gate.Checks(family)
    _my, pg, note = states()
    if pg is None:
        return gate.crashed(family, note, len(with_rows))
    c.note(note)
    compare_tables(c, pg, with_rows, "data/table")
    compare_seed_rows(c, pg, "data/seed-rows")
    return c


def _seed_part(state, table, keep):
    """The rows of `table` whose `ID` is at or below `keep`, as a digest."""
    names = _column_names(state, table)
    at = [i for i, n in enumerate(names) if n.lower() == "id"]
    if not at:
        raise KeyError("%s has no ID column" % table)
    rows = [r for r in state.rows(table)
            if r[at[0]] is not None and int(r[at[0]]) <= keep]
    return dbstate.table_digest(rows)


def compare_seed_rows(c, pg, prefix):
    """`jf_logs` and `jf_stats`, compared over the rows they were handed over with.

    The two tables are out of the whole-table comparison above because their
    extent is a record of traffic: the deployment appends a row to each on
    every request, the harness's own included, so their row counts on the two
    sides never agree. What does not move is the rows that were there before
    anyone browsed -- the recording's own count of them, in ID order -- and
    the instruction puts those in scope like every other row ("Every row of
    every table ... is in scope"). So the authority's first `n` rows by ID,
    where `n` is the recorded count, must be exactly the rows the delivered
    PostgreSQL table holds at or below that ID. Rows either side has gained
    since are not compared. (Round-1 migration review, 2026-10-05: before
    this, a load that skipped both tables lost nothing.)

    `pg` need not be the delivered database: `loader_family` and
    `install_data_family` hand in the scratch database the candidate's own
    schema and data files built, so the data file's copy of these rows is held
    to the same rule ("Both files together must install this application from
    nothing ... these rows"). The authority is the same either way. (Round-2
    migration review, 2026-10-05: before this, a data file that left both
    tables out lost nothing in either file family.)
    """
    recorded = recorded_rows()
    src = source_state()
    for t in seed_tables():
        check = "%s/%s" % (prefix, t)
        n = recorded.get(t)
        try:
            names = _column_names(src, t)
            at = [i for i, x in enumerate(names) if x.lower() == "id"][0]
            ids = sorted(int(r[at]) for r in src.rows(t) if r[at] is not None)
        except Exception as e:
            c.add(check, False, "could not read %s's %s: %s"
                  % (source_label(), t, str(e).strip()[:200]))
            continue
        if len(ids) < n:
            c.add(check, False, "%s's %s holds %d row(s), fewer than the %d it"
                  " was handed over with" % (source_label(), t, len(ids), n))
            continue
        keep = ids[n - 1]
        try:
            want = _seed_part(src, t, keep)
            got = _seed_part(pg, t, keep)
        except Exception as e:
            c.add(check, False, "could not read %s: %s"
                  % (t, str(e).strip().splitlines()[0][:200]))
            continue
        if got[1] != want[1]:
            c.add(check, False, "%s holds %d row(s) at or below ID %d, this one %d"
                  % (source_label(), want[1], keep, got[1]))
        elif got[0] != want[0]:
            c.add(check, False, "%d row(s) at or below ID %d on both sides, but"
                  " the contents differ" % (want[1], keep))
        else:
            c.add(check, True, "%d row(s) at or below ID %d" % (want[1], keep))


@_releasing
def empty_tables_family(ctx=None, family="behavior:data-empty"):
    """The tables MySQL has empty are empty in PostgreSQL too.

    A guard: true before the step begins and true after it, so it caps and
    does not pay. What it catches is a loader that invents rows in the nine
    `jf__test_*` tables -- which is a defect whether or not the fifteen that
    matter are right, and which no comparison of the fifteen can see.

    It reads the database the candidate delivered, like the two families above
    it, and not the scratch database the two below build. A candidate's file
    that fills those nine tables is caught here only if they ran it against
    their own database, which every plausible route to a data file does. The
    file read on its own would not be; that is the limit of this guard and the
    reason it is a guard rather than a payment.
    """
    _with_rows, empty = split_tables()
    c = gate.Checks(family)
    _my, pg, note = states()
    if pg is None:
        return gate.crashed(family, note, len(empty))
    c.note(note)
    compare_tables(c, pg, empty, "data/empty")
    return c


# ----------------------------------------------------- migrated:sequences ---

SEQUENCES_SQL = (
    "SELECT tn.nspname, t.relname, a.attname,"
    "       sn.nspname, s.relname, q.seqincrement"
    "  FROM pg_class s"
    "  JOIN pg_namespace sn ON sn.oid = s.relnamespace"
    "  JOIN pg_sequence q ON q.seqrelid = s.oid"
    "  JOIN pg_depend d ON d.objid = s.oid"
    "                  AND d.classid = 'pg_class'::regclass"
    "                  AND d.refclassid = 'pg_class'::regclass"
    "                  AND d.refobjsubid > 0"
    "  JOIN pg_class t ON t.oid = d.refobjid"
    "  JOIN pg_namespace tn ON tn.oid = t.relnamespace"
    "  JOIN pg_attribute a ON a.attrelid = t.oid AND a.attnum = d.refobjsubid"
    " WHERE s.relkind = 'S'")


def _quoted(schema, name):
    return '"%s"."%s"' % (schema.replace('"', '""'), name.replace('"', '""'))


def next_values(conn):
    """{(schema, table, column): the value the sequence will hand out next}.

    Asked of the sequence rather than by calling `nextval`, which would both
    move the sequence on and make the answer depend on whether anything else
    in this process had already called it. The first draft of the calibration
    measured "has this sequence been called at all" and reported 1 of 17
    purely because an earlier probe in the same session had burned an id.

    Two columns decide the answer and `pg_sequence_last_value` gives only one
    of them. That function returns NULL whenever `is_called` is false, which
    is true both of a sequence nobody has touched *and* of one that has just
    been positioned: `ALTER TABLE t ALTER COLUMN id RESTART WITH 3004` writes
    `last_value = 3004, is_called = false` and leaves `pg_sequence.seqstart`
    at 1. A draft of this gate read the catalogue's `seqstart` in that case and
    scored the reference solution 0 of 11 for doing exactly what the step asks.
    So the sequence relation itself is read -- `last_value` and `is_called`,
    the two values `nextval` consults -- and the next value is `last_value`
    when it has not been called and `last_value + seqincrement` when it has.
    That is a plain `SELECT` on a table the scoring role owns; it moves
    nothing.

    The join is `pg_depend` rather than `pg_get_serial_sequence` so that an
    identity column and a `serial` default are found by the same query and
    mixed-case column names need no quoting round trip.
    """
    cur = conn.cursor()
    cur.execute(SEQUENCES_SQL)
    rows = cur.fetchall()
    out = {}
    for tsch, tbl, col, ssch, seq, incr in rows:
        cur.execute("SELECT last_value, is_called FROM %s"
                    % _quoted(ssch, seq))
        last, called = cur.fetchone()
        out[(tsch, tbl, col)] = int(last) + (int(incr) if called else 0)
    cur.close()
    return out


@_releasing
def sequences_family(ctx=None, family="migrated:sequences"):
    """Will the next insert get an id past the rows that were just loaded?"""
    wanted = recorded_sequences()
    c = gate.Checks(family)
    _my, pg, note = states()
    if pg is None:
        return gate.crashed(family, note, len(wanted))
    c.note(note)
    _sequence_checks(c, source_state(), pg, wanted, "data/sequence")
    return c


_HIGH = {}


def _column_names(state, table):
    """The column names of `table`, whichever state was handed in.

    `MySQLState.columns` returns (name, charset) pairs because the decoder
    needs the charset; `PostgresState.columns` returns names. One reader has
    to accept both since the authority moves at the cutover.
    """
    return [c[0] if isinstance(c, (tuple, list)) else c
            for c in state.columns(table)]


def _source_high(state, table, column):
    """The authority's largest value in one auto-numbered column, read once.

    Once per verifier run, and cached, because the two families that ask for
    it ask on either side of the candidate's own SQL being applied. The number
    has to be the one the tree was handed over with: a file that lowers what
    it is compared against -- by deleting rows from the database it is
    compared to, say -- would otherwise make `nxt > high` true by moving
    `high`. Nothing here can reach that database (`installgate` applies
    statements through a library connection to a scratch database and
    interprets nothing), but the comparison should not depend on that being
    true, and the first read happens before the first application either way.

    It is read out of the rows rather than with `MAX()` because the authority
    is MySQL up to step 03 and the delivered PostgreSQL database from step 04,
    and the two spell an identifier differently. `state.rows` already
    canonicalises and already orders, both families already read every row of
    every table through it, and a second spelling of the same query is a
    second thing to get wrong.
    """
    key = (id(state), table, column)
    if key not in _HIGH:
        names = _column_names(state, table)
        at = [i for i, n in enumerate(names) if n.lower() == column.lower()]
        if not at:
            raise KeyError("%s has no column %r" % (table, column))
        high = 0
        for r in state.rows(table):
            v = r[at[0]]
            if v is not None:
                high = max(high, int(v))
        _HIGH[key] = high
    return _HIGH[key]


def _sequence_checks(c, src, pg, wanted, prefix):
    """One check per identity column: its next value is past the authority's.

    Shared with `migrated:install-data`, which asks the same question of a
    database the candidate's two files built from nothing. The two readings
    are not the same reading: on the delivered database the sequence may have
    been positioned by hand after the load, while on the fresh install only
    the data file itself can have done it.

    "Highest" is asked of the authority in both cases, and of its rows rather
    than of the recording, because the recording's row counts are not its key
    values -- `jf_stats` has 3,003 rows and ids that do not start at 1.
    """
    try:
        seqs = next_values(pg.conn)
    except Exception as e:
        for table, column in wanted:
            c.add("%s/%s/%s" % (prefix, table, column), False,
                  "could not read the sequences: %s" % e)
        return

    for table, column in wanted:
        check = "%s/%s/%s" % (prefix, table, column)
        try:
            schema, relname = pg._loc(table)
        except KeyError:
            c.add(check, False, "no relation resolves for %r" % table)
            continue
        hit = [v for (s, t, col), v in seqs.items()
               if s == schema and t == relname and col.lower() == column.lower()]
        if not hit:
            c.add(check, False,
                  "%s.%s is on no sequence: an identity column or a serial"
                  " default is what makes the next insert pick its own id"
                  % (relname, column))
            continue
        # For a table the framework appends to on every request the authority's
        # highest id is a record of traffic, so asking the candidate to clear it
        # measures who browsed rather than whether the load positioned the
        # sequence. The question that stays sound is self-consistency: is this
        # database's sequence past this database's own highest row? That is what
        # "the first insert collides" actually means, and for `install-data` --
        # where the rows can only have come from the candidate's two files -- it
        # is the same reading. Every other table keeps the stronger comparison
        # against the authority. §24.18.
        volatile = table in dbstate.VOLATILE_TABLES
        against = pg if volatile else src
        try:
            high = _source_high(against, table, column)
        except Exception as e:
            c.add(check, False, "could not read %s's %s.%s: %s"
                  % ("this database" if volatile else source_label(),
                     table, column, e))
            continue
        nxt = max(hit)
        c.add(check, nxt > high,
              "next %d, past the highest row's %d" % (nxt, high) if nxt > high
              else "the sequence will hand out %d and the rows already reach"
                   " %d, so the first insert collides" % (nxt, high))


# ------------------------------------------------------ the candidate files -

def _clip(text, n=300):
    text = " ".join(text.split())
    return text[:n] + ("..." if len(text) > n else "")


_PLACEHOLDER_RE = re.compile(r"PREFIX_[A-Za-z0-9_]")


def placeholder_kept(text):
    """Is `PREFIX_` in the file somewhere the server will see it?

    The obvious form of this check -- `"PREFIX_" in text` -- is satisfied by a
    file with `jf_` baked into every statement and the word `PREFIX_` in a
    header comment. `GetSQL()` substitutes over the whole file, comments
    included, so a placeholder that appears only in a comment moves nothing:
    what has to carry one is the SQL.

    The comments come out through `sqlsplit`, which is the same lexer the file
    is cut into statements with, so "in a comment" means what the server means
    by it. A `--` inside a string literal is not the start of one, which
    matters here: this database puts 12,730 semicolons inside the string
    literals of its own data file, and a good many dashes in with them.

    This is the cheap half of the question. The expensive half is
    `migrated:install-data`, which installs the file at `zz_` and reads the
    rows back out of the tables that prefix names.
    """
    live = "".join(text[i:j] for kind, i, j, _ in sqlsplit.scan(text)
                   if kind != "comment")
    return bool(_PLACEHOLDER_RE.search(live))


def _apply(text, database, prefix):
    """One application of a candidate file. (rc, errors, raw) or (None, None, why).

    Every place this module hands the candidate's SQL to the server goes
    through here, and through `installgate.install` beneath it: the file is
    cut into statements by `sqlsplit` and sent one at a time as an
    unprivileged role, never given whole to a client that would read
    meta-commands out of it. An exhausted budget comes back as `rc is None`
    and fails the checks that depended on it, rather than running the verifier
    out of time and taking the step out of the mean.
    """
    try:
        return installgate.install(text, database, prefix)
    except Exception as e:
        return None, None, "the scorer could not apply the file: %s" % e


def _renamed(table, prefix):
    """The recording's table name as a deployment configured at `prefix` spells it."""
    return prefix + table[len(PREFIX):] if table.startswith(PREFIX) else table


def _build(database, schema_text, prefix, tables):
    """(state, None) or (None, why): a scratch database built by the candidate's
    own schema file.

    This is what holds the candidate's two deliverables to each other. Neither
    family below is given a database the gate built from the recording: both
    get one `install/_db/pdo_pgsql.schema.sql` has just created, so a data
    file written for a column the schema file does not declare that way lands
    wrong or does not land at all, and the instruction's "change it in both
    places" is a thing the gate can see rather than a thing it asks for.

    The returned state's `resolved` map is keyed by the recording's own table
    names and points at whatever relations the file built under `prefix`, so
    every comparison downstream goes on naming tables the way the recording
    names them.
    """
    try:
        rc, err = installgate.create_fresh(database, installgate.app_owner())
    except Exception as e:
        return None, "could not create %s: %s" % (database, e)
    if rc != 0:
        return None, "could not create %s: %s" % (database, _clip(err, 200))

    rc, errors, why = _apply(schema_text, database, prefix)
    if rc is None:
        return None, why
    if errors:
        return None, ("the schema file did not build a database to fill:"
                      " %d error(s) from the server at prefix %r, first: %s"
                      % (len(errors), prefix, _clip(errors[0])))
    try:
        import psycopg2
        conn = psycopg2.connect(host=deploy.PG_RUNDIR, port=deploy.PG_PORT,
                                user=deploy.PG_USER, dbname=database,
                                connect_timeout=10)
        conn.autocommit = True
    except Exception as e:
        return None, "cannot read %s: %s" % (database, e)

    try:
        found = installgate._tables_present(
            conn, [_renamed(t, prefix) for t in tables])
    except Exception as e:
        return None, "cannot read %s's catalogue: %s" % (database, e)
    resolved = {t: found[_renamed(t, prefix)] for t in tables}
    return dbstate.PostgresState(conn, resolved=resolved,
                                 binary_columns=()), None


def _drop(state, database):
    """Close and remove a scratch database, whatever happened to it."""
    if state is not None:
        try:
            state.conn.close()
        except Exception:
            pass
    try:
        installgate.drop_database(database)
    except Exception:
        pass


def _rest(ids, c):
    return [i for i in ids if i not in c._ids]


def _no_drift(ids, c):
    if set(ids) != set(c._ids):
        raise AssertionError(
            "denominator drift: planned %d, scored %d, only-planned %s"
            % (len(ids), len(c._ids), sorted(set(ids) - set(c._ids))[:5]))


# -------------------------------------------------- behavior:data-script ---

def loader_ids(with_rows):
    ids = ["loader/file-present", "loader/prefix-placeholder-kept",
           "loader/from-empty/applies-cleanly",
           "loader/repeatable/applies-cleanly"]
    ids += ["loader/from-empty/%s" % t for t in with_rows]
    ids += ["loader/repeatable/%s" % t for t in with_rows]
    ids += ["loader/%s/seed-rows/%s" % (r, t)
            for r in ("from-empty", "repeatable") for t in seed_tables()]
    return ids


@_releasing
def loader_family(ctx=None, family="behavior:data-script"):
    """Does the shipped data file rebuild the data, from empty and from full?

    The file is `install/_db/pdo_pgsql.data.sql` and it is run the way
    `BaseDatabase::InitializeData()` runs one: read whole, `PREFIX_` replaced
    with the configured prefix, fed to the server. `installgate.install` is the
    same function that applies the schema file for step 02's three install
    families, so the two deliverables are handed to the database identically
    and a difference between them is the candidate's rather than the gate's.

    A failed statement is stepped over rather than stopped at, again as in
    `installgate` and for the same reason: the framework applies the file one
    statement at a time and throws away what `query()` returns, so a file that
    half-works has to be seen half-working rather than aborting at the first
    error and failing everything alike. The check is how many statements the
    server rejected. There is no exit status to read -- `installgate` applies
    the file over a library connection, not through a client -- and a client
    would have left one at 0 either way.

    Two rounds, and the second is not the first repeated for luck.

    From empty is the question the deliverable is for: a database holding this
    schema and no rows ends up holding MySQL's rows. From full is what
    separates a load from an append -- run again on the database it has just
    filled, an `INSERT`-only file collides on every primary key and doubles
    every table that has none, while a file that clears what it is about to
    write, or writes `ON CONFLICT DO NOTHING`, passes. The framework's own
    `InitializeData()` calls `TruncateTables()` first, but that truncates only
    the 19 tables carrying the configured prefix; the other 8 are the
    candidate's problem either way.

    The database both rounds run against is a scratch one `_build` creates and
    this function drops, built by the candidate's own schema file at the
    deployment's own prefix. An earlier draft truncated the delivered database
    and reloaded that, which was worse twice over: it left the delivered state
    unreadable after the fact, and it put a candidate whose file opens with
    `TRUNCATE ... CASCADE` one failed reload away from zeroing four guard
    families that had nothing to do with this one.
    """
    with_rows, empty = split_tables()
    ids = loader_ids(with_rows)
    c = gate.Checks(family)
    _my, pg, note = states()
    if pg is None:
        return gate.crashed(family, note, len(ids))
    c.note(note)

    text, why = installgate.read_file(rel=DATA_FILE)
    if text is None:
        c.fail_all(ids, why)
        return c
    c.add("loader/file-present", True, why)
    kept = placeholder_kept(text)
    c.add("loader/prefix-placeholder-kept", kept,
          "" if kept else
          "the file spells its table names out instead of using the"
          " framework's PREFIX_ placeholder, so a deployment configured with"
          " another prefix would load nothing")

    # The authority is read before any of the candidate's SQL runs. Nothing in
    # this module writes to it, and taking the reading first means a failure
    # later cannot be mistaken for a moved source.
    source_digests(with_rows + empty)

    schema, why = installgate.read_file(rel=installgate.SCHEMA_FILE)
    if schema is None:
        c.fail_all(_rest(ids, c),
                   "the data file has no schema file to fill: %s" % why)
        return c

    state = None
    try:
        state, why = _build(LOAD_DB, schema, PREFIX, resolvable_tables())
        if state is None:
            c.fail_all(_rest(ids, c), why)
            return c
        for round_name in ("from-empty", "repeatable"):
            check = "loader/%s/applies-cleanly" % round_name
            rc, errors, why = _apply(text, LOAD_DB, PREFIX)
            if rc is None:
                c.fail_all(_rest(ids, c), why)
                return c
            c.add(check, not errors,
                  "" if not errors else
                  "%d error(s) from the server, first: %s"
                  % (len(errors), _clip(errors[0])))
            if errors:
                c.note("%s: %s" % (round_name, _clip("; ".join(errors[:4]), 400)))
            state.refresh()
            compare_tables(c, state, with_rows, "loader/%s" % round_name)
            compare_seed_rows(c, state, "loader/%s/seed-rows" % round_name)
    finally:
        _drop(state, LOAD_DB)

    _no_drift(ids, c)
    return c


# --------------------------------------------------- migrated:install-data --

def install_data_ids(with_rows, sequences):
    ids = ["install-data/applies-cleanly"]
    ids += ["install-data/table/%s" % t for t in with_rows]
    ids += ["install-data/sequence/%s/%s" % (t, col) for t, col in sequences]
    ids += ["install-data/seed-rows/%s" % t for t in seed_tables()]
    return ids


@_releasing
def install_data_family(ctx=None, family="migrated:install-data"):
    """Can the framework install this deployment somewhere else?

    Everything above reads the database the candidate delivered, or a copy of
    it built at the prefix that database already uses. This family asks the
    question those cannot: `GetSQL()` substitutes `PREFIX_` on every install,
    so the two files are the deployment's *installer* and not a snapshot of
    it, and an installer that only works at `jf_` is one the framework can run
    exactly once. Both files go into a database that had neither, at `zz_`.

    That is migrated-ness rather than equivalence in the way the distinction
    is meant. The rows are identical on both sides of it -- the same
    fifteen digests `behavior:data` compares -- but what is being scored is
    that the platform's install path still exists after the move. The eleven
    sequence checks are the same point from the other end: on a fresh install
    there is no pre-existing sequence state for a loader to inherit, so a file
    that leaves the identity columns where `CREATE TABLE` left them produces a
    deployment whose first insert collides. Nothing but the file itself can
    position them here.

    It is also what closes the cheapest route through this step. A dump of the
    candidate's own PostgreSQL database, saved as the data file, passes the
    two families above and scores nothing here: every table name in it is
    spelled `jf_...`, the substitution finds no placeholder to replace, and
    every statement fails against a database holding `zz_...`.
    """
    with_rows, empty = split_tables()
    wanted = recorded_sequences()
    ids = install_data_ids(with_rows, wanted)
    c = gate.Checks(family)
    _my, pg, note = states()
    if pg is None:
        return gate.crashed(family, note, len(ids))
    c.note(note)

    schema, s_note = installgate.read_file(rel=installgate.SCHEMA_FILE)
    text, d_note = installgate.read_file(rel=DATA_FILE)
    if text is None:
        c.fail_all(ids, d_note)
        return c
    if schema is None:
        c.fail_all(ids, "the data file has no schema file to fill: %s" % s_note)
        return c

    source_digests(with_rows)

    state = None
    try:
        state, why = _build(FLIP_DB, schema, FLIP_PREFIX, resolvable_tables())
        if state is None:
            c.fail_all(ids, why)
            return c
        rc, errors, why = _apply(text, FLIP_DB, FLIP_PREFIX)
        if rc is None:
            c.fail_all(ids, why)
            return c
        c.add("install-data/applies-cleanly", not errors,
              "" if not errors else
              "%d error(s) from the server installing at %r, first: %s"
              % (len(errors), FLIP_PREFIX, _clip(errors[0])))
        for line in errors[:6]:
            c.note(line[:200])
        state.refresh()
        compare_tables(c, state, with_rows, "install-data/table")
        compare_seed_rows(c, state, "install-data/seed-rows")
        _sequence_checks(c, source_state(), state,
                         wanted, "install-data/sequence")
    finally:
        _drop(state, FLIP_DB)

    _no_drift(ids, c)
    return c


# ---------------------------------------------------------------- selftest --

def summary():
    """What the families would ask, without a deployment. For the rehearsal."""
    with_rows, empty = split_tables()
    seqs = recorded_sequences()
    return {
        "behavior:data": len(with_rows),
        "behavior:data-empty": len(empty),
        "behavior:data-script": len(loader_ids(with_rows)),
        "migrated:sequences": len(seqs),
        "migrated:install-data": len(install_data_ids(with_rows, seqs)),
    }


if __name__ == "__main__":
    print(json.dumps(summary(), indent=1, sort_keys=True))
