"""Reads the PostgreSQL server log the scorer's own postmaster writes.

Every other instrument in this set reads the tree, the schema, or a response
body. This one reads what the server was *asked to do*, which is the only place
two questions can be answered honestly.

*What SQL did the application actually send?* `sqlgate` answers a neighbouring
question -- what SQL is in the tree -- by folding string expressions with a PHP
parser. That is the stronger instrument for coverage, because it reaches
statements no request happens to run, and it is the weaker one for truth,
because a folded string is a reconstruction. A statement in the server log is
not a reconstruction: it is the text the server parsed, placeholders and all.
A tree can be made to look ported and still send MySQL at runtime -- a shim
that rewrites statements on their way out, a second code path chosen at
runtime -- and only the log tells them apart.

*Which role is the application?* The configuration file names one, but a
configuration file is a claim. `log_connections` reports the role each session
actually authenticated as, and the catalogs say whether that role is a
superuser.

Both depend on settings `deploy` forces on the postmaster command line, which
is the one place that beats postgresql.conf and postgresql.auto.conf. It does
not beat a setting attached to a role or a database, so `enforce()` clears
those first. After that the instrument is either working -- statements appear
-- or it is not, and a family that sees no statements at all fails closed
rather than scoring a silent zero-denominator pass (trap 4).

The log is append-only for the life of a run, so a caller brackets the window
it cares about with `mark()` and reads from there.
"""

import json
import os
import re

from . import deploy
from . import gate

# `%m|%p|%u|%d|@` -- timestamp, pid, user, database, sentinel. User and
# database are empty for the postmaster's own lines, which is how those are
# told apart from a session's.
PREFIX = re.compile(
    r"^(?P<ts>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+ \w+)\|"
    r"(?P<pid>\d*)\|(?P<user>[^|]*)\|(?P<db>[^|]*)\|@"
    r"(?P<sev>[A-Z0-9]+):\s{1,2}(?P<text>.*)$")

# The severities a record can open with. Anything else on a record line is the
# server talking about a record it already emitted.
CONTINUATION = ("DETAIL", "HINT", "CONTEXT", "STATEMENT", "QUERY", "LOCATION")

STATEMENT = re.compile(r"^statement:\s(?P<sql>.*)$", re.S)
EXECUTE = re.compile(r"^execute\s(?:<unnamed>|[^:]+):\s(?P<sql>.*)$", re.S)
AUTHORIZED = re.compile(
    r"^connection authorized: user=(?P<user>\S+) database=(?P<db>\S+)")

# The scorer's own two accounts. Statements they send are the scorer's work --
# reading catalogs, applying the candidate's install SQL -- and are not the
# application's.
SCORER_ROLES = frozenset([deploy.PG_USER, deploy.PG_INSTALL_USER])

# Settings a role- or database-level `SET` could use to switch the instrument
# off underneath the command line.
ERROR_SEVERITIES = ("ERROR", "FATAL", "PANIC")
MAX_SHOWN = 8

GUCS = ("log_statement", "log_connections", "log_destination",
        "log_line_prefix", "log_min_messages", "logging_collector",
        "log_min_error_statement", "log_error_verbosity",
        # Not logging settings, and here for the same reason they are: each is
        # forced on the command line by `deploy.PG_FORCED_SAFETY`, and a
        # command line loses to `ALTER ROLE ... SET`. These are the three
        # entries of that list a per-role `SET` can still reach --
        # `shared_preload_libraries` and `archive_mode` are PGC_POSTMASTER and
        # the command runners are PGC_SIGHUP, and `ALTER ROLE ... SET` refuses
        # all of those outright. Left out, a role-level setting of one of these
        # three loads a library of the candidate's choosing into the
        # application's sessions, or points the loader at a directory the
        # candidate writes.
        "session_preload_libraries",
        "local_preload_libraries",
        "dynamic_library_path")

# The subset of the above that is about logging. The three library settings
# are cleared for safety; setting one says nothing about the stream.
_LIBRARY_GUCS = frozenset(["session_preload_libraries",
                           "local_preload_libraries",
                           "dynamic_library_path"])

# A session `SET` of a logging setting cannot be cleared from outside the
# session -- but it is itself a statement, and `log_statement = all` logs a
# statement before it runs, so the switch that silences the instrument is the
# last thing the instrument records. These are the names whose setting, sent
# by the application, is evidence that what follows is not the whole stream.
#
# Not a check of its own. The instruction never asks a candidate to leave the
# server's statement logging alone -- it is the scorer's instrument, not a
# property of the tree -- so scoring its absence would be a requirement that
# exists only in the checks. What a `SET` of one of these does instead is fail
# the three checks below that read the stream, because a narrowed stream is
# not evidence for them. Same direction the old scored check pointed in, and
# one fewer thing the instruction would have to say.
LOGGING_GUCS = frozenset(GUCS) - _LIBRARY_GUCS


class Record(object):
    """One server log record: its prefix fields and its whole text."""

    __slots__ = ("ts", "pid", "user", "db", "sev", "text")

    def __init__(self, ts, pid, user, db, sev, text):
        self.ts = ts
        self.pid = pid
        self.user = user
        self.db = db
        self.sev = sev
        self.text = text

    @property
    def sql(self):
        """The statement this record reports, or None if it reports no SQL.

        Two shapes, because PDO uses both protocols. `statement: <sql>` is the
        simple query protocol; `execute <name>: <sql>` is an Execute against a
        prepared statement, and the text carries `$1`-style placeholders --
        which is what the grammar should see, since that is what the server
        parsed.
        """
        if self.sev != "LOG":
            return None
        for pat in (STATEMENT, EXECUTE):
            m = pat.match(self.text)
            if m:
                return m.group("sql")
        return None

    @property
    def authorized(self):
        """(user, database) if this record is a successful authentication."""
        if self.sev != "LOG":
            return None
        m = AUTHORIZED.match(self.text)
        return (m.group("user"), m.group("db")) if m else None

    def __repr__(self):
        return "<%s %s@%s %s>" % (self.sev, self.user or "-", self.db or "-",
                                  self.text[:60])


def parse(text):
    """[Record] from raw log text.

    A logged statement is frequently several lines long -- this application
    builds twenty-line `CREATE TABLE`s -- and a continuation line has no
    prefix on it. Lines that do not open a record belong to the record above,
    which is why the prefix ends in a sentinel no SQL line can imitate.
    """
    out = []
    for line in text.splitlines():
        m = PREFIX.match(line)
        if m is None:
            if out:
                out[-1].text += "\n" + line
            continue
        out.append(Record(m.group("ts"), m.group("pid"), m.group("user"),
                          m.group("db"), m.group("sev"), m.group("text")))
    return [r for r in out if r.sev not in CONTINUATION]


def mark(path=None):
    """The current end of the log, to read forward from later.

    The server's output reaches the file through the scorer's own pump
    (`deploy._LogPump`), so the end of the file is the end of the server's
    output only once the pump has caught up; `pg_log_sync` waits for that.
    """
    deploy.pg_log_sync()
    path = path or deploy.PG_LOGFILE
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def read(since=0, path=None):
    """[Record] appended since `since`.

    A log that shrank -- rotated, truncated, replaced -- is read from the
    start rather than from a byte offset that now means something else.
    """
    deploy.pg_log_sync()
    path = path or deploy.PG_LOGFILE
    try:
        with open(path, "rb") as fh:
            size = os.fstat(fh.fileno()).st_size
            fh.seek(0 if size < since else since)
            raw = fh.read()
    except OSError:
        return []
    return parse(raw.decode("utf-8", "replace"))


def app_records(records, extra_roles=()):
    """The records a session of the application's own made.

    "The application's" is defined by exclusion: any role that is not one of
    the scorer's two. A candidate who runs the application as `postgres` is
    invisible here, and that is what `evaluate_app_role` is for -- it fails on
    exactly that tree, so the two families do not both have to catch it.
    """
    skip = set(SCORER_ROLES) | set(extra_roles)
    return [r for r in records if r.user and r.user not in skip]


def statements(records):
    """[(Record, sql)] for every record that reports a statement."""
    out = []
    for r in records:
        sql = r.sql
        if sql is not None:
            out.append((r, sql))
    return out


def sessions(records):
    """{pid: (user, database)} for every session that authenticated."""
    out = {}
    for r in records:
        who = r.authorized
        if who:
            out[r.pid] = who
    return out


# ---------------------------------------------------------------- enforce ---

def enforce(conn):
    """Clear role- and database-level overrides of the logging settings.

    `ALTER ROLE webgoat SET log_statement = 'none'` outranks the postmaster's
    command line for that role's sessions. Nothing in this task asks a
    candidate to set it, and a deployment is not graded differently for having
    it cleared, so the scorer resets it rather than treating its presence as an
    accusation. Returns the list of (role, database, setting) it cleared, for
    the report.
    """
    cleared = []
    cur = conn.cursor()
    cur.execute("""
        SELECT COALESCE(r.rolname, ''), COALESCE(d.datname, ''), s.setconfig
          FROM pg_db_role_setting s
          LEFT JOIN pg_roles r ON r.oid = s.setrole
          LEFT JOIN pg_database d ON d.oid = s.setdatabase
    """)
    rows = cur.fetchall()
    for rolname, datname, setconfig in rows:
        for item in (setconfig or []):
            name = item.split("=", 1)[0].strip().lower()
            if name not in GUCS:
                continue
            target = "ROLE %s" % _ident(rolname) if rolname else "ROLE ALL"
            scope = " IN DATABASE %s" % _ident(datname) if datname else ""
            cur.execute("ALTER %s%s RESET %s" % (target, scope, name))
            cleared.append((rolname or "ALL", datname or "ALL", name))
    conn.commit()
    return cleared


def _ident(name):
    return '"%s"' % name.replace('"', '""')


# ------------------------------------------------------------- the family ---

FIXTURE = "/tests/fixtures/runtime-baseline-04.json"

# The denominator, fixed before anything is read. A family that cannot run at
# all fails all of its checks rather than reporting a mean over an empty set
# (trap 4).
#
# Two families, because they answer two different sentences and fail in two
# different ways. `:166-171` is about *who* the application is to the server,
# and it is answered by the connection records alone. `:48`, `:67` and `:140`
# are about *what it sends*, and they are answered by the statement and error
# stream -- which a session-level setting can still narrow, which is why they
# are worth separating and weighting as their own family rather than diluting
# with four identity checks that a correct deployment passes for free.
IDENTITY_IDS = ("runtime/authenticated",
                "runtime/on-app-database",
                "runtime/role-exists",
                "runtime/not-superuser")
SQL_IDS = ("runtime/statements-observed",
           "runtime/every-scenario",
           "runtime/no-max-generated-key",
           "runtime/no-unrecorded-errors")
CHECK_IDS = IDENTITY_IDS + SQL_IDS


def identity_columns(conn):
    """{column name: {table, ...}} for every generated-key column there is.

    Both spellings, because a schema can reach PostgreSQL either way: an
    identity column (`attidentity`) and an old-style `serial` whose default
    owns a sequence (`pg_get_serial_sequence`). Keyed by column name because
    that is what a statement's `max(...)` names; the table set is what keeps
    `max(rgt)` in the nested-set library from being mistaken for one.
    """
    cur = conn.cursor()
    cur.execute("""
        SELECT lower(c.relname), lower(a.attname)
          FROM pg_attribute a
          JOIN pg_class c ON c.oid = a.attrelid
          JOIN pg_namespace n ON n.oid = c.relnamespace
         WHERE a.attnum > 0 AND NOT a.attisdropped
           AND c.relkind = 'r'
           AND n.nspname NOT IN ('pg_catalog', 'information_schema')
           AND (a.attidentity <> ''
                OR pg_get_serial_sequence(
                       quote_ident(n.nspname) || '.' || quote_ident(c.relname),
                       a.attname) IS NOT NULL)
    """)
    out = {}
    for table, col in cur.fetchall():
        out.setdefault(col, set()).add(table)
    return out


def max_over_generated_key(sqls, idcols):
    """[(sql, table, column)] for statements that read a key as `max(col)`.

    `:122-126` of the instruction: what an insert reads back must be the key
    the server assigned on that connection, "not the largest key in the
    table". On a corpus that is one connection at a time and strictly
    sequential, `SELECT max(id)` and `RETURNING id` return the same number for
    every request, so no comparison of responses can tell them apart -- the
    difference is only visible in what was sent. This is the instrument for
    that sentence and the only one in the set.
    """
    from pglast.parser import parse_sql_json
    from . import sqlgate
    hits = []
    for sql in sqls:
        try:
            tree = json.loads(parse_sql_json(sql))
        except Exception:
            continue          # the server accepted it; our copy of the
            # grammar disagreeing is not the candidate's failure
        nodes = list(sqlgate._walk(tree))
        tables = set()
        called = set()
        for n in nodes:
            if "RangeVar" in n and n["RangeVar"].get("relname"):
                tables.add(n["RangeVar"]["relname"].lower())
            fc = n.get("FuncCall")
            if fc:
                called.add(sqlgate._last(fc.get("funcname")) or "")
        # `setval(pg_get_serial_sequence(...), max(id))` is repositioning a
        # sequence, which is what step 03 asked for and what a ported lesson
        # `reset()` may legitimately do after rebuilding its own table -- it is
        # not an insert reading its key back, which is the only thing `:140`
        # forbids. Measured on the pinned tree: no application statement reads
        # `max(<identity column>)` at all, so the exclusion costs no coverage
        # and removes a way a correct migration fails an earned check.
        if "setval" in called:
            continue
        for n in nodes:
            fc = n.get("FuncCall")
            if not fc or sqlgate._last(fc.get("funcname")) != "max":
                continue
            for arg in fc.get("args") or ():
                col = _colname(arg)
                if col and col in idcols and (idcols[col] & tables):
                    hits.append((sql, sorted(idcols[col] & tables)[0], col))
    return hits


def _colname(arg):
    """The bare column name a `max(...)` argument refers to, if it is one."""
    ref = arg.get("ColumnRef") if isinstance(arg, dict) else None
    if not ref:
        return None
    parts = [f["String"]["sval"] for f in ref.get("fields") or ()
             if "String" in f]
    return parts[-1].lower() if parts else None


def _set_variable(sql):
    """The parameter name a `SET`/`ALTER ... SET` statement sets, lowercased.

    Parsed rather than matched, so `set   LOG_STATEMENT='none'`, a leading
    comment and `SET SESSION log_statement TO DEFAULT` are all the same
    statement to this, and a string literal that merely contains the words is
    not one. `None` when the statement is not setting a parameter -- including
    when our copy of the grammar cannot read it, which is the server's problem
    and not the candidate's (`max_over_generated_key` says the same).
    """
    from pglast.parser import parse_sql_json
    from . import sqlgate
    try:
        tree = json.loads(parse_sql_json(sql))
    except Exception:
        return None
    for n in sqlgate._walk(tree):
        stmt = n.get("VariableSetStmt")
        # Only a set *to a value*. `RESET x` and `SET x TO DEFAULT` restore
        # what the postmaster's command line says, which is the setting this
        # deployment forced, so neither can narrow the stream -- measured:
        # pglast reports them as VAR_RESET and VAR_SET_DEFAULT, and
        # `VAR_SET_VALUE` is the only kind that can carry `none`.
        if (isinstance(stmt, dict) and stmt.get("name")
                and stmt.get("kind") == "VAR_SET_VALUE"):
            return str(stmt["name"]).lower()
        # The same thing said as a function call. `SELECT set_config(
        # 'log_statement', 'none', false)` is not a `VariableSetStmt` and
        # narrows the session exactly as `SET` does, so the grammar is asked
        # about it too. Only a literal first argument: a name computed at
        # runtime is one this cannot read, and it is the same "our copy of the
        # grammar cannot read it" case as an unparseable statement.
        fn = n.get("FuncCall")
        if isinstance(fn, dict) and sqlgate._last(fn.get("funcname")) \
                == "set_config":
            args = fn.get("args") or ()
            if args:
                const = (args[0] or {}).get("A_Const") or {}
                sval = const.get("sval") or {}
                if isinstance(sval, dict) and sval.get("sval"):
                    return str(sval["sval"]).lower()
    return None


def scenario_slices(path, windows):
    """{scenario: [Record]} from the byte windows the recorder marked."""
    out = {}
    deploy.pg_log_sync()
    try:
        fh = open(path or deploy.PG_LOGFILE, "rb")
    except OSError:
        return {name: [] for name, _a, _b in windows}
    try:
        for name, start, end in windows:
            fh.seek(start or 0)
            raw = fh.read(max(0, (end or 0) - (start or 0)))
            out[name] = parse(raw.decode("utf-8", "replace"))
    finally:
        fh.close()
    return out


def families(conn, windows, fixture=FIXTURE, path=None, database=None,
             names=("migrated:runtime-identity", "migrated:runtime-sql")):
    """The two earned families this log supports, from one reading of it.

    Both are false on the tree this step is handed, where the application
    never opens a PostgreSQL connection at all -- so both are earned families
    and neither is a guard, whatever their checks look like in isolation.

    `runtime-identity` is `:166-171`: the application authenticated, against
    the application's database, as a role that exists and is not a superuser.
    Four checks, and a configuration file cannot evidence any of them.

    `runtime-sql` is what the application *sent*: that the instrument saw
    anything at all, that every scenario contributed, that no generated key
    was read as the largest value in its column, and that no statement was
    refused in a scenario the recorded tree sends cleanly. Four checks, and
    each is something no comparison of responses can produce. A session that
    narrows the stream from inside is not a fifth check -- the instruction
    says nothing about the server's logging -- it fails the three of these
    that read the stream, because a narrowed stream is not evidence for them.

    Split because they fail for different reasons and are worth different
    amounts. A correct deployment passes the four identity checks the moment
    it connects; the four below are where a compatibility shim shows.
    """
    whole = _evaluate(conn, windows, fixture=fixture, path=path,
                      database=database)
    out = []
    for name, ids in zip(names, (IDENTITY_IDS, SQL_IDS)):
        c = gate.Checks(name)
        if whole.error is not None:
            c.fail_all(list(ids), "the runtime log could not be read")
            c.error = whole.error
            out.append(c)
            continue
        by_id = {i: (ok, d) for i, ok, d in whole.results}
        for i in ids:
            ok, d = by_id.get(i, (False, "not evaluated"))
            c.add(i, ok, d)
        for text in whole.notes:
            c.note(text)
        out.append(c)
    return out


def _evaluate(conn, windows, fixture=FIXTURE, path=None,
              database=None, family="migrated:runtime"):
    """Every check both families draw from, scored once against one reading.

    Not a family itself: nothing weighs the nine together, and `families()`
    is what a plan calls.
    """
    c = gate.Checks(family)
    database = database or deploy.APP_DB
    try:
        want = json.load(open(fixture)) if isinstance(fixture, str) else fixture
    except (OSError, ValueError) as e:
        return gate.crashed(family, "no recorded runtime baseline: %s" % e,
                            len(CHECK_IDS))
    # Trap 6: the instrument before the finding. Both families here are read
    # out of one text file, and every way this can be wrong -- a prefix the
    # parser stops matching, a continuation line promoted to a record, a
    # suppressor spelled in a form the grammar layer does not claim -- is
    # silent and green. So the parser and the suppressor detector are run
    # against a known answer first, and neither family scores if they miss it.
    try:
        selftest()
    except Exception as e:
        return gate.crashed(family, "the scorer's runtime-log instrument"
                            " failed its own selftest (%s) and nothing the"
                            " server was sent was judged"
                            % " ".join(str(e).split())[:300], len(CHECK_IDS))

    slices = scenario_slices(path, windows)
    records = [r for name in slices for r in slices[name]]

    # -- who connected ----------------------------------------------------
    seen = {}
    for r in records:
        who = r.authorized
        if who:
            seen.setdefault(who[0], set()).add(who[1])
    app_roles = sorted(u for u in seen if u not in SCORER_ROLES)

    any_app = bool(app_roles)
    c.add("runtime/authenticated", any_app,
          "" if any_app else
          "no session authenticated as anything but the scorer's own accounts"
          " (%s) while the corpus ran: either the application never reached"
          " PostgreSQL, or it reached it as the scorer's role"
          % ", ".join(sorted(SCORER_ROLES)))

    on_db = sorted(u for u in app_roles if database in seen[u])
    c.add("runtime/on-app-database", bool(on_db),
          "" if on_db else
          "no non-scorer role authenticated against %r; roles seen: %s"
          % (database, ", ".join("%s->%s" % (u, ",".join(sorted(seen[u])))
                                 for u in sorted(seen)) or "none"))

    try:
        cur = conn.cursor()
        cur.execute("SELECT rolname, rolsuper FROM pg_roles")
        attrs = {r[0]: r[1] for r in cur.fetchall()}
    except Exception as e:
        attrs = None
        c.fail_all(["runtime/role-exists", "runtime/not-superuser"],
                   "could not read pg_roles: %s"
                   % " ".join(str(e).split())[:200])
    if attrs is not None:
        missing = [u for u in app_roles if u not in attrs]
        c.add("runtime/role-exists", any_app and not missing,
              "" if (any_app and not missing) else
              ("no application session to check" if not any_app else
               "role(s) authenticated but absent from pg_roles: %s"
               % ", ".join(missing)))
        supers = [u for u in app_roles if attrs.get(u, True)]
        c.add("runtime/not-superuser", any_app and not supers,
              "" if (any_app and not supers) else
              ("no application session to check" if not any_app else
               "the application connects as a superuser: %s"
               % ", ".join(supers)))

    # -- what it sent -----------------------------------------------------
    per_scenario = {}
    sqls = set()
    for name, recs in slices.items():
        st = statements(app_records(recs))
        per_scenario[name] = len(st)
        for _r, sql in st:
            sqls.add(sql)

    total = sum(per_scenario.values())
    c.add("runtime/statements-observed", total > 0,
          "" if total else
          "the server logged no statement from any non-scorer role during the"
          " whole corpus")

    # The one evasion the command line cannot reach. `enforce()` clears a
    # role- or database-level setting of these names, and the postmaster's own
    # command line beats `postgresql.conf` and `postgresql.auto.conf` -- but a
    # session that says `SET log_statement = none` after it connects silences
    # the rest of its own traffic, and nothing outside that session can undo
    # it. What it cannot do is hide the switch: `log_statement = all` is still
    # in force when the `SET` arrives, so the statement that turns the
    # instrument off is the last thing the instrument writes down.
    #
    # Not scored on its own. The instruction asks nothing about the server's
    # statement logging -- it is this family's instrument, not a property of
    # the migrated tree -- and a check whose subject the instruction never
    # names is a requirement invented by the scorer. So a `SET` is read for
    # exactly what it is: evidence that the stream the three checks below read
    # is not the whole stream, which makes it no evidence for them. They fail
    # closed, and the report says why.
    suppressors = []
    for _r, sql in [(r, q) for name in slices
                    for (r, q) in statements(app_records(slices[name]))]:
        name = _set_variable(sql)
        if name in LOGGING_GUCS:
            suppressors.append(" ".join(sql.split())[:90])
    narrowed = ""
    if suppressors:
        narrowed = ("; and %d statement(s) of the application's set a logging"
                    " parameter for their own session, so what the server"
                    " recorded after them is not the whole stream: %s"
                    % (len(suppressors),
                       "; ".join(sorted(set(suppressors))[:MAX_SHOWN])))
        c.note("statement logging was narrowed from inside %d application"
               " session statement(s); the three stream checks fail closed"
               % len(suppressors))

    silent = sorted(n for n, k in per_scenario.items() if k == 0)
    want_scenarios = int(want.get("scenarios", len(per_scenario)))
    covered = (not silent) and len(per_scenario) >= want_scenarios
    c.add("runtime/every-scenario", covered and not narrowed,
          "" if (covered and not narrowed) else
          "%d of %d scenario(s) sent the server nothing: %s%s"
          % (len(silent) + max(0, want_scenarios - len(per_scenario)),
             want_scenarios, ", ".join(silent[:MAX_SHOWN]) or "(none)",
             narrowed))

    # -- how it read back a generated key ---------------------------------
    if total == 0 or narrowed:
        c.add("runtime/no-max-generated-key", False,
              "nothing was sent, so nothing about generated keys was shown"
              if not total else
              "the statements this would have read are not the whole stream%s"
              % narrowed)
    else:
        try:
            idcols = identity_columns(conn)
        except Exception as e:
            idcols = None
            c.add("runtime/no-max-generated-key", False,
                  "could not read the generated-key columns: %s"
                  % " ".join(str(e).split())[:200])
        if idcols is not None:
            hits = max_over_generated_key(sqls, idcols)
            c.add("runtime/no-max-generated-key", not hits,
                  "" if not hits else
                  "%d statement(s) read a generated key as the largest value"
                  " in the column rather than the key this connection was"
                  " given: %s"
                  % (len(hits), "; ".join("%s.%s in %r"
                                          % (t, col,
                                             " ".join(sql.split())[:90])
                                          for sql, t, col
                                          in hits[:MAX_SHOWN])))

    # -- what the server refused ------------------------------------------
    errored = set()
    detail = {}
    for name, recs in slices.items():
        for r in recs:
            if r.sev in ERROR_SEVERITIES and r.user and r.user not in SCORER_ROLES:
                errored.add(name)
                detail.setdefault(name, " ".join(r.text.split())[:120])
    # One-sided, and the direction is the point. What the instruction requires
    # is that every statement this application builds is one PostgreSQL 17
    # would run, so a scenario the server refused a statement in and the
    # recording does not is direct evidence against a stated requirement --
    # and evidence no response comparison can produce, because the page above
    # a swallowed error can be byte-identical.
    #
    # The other direction is deliberately not asked. One of the recorded
    # scenarios (`lesson-blac-collation`) compares an integer column against
    # the empty string the request supplied: MySQL coerced it, PostgreSQL
    # refuses it, and the page is byte-identical either way because the lesson
    # shows nothing for an empty result. The golden tree leaves it refused. A
    # candidate who instead makes that statement run has broken nothing, and
    # failing them for it would be scoring the golden tree's choice about an
    # uncoercible literal as though the instruction had asked for it. Fewer
    # refusals than the recording is never worse than the recording.
    baseline = set(want.get("error_scenarios", ()))
    extra = sorted(errored - baseline)
    gone = sorted(baseline - errored)
    # An empty error stream is not evidence of well-formed SQL when there was
    # no SQL. A tree whose application never reaches PostgreSQL logs nothing at
    # all, and reading that as a pass is trap 4 wearing a check's clothes --
    # measured on the step-03 floor tree, where this was the one check of the
    # nine that passed, and it passed because the application was not there.
    # `runtime/no-max-generated-key` above is gated on the same `total`, for
    # the same reason: an unevaluable check is a failure, never a skip.
    if total == 0 or narrowed:
        c.add("runtime/no-unrecorded-errors", False,
              "nothing was sent, so no statement of the application's was"
              " shown to be one this server would run" if not total else
              "the errors this would have read are not the whole stream%s"
              % narrowed)
    else:
        c.add("runtime/no-unrecorded-errors", not extra,
              "" if not extra else
              "the server refused a statement from the application in %d"
              " scenario(s) it accepts everything in on the recorded tree: %s"
              % (len(extra),
                 "; ".join("%s (%s)" % (n, detail[n]) for n in extra[:4])))
    if gone:
        c.note("the recording is refused in %d scenario(s) this tree is not,"
               " which is allowed and not scored: %s"
               % (len(gone), ", ".join(gone[:6])))

    c.note("%d statement(s) from %d application session(s); roles: %s"
           % (total, len(sessions(records)),
              ", ".join("%s (%s)" % (u, ",".join(sorted(seen[u])))
                        for u in sorted(seen)) or "none"))
    c.note("server refused statements in: %s"
           % (", ".join(sorted(errored)) or "no scenario"))
    return c


# --------------------------------------------------------------- selftest ---

class SelfTestError(Exception):
    pass


# One synthetic reading of the log, written in the prefix `deploy` forces.
# Every shape the parser has to get right is in here once: the postmaster's
# own lines, which carry no role; a scorer session, which is excluded by role;
# an application session; a statement whose text runs over three lines with no
# prefix on the second and third; an Execute against a prepared statement,
# which is the other protocol PDO uses; an `ERROR` with the `STATEMENT` and
# `DETAIL` continuations the server emits after it, which are records to the
# regex and not records to this module.
_SELFTEST_LOG = """\
2026-01-01 00:00:00.000 UTC|101|||@LOG:  database system is ready
2026-01-01 00:00:01.000 UTC|201|postgres|webgoat|@LOG:  connection authorized: user=postgres database=webgoat
2026-01-01 00:00:01.100 UTC|201|postgres|webgoat|@LOG:  statement: SELECT 1 FROM pg_class
2026-01-01 00:00:02.000 UTC|301|webgoat|webgoat|@LOG:  connection authorized: user=webgoat database=webgoat
2026-01-01 00:00:02.100 UTC|301|webgoat|webgoat|@LOG:  statement: SELECT *
  FROM users
 WHERE id = 1
2026-01-01 00:00:02.200 UTC|301|webgoat|webgoat|@LOG:  execute <unnamed>: SELECT * FROM lessons WHERE id = $1
2026-01-01 00:00:02.300 UTC|301|webgoat|webgoat|@ERROR:  relation "nope" does not exist at character 15
2026-01-01 00:00:02.300 UTC|301|webgoat|webgoat|@STATEMENT:  SELECT * FROM nope
2026-01-01 00:00:02.301 UTC|301|webgoat|webgoat|@DETAIL:  there is no such relation
"""

# (statement, is it a suppressor). The false half matters as much as the true
# half: a `RESET` restores what the command line forced, a `SET search_path`
# is ordinary application traffic, and a name this cannot read is not an
# accusation. Any of them read as a suppressor would fail three checks on a
# correct tree.
_SELFTEST_SETS = (
    ("SET log_statement = none", True),
    ("set   LOG_STATEMENT='none'", True),
    ("SET SESSION log_min_messages TO 'panic'", True),
    ("SELECT set_config('log_statement','none',false)", True),
    ("SELECT pg_catalog.set_config('log_destination', '', false)", True),
    ("SET SESSION log_statement TO DEFAULT", False),
    ("RESET log_statement", False),
    ("SET search_path = public", False),
    ("SET local_preload_libraries = 'x'", False),
    ("SELECT set_config(colname, 'none', false)", False),
    ("SELECT 'SET log_statement = none' AS advice", False),
    ("SELECT * FROM users WHERE name = 'log_statement'", False),
)


def selftest():
    """Prove the reader against a known answer before it reads anything.

    Two layers, because both are silent when they break. The parser: a prefix
    that stopped matching turns every record into a continuation of the one
    above and the stream goes quiet, which reads as an application that sent
    nothing -- and `runtime/statements-observed` would call that out, but only
    if the *other* half of the run were also quiet. And the suppressor
    detector, which is the one thing here that decides a check fails without a
    fixture behind it.

    Raises SelfTestError naming everything that came out wrong. Returns a
    summary dict on success.
    """
    wrong = []
    if deploy.PG_LOG_LINE_PREFIX != "%m|%p|%u|%d|@":
        wrong.append("the forced log_line_prefix is %r, which is not the one"
                     " PREFIX reads" % deploy.PG_LOG_LINE_PREFIX)

    recs = parse(_SELFTEST_LOG)
    # The two continuation severities the fixture carries, named here rather
    # than read out of `CONTINUATION`: a test that asks the constant it is
    # testing what the right answer is still passes when that constant is
    # emptied. The count is pinned for the same reason -- it is the one
    # assertion a parser that promotes continuations cannot satisfy.
    bad_sev = sorted(set(r.sev for r in recs) & set(["STATEMENT", "DETAIL"]))
    if bad_sev:
        wrong.append("continuation line(s) survived as records: %s"
                     % ", ".join(bad_sev))
    if len(recs) != 7:
        wrong.append("parse() read %d record(s) from the fixture, expected 7"
                     % len(recs))

    got = sessions(recs)
    want_sessions = {"201": ("postgres", "webgoat"),
                     "301": ("webgoat", "webgoat")}
    if got != want_sessions:
        wrong.append("sessions() read %r, expected %r" % (got, want_sessions))

    app = app_records(recs, extra_roles=())
    if any(r.user in SCORER_ROLES for r in app):
        wrong.append("app_records() kept a scorer session")
    if any(not r.user for r in app):
        wrong.append("app_records() kept a postmaster line")

    sqls = [q for _r, q in statements(app)]
    if len(sqls) != 2:
        wrong.append("statements() found %d statement(s) in the application's"
                     " records, expected 2: %r" % (len(sqls), sqls))
    else:
        if "\n" not in sqls[0] or "WHERE id = 1" not in sqls[0]:
            wrong.append("the three-line statement was not rejoined: %r"
                         % sqls[0])
        if "$1" not in sqls[1]:
            wrong.append("the Execute form was not read: %r" % sqls[1])

    errs = [r for r in app if r.sev in ERROR_SEVERITIES]
    if len(errs) != 1:
        wrong.append("%d error record(s) in the application's records,"
                     " expected 1" % len(errs))

    for sql, want_hit in _SELFTEST_SETS:
        hit = _set_variable(sql) in LOGGING_GUCS
        if hit != want_hit:
            wrong.append("%r read as %s"
                         % (sql, "a suppressor" if hit else "not a suppressor"))

    if wrong:
        raise SelfTestError("; ".join(wrong))
    return {"records": len(recs), "app_statements": len(sqls),
            "set_cases": len(_SELFTEST_SETS)}
