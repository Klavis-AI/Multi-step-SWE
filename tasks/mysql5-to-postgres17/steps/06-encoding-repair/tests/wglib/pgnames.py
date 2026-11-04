"""Finding the migrated tables the way the application finds them.

A gate that reads `information_schema.columns WHERE table_schema = 'public'`
is asking a question the application never asks. The application says
`SELECT ... FROM jf_users`, unqualified, and the server resolves that through
the connection's `search_path`. Where the tables live is the migration's
business -- `public`, a schema named after the database, anywhere the search
path reaches -- and a gate that insists on one of those is scoring a spelling.

So every lookup here goes through `to_regclass()`, which is the resolver the
application's own statements go through, on a connection opened as the
application's user against the application's database.

The second thing this module settles is case. MySQL gave this schema
`lesson_AccessControlMatrix_users` and `jf_users` side by side. An unquoted
PostgreSQL identifier folds to lower case, so a migration has two honest
answers -- fold the names and let the application's unquoted SQL find them, or
keep the mixed case and quote it everywhere -- and both are accepted here. What
is not accepted is two objects whose names differ only in case, which is a
collision MySQL's own case-insensitive table namespace could never have had.
"""


class Resolved(object):
    """One expected table, and what answered for it."""

    def __init__(self, want, oid=None, schema=None, name=None, spelling=None):
        self.want = want            # the MySQL name, as recorded
        self.oid = oid              # pg_class.oid, or None if nothing answered
        self.schema = schema
        self.name = name
        self.spelling = spelling    # which of the two forms resolved

    @property
    def found(self):
        return self.oid is not None

    @property
    def qualified(self):
        """The name to put in a statement, quoted both halves."""
        return qualified(self.schema, self.name)

    def __repr__(self):
        if not self.found:
            return '<Resolved %s: absent>' % self.want
        return '<Resolved %s -> %s.%s (%s)>' % (self.want, self.schema,
                                                self.name, self.spelling)


def quote_ident(name):
    return '"%s"' % name.replace('"', '""')


def qualified(schema, name):
    """`schema.name` with both halves quoted, for a caller holding the pair.

    The same spelling `Resolved.qualified` produces, available to code that
    has the schema and relation names without a `Resolved` in hand.
    """
    return '%s.%s' % (quote_ident(schema), quote_ident(name))


def candidates(name):
    """The two spellings a migration may have chosen, in the order tried.

    Folded first: it is what the application's unquoted SQL resolves to, so a
    tree that has both (which is a collision this module reports) is read the
    way the application would read it.
    """
    folded = name.lower()
    if folded == name:
        return [(folded, 'folded')]
    return [(folded, 'folded'), (quote_ident(name), 'quoted')]


def widen_search_path(conn):
    """Put every non-system schema on the path, and say which.

    `to_regclass()` answers through the *caller's* search_path, and the caller
    here is the scorer -- which connects as `postgres` over the socket,
    because that is the one way in the forced hba file guarantees. A migration
    that put its tables in a schema and made the application find them with
    `ALTER ROLE webgoat SET search_path = ...` would then be invisible to a
    gate that never set its own path, and would fail every structural check
    for a choice this package has already said it does not score.

    Whether the *application's* connection finds the tables is not left
    unmeasured by this: it is what the 413 scored requests measure, end to
    end, through the application's own driver and its own configuration. This
    widening is only how the structural gates avoid asking the same question
    badly.
    """
    cur = conn.cursor()
    cur.execute("SELECT nspname FROM pg_namespace"
                " WHERE nspname NOT IN ('information_schema')"
                "   AND nspname !~ '^pg_' ORDER BY nspname")
    schemas = [r[0] for r in cur.fetchall()]
    if schemas:
        cur.execute("SET search_path = %s"
                    % ", ".join(quote_ident(s) for s in schemas))
    cur.close()
    return schemas


def resolve_tables(conn, names):
    """{expected name: Resolved}, resolved through the connection's search_path."""
    cur = conn.cursor()
    out = {}
    for want in names:
        found = None
        for spelling, how in candidates(want):
            cur.execute("SELECT to_regclass(%s)::oid", (spelling,))
            row = cur.fetchone()
            if row and row[0]:
                cur.execute("SELECT n.nspname, c.relname FROM pg_class c"
                            " JOIN pg_namespace n ON n.oid = c.relnamespace"
                            " WHERE c.oid = %s", (row[0],))
                schema, relname = cur.fetchone()
                found = Resolved(want, int(row[0]), schema, relname, how)
                break
        out[want] = found or Resolved(want)
    cur.close()
    return out


def case_collisions(conn):
    """Relations in the search path whose names differ only in case.

    Reported rather than tolerated: MySQL's table namespace on this deployment
    is case insensitive (`lower_case_table_names` aside, the recorded schema has
    no two names that fold together), so a migration that produces a pair has
    invented an ambiguity the application's unquoted SQL cannot resolve.
    """
    cur = conn.cursor()
    cur.execute(
        "SELECT n.nspname, lower(c.relname), count(*), string_agg(c.relname, ',' ORDER BY c.relname)"
        " FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace"
        " WHERE c.relkind IN ('r', 'p', 'v', 'm')"
        "   AND n.nspname NOT IN ('pg_catalog', 'information_schema')"
        "   AND n.nspname !~ '^pg_'"
        " GROUP BY 1, 2 HAVING count(*) > 1", ())
    out = ['%s.(%s)' % (nsp, names) for nsp, _, _, names in cur.fetchall()]
    cur.close()
    return out


def columns_of(conn, resolved):
    """{folded column name: row} for one resolved table, in ordinal order.

    Reads `information_schema.columns`, which is the portable view and the one
    `schemagate` already compares against, but keyed by the resolved schema and
    relation rather than by a schema this module assumed.
    """
    cur = conn.cursor()
    cur.execute(
        "SELECT column_name, ordinal_position, data_type, udt_name,"
        "       character_maximum_length, numeric_precision, numeric_scale,"
        "       is_nullable, column_default, is_identity, identity_generation,"
        "       collation_name, is_generated"
        "  FROM information_schema.columns"
        " WHERE table_schema = %s AND table_name = %s"
        " ORDER BY ordinal_position", (resolved.schema, resolved.name))
    keys = ("name", "ordinal", "data_type", "udt", "length", "precision",
            "scale", "is_nullable", "default", "is_identity",
            "identity_generation", "collation", "is_generated")
    out = {}
    for row in cur.fetchall():
        col = dict(zip(keys, row))
        col["nullable"] = (col.pop("is_nullable") == "YES")
        col["identity"] = (col.pop("is_identity") == "YES")
        out[col["name"].lower()] = col
    cur.close()
    return out


def find_column(cols, want):
    """One expected column, matched case insensitively, or None."""
    return cols.get(want.lower())
