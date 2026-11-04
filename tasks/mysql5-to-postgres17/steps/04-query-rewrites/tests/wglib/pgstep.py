"""The PostgreSQL side of a step's plan, resolved once and shared.

Every step from 02 onward asks the same questions of the candidate's cluster
-- is the schema the one MySQL had, does it answer what MySQL answered, does
the shipped install file build that same schema from nothing, and from step 04
whether the SQL in the tree is SQL this server would run -- and each of those
is an expensive setup in front of a cheap reading. This module does the setup
once per verifier run and hands `plan.py` the family functions.

Two things here are load-bearing rather than convenience.

*The database is resolved before the install gate runs.* `deploy.pick_database`
falls back to name order over whatever non-system databases exist, and the
install gate builds two of its own. A candidate whose database sorts after
`wg_` would otherwise be scored on the grader's scratch copy of their install
file -- a green for a tree that was never deployed. `live()` is memoised and
`plan.py` lists the live families first, so the resolution happens while the
cluster still contains only the candidate's databases. `installgate.evaluate`
drops its scratch databases afterwards as the second half of the same defence.

*A cluster that is not there fails the full denominator.* Not one synthetic
check, and not an empty family that `gate.combine` would drop for contributing
no weight: the number of questions this step asks is fixed by the recording
before the candidate's tree is looked at, and every one of them is
unanswerable, which `gate.py` counts as failed. Getting that wrong is how a
no-op scores above zero.
"""

from . import gate, installgate, pgnames, pglog, schemagate
from . import semantics, sqlgate


# Memo for one verifier process. Each entry is filled on first use and the
# failure is memoised too, so a dead cluster is reported identically by every
# family instead of being retried once per family.
_CONN = {}
_LIVE = {}
_INSTALL = {}


def n_schema(fixture=schemagate.FIXTURE):
    return len(schemagate.expected_ids(schemagate.load(fixture)))


def n_semantics(baseline=semantics.BASELINE):
    # Units, not probes. `semantics.evaluate` scores one check per column,
    # table, unique index and foreign key -- 193 of them over 544 probes -- so
    # the denominator held open when the family cannot be evaluated has to be
    # the one the family would have used if it could. Counting the plan instead
    # holds open 544, which is the same score (all of them failed) read off a
    # different question count, and a report that says 0/544 where the healthy
    # report says 193/193 is an instrument disagreeing with itself about how
    # many questions it asks. The install path had the identical defect;
    # `rehearse_schemagate.check_denominators` now asserts both.
    return len(semantics.unit_ids(semantics.build_plan(semantics.load(baseline))))


def n_dialect(fixture=sqlgate.FIXTURE):
    return len(sqlgate.check_ids(fixture))


def n_runtime_identity():
    return len(pglog.IDENTITY_IDS)


def n_runtime_sql():
    return len(pglog.SQL_IDS)


def connection():
    """(conn, note) for the candidate's cluster, or (None, why).

    Split out of `live()` because resolving the recorded table names is the
    schema families' setup and not everyone's. A family that only needs to ask
    the server a question must not be reported as "no PostgreSQL database to
    score" because a *different* family's fixture would not load -- that is a
    scorer fault wearing a candidate fault's words.

    Never raises: a cluster that is down, absent, or empty is a reading this
    package takes, not an error it propagates. `deploy.connect_postgres` is
    where the database is picked, so the first family to call this is the one
    that fixes it -- see the module docstring.
    """
    if not _CONN:
        from . import deploy
        try:
            conn, note = deploy.connect_postgres()
            conn.autocommit = True
            _CONN.update(conn=conn, note=note)
        except Exception as e:
            _CONN.update(conn=None,
                         note="no PostgreSQL database to score: %s" % e)
    return _CONN["conn"], _CONN["note"]


def live():
    """(conn, resolved, note) for the candidate's database, or (None, None, why).

    Never raises, for the same reason `connection()` does not.
    """
    if not _LIVE:
        conn, note = connection()
        if conn is None:
            _LIVE.update(conn=None, resolved=None, note=note)
        else:
            try:
                want = schemagate.load(schemagate.FIXTURE)
                schemas = pgnames.widen_search_path(conn)
                resolved = pgnames.resolve_tables(conn, sorted(want["tables"]))
                _LIVE.update(conn=conn, resolved=resolved,
                             note="%s; search_path over %d schema(s)"
                                  % (note, len(schemas)))
            except Exception as e:
                _LIVE.update(conn=None, resolved=None,
                             note="the recorded schema could not be resolved"
                                  " against %s: %s" % (note, e))
    return _LIVE["conn"], _LIVE["resolved"], _LIVE["note"]


def schema_family(ctx=None, family="migrated:schema", extra_columns=None):
    """Is the candidate's live schema the one MySQL had?

    `extra_columns` is the step's widening of `schemagate.TYPE_MAP` -- see
    `schemagate.widened`. A step that forces a column into a type an
    earlier step had no reason to accept passes it here *and* to
    `install_schema_family`, or its own deliverable fails one of its guards.
    """
    conn, _resolved, note = live()
    if conn is None:
        return gate.crashed(family, note, n_schema())
    c = schemagate.evaluate(conn, extra_columns=extra_columns)
    c.family = family
    c.note(note)
    return c


def semantics_family(ctx=None, family="behavior:schema-semantics"):
    """Does the candidate's live schema answer what MySQL answered?"""
    conn, resolved, note = live()
    if conn is None:
        return gate.crashed(family, note, n_semantics())
    c = semantics.evaluate(conn, resolved, family=family)
    c.note(note)
    return c


def dialect_family(ctx=None, family="migrated:dialect", roots=("/app",),
                   fixture=sqlgate.FIXTURE):
    """Is every SQL statement in the tree SQL this server would run?

    It reads the cluster for the same reason the schema families do -- the
    third layer of `sqlgate` asks the *candidate's* catalogs what exists, so a
    tree that defines the function it calls has not been caught out by a
    reference server's opinion. A cluster that is not there therefore fails
    the full denominator, exactly as above: the statements could be parsed,
    but the question the family asks is not "does the grammar accept this",
    it is "would this database run it", and nothing here can answer that.
    """
    conn, note = connection()
    if conn is None:
        return gate.crashed(family, note, n_dialect(fixture))
    c = sqlgate.evaluate(list(roots), conn, fixture=fixture, family=family)
    c.note(note)
    return c


def _dbname(conn):
    """The database `connection()` resolved, asked of the connection itself.

    Not `deploy.APP_DB`: which database is the candidate's is decided by
    `deploy.pick_database` at the top of the run, and a family that assumed
    the recorded name would be asking about a database nobody connected to.
    """
    try:
        return conn.info.dbname
    except Exception:
        try:
            return conn.get_dsn_parameters().get("dbname")
        except Exception:
            return None


RUNTIME_FAMILIES = ("migrated:runtime-identity", "migrated:runtime-sql")


def runtime_families(ctx=None, windows=(), fixture=pglog.FIXTURE,
                     names=RUNTIME_FAMILIES):
    """[identity, sql] -- what the running application did, from the log.

    `windows` are the per-scenario byte ranges the corpus recorder marked. An
    empty tuple is not an excuse: `pglog.families` then sees no statements and
    no authenticated session, which is what a tree that never reached
    PostgreSQL looks like, and both families fail closed on it.

    One reading of the log, two families out of it, so the identity checks and
    the traffic checks can be weighed separately without driving the corpus
    twice.
    """
    conn, note = connection()
    if conn is None:
        return [gate.crashed(names[0], note, n_runtime_identity()),
                gate.crashed(names[1], note, n_runtime_sql())]
    out = pglog.families(conn, windows, fixture=fixture,
                         database=_dbname(conn), names=names)
    for c in out:
        c.note(note)
    return out


def _install(extra_columns=None):
    """The install gate's three families, run once.

    The memo is keyed on nothing, because the three families that share it are
    three readings of one run. `extra_columns` is therefore asserted to agree
    across them rather than being allowed to select a second run: a plan that
    passed the widening to one install family and not to another would get
    whichever call happened to be first, which is exactly the kind of
    order-dependent grading this module exists to remove.
    """
    if not _INSTALL:
        _INSTALL["extra_columns"] = extra_columns
        try:
            c, sch, sem = installgate.evaluate(extra_columns=extra_columns)
        except Exception as e:                  # the gate handles its own; belt
            why = "the install gate could not run: %s" % e
            c = gate.crashed("install", why, len(installgate.expected_ids(
                schemagate.load(schemagate.FIXTURE))))
            sch = gate.crashed("install-schema", why, n_schema())
            sem = gate.crashed("install-semantics", why, n_semantics())
        _INSTALL.update(install=c, schema=sch, semantics=sem)
    if _INSTALL["extra_columns"] != extra_columns:
        raise AssertionError(
            "the install families disagree about the step's column widening:"
            " %r then %r" % (_INSTALL["extra_columns"], extra_columns))
    return _INSTALL


def install_family(ctx=None, family="migrated:install-file", extra_columns=None):
    """Does `install/_db/pdo_pgsql.schema.sql` exist, apply, and keep PREFIX_?"""
    c = _install(extra_columns)["install"]
    c.family = family
    return c


def install_schema_family(ctx=None, family="migrated:install-schema",
                          extra_columns=None):
    """Is the schema that file builds from nothing the one MySQL had?"""
    c = _install(extra_columns)["schema"]
    c.family = family
    return c


def install_semantics_family(ctx=None, family="behavior:install-semantics",
                             extra_columns=None):
    """Does that freshly-built schema answer what MySQL answered?"""
    c = _install(extra_columns)["semantics"]
    c.family = family
    return c
