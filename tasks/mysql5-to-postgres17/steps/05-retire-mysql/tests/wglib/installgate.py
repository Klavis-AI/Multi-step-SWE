"""The schema file, run the way the framework runs it, on a database the
grader made.

`schemagate` and `semantics` read the database the candidate built. This module
reads the *other* deliverable: `install/_db/pdo_pgsql.schema.sql`, the file
jFramework installs from. §20.1 established that the two are separate objects
and that the shipped MySQL one is already broken -- `app_user` carries a
foreign key to `jf_users` eleven lines before `jf_users` exists, so
`mysqli.schema.sql` builds 26 of its 27 tables and `InitializeSchema()` says
nothing about it, because it ignores what `query()` returns. MySQL only gets
away with the full dump in `SQL/webgoat.sql` because mysqldump wrapped it in
`/*!40014 SET FOREIGN_KEY_CHECKS=0 */`, which is a comment PostgreSQL does not
honour and has no equivalent for. A file that installs on PostgreSQL therefore
has to be ordered, or to add its constraints at the end, and the only way to
find that out is to run it.

So this gate runs it. Three properties, each measured rather than inspected:

* **It applies.** Cut into statements and sent one at a time, carrying on
  past a failure -- the framework splits on `;` and ignores what each piece
  returns, so a file that half-works must be seen half-working rather than
  abort at the first error and fail everything alike. The number of errors the
  server reported is itself a check. The cutting is `sqlsplit`'s rather than
  psql's, and the account is an unprivileged one rather than `postgres`, for
  the reason set out on `_psql` below: a file handed to psql is a file psql
  executes meta-commands out of.

* **It is the same schema.** The whole of `schemagate` and the whole of
  `semantics` run again, against the freshly installed database. A candidate
  who hand-tuned the live database and shipped a stale file is caught here and
  nowhere else.

* **`PREFIX_` still means something.** The placeholder is jFramework's, not
  decoration: `GetSQL()` does `str_replace("PREFIX_", $this->Config->TablePrefix, ...)`.
  So the file is installed a second time with the prefix set to `zz_`, and the
  19 prefixed tables must move while the 8 unprefixed ones must not.

The database is created `TEMPLATE template0 ENCODING UTF8` with the **C
locale**, and that is the point rather than an incidental. Case behaviour that
came from the cluster's default collation does not survive the move; case
behaviour that the file declares -- `citext`, or a collation the file creates
itself -- does. The 65 `case` probes are the same 65 asked of the candidate's
own database, so the pair of runs says which of the two it was. Collation
objects and extensions are per-database, so a file that needs `citext` has to
say `CREATE EXTENSION`, and a file that needs an ICU collation has to create
it.
"""

import os
import time

from . import deploy
from . import gate
from . import pgnames
from . import schemagate
from . import semantics
from . import sqlsplit

SCHEMA_FILE = "install/_db/pdo_pgsql.schema.sql"

# Wall-clock the scorer will spend running SQL a candidate wrote: per
# application of one file, and in total across every application any gate in
# this step makes.
#
# The per-call number is the one the instruction states, so a candidate who
# read only the instruction knows the bound that is applied. The total is the
# reason the two numbers are not the same: this module applies the schema file
# twice for its own three families, and on the step that also has a data file
# `datagate` applies the pair five more times, which is seven applications in
# all. A per-call limit alone bounds that at seven times the limit -- and a
# verifier Harbor kills writes no `reward.json`, so the step drops out of the
# mean instead of scoring the zero it earned. BUDGET is set to exactly
# CALL_LIMIT x 7 so that every one of the seven can take its full share and
# the total is still well inside `task.toml`'s verifier timeout. The
# reference solution's two files spend about ten seconds of it between them.
#
# The counter lives here rather than in `datagate` because this module is the
# one place candidate SQL is applied, so it is the one place that can count.
CALL_LIMIT = 300.0
BUDGET = 7 * CALL_LIMIT
_SPENT = {"used": 0.0}
BUDGET_NOTE = ("the scorer's budget for running this tree's own SQL ran out:"
               " %d seconds over the applications of the schema and data"
               " files" % BUDGET)


def budget_left():
    return BUDGET - _SPENT["used"]


def spent():
    return _SPENT["used"]

FRESH_DB = "wg_install_check"
FLIP_DB = "wg_install_prefix"
PREFIX = "jf_"
FLIP_PREFIX = "zz_"

# The 8 tables the schema file names outright, and therefore the 8 that must
# NOT move when the prefix changes. Everything else in the baseline is named
# `PREFIX_...`. Not hard-coded from reading the file: derived below from the
# baseline by which names start with the deployment's own prefix, with this
# list as the assertion that the derivation still agrees with the tree.
UNPREFIXED = (
    "app_user",
    "contest_challenges",
    "contest_details",
    "contest_submissions",
    "lesson_AccessControlMatrix_users",
    "lesson_BusinessLayerAccessControl_users",
    "lesson_NumericSQLInjection_weather",
    "lesson_XSS2_messages",
)


def split_tables(want):
    """(prefixed, unprefixed) table names, from the recording."""
    pre = sorted(t for t in want["tables"] if t.startswith(PREFIX))
    un = sorted(t for t in want["tables"] if not t.startswith(PREFIX))
    return pre, un


def read_file(root=deploy.APP_ROOT, rel=SCHEMA_FILE):
    """(text, note). None when the candidate did not write one."""
    path = os.path.join(root, rel)
    if not os.path.exists(path):
        return None, "no %s" % rel
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError as e:
        return None, "cannot read %s: %s" % (rel, e)
    return text, "%s, %d bytes" % (rel, len(text))


# ------------------------------------------------------------ the database --

def _psql(sql, database="postgres", port=deploy.PG_PORT,
          sockdir=deploy.PG_RUNDIR, timeout=120):
    """One statement of the *scorer's* own SQL, as `postgres`. (rc, out, err).

    Nothing a candidate wrote ever comes through here, and the signature no
    longer lets it: there is no way to hand this function a file. psql reads
    backslash meta-commands out of whatever it is given -- `\\!` runs a shell,
    `\\copy ... from program` runs a shell, `\\o |` runs a shell -- and no flag
    turns that off, so a candidate's file reaching psql is a candidate's file
    being executed by the account psql runs as. `install()` applies candidate
    SQL statement by statement over a library connection instead, as an
    unprivileged role.

    `-t -A` because the only outputs read here are single values.
    """
    cmd = ["psql", "--no-psqlrc", "-X", "-q", "-t", "-A",
           "-v", "ON_ERROR_STOP=0",
           "-h", sockdir, "-p", str(port), "-U", deploy.PG_USER,
           "-d", database, "-c", sql]
    p = deploy.run(deploy.as_user(deploy.PG_USER, cmd), timeout=timeout)
    return (p.returncode,
            p.stdout.decode("utf-8", "replace"),
            p.stderr.decode("utf-8", "replace"))


def _literal(value):
    return "'%s'" % str(value).replace("'", "''")


class _Broken(Exception):
    """The session could not be set up, so no statement of the file ran.

    Raised from inside `_apply` and turned into `install`'s `(None, None,
    why)` by the caller, which is the shape every family here already reads
    as "the scorer was broken, do not blame the file".
    """


# The predefined roles that hand out the two capabilities a non-superuser
# would otherwise not have: running a program on the server, and reading or
# writing a file on it. Membership in any of them turns `COPY ... FROM
# PROGRAM` or `COPY ... FROM '/etc/shadow'` back on.
_DANGEROUS_ROLES = ("pg_execute_server_program", "pg_read_server_files",
                    "pg_write_server_files")

# Attributes are not the whole of it, and reading only `pg_roles` is how a
# privileged role passes for an unprivileged one. `SET ROLE x` asks for
# *membership* in x, not for any attribute on the role asking, and the
# superuser-ness that follows is x's: a role that is `NOSUPERUSER` and a
# member of `postgres` is a superuser one statement later. What has to be
# true instead is that nothing reachable from the role carries any of the
# attributes and none of the predefined roles above is reachable either --
# reachable meaning by membership, transitively, which is the question
# `pg_has_role(..., 'MEMBER')` answers. A role is a member of itself, so this
# subsumes the direct test on its own attributes.
_UNPRIVILEGED = (
    " AND NOT EXISTS (SELECT 1 FROM pg_roles reachable"
    " WHERE pg_has_role(pg_roles.oid, reachable.oid, 'MEMBER')"
    " AND (reachable.rolsuper OR reachable.rolcreaterole"
    " OR reachable.rolcreatedb OR reachable.rolreplication"
    " OR reachable.rolbypassrls OR reachable.rolname IN (%s)))"
    % ",".join(_literal(r) for r in _DANGEROUS_ROLES))


def ensure_install_role(port=deploy.PG_PORT, sockdir=deploy.PG_RUNDIR):
    """Create `deploy.PG_INSTALL_USER`, and hold it to what it is for.

    Every run, not only the first, and by `ALTER` rather than by `CREATE ... IF
    NOT EXISTS`: the cluster being scored is the candidate's, they were a
    superuser in it while they worked, and a role of this name that they
    created themselves would otherwise arrive with whatever they gave it.
    Forcing the attributes makes the role the scorer's regardless.

    `RESET ALL` twice for the same reason from the other direction. A
    parameter a superuser attached to the role, or to every role, is applied
    at login and would be in force while a candidate's file runs --
    `session_replication_role = replica` switches off exactly the foreign-key
    enforcement `migrated:install-data` is checking the file's row order
    against. `ALTER ROLE ALL RESET` touches only the cluster-wide default
    entry, not the per-role ones a deployment may legitimately carry.

    Returns None when the cluster was left the way this says, or a one-line
    reason when one of these statements did not run. Not a thing to step
    over: each of them removes a capability the file is about to be read as
    not having, and a hardening statement that silently failed is how a
    scorer ends up certifying the opposite of what it claims. `create_fresh`
    turns the reason into a failed creation, which every caller already
    fails closed on.
    """
    role = pgnames.quote_ident(deploy.PG_INSTALL_USER)
    lit = _literal(deploy.PG_INSTALL_USER)
    failed = []

    def force(sql):
        rc, _, err = _psql(sql, port=port, sockdir=sockdir)
        if rc != 0:
            failed.append(" ".join(err.split())[:160] or sql[:60])

    force("DO $do$ BEGIN"
          " IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = %s)"
          " THEN CREATE ROLE %s; END IF; END $do$" % (lit, role))
    force("ALTER ROLE %s NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION"
          " NOBYPASSRLS INHERIT LOGIN" % role)
    force("ALTER ROLE %s RESET ALL" % role)
    force("ALTER ROLE ALL RESET session_replication_role")
    # Superuser is an attribute; `SET ROLE postgres` is a *membership*, and
    # none of the attributes above stops it. The candidate was a superuser in
    # this cluster while they worked, so `GRANT postgres TO wg_install` is a
    # thing that may have happened, and a file that opens `SET ROLE postgres`
    # would then have back everything the lines above take away. Every
    # membership the role holds is revoked rather than a list of the
    # dangerous ones, because that list cannot be complete: `postgres` is
    # only the obvious member of it, any role a candidate created with
    # SUPERUSER or CREATEROLE is another, and the role needs no membership at
    # all until `create_fresh` grants it the one it installs under, below.
    force("DO $do$ DECLARE g text; BEGIN"
          " FOR g IN SELECT r.rolname FROM pg_auth_members m"
          "  JOIN pg_roles r ON r.oid = m.roleid"
          "  WHERE m.member = (SELECT oid FROM pg_roles WHERE rolname = %s)"
          " LOOP EXECUTE format('REVOKE %%I FROM %%I', g, %s);"
          " END LOOP; END $do$" % (lit, lit))
    # PostgreSQL 15 made the SET privilege on a configuration parameter
    # grantable, which is a third road to the same place and one that leaves
    # neither an attribute nor a membership behind: `GRANT SET ON PARAMETER
    # session_replication_role TO wg_install` lets an ordinary role switch off
    # the constraint enforcement this gate reads a file against. Every grant
    # of that shape in the cluster is removed, for the same reason the
    # memberships are. Custom parameters (`extension.name`) are left alone:
    # they cannot change what the server does with a statement, and their
    # name is two identifiers rather than one. `pg_parameter_acl` holds a row
    # only for a parameter somebody granted on, so on a cluster nobody
    # touched this loop does nothing at all.
    force("DO $do$ DECLARE p text; g text; BEGIN"
          " FOR p IN SELECT parname FROM pg_parameter_acl"
          "  WHERE strpos(parname, '.') = 0 LOOP"
          "  EXECUTE format('REVOKE SET, ALTER SYSTEM ON PARAMETER %I"
          " FROM PUBLIC', p);"
          "  FOR g IN SELECT rolname FROM pg_roles LOOP"
          "   EXECUTE format('REVOKE SET, ALTER SYSTEM ON PARAMETER %I"
          " FROM %I', p, g);"
          "  END LOOP;"
          " END LOOP; END $do$")

    if failed:
        return ("the scorer could not hold %s to an unprivileged role: %s"
                % (deploy.PG_INSTALL_USER, failed[0]))
    return None


def app_owner():
    """The role the application connects as, if the tree names one."""
    dsn = deploy.app_dsn()
    user = (dsn or {}).get("user") or deploy.APP_DB_USER
    return user


def scratch_owner(owner=None, port=deploy.PG_PORT, sockdir=deploy.PG_RUNDIR):
    """Who a scratch database should belong to, and be installed into.

    The application's own role where the tree names one that exists and is
    unprivileged, because that is the account the deployment's files are meant
    to install under and it is what makes a `pg_dump`-shaped schema file's
    `ALTER TABLE ... OWNER TO` land. `deploy.PG_INSTALL_USER` otherwise -- and
    "otherwise" includes the case that matters: a role the candidate created
    with SUPERUSER, or with membership in a role that reads files or runs
    programs, and then pointed the application's configuration at. Installing
    as that role would hand the whole of `postgres` back, so the check is on
    the attributes rather than on the name.
    """
    name = deploy.PG_INSTALL_USER
    owner = owner if owner is not None else app_owner()
    if owner and owner != deploy.PG_USER and owner != name:
        rc, out, _ = _psql("SELECT rolname FROM pg_roles WHERE rolname = %s%s"
                           % (_literal(owner), _UNPRIVILEGED),
                           port=port, sockdir=sockdir)
        if rc == 0 and out.strip() == owner:
            return owner
    return name


def create_fresh(database, owner):
    """Empty, UTF-8, C locale, and owned by whoever will install into it.

    `template0` because `template1` may carry whatever the candidate put in
    it, and a grader's database that inherited the candidate's extensions
    would be measuring the cluster rather than the file.

    The owner is the application's own role where `scratch_owner` says that is
    safe, so the file does not need superuser -- `citext` is a trusted
    extension and a database owner may create it. `PG_INSTALL_USER` is made a
    member of it either way, because that is the role `install()` connects as
    and it has to be able to `SET ROLE` to the owner to create objects under
    the name the file expects.
    """
    live = (deploy.app_dsn() or {}).get("database")
    if database in (live, deploy.APP_DB) or database in deploy.SYSTEM_DATABASES:
        raise ValueError("refusing to recreate %r: it is a live database"
                         % database)
    why = ensure_install_role()
    if why is not None:
        return 1, why
    _psql('DROP DATABASE IF EXISTS %s' % pgnames.quote_ident(database))
    rc, out, err = _psql(
        "CREATE DATABASE %s TEMPLATE template0 ENCODING 'UTF8'"
        " LC_COLLATE 'C' LC_CTYPE 'C'"
        % pgnames.quote_ident(database))
    if rc != 0:
        return rc, err
    # `template0` is the pristine template by convention, not by
    # construction: a superuser can set `datallowconn` on it, leave a
    # SECURITY DEFINER function owned by `postgres` inside, and put the flag
    # back, and every database created from it afterwards carries the plant
    # -- a file that calls the function then runs its body as `postgres`.
    # Rather than inspecting the template, the copy is inspected, which costs
    # one query and catches whatever the template carried: a database cloned
    # from an untouched `template0` owns no object of its own at all, because
    # everything `initdb` puts there is numbered below the first ordinary
    # OID.
    rc2, out, err2 = _psql(
        "SELECT count(*) FROM ("
        "  SELECT oid FROM pg_class WHERE oid >= 16384"
        "  UNION ALL SELECT oid FROM pg_proc WHERE oid >= 16384"
        "  UNION ALL SELECT oid FROM pg_namespace WHERE oid >= 16384"
        "  UNION ALL SELECT oid FROM pg_type WHERE oid >= 16384"
        "  UNION ALL SELECT oid FROM pg_extension WHERE oid >= 16384"
        "  UNION ALL SELECT oid FROM pg_event_trigger WHERE oid >= 16384"
        ") planted", database=database)
    if rc2 != 0:
        return rc2, err2
    if out.strip() not in ("0",):
        return 1, ("this cluster's template0 is not empty: a database just"
                   " created from it already contains %s object(s) of its"
                   " own, so nothing installed into it would be being read"
                   " on its own merits" % out.strip())
    inst = pgnames.quote_ident(deploy.PG_INSTALL_USER)
    eff = scratch_owner(owner)
    q = pgnames.quote_ident(eff)
    _psql('ALTER DATABASE %s OWNER TO %s'
          % (pgnames.quote_ident(database), q))
    _psql('GRANT ALL ON SCHEMA public TO %s' % q, database=database)
    if eff != deploy.PG_INSTALL_USER:
        _psql('GRANT %s TO %s' % (q, inst))
        _psql('GRANT ALL ON SCHEMA public TO %s' % inst, database=database)
    return rc, err


# psycopg2 refuses `COPY ... FROM STDIN` and `COPY ... TO STDOUT` before
# sending them: the copy protocol is a client/server exchange, not a statement,
# and there is no data on the wire for it to exchange. Its own message names a
# Python method, which is both useless to a candidate and a statement about the
# scorer rather than about their file. What their file actually has wrong is
# that the framework cannot install it either -- `InitializeData()` hands each
# piece to `query()`, which performs no copy protocol either -- so that is what
# is reported instead.
_COPY_NOTE = ("COPY ... FROM STDIN carries its rows in the copy protocol"
              " rather than in the file, and the framework installs this file"
              " one statement at a time through query(), which does not speak"
              " it; write the rows as statements")


def _diagnostic(exc, line):
    """One candidate error, in the shape psql prints one: source, line, text."""
    # By class name rather than by class: this module tolerates a missing
    # driver at import time and checks for one where it connects.
    if (type(exc).__name__ == "ProgrammingError"
            and "COPY" in str(exc) and "copy_" in str(exc)):
        return "sql:%d: ERROR:  %s" % (line, _COPY_NOTE)
    diag = getattr(exc, "diag", None)
    severity = (getattr(diag, "severity_nonlocalized", None)
                or getattr(diag, "severity", None) or "ERROR")
    message = getattr(diag, "message_primary", None) or str(exc)
    detail = getattr(diag, "message_detail", None)
    if detail:
        message = "%s DETAIL: %s" % (message, detail)
    return "sql:%d: %s:  %s" % (line, severity, " ".join(message.split())[:400])


_INSTRUMENT = {}


def _instrument_ok():
    """Has the applier been shown to work, this run, before it is believed?

    Once, and before the first statement is sent anywhere. Two things carry
    everything this module reports: the lexer that cuts the file where the
    server would cut it, and the driver that sends the pieces. A splitter that
    quietly stopped doing its job would turn a correct file into a wall of
    syntax errors; a driver that would not import would turn it into a file
    that "could not connect". Neither is distinguishable downstream from a bad
    file, and both would score a correct candidate zero -- so the instrument is
    checked before the finding, in the run that produces the finding, rather
    than only in the authoring harness.

    The distinction that matters is who is at fault. When this returns a
    reason, `install` returns `(None, None, reason)` and the family says the
    scorer was broken. When it returns `None` and a later connection fails,
    that is reported against the file, because by then the only things left
    that can stop a connection are the cluster and the candidate.
    """
    if "why" not in _INSTRUMENT:
        _INSTRUMENT["why"] = None
        try:
            sqlsplit.selftest()
        except Exception as e:
            _INSTRUMENT["why"] = ("the scorer's SQL splitter failed its own"
                                  " selftest (%s) and no file was applied"
                                  % " ".join(str(e).split())[:200])
        else:
            try:
                import psycopg2                      # noqa: F401
            except Exception as e:
                _INSTRUMENT["why"] = ("the scorer could not load the driver it"
                                      " applies files with (%s) and no file"
                                      " was applied"
                                      % " ".join(str(e).split())[:200])
    return _INSTRUMENT["why"]


def _apply(body, database, limit, port, sockdir):
    """Statement by statement, as `deploy.PG_INSTALL_USER`. (rc, errors, raw).

    This is `InitializeData()`'s own loop: the file is cut into statements,
    each is sent on its own, and a failure is recorded and stepped over rather
    than stopping the rest. `autocommit` is what makes stepping over a failure
    possible at all -- each statement is its own transaction, so statement 40
    still runs after statement 39 raised, which is the behaviour the
    instruction describes and the reason the errors are counted instead of the
    exit status. A file that opens its own `BEGIN` gets the other behaviour,
    from the server, exactly as it would through any client.

    `statement_timeout` is reset before every statement rather than once at
    connection time, because a file that sets it itself -- or a
    `set_config()`, or a rolled-back `SET LOCAL` -- would otherwise be setting
    the scorer's own bound. Reset before each statement, nothing the file does
    to it survives into the next one.
    """
    # Imported here, not at module scope: this module is imported by steps that
    # never apply a file, and a missing driver should not make importing it
    # fail. Outside the `try` on purpose -- a driver that is not installed is a
    # defect in the verifier image, and turning it into a connection error
    # would report it as the candidate's file failing to apply.
    import psycopg2

    deadline = time.time() + limit
    try:
        conn = psycopg2.connect(host=sockdir, port=port,
                                user=deploy.PG_INSTALL_USER, dbname=database,
                                connect_timeout=min(15, max(2, int(limit))))
        conn.autocommit = True
    except Exception as e:
        why = ("could not connect to %s as %s: %s"
               % (database, deploy.PG_INSTALL_USER, " ".join(str(e).split())))
        return 2, ["sql:0: ERROR:  " + why[:400]], why

    errors = []
    statements = sqlsplit.split(body)
    try:
        cur = conn.cursor()
        # Objects the file creates should belong to the database's owner, so
        # that a second file installing into the same database is working with
        # what the first one left. A failure here is not the candidate's --
        # `create_fresh` granted the membership itself -- and it is not
        # survivable either: going on as the installer role would build the
        # schema under a different owner and report everything that then
        # broke against the file. So it is raised, not swallowed.
        owner = scratch_owner(port=port, sockdir=sockdir)
        if owner != deploy.PG_INSTALL_USER:
            try:
                cur.execute("SET ROLE %s" % pgnames.quote_ident(owner))
            except Exception as e:
                raise _Broken("the scorer could not install as %s, which owns"
                              " %s (%s), so no file was applied"
                              % (owner, database,
                                 " ".join(str(e).split())[:200]))
        # A parameter that disables what is being measured must not be in
        # force before the first statement runs. `ensure_install_role` clears
        # the two places it could have been set from; this is the assertion
        # that it did, made from inside the session it would apply to.
        cur.execute("SELECT current_setting('session_replication_role')")
        srr = cur.fetchone()[0]
        if srr != "origin":
            errors.append("sql:0: ERROR:  the session this file would be"
                          " applied in has session_replication_role = %r,"
                          " which switches off the constraint enforcement it"
                          " is being read against" % srr)
            statements = []

        for i, (stmt, line) in enumerate(statements):
            left = deadline - time.time()
            if left <= 0.5:
                errors.append("sql:%d: ERROR:  the scorer's %d-second limit"
                              " for one application of this file ran out with"
                              " %d of its %d statements still to run"
                              % (line, limit, len(statements) - i,
                                 len(statements)))
                break
            try:
                cur.execute("SET statement_timeout = %d"
                            % max(1000, int(left * 1000)))
                # No parameters, ever: psycopg2 only reads `%` as a
                # placeholder when it is given some, and this file is full of
                # them.
                cur.execute(stmt)
            except Exception as e:
                # Server errors and client-side rejections alike: psycopg2
                # refuses a statement carrying a NUL byte before it sends it,
                # and psql would have refused that one statement too and gone
                # on to the next. Only losing the connection stops the file.
                errors.append(_diagnostic(e, line))
                if conn.closed:
                    errors.append("sql:%d: ERROR:  the connection did not"
                                  " survive that, so the rest of the file was"
                                  " not applied" % line)
                    break
    finally:
        try:
            conn.close()
        except Exception:
            pass
    return (1 if errors else 0), errors, "\n".join(errors)


def install(text, database, prefix, port=deploy.PG_PORT,
            sockdir=deploy.PG_RUNDIR):
    """Run the file the way GetSQL() hands it over: PREFIX_ substituted.

    (rc, errors, raw), or (None, None, why) when the shared budget is gone --
    a caller that cannot tell those apart would read an empty error list as a
    clean application, so every one of them checks `rc is None` first.
    """
    broken = _instrument_ok()
    if broken is not None:
        return None, None, broken
    left = budget_left()
    if left <= 1.0:
        return None, None, BUDGET_NOTE
    limit = min(CALL_LIMIT, left)
    body = text.replace("PREFIX_", prefix)
    started = time.time()
    try:
        return _apply(body, database, limit, port, sockdir)
    except _Broken as e:
        return None, None, str(e)
    finally:
        _SPENT["used"] += time.time() - started


def _tables_present(conn, names):
    pgnames.widen_search_path(conn)
    return pgnames.resolve_tables(conn, names)


# ----------------------------------------------------------------- the gate --

def expected_ids(want):
    pre, un = split_tables(want)
    ids = ["install/file-present", "install/applies-cleanly",
           "install/prefix-placeholder-kept"]
    ids += ["install/table/%s" % t for t in sorted(want["tables"])]
    ids += ["install/prefix-moves/%s" % t for t in pre]
    ids += ["install/prefix-keeps/%s" % t for t in un]
    return ids


def drop_database(name, port=deploy.PG_PORT, sockdir=deploy.PG_RUNDIR):
    """Remove one scratch database, whatever is still connected to it.

    FORCE because a family that crashed partway may have left a connection
    open, and a cleanup that a stale backend can veto is not one.
    """
    _psql("DROP DATABASE IF EXISTS %s WITH (FORCE)"
          % pgnames.quote_ident(name), port=port, sockdir=sockdir)


def drop_scratch(port=deploy.PG_PORT, sockdir=deploy.PG_RUNDIR):
    """Leave the cluster the way the candidate had it.

    Neither of these databases is the candidate's, and `deploy.pick_database`
    resolves the application's database partly by looking at what is on the
    cluster. A candidate whose database sorts after `wg_` -- the fallback is
    name order -- could otherwise be scored on the grader's scratch copy of
    their own install file, which would be a green for a tree that never ran.
    `plan.py` runs the live-database families before this one, so that
    ordering is the first defence and this is the second; it is also plain
    hygiene, since a gate that leaves debris behind is a gate whose second run
    on a cluster differs from its first.

    """
    for db in (FLIP_DB, FRESH_DB):
        drop_database(db, port, sockdir)


def evaluate(root=deploy.APP_ROOT, fixture=schemagate.FIXTURE,
             port=deploy.PG_PORT, sockdir=deploy.PG_RUNDIR,
             extra_columns=None):
    """(install family, fresh-schema family, fresh-semantics family).

    Three families rather than one, because they answer three different
    questions and `plan.py` weighs them differently. All three are returned
    even when the file is missing -- the schema and semantics families then
    fail wholesale, which is the same rule every other gate here follows.

    `extra_columns` is the calling step's widening of `schemagate.TYPE_MAP`,
    passed straight through: the schema this file builds is read by the same
    gate as the live one, so a step that accepts a `bytea` in one has to
    accept it in both or its own deliverable fails one of its guards.
    """
    try:
        return _evaluate(root, fixture, port, sockdir, extra_columns)
    finally:
        drop_scratch(port, sockdir)


def _evaluate(root, fixture, port, sockdir, extra_columns=None):
    try:
        want = schemagate.load(fixture)
    except OSError as e:
        crash = "no recorded schema baseline: %s" % e
        return (gate.crashed("install", crash, 8),
                gate.crashed("install-schema", crash, 8),
                gate.crashed("install-semantics", crash, 8))

    c = gate.Checks("install")
    ids = expected_ids(want)
    pre, un = split_tables(want)
    n_schema = len(schemagate.expected_ids(want))
    # Units, not probes: `semantics.evaluate` scores one check per column, so
    # the denominator held open when there is no file to install has to be the
    # same denominator the live family uses, or the two readings of the same
    # 193 questions would disagree about how many questions there are.
    n_sem = len(semantics.unit_ids(semantics.build_plan(want)))

    # The derivation and the tree have to still agree about which tables the
    # framework prefixes. A migration that renamed one would otherwise move
    # silently from one list to the other.
    if tuple(un) != tuple(sorted(UNPREFIXED)):
        c_note = "prefix split drifted: unprefixed is %s" % (un,)
    else:
        c_note = "%d prefixed, %d unprefixed" % (len(pre), len(un))

    c.note(c_note)
    text, note = read_file(root)
    c.note(note)
    if text is None:
        c.fail_all(ids, note)
        return (c,
                gate.crashed("install-schema", note, n_schema),
                gate.crashed("install-semantics", note, n_sem))

    # The placeholder has to survive: a file with `jf_` baked in installs once
    # and can never be installed under another prefix, which is a capability
    # the framework has and the migration would have removed.
    kept = "PREFIX_" in text
    c.add("install/file-present", True, "")
    c.add("install/prefix-placeholder-kept", kept,
          "" if kept else "the file contains no PREFIX_ placeholder")

    owner = app_owner()
    rc, err = create_fresh(FRESH_DB, owner)
    if rc != 0:
        why = "could not create %s: %s" % (FRESH_DB, err.strip()[:200])
        c.fail_all([i for i in ids if i not in c._ids], why)
        return (c,
                gate.crashed("install-schema", why, n_schema),
                gate.crashed("install-semantics", why, n_sem))

    rc, errors, raw = install(text, FRESH_DB, PREFIX)
    if rc is None:
        c.fail_all([i for i in ids if i not in c._ids], raw)
        return (c,
                gate.crashed("install-schema", raw, n_schema),
                gate.crashed("install-semantics", raw, n_sem))
    c.add("install/applies-cleanly", not errors,
          "" if not errors
          else "%d error(s) from the server, first: %s"
               % (len(errors), errors[0]))
    for line in errors[:10]:
        c.note(line[:200])

    conn = None
    try:
        import psycopg2
        conn = psycopg2.connect(host=sockdir, port=port, user=deploy.PG_USER,
                                dbname=FRESH_DB, connect_timeout=10)
        conn.autocommit = True
    except Exception as e:
        why = "cannot read %s: %s" % (FRESH_DB, e)
        c.fail_all([i for i in ids if i not in c._ids], why)
        return (c,
                gate.crashed("install-schema", why, n_schema),
                gate.crashed("install-semantics", why, n_sem))

    resolved = _tables_present(conn, sorted(want["tables"]))
    for t in sorted(want["tables"]):
        c.add("install/table/%s" % t, resolved[t].found,
              "" if resolved[t].found else "the file created no %r" % t)

    fresh_schema = schemagate.evaluate(conn, fixture, extra_columns)
    fresh_schema.family = "install-schema"
    fresh_semantics = semantics.evaluate(conn, resolved,
                                         family="install-semantics")

    # ------------------------------------------------- the prefix, flipped --
    flip_note = None
    rc, err = create_fresh(FLIP_DB, owner)
    if rc != 0:
        flip_note = "could not create %s: %s" % (FLIP_DB, err.strip()[:200])
    else:
        rc, errors, raw = install(text, FLIP_DB, FLIP_PREFIX)
        if rc is None:
            flip_note = raw
    if flip_note:
        for t in pre:
            c.add("install/prefix-moves/%s" % t, False, flip_note)
        for t in un:
            c.add("install/prefix-keeps/%s" % t, False, flip_note)
    else:
        flip = None
        try:
            import psycopg2
            flip = psycopg2.connect(host=sockdir, port=port,
                                    user=deploy.PG_USER, dbname=FLIP_DB,
                                    connect_timeout=10)
            flip.autocommit = True
            names = [FLIP_PREFIX + t[len(PREFIX):] for t in pre] + list(un)
            moved = _tables_present(flip, names)
            old = _tables_present(flip, pre)
            for t in pre:
                want_name = FLIP_PREFIX + t[len(PREFIX):]
                ok = moved[want_name].found and not old[t].found
                c.add("install/prefix-moves/%s" % t, ok,
                      "" if ok else
                      "with the prefix set to %r the file should create %r and "
                      "not %r (created %s / %s)"
                      % (FLIP_PREFIX, want_name, t,
                         moved[want_name].found, old[t].found))
            for t in un:
                ok = moved[t].found
                c.add("install/prefix-keeps/%s" % t, ok,
                      "" if ok else
                      "%r is not a prefixed table and must be created "
                      "whatever the prefix is" % t)
        except Exception as e:
            why = "cannot read %s: %s" % (FLIP_DB, e)
            for t in pre:
                c.add("install/prefix-moves/%s" % t, False, why)
            for t in un:
                c.add("install/prefix-keeps/%s" % t, False, why)
        finally:
            if flip is not None:
                flip.close()

    conn.close()
    if set(ids) != set(c._ids):
        raise AssertionError(
            "denominator drift: only-planned %s, only-scored %s"
            % (sorted(set(ids) - set(c._ids))[:5],
               sorted(set(c._ids) - set(ids))[:5]))
    return c, fresh_schema, fresh_semantics
