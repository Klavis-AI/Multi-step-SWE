"""Per-table database state in a form that MySQL and PostgreSQL can be
compared in.

The comparison this task needs is not "same bytes": the whole point of the
migration is that latin1 bytes become UTF-8. What must be preserved is the
*information* — the sequence of Unicode code points the application sees. So a
canonical row is built by decoding each value with its own column's declared
character set:

    MySQL latin1 column -> wglib.mysqllatin1.decode (CP1252 + 5, see that module)
    MySQL utf8 column   -> UTF-8
    PostgreSQL text     -> already text

Numbers are canonicalised to a decimal string so that MySQL's `int(11)` and
PostgreSQL's `integer`, or `decimal(10,2)` rendered differently by two drivers,
do not register as a difference. Binary columns stay bytes.

`table_digest()` then hashes the canonical rows, giving a per-table checksum
that is identical before and after a correct migration.
"""

import datetime
import decimal
import hashlib

from . import mysqllatin1
from . import pgnames

# Tables whose contents are not part of the comparable state.
#
#   jf_session  ENGINE=MEMORY, holds PHP session ids and client IPs -- random by
#               construction and empty after any restart.
RANDOM_TABLES = frozenset(["jf_session"])

# Tables whose extent records who exercised the deployment rather than how it
# was migrated, so that comparing it measures traffic:
#
#   jf_stats  StatsPlugin::Insert() appends a row on every HTTP request. Two
#             things move it and neither is a defect: a candidate that browses
#             the application to check its own work, and this verifier's own
#             corpus of 413 requests, read before `tables_before` -- the
#             instrument perturbing its own subject. The recorded count (3003)
#             is reproducible only on a tree that has served nothing, which no
#             correct candidate leaves behind: the step-05 blind run lost
#             0.2796 of reward for checking its work, and said so itself.
#             §24.18.
#
#   jf_logs   In this set on a premise that is false (§29.4-29.5). The
#             premise was "the logger writes a jf_logs row" per request. It
#             does not: jf::Log() (_japp/model/j.php:309) has no caller
#             anywhere in the pinned tree, and 14 measured requests left the
#             table at 1500 rows with jf_logs_id_seq still unread. The
#             text-value recordings were taken through this set
#             (fixtures/record_mysql.py reads `MySQLState.tables()`), so the
#             five damaged serialized values in jf_logs.data are not in
#             textvalues-*.json. Since the round-2 migration review
#             (2026-10-05) step 06 scores them from fixtures/logvalues-06.json
#             instead, and `datagate.compare_seed_rows` compares the rows the
#             table held on arrival.
#
# Only the contents go. Their schema is still verified in full -- the schema
# families enumerate from their own fixtures and never consult this set -- and
# their identity sequences are still checked, by the one question that stays
# sound when the extent moves. See `datagate._sequence_checks`.
VOLATILE_TABLES = frozenset(["jf_logs", "jf_stats"])

EXCLUDED_TABLES = RANDOM_TABLES | VOLATILE_TABLES


# --------------------------------------------------------------- canonical --

_NULL = b"\x00N"

# The PHP session id reaches the database in three places -- jf_stats.SessionID,
# jf_logs.SessionID, and the `<session id>_<key>` names SettingManager::SaveSession
# writes into jf_options -- and it is random by construction. Callers that know
# which ids a run produced pass them to snapshot(); every occurrence is replaced
# with a fixed marker. That is the same single substitution httpdrv.normalize()
# makes, applied to state instead of to responses, and it is the only one.
SESSION_MARKER = "<SESSION>"


def canon_value(v, session_ids=()):
    """One column value -> canonical bytes."""
    if v is None:
        return _NULL
    if session_ids and isinstance(v, str):
        for sid in session_ids:
            if sid:
                v = v.replace(sid, SESSION_MARKER)
    if isinstance(v, bool):
        return b"\x00I" + (b"1" if v else b"0")
    if isinstance(v, int):
        return b"\x00I" + str(v).encode("ascii")
    if isinstance(v, (decimal.Decimal, float)):
        d = decimal.Decimal(str(v)).normalize()
        # normalize() renders small integers in exponent form (1E+2); undo that.
        if d == d.to_integral_value():
            d = d.quantize(decimal.Decimal(1))
        return b"\x00D" + format(d, "f").encode("ascii")
    # psycopg2 hands back a memoryview for bytea where PyMySQL hands back
    # bytes. Without this line the memoryview falls through to str() and the
    # canonical value becomes "<memory at 0x7f...>" -- a per-run address, which
    # would make every binary column look like a difference and every digest
    # look unstable. Measured against a bytea round trip, not assumed.
    if isinstance(v, (bytes, bytearray, memoryview)):
        return b"\x00B" + bytes(v).hex().encode("ascii")
    if isinstance(v, str):
        return b"\x00S" + v.encode("utf-8", "surrogatepass")
    # A time of day is the one type the two drivers disagree on in Python: a
    # MySQL TIME arrives as a timedelta (it is an interval there, and may be
    # negative or exceed 24 hours), a PostgreSQL `time` as a datetime.time.
    # Both are rendered to the same HH:MM:SS.ffffff so the disagreement is the
    # driver's and not the migration's.
    if isinstance(v, datetime.timedelta):
        return b"\x00T" + _clock(v.total_seconds()).encode("ascii")
    if isinstance(v, datetime.time):
        secs = v.hour * 3600 + v.minute * 60 + v.second + v.microsecond / 1e6
        return b"\x00T" + _clock(secs).encode("ascii")
    # dates and anything else: their textual form, which both drivers agree on
    return b"\x00X" + str(v).encode("utf-8")


def _clock(total_seconds):
    """Seconds -> [-]HH:MM:SS.ffffff, hours uncapped."""
    sign = "-" if total_seconds < 0 else ""
    total = abs(total_seconds)
    micro = int(round((total - int(total)) * 1e6))
    whole = int(total)
    if micro == 1000000:        # rounding carried into the next second
        whole += 1
        micro = 0
    return "%s%02d:%02d:%02d.%06d" % (sign, whole // 3600, (whole // 60) % 60,
                                      whole % 60, micro)


def canon_row(values, session_ids=()):
    return b"".join(canon_value(v, session_ids) for v in values)


def table_digest(rows, session_ids=()):
    """Digest over the *set* of canonical rows.

    The rows are sorted here rather than relied on in the order the query
    returned them. A table is a set: ordering it by primary key would be enough
    for MySQL and PostgreSQL to agree, except that jf_options is keyed by a name
    that embeds the session id -- so the key itself moves between runs, and only
    the canonical form (with the id substituted out) is stable. Sorting the
    canonical rows sidesteps that entirely and costs nothing at this size.
    """
    canon = sorted(canon_row(values, session_ids) for values in rows)
    h = hashlib.sha256()
    for row in canon:
        h.update(row)
        h.update(b"\n\x00R")
    return h.hexdigest(), len(canon)


# ----------------------------------------------------------------- MySQL ----

class MySQLState(object):
    """Reads canonical state out of MySQL 5.7.

    Values must be fetched as raw bytes -- `SET NAMES binary` on the server side
    and use_unicode=False on the driver side, which collect.open_mysql() does --
    and are decoded here from each column's own declared charset. The canonical
    form then does not depend on how the connection was negotiated.
    """

    def __init__(self, conn, schema):
        self.conn = conn
        self.schema = schema

    def refresh(self):
        """End the open transaction so the next read sees committed writes.

        InnoDB defaults to REPEATABLE READ, so without this a connection that
        has already read a table keeps returning the rows it saw first however
        much the application writes in between.
        """
        self.conn.commit()

    def _q(self, sql, args=None):
        cur = self.conn.cursor()
        cur.execute(sql, args or ())
        rows = cur.fetchall()
        cur.close()
        return rows

    def tables(self):
        rows = self._q(
            "SELECT table_name FROM information_schema.tables"
            " WHERE table_schema=%s AND table_type='BASE TABLE' ORDER BY table_name",
            (self.schema,))
        return [self._name(r[0]) for r in rows
                if self._name(r[0]) not in EXCLUDED_TABLES]

    @staticmethod
    def _name(v):
        return v.decode("utf-8") if isinstance(v, (bytes, bytearray)) else v

    def columns(self, table):
        """[(name, charset|None)] in ordinal order."""
        rows = self._q(
            "SELECT column_name, character_set_name FROM information_schema.columns"
            " WHERE table_schema=%s AND table_name=%s ORDER BY ordinal_position",
            (self.schema, table))
        return [(self._name(r[0]), self._name(r[1])) for r in rows]

    def order_columns(self, table):
        """Primary key if there is one, else every column: a total order."""
        rows = self._q(
            "SELECT column_name FROM information_schema.key_column_usage"
            " WHERE table_schema=%s AND table_name=%s AND constraint_name='PRIMARY'"
            " ORDER BY ordinal_position",
            (self.schema, table))
        pk = [self._name(r[0]) for r in rows]
        return pk if pk else [c for c, _ in self.columns(table)]

    def rows(self, table):
        cols = self.columns(table)
        order = self.order_columns(table)
        sel = ", ".join("`%s`" % c for c, _ in cols)
        by = ", ".join("`%s`" % c for c in order)
        raw = self._q("SELECT %s FROM `%s` ORDER BY %s" % (sel, table, by))
        charsets = [cs for _, cs in cols]
        for r in raw:
            yield [self._decode(v, cs) for v, cs in zip(r, charsets)]

    @staticmethod
    def _decode(v, charset):
        if v is None or charset is None:
            return v
        if isinstance(v, str):
            v = v.encode("latin-1", "surrogateescape")
        if charset == "latin1":
            return mysqllatin1.decode(v)
        if charset.startswith("utf8"):
            return v.decode("utf-8", "surrogateescape")
        if charset == "binary":
            return bytes(v)
        return v.decode("utf-8", "surrogateescape")


# ------------------------------------------------------------- PostgreSQL ---

class PostgresState(object):
    """Reads canonical state out of PostgreSQL 17.

    Two readings are corrected here, for the same reason `MySQLState._decode`
    dispatches on the column's declared character set: the canonical form is
    the information the application sees, and on each side that takes reading
    the column's own declared type. Both corrections were forced by measuring
    a *known-good* load rather than guessed at, and both are the difference
    between an instrument that scores the migration and one that scores the
    driver (§21.1).

    *A `character(n)` value is compared without its padding.* MySQL strips the
    trailing spaces from a `CHAR` column on retrieval and PostgreSQL does not,
    so a byte-perfect load of the ten `char` columns came back different in
    every one of their 4,539 values. Uncorrected, the gate would have failed
    three whole tables for a load that was right, and would have paid a
    candidate for choosing `varchar` over the faithful `character(n)` --
    scoring a spelling, and the wrong one. The strip is one-sided and does not
    hide a real loss: a MySQL `varchar` holding `"abc "` still differs from a
    `character(n)` that padded it, because the MySQL side keeps its space and
    the stripped PostgreSQL side does not.

    *A `bytea` value is decoded as UTF-8.* PostgreSQL will not store a NUL in
    any character type, and `jf_options.Value` holds 96 of them -- PHP's
    `serialize()` writes `\0*\0` around a protected property name -- so the
    one faithful home for that column is `bytea` (§21.2). What it holds is
    still character data, so it is decoded back to the code points the
    application wrote; `surrogateescape` keeps a sequence that is not UTF-8
    distinguishable rather than replacing it.

    `binary_columns` is how a column that is *genuinely* binary opts out of
    that decode, as `(table, column)` pairs in the recording's own spelling.
    The recorded MySQL schema has none -- its 155 columns are int, varchar,
    char, text, float and tinyint, and not one declares `binary` -- so every
    caller passes nothing today. It is a parameter rather than an assumption
    because the alternative is a silent wrong answer on the day one appears.

    `resolved` is `pgnames.resolve_tables()`'s map, and giving it makes every
    method take the recording's name and read whatever relation that name
    resolves to. Step 2 leaves the candidate free to fold the four mixed-case
    lesson table names or to keep and quote them, and free to put the schema
    anywhere their `search_path` reaches, so a data gate that looked up
    `lesson_XSS2_messages` in `public` would fail a migration the schema gate
    passed. Without `resolved` the plain `schema.name` lookup is used, which
    is what the authoring scripts want.
    """

    def __init__(self, conn, schema="public", resolved=None,
                 binary_columns=()):
        self.conn = conn
        self.schema = schema
        self.resolved = resolved
        self.binary_columns = frozenset((t.lower(), c.lower())
                                        for t, c in binary_columns)

    def refresh(self):
        self.conn.commit()

    def _q(self, sql, args=None):
        cur = self.conn.cursor()
        cur.execute(sql, args or ())
        rows = cur.fetchall()
        cur.close()
        return rows

    def _loc(self, table):
        """(schema, relname) for a table named the way the recording names it."""
        if self.resolved is None:
            return self.schema, table
        res = self.resolved.get(table)
        if res is None or not res.found:
            raise KeyError("%r resolves to no relation" % table)
        return res.schema, res.name

    def tables(self):
        if self.resolved is not None:
            return [t for t in sorted(self.resolved)
                    if self.resolved[t].found and t not in EXCLUDED_TABLES]
        rows = self._q(
            "SELECT table_name FROM information_schema.tables"
            " WHERE table_schema=%s AND table_type='BASE TABLE' ORDER BY table_name",
            (self.schema,))
        return [r[0] for r in rows if r[0] not in EXCLUDED_TABLES]

    def _declared(self, table):
        """[(name, data_type)] in ordinal order."""
        schema, relname = self._loc(table)
        return [(r[0], r[1]) for r in self._q(
            "SELECT column_name, data_type FROM information_schema.columns"
            " WHERE table_schema=%s AND table_name=%s ORDER BY ordinal_position",
            (schema, relname))]

    def columns(self, table):
        return [c for c, _ in self._declared(table)]

    def order_columns(self, table):
        schema, relname = self._loc(table)
        rows = self._q(
            "SELECT a.attname FROM pg_index i"
            " JOIN pg_attribute a ON a.attrelid=i.indrelid AND a.attnum=ANY(i.indkey)"
            " WHERE i.indrelid = to_regclass(%s) AND i.indisprimary",
            (pgnames.qualified(schema, relname),))
        pk = [r[0] for r in rows]
        return pk if pk else self.columns(table)

    def rows(self, table):
        schema, relname = self._loc(table)
        declared = self._declared(table)
        order = self.order_columns(table)
        sel = ", ".join('"%s"' % c for c, _ in declared)
        by = ", ".join('"%s"' % c for c in order)
        fixups = [self._fixup(table, name, ty) for name, ty in declared]
        for r in self._q('SELECT %s FROM %s ORDER BY %s'
                         % (sel, pgnames.qualified(schema, relname), by)):
            yield [f(v) if f else v for f, v in zip(fixups, r)]

    def _fixup(self, table, column, data_type):
        """The correction this column's declared type calls for, or None."""
        if data_type == "character":
            return _unpad
        if data_type == "bytea" \
                and (table.lower(), column.lower()) not in self.binary_columns:
            return _as_text
        return None


def _unpad(v):
    """`character(n)` pads; MySQL's `CHAR` does not survive retrieval padded."""
    return v.rstrip(" ") if isinstance(v, str) else v


def _as_text(v):
    """A `bytea` standing in for a character column, read back as its text."""
    if isinstance(v, (bytes, bytearray, memoryview)):
        return bytes(v).decode("utf-8", "surrogateescape")
    return v


# ------------------------------------------------------------------ digest --

def snapshot(state, tables=None, session_ids=()):
    """{table: {"digest": hex, "rows": n}} over every comparable table."""
    out = {}
    for t in (tables if tables is not None else state.tables()):
        digest, n = table_digest(state.rows(t), session_ids)
        out[t] = {"digest": digest, "rows": n}
    return out


def diff(expected, actual):
    """Tables that disagree, as [(table, why, expected, actual)]."""
    problems = []
    for t in sorted(set(expected) | set(actual)):
        e, a = expected.get(t), actual.get(t)
        if e is None:
            problems.append((t, "unexpected table", None, a))
        elif a is None:
            problems.append((t, "missing table", e, None))
        elif e["rows"] != a["rows"]:
            problems.append((t, "row count", e["rows"], a["rows"]))
        elif e["digest"] != a["digest"]:
            problems.append((t, "contents", e["digest"], a["digest"]))
    return problems
