"""The schema gate: is the PostgreSQL schema the MySQL schema, migrated?

The expectation is `fixtures/schema-baseline.json`, read out of the untouched
MySQL 5.7 deployment by `authoring/record_schema.py` -- 27 tables, their
columns in ordinal order with each one's declared type, nullability, character
set and collation, their primary keys, their 27 AUTO_INCREMENT columns, their
indexes (including the three that index only a prefix of a column) and their
six foreign keys. Nothing here was typed from reading the .sql file; it is what
`information_schema` said the running server had.

What is authored is the *acceptance*: which PostgreSQL types count as a
faithful move of `int(11)`, of `tinyint(1)`, of `varchar(255)`. That has to be
authored, because it is the definition of the task, and it is deliberately
plural -- `integer` and `bigint` are both honest homes for `int(11)`, and a
migration is not wrong for preferring one. What it is not is permissive about
loss: a `varchar(255)` that arrives as `varchar(50)` fails, a `NOT NULL` that
arrives nullable fails, a primary key that arrives as an ordinary index fails.

Two checks are about the move rather than the shape. Every AUTO_INCREMENT
column must arrive as an identity column or on a sequence -- a bare integer
would leave the application inserting explicit ids forever. And the three
prefix indexes have no PostgreSQL spelling at all: `KEY Page (Page(255))`
indexes the first 255 characters, and the only ways to say that are an
expression index over a prefix or an index over the whole column. Both are
accepted; neither being there is not.

A missing table fails every check underneath it. That is what stops a tree
from scoring better by deleting what it could not migrate.

**Where the tables are is not scored.** Every lookup goes through
`pgnames.resolve_tables()`, which asks `to_regclass()` on the application's own
connection, so a migration is free to put the schema in `public`, in a schema
named after the database, or anywhere else its `search_path` reaches, and free
to fold the mixed-case lesson table names or to keep and quote them. What is
not free is two relations whose names differ only in case, which is an
ambiguity MySQL's table namespace could not have had.

**Collation is not scored here.** It was, once, as "declared" and
"deterministic", and both were wrong for this task: §20.3 accepts `citext` and
a nondeterministic ICU collation equally, so "deterministic" would have failed
one of the two answers the step accepts, and "declared" scores a spelling
rather than an effect. What replaces them is two behavioural readings of the
same thing -- `semantics` asks 65 case probes of the candidate's own database,
and `installgate` asks them again of a database the *grader* created with a
fixed locale, which is where a schema that leaned on the cluster default
stops working.
"""

import json
import os
import re

from . import gate
from . import pgnames

FIXTURE = "/tests/fixtures/schema-baseline.json"

# MySQL declared type -> the PostgreSQL `data_type` values that are a faithful
# move of it. Plural where more than one is honest; never wider than that.
# What a MySQL type is allowed to have become. Widening is allowed and
# narrowing is not, which is why `int` does not list `smallint`: a `smallint`
# column accepts every value the `int` behaviour probes insert except the top
# of the range, so the structural reading is where a narrowed integer has to be
# caught. The behavioural half is `semantic/inthigh`.
TYPE_MAP = {
    "int": {"integer", "bigint", "numeric"},
    "tinyint": {"boolean", "smallint", "integer", "bigint", "numeric"},
    "smallint": {"smallint", "integer", "bigint", "numeric"},
    "mediumint": {"integer", "bigint", "numeric"},
    "bigint": {"bigint", "numeric"},
    "float": {"real", "double precision", "numeric"},
    "double": {"double precision", "numeric"},
    "decimal": {"numeric"},
    "char": {"character", "character varying", "text", "citext"},
    "varchar": {"character varying", "text", "character", "citext"},
    "tinytext": {"text", "character varying", "citext"},
    "text": {"text", "character varying", "citext"},
    "mediumtext": {"text", "character varying", "citext"},
    "longtext": {"text", "character varying", "citext"},
    "blob": {"bytea"},
    "tinyblob": {"bytea"},
    "mediumblob": {"bytea"},
    "longblob": {"bytea"},
    "binary": {"bytea"},
    "varbinary": {"bytea"},
    "datetime": {"timestamp without time zone", "timestamp with time zone"},
    "timestamp": {"timestamp with time zone", "timestamp without time zone"},
    "date": {"date"},
    "time": {"time without time zone", "interval"},
    "year": {"integer", "smallint", "date"},
    "enum": {"USER-DEFINED", "text", "character varying"},
    "set": {"USER-DEFINED", "text", "character varying", "ARRAY"},
    "json": {"json", "jsonb", "text"},
}


def load(path=FIXTURE):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


# ------------------------------------------------------- what PG actually has

class PgSchema(object):
    """Everything the gate needs, read once, keyed by the *expected* name.

    Keyed by the MySQL name the baseline recorded rather than by whatever the
    migration called the relation, so every lookup below is a question about
    the recording and a table that did not resolve is simply absent. The
    resolution itself -- which schema, which case -- is `pgnames`' business.
    """

    def __init__(self, conn, resolved):
        self.conn = conn
        self.resolved = resolved    # {mysql name: pgnames.Resolved}
        self.columns = {}           # mysql table -> {folded column: row}
        self.pk = {}                # mysql table -> [column, ...]
        self.indexes = {}           # mysql table -> [{name, unique, cols, defn}]
        self.fks = {}               # mysql table -> [{cols, ref_oid, ref_cols}]
        self._read()

    def _q(self, sql, args=None):
        cur = self.conn.cursor()
        cur.execute(sql, args or ())
        rows = cur.fetchall()
        cur.close()
        return rows

    def _read(self):
        for want, res in self.resolved.items():
            if not res.found:
                continue
            self.columns[want] = pgnames.columns_of(self.conn, res)

            self.pk[want] = [a.lower() for (a,) in self._q(
                "SELECT a.attname FROM pg_index i"
                " JOIN pg_attribute a ON a.attrelid = i.indrelid"
                "   AND a.attnum = ANY(i.indkey)"
                " WHERE i.indisprimary AND i.indrelid = %s"
                " ORDER BY a.attnum", (res.oid,))]

            self.indexes[want] = []
            for (name, defn, uniq) in self._q(
                    "SELECT ic.relname, pg_get_indexdef(i.indexrelid),"
                    "       i.indisunique"
                    "  FROM pg_index i"
                    "  JOIN pg_class ic ON ic.oid = i.indexrelid"
                    " WHERE i.indrelid = %s ORDER BY ic.relname", (res.oid,)):
                body = defn[defn.index("(") + 1:defn.rindex(")")] \
                    if "(" in defn else ""
                cols = [x.strip().strip('"').lower() for x in _split_top(body)]
                self.indexes[want].append(
                    {"name": name, "unique": uniq, "cols": cols, "defn": defn})

            self.fks[want] = [
                {"cols": [x.lower() for x in cols], "ref_oid": int(ref),
                 "ref_cols": [x.lower() for x in rcols]}
                for (cols, ref, rcols) in self._q(
                    "SELECT array_agg(a.attname ORDER BY k.ord),"
                    "       con.confrelid,"
                    "       array_agg(ra.attname ORDER BY k.ord)"
                    "  FROM pg_constraint con"
                    "  JOIN LATERAL unnest(con.conkey, con.confkey)"
                    "    WITH ORDINALITY AS k(att, ratt, ord) ON true"
                    "  JOIN pg_attribute a ON a.attrelid = con.conrelid"
                    "   AND a.attnum = k.att"
                    "  JOIN pg_attribute ra ON ra.attrelid = con.confrelid"
                    "   AND ra.attnum = k.ratt"
                    " WHERE con.contype = 'f' AND con.conrelid = %s"
                    " GROUP BY con.oid, con.confrelid", (res.oid,))]


def _split_top(text):
    """Split an index definition body on commas outside parentheses."""
    out, depth, buf = [], 0, []
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    if buf:
        out.append("".join(buf))
    return out


# ------------------------------------------------------------------- checks

def pg_type_name(pg):
    """The type as a reader would name it.

    `information_schema` reports an extension type as `USER-DEFINED` and keeps
    the real name in `udt_name`, which is where `citext` lives -- and `citext`
    is one of the two answers §20.3 accepts for a case-insensitive column, so
    a gate that only read `data_type` would fail it for being an extension.
    """
    if pg["data_type"] == "USER-DEFINED" and pg.get("udt"):
        return pg["udt"].lower()
    return pg["data_type"]


def widened(extra_columns=None):
    """{(table, column): {extra type, ...}}, folded, from a step's widening.

    A later step can force a home for one column that an earlier step had no
    reason to accept, and when it does, the earlier step's gate must not be
    the thing that fails it. Step 3 is the case: `jf_options.Value` is a
    MySQL `text` holding 96 NUL bytes, PostgreSQL will not put a NUL in any
    character type, and the only faithful home is `bytea` (§21.2).

    The widening is per **column** and not per MySQL type, which is the
    difference between "this column may be binary" and "every `text` column
    may be binary". There are nine recorded `text` columns and only one of
    them holds a NUL; a per-type widening would have let the other eight
    become `bytea` for nothing, which is exactly what the instruction says
    costs the candidate. Step 2's plan passes nothing, so step 2's shipped
    behaviour is unchanged by construction and not only by measurement.

    Widening is additive: nothing `TYPE_MAP` already accepts is withdrawn.
    """
    out = {}
    for (table, column), more in (extra_columns or {}).items():
        out[(table.lower(), column.lower())] = frozenset(more)
    return out


def _type_ok(mysql_type, pg, extra=()):
    allowed = TYPE_MAP.get(mysql_type)
    if allowed is None:
        return None, "no mapping recorded for MySQL type %r" % mysql_type
    allowed = set(allowed) | set(extra)
    got = pg_type_name(pg)
    if got not in allowed:
        return False, "%s -> %s, expected one of %s" % (
            mysql_type, got, ", ".join(sorted(allowed)))
    return True, "%s -> %s" % (mysql_type, got)


def _length_ok(expected_len, pg):
    """A character column may widen or become unbounded, never narrow."""
    if not expected_len:
        return True, ""
    got = pg_type_name(pg)
    if got not in ("character", "character varying"):
        # An unbounded type is not a pass by default: whether the column still
        # refuses the string that overflowed the MySQL one is asked in
        # statements, by semantics' 56 `over` probes, which a `text` column
        # with a CHECK constraint passes and a bare one does not. Declaring a
        # length is a spelling; refusing the value is the requirement.
        return True, "%s carries no declared length" % got
    if pg["length"] is None:
        return True, "unbounded"
    if pg["length"] < expected_len:
        return False, "holds %d characters, the MySQL column held %d" % (
            pg["length"], expected_len)
    return True, "%d >= %d" % (pg["length"], expected_len)


def _identity_ok(pg):
    if pg["identity"]:
        return True, "identity column"
    d = pg["default"] or ""
    if "nextval(" in d:
        return True, "serial: %s" % d
    return False, "neither an identity column nor on a sequence (default %r)" \
        % (d or None)


def _index_matches(want, have):
    """Does one recorded MySQL index have a PostgreSQL counterpart?

    Column sets are compared, not orders: a multi-column index in the other
    order is a different access path but it is still the same declaration of
    what is indexed, and this gate is about the schema having moved rather
    than about query plans -- those are the performance gate's business.

    A prefix index (`Page(255)`) matches either an index over the whole column
    or an expression index whose definition mentions the column, which is how
    `left(page, 255)` and `substr(page, 1, 255)` both arrive.

    More than one index can cover the same columns -- a migration that adds an
    ordinary index beside the unique one is doing nothing wrong -- so every
    candidate is collected and the one whose uniqueness agrees with MySQL's is
    preferred. Taking the first by name instead would let a harmless extra
    index stand in for a correct unique one and fail it, which is the caller's
    uniqueness comparison reading a schema that is in fact right.
    """
    want_cols = [p["column"].lower() for p in want["parts"]]
    prefixed = any(p.get("prefix") for p in want["parts"])
    cands = [idx for idx in have
             if idx["cols"] == want_cols or set(idx["cols"]) == set(want_cols)]
    if not cands and prefixed:
        cands = [idx for idx in have
                 if all(re.search(r"\b%s\b" % re.escape(c),
                                  idx["defn"].lower())
                        for c in want_cols)]
    for idx in cands:
        if bool(idx["unique"]) == bool(want["unique"]):
            return idx
    return cands[0] if cands else None


def _fk_matches(want, have, ref_oid):
    """The referenced table is compared by oid, not by name.

    The migration is allowed to have folded or requalified `jf_users`, so
    "does this foreign key point at the table the recording called jf_users"
    is a question about identity rather than spelling. `ref_oid` is None when
    the referenced table did not resolve at all, and then nothing matches.
    """
    cols = [c.lower() for c in want["columns"]]
    refcols = [c.lower() for c in want["ref_columns"]]
    if ref_oid is None:
        return None
    for fk in have:
        if fk["cols"] == cols and fk["ref_oid"] == ref_oid \
                and fk["ref_cols"] == refcols:
            return fk
    return None


def expected_ids(want):
    """Every check this gate will make, named before the database is opened.

    gate.py's second rule: the denominator is fixed by the recording. Built
    here so a database that will not talk fails exactly the checks a database
    that talks would have been asked.
    """
    ids = []
    for tname in sorted(want["tables"]):
        t = want["tables"][tname]
        ids.append("table/%s/exists" % tname)
        ids.append("table/%s/primary-key" % tname)
        for col in t["columns"]:
            base = "column/%s/%s" % (tname, col["name"])
            ids += [base + "/exists", base + "/type", base + "/nullable"]
            if col.get("auto_increment"):
                ids.append("identity/%s/%s" % (tname, col["name"]))
        for idx in t["indexes"]:
            ids.append("index/%s/%s" % (tname, idx["name"]))
        for fk in t["foreign_keys"]:
            ids.append("foreign-key/%s/%s" % (tname, fk["name"]))
    ids.append("schema/no-case-collisions")
    return ids


def evaluate(conn, fixture=FIXTURE, extra_columns=None):
    """The schema family for one candidate database.

    `extra_columns` is the calling step's per-column widening of `TYPE_MAP`;
    see `widened`. It is a parameter rather than a module setting so that two
    steps scored in one process could not read each other's.
    """
    c = gate.Checks("schema")
    try:
        want = load(fixture)
    except OSError as e:
        return gate.crashed("schema", "no recorded schema baseline: %s" % e, 8)

    ids = expected_ids(want)
    wide = widened(extra_columns)
    if wide:
        c.note("this step also accepts: %s"
               % "; ".join("%s.%s -> %s" % (t, col, ", ".join(sorted(v)))
                           for (t, col), v in sorted(wide.items())))
    try:
        schemas = pgnames.widen_search_path(conn)
        c.note("search_path widened to %d schema(s): %s"
               % (len(schemas), ", ".join(schemas)))
        resolved = pgnames.resolve_tables(conn, sorted(want["tables"]))
        pg = PgSchema(conn, resolved)
    except Exception as e:                      # a database that will not talk
        c.error = "cannot read the schema: %s" % e
        c.fail_all(ids, c.error)
        return c

    for tname in sorted(want["tables"]):
        t = want["tables"][tname]
        res = resolved[tname]
        cols = pg.columns.get(tname)
        here = c.add("table/%s/exists" % tname, res.found,
                     "" if res.found else
                     "nothing named %r resolves on the application's search_path"
                     % tname)
        if res.found and res.spelling == "quoted":
            c.note("%s resolves only as a quoted mixed-case name (%s)"
                   % (tname, res.qualified))

        # Columns. Every one is scored on existence, type and nullability, and
        # a missing table fails all three for all of them rather than removing
        # them from the denominator.
        for col in t["columns"]:
            cname = col["name"]
            base = "column/%s/%s" % (tname, cname)
            got = pgnames.find_column(cols or {}, cname)
            if got is None:
                why = ("table is missing" if not here
                       else "no column %r" % cname)
                c.add(base + "/exists", False, why)
                c.add(base + "/type", False, why)
                c.add(base + "/nullable", False, why)
                if col.get("auto_increment"):
                    c.add("identity/%s/%s" % (tname, cname), False, why)
                continue
            c.add(base + "/exists", True, "")
            ok, why = _type_ok(col["type"], got,
                               wide.get((tname.lower(), cname.lower()), ()))
            if ok is None:
                c.note("%s: %s" % (base, why))
                ok = False
            len_ok, len_why = _length_ok(col.get("length"), got)
            c.add(base + "/type", ok and len_ok,
                  why if not ok else ("" if len_ok else len_why))
            c.add(base + "/nullable", got["nullable"] == col["nullable"],
                  "" if got["nullable"] == col["nullable"]
                  else "MySQL %s, PostgreSQL %s"
                  % ("NULL" if col["nullable"] else "NOT NULL",
                     "NULL" if got["nullable"] else "NOT NULL"))

            if col.get("auto_increment"):
                ok, why = _identity_ok(got)
                c.add("identity/%s/%s" % (tname, cname), ok, why)

        # Primary key, as a set: PostgreSQL keeps the declared order, and a
        # migration that reorders a composite key has still declared the same
        # key.
        #
        # "The same key" includes *no* key. `lesson_AccessControlMatrix_users`
        # has none in MySQL -- it carries a UNIQUE KEY on `name` and nothing
        # else -- so a migration that invents one has added a uniqueness and a
        # NOT NULL that the lesson's data never had to satisfy. No probe in the
        # semantics family would see that, because there is no recorded index
        # to build a unique-dup pair from, which is exactly why this check
        # compares both directions rather than requiring a key to exist.
        # A missing table answers "no primary key" the same way that table
        # does, so `here` is asked first: an unevaluable check is a failure,
        # not a match against an absence.
        got_pk = pg.pk.get(tname, [])
        want_pk = [x.lower() for x in t["primary_key"]]
        pk_ok = here and sorted(got_pk) == sorted(want_pk)
        c.add("table/%s/primary-key" % tname, pk_ok,
              "" if pk_ok else
              ("table is missing" if not here else
               "MySQL %s, PostgreSQL %s" % (want_pk or None, got_pk or None)))

        for idx in t["indexes"]:
            hit = _index_matches(idx, pg.indexes.get(tname, []))
            detail = ""
            if hit is None:
                parts = "".join(
                    "%s%s" % (p["column"],
                              "(%d)" % p["prefix"] if p.get("prefix") else "")
                    for p in idx["parts"])
                detail = "nothing indexes %s" % parts
                ok = False
            else:
                # Uniqueness is compared in both directions, for the same
                # reason the primary key above is. The seven unique indexes are
                # covered behaviourally by semantics' `unique-dup` pairs, but
                # the twenty-seven ordinary ones are not -- there is no recorded
                # collision to build a probe from -- so an index that arrives
                # UNIQUE when MySQL's was not adds a constraint the lesson data
                # never had to satisfy, and nothing else would see it.
                ok = bool(hit["unique"]) == bool(idx["unique"])
                if not ok:
                    detail = "MySQL %s, PostgreSQL %s" % (
                        "UNIQUE" if idx["unique"] else "not unique",
                        "UNIQUE" if hit["unique"] else "not unique")
            c.add("index/%s/%s" % (tname, idx["name"]), ok, detail)

        for fk in t["foreign_keys"]:
            ref = resolved.get(fk["ref_table"])
            hit = _fk_matches(fk, pg.fks.get(tname, []),
                              ref.oid if ref and ref.found else None)
            c.add("foreign-key/%s/%s" % (tname, fk["name"]), hit is not None,
                  "" if hit else "no foreign key %s -> %s(%s)"
                  % (fk["columns"], fk["ref_table"], fk["ref_columns"]))

    # Two relations that differ only in case cannot both be what the
    # application's unquoted SQL reads, and the four mixed-case lesson tables
    # are exactly where that happens.
    try:
        dupes = pgnames.case_collisions(conn)
    except Exception as e:
        dupes = ["could not be read: %s" % e]
    c.add("schema/no-case-collisions", not dupes,
          "" if not dupes else "names differing only in case: %s" % dupes)

    # expected_ids() is what a database that will not talk fails, so it has to
    # be exactly what a database that does talk is asked. Checked here rather
    # than trusted, because the two lists are written in two places.
    if set(ids) != set(c._ids):
        raise AssertionError(
            "denominator drift: %d planned, %d scored, only-planned %s,"
            " only-scored %s"
            % (len(ids), len(c._ids), sorted(set(ids) - set(c._ids))[:5],
               sorted(set(c._ids) - set(ids))[:5]))
    return c
