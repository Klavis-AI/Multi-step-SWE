"""The dialect gate: every SQL statement in the tree must be PostgreSQL 17 SQL.

Three layers, in order of strength:

1. `sqlextract.php` walks each file's AST with nikic/php-parser and folds every
   string expression -- literal, interpolated, or a whole `.` chain -- into one
   text, with the parts it cannot evaluate marked. Text scanning cannot do this:
   a twenty-line `CREATE TABLE` reaches the database as one statement built from
   one expression, and a regular expression sees twenty unrelated lines.
2. `libpg_query`, through pglast, parses each statement with the *real*
   PostgreSQL 17 grammar. Nothing here encodes an opinion about what MySQL
   syntax looks like; the target's own parser decides.
3. The catalogs of the live target database answer what the grammar cannot:
   `SELECT GROUP_CONCAT(x)` and `COLLATE utf8_bin` are both syntactically valid
   PostgreSQL, and both are wrong. Every function and collation an accepted
   statement names must exist in `pg_proc` / `pg_collation`.

Nothing is excluded. Vendored trees, test fixtures and dead plugins are all
inside the repository and all in scope (rule 11); a tree keeps them ported or
does not keep them.
"""

import json
import os
import re

from . import deploy
from . import gate

HERE = os.path.dirname(os.path.abspath(__file__))
HOLE = "\x01"

# Walking the 908-file baseline tree takes about half a minute. The bound is
# for the tree that does not terminate, not for a slow machine.
EXTRACT_TIMEOUT = 600

# A statement is recognised by its opening tokens, taken from the SQL grammar.
# A single leading verb is not enough: ordinary prose in this tree begins
# "Create user result:", "Replace", "set cURL username/password".
STMT = re.compile(r"""^[\s(]*(?:
    SELECT\s+(?:ALL\s+|DISTINCT\s+)?[\w`"*(\x01] |
    INSERT\s+(?:LOW_PRIORITY\s+|DELAYED\s+|HIGH_PRIORITY\s+|IGNORE\s+)*INTO\b |
    REPLACE\s+(?:LOW_PRIORITY\s+|DELAYED\s+)*INTO\b |
    UPDATE\s+(?:LOW_PRIORITY\s+|IGNORE\s+)*[\w`"{\x01] |
    DELETE\s+(?:LOW_PRIORITY\s+|QUICK\s+|IGNORE\s+)*FROM\b |
    CREATE\s+(?:OR\s+REPLACE\s+|TEMPORARY\s+|UNIQUE\s+|FULLTEXT\s+|SPATIAL\s+)*
        (?:TABLE|DATABASE|SCHEMA|INDEX|VIEW|SEQUENCE|TYPE|EXTENSION|FUNCTION|
           TRIGGER|COLLATION|ROLE|USER)\b |
    ALTER\s+(?:TABLE|DATABASE|SCHEMA|INDEX|VIEW|SEQUENCE|TYPE|ROLE|USER)\b |
    DROP\s+(?:TEMPORARY\s+)?(?:TABLE|DATABASE|SCHEMA|INDEX|VIEW|SEQUENCE|TYPE|
           EXTENSION|FUNCTION|TRIGGER|COLLATION|ROLE|USER)\b |
    TRUNCATE\b | LOCK\s+TABLES?\b | UNLOCK\s+TABLES\b |
    SET\s+(?:NAMES|CHARACTER\s+SET|AUTOCOMMIT|SESSION|GLOBAL|TRANSACTION|SQL_MODE|@) |
    SHOW\s+(?:TABLES|COLUMNS|DATABASES|CREATE|INDEX|VARIABLES|STATUS)\b |
    START\s+TRANSACTION\b | BEGIN\s*;?\s*$ | COMMIT\s*;?\s*$ | ROLLBACK\b |
    GRANT\s+\w | REVOKE\s+\w | CALL\s+\w+\s*\( | EXPLAIN\s+\w | ANALYZE\b |
    VACUUM\b | COPY\s+\w+\s |
    WITH\s+(?:RECURSIVE\s+)?\w+\s+AS\s*\(
)""", re.I | re.X)


# Comments the server skips before a statement's first token. A statement is
# still a statement with `/**/` or `-- note` in front of it -- the server runs
# it -- so shape is judged after them. (Round-2 review, 2026-10-05: before this
# a one-token `/**/` prefix turned an unported MySQL statement into a
# "fragment" and passed its `dialect/ported/...` check. Measured with
# `fixtures/record_static.py`: no baseline string opens with a comment, so the
# recorded `sql-baseline.json` is unchanged.) `#` is MySQL's own comment and
# PostgreSQL's parser rejects it, which is the right answer for a statement
# that still carries one.
_LEAD_COMMENTS = re.compile(r"(?:\s*(?:/\*.*?\*/|--[^\n]*(?:\n|$)|#[^\n]*(?:\n|$)))*",
                            re.S)


def _uncommented(text):
    m = _LEAD_COMMENTS.match(text)
    return text[m.end():] if m else text


# ---------------------------------------------------------------- extract ---

def run_extractor(roots, tools=None):
    """The extractor's raw output: every folded string in every .php file.

        {"strings": [{file, line, end, class, function, text, parts, sink}],
         "unparsable": [{file, error}], "files": <count>}

    Split out of `extract()` because the two gates that read PHP want different
    halves of it. This one scores the statements a driver is handed, so it
    buckets by sink; `strictgate` has to see every folded string whatever its
    sink, because `PDO::MYSQL_ATTR_INIT_COMMAND => "SET sql_mode=''"` is an
    array value that reaches no call site and relaxes the connection anyway.
    """
    tools = tools or HERE
    # The candidate's tree is the input to a parser, and a parser is a program
    # with a stack. Dropped to `nobody` because nothing the candidate wrote
    # should be read by a root process even when it is only being parsed, and
    # bounded because a pathological file that makes php-parser run forever
    # would otherwise be a Harbor kill rather than a failed check.
    p = deploy.run(
        deploy.as_user(deploy.NOBODY,
                       ["php", os.path.join(tools, "sqlextract.php")]
                       + list(roots)),
        timeout=EXTRACT_TIMEOUT)
    if p.returncode != 0:
        raise RuntimeError("sqlextract.php failed (rc=%d): %s"
                           % (p.returncode, deploy.diagnostics(p)))
    return json.loads(p.stdout.decode("utf-8", "replace"))


def extract(roots, tools=None):
    """Run the AST extractor and split what it found into four buckets.

    Returns a dict:
      files        how many .php files were walked
      unparsable   files the PHP parser could not read -- a failure, not a skip
      strings      how many string expressions were folded in total
      statements   sink-reachable and statement-shaped: what the gate scores
      fragments    sink-reachable but not a whole statement (a `.=` piece, or a
                   non-SQL argument to a same-named method) -- reported, not
                   scored, because half a statement has no dialect
      unreached    statement-shaped but never handed to a driver -- reported,
                   not scored; a page title that opens with SELECT is not SQL
    """
    data = run_extractor(roots, tools)
    out = {"files": data["files"], "unparsable": data["unparsable"],
           "strings": len(data["strings"]),
           "statements": [], "fragments": [], "unreached": []}
    for s in data["strings"]:
        shaped = bool(STMT.match(_uncommented(s["text"])))
        if s["sink"]:
            out["statements" if shaped else "fragments"].append(s)
        elif shaped:
            out["unreached"].append(s)
    return out


# -------------------------------------------------------------- normalise ---

_WORD = re.compile(r"\w")


def _spans(sql):
    """Index ranges of quoted regions, so placeholders inside them are left."""
    out, i, n = [], 0, len(sql)
    while i < n:
        c = sql[i]
        if c in "'\"`":
            j = i + 1
            while j < n:
                if sql[j] == "\\" and c == "'":
                    j += 2
                    continue
                if sql[j] == c:
                    if j + 1 < n and sql[j + 1] == c:   # '' escape
                        j += 2
                        continue
                    break
                j += 1
            out.append((i, min(j, n - 1)))
            i = j + 1
            continue
        i += 1
    return out


# What a hole may stand for. A hole is a piece of the expression that was not a
# literal, so its syntactic role is exactly what the extractor cannot know:
# `{$prefix}users` is part of an identifier, `WHERE ID={$id}` is a value,
# `DROP TABLE $table` is a whole identifier, `SET {$assignments}` is a list of
# assignments, and `SELECT 1 {$where}{$order}` is often nothing at all. The
# search below tries them, and a statement is reported only when *no* reading
# of its holes is PostgreSQL. Permissiveness here is the safe direction: it can
# only hide a defect that lives entirely inside an unknown, while every MySQL
# construct in the literal text -- a backtick, `AUTO_INCREMENT`, `LIMIT` on an
# `UPDATE`, `REPLACE INTO`, `SEPARATOR` inside `GROUP_CONCAT` -- survives every
# substitution and is still reported.
FORMS = ("ident", "param", "empty", "assign")
_FORM_TEXT = {"ident": "x", "empty": "", "assign": "x = x"}


def _clean(text):
    return text.strip().rstrip(";").strip()


def _pieces(text):
    """Split into literal runs, holes, and the `?` placeholders outside quotes.

    `?` becomes `$n`: the framework's own driver writes `?` and PDO keeps
    writing it against PostgreSQL, so a `?` is not evidence of an unmigrated
    tree. A `?` inside a string literal is data and is left alone.
    """
    quoted = _spans(text)

    def inside(k):
        return any(a <= k <= b for a, b in quoted)

    out, buf = [], []
    for k, ch in enumerate(text):
        if ch == HOLE or (ch == "?" and not inside(k)):
            if buf:
                out.append(("t", "".join(buf)))
                buf = []
            out.append(("h",) if ch == HOLE else ("p",))
        else:
            buf.append(ch)
    if buf:
        out.append(("t", "".join(buf)))
    return out


def _render(pieces, forms):
    """(sql, spans) for one assignment of forms to holes, spans in sql indices."""
    out, spans, n, h, pos = [], [], 0, 0, 0
    for p in pieces:
        if p[0] == "t":
            out.append(p[1])
            pos += len(p[1])
            continue
        if p[0] == "p":
            n += 1
            s = "$%d" % n
        elif FORMS[forms[h]] == "param":
            n += 1
            s = "$%d" % n
            spans.append((pos, pos + len(s)))
            h += 1
        else:
            s = _FORM_TEXT[FORMS[forms[h]]]
            spans.append((pos, pos + len(s)))
            h += 1
        out.append(s)
        pos += len(s)
    return "".join(out), spans


def _try(sql):
    """(ok, message, location) from the real PostgreSQL 17 grammar."""
    from pglast import parse_sql
    try:
        parse_sql(sql)
        return True, None, None
    except Exception as e:
        loc = getattr(e, "location", None)
        if loc is None:
            m = re.search(r"at index (\d+)", str(e))
            loc = int(m.group(1)) if m else 0
        return False, str(e).split("\n")[0], loc


def resolve(text, budget=2000):
    """(ok, sql, message): is there any reading of the holes PostgreSQL accepts?

    All-identifiers is tried first, because that is what a hole usually is.
    After that the search is guided by the parser: libpg_query reports where it
    gave up, so the holes nearest that point are the ones worth re-reading, and
    each re-reading is queued breadth-first. Exhaustive substitution would be
    4^holes; this settles the statements in this tree in a handful of parses
    and is bounded by `budget` regardless.

    On failure the message and SQL returned are the all-identifiers reading,
    which is the one a reader can compare against the source.
    """
    pieces = _pieces(_clean(text))
    nh = sum(1 for p in pieces if p[0] == "h")
    start = (0,) * nh
    sql0, _ = _render(pieces, start)
    ok, msg0, _ = _try(sql0)
    if ok:
        return True, sql0, None
    if nh == 0:
        return False, sql0, msg0

    seen = {start}
    queue = [start]
    while queue and budget > 0:
        forms = queue.pop(0)
        sql, spans = _render(pieces, forms)
        ok, _, loc = _try(sql)
        budget -= 1
        if ok:
            return True, sql, None
        # The holes closest to where the parser stopped are the ones whose
        # reading is most likely wrong; try the rest of the forms for those.
        order = sorted(range(nh), key=lambda i: abs(spans[i][0] - (loc or 0)))
        for i in order[:3]:
            for f in range(len(FORMS)):
                if f == forms[i]:
                    continue
                nxt = forms[:i] + (f,) + forms[i + 1:]
                if nxt not in seen:
                    seen.add(nxt)
                    queue.append(nxt)
    return False, sql0, msg0


# ------------------------------------------------------------------ parse ---

def parse_failures(stmts):
    """[(stmt, sql, message)] for statements no reading makes PostgreSQL 17."""
    bad = []
    for s in stmts:
        ok, sql, msg = resolve(s["text"])
        if not ok:
            bad.append((s, sql, msg))
    return bad


def parsed(stmts):
    """[(stmt, sql)] for statements that do parse, in the reading that worked."""
    out = []
    for s in stmts:
        ok, sql, _ = resolve(s["text"])
        if ok:
            out.append((s, sql))
    return out


def _walk(node):
    """Yield every dict in a libpg_query JSON parse tree."""
    if isinstance(node, dict):
        yield node
        for v in node.values():
            for x in _walk(v):
                yield x
    elif isinstance(node, list):
        for v in node:
            for x in _walk(v):
                yield x


def _last(parts):
    """The unqualified name: `pg_catalog.substring` is `substring`."""
    names = [p["String"]["sval"] for p in parts or () if "String" in p]
    return names[-1].lower() if names else None


def names_used(stmts):
    """[(stmt, kind, name)] -- every function and collation the tree names.

    The grammar accepts `GROUP_CONCAT(x)` and `COLLATE utf8_bin` -- they are a
    function call and a collation name, and syntax has nothing to say about
    whether either exists. So the names are lifted out of the parse tree here
    and checked against the target's catalogs by the caller. `kind` is
    "function" or "collation".
    """
    from pglast.parser import parse_sql_json
    out = []
    for s, sql in parsed(stmts):
        tree = json.loads(parse_sql_json(sql))
        for node in _walk(tree):
            if "FuncCall" in node:
                name = _last(node["FuncCall"].get("funcname"))
                if name:
                    out.append((s, "function", name))
            # `x COLLATE c` in an expression, and the COLLATE of a column
            # definition, arrive as different nodes.
            if "CollateClause" in node:
                name = _last(node["CollateClause"].get("collname"))
                if name:
                    out.append((s, "collation", name))
            if "ColumnDef" in node:
                cc = node["ColumnDef"].get("collClause") or {}
                name = _last(cc.get("collname"))
                if name:
                    out.append((s, "collation", name))
    return out


# Constructs PostgreSQL's grammar turns into a call or a collation but that no
# catalog row backs. They are the language, not a name to resolve.
GRAMMAR_FUNCS = frozenset([
    "coalesce", "nullif", "greatest", "least", "cast", "extract", "overlay",
    "position", "substring", "trim", "collation", "normalize", "xmlconcat",
    "xmlelement", "xmlexists", "xmlforest", "xmlparse", "xmlpi", "xmlroot",
    "xmlserialize", "json_object", "json_array", "json_objectagg",
    "json_arrayagg", "json_serialize", "json_scalar", "system_user",
])
GRAMMAR_COLLATIONS = frozenset(["default"])


def catalog_names(conn):
    """({function names}, {collation names}) the target database actually has.

    Read from the candidate's own database, not from a reference server: a
    migration that defines `group_concat` as a PostgreSQL aggregate has made
    `GROUP_CONCAT(x)` correct, and the gate has no business calling it wrong.
    """
    cur = conn.cursor()
    cur.execute("SELECT DISTINCT lower(proname) FROM pg_proc")
    funcs = set(r[0] for r in cur.fetchall())
    cur.execute("SELECT DISTINCT lower(collname) FROM pg_collation")
    colls = set(r[0] for r in cur.fetchall())
    cur.close()
    return funcs | GRAMMAR_FUNCS, colls | GRAMMAR_COLLATIONS


def catalog_failures(stmts, funcs, colls):
    """[(stmt, kind, name)] for names the target database does not have."""
    bad = []
    for s, kind, name in names_used(stmts):
        known = funcs if kind == "function" else colls
        if name not in known:
            bad.append((s, kind, name))
    return bad


def failures(stmts, funcs, colls):
    """[(stmt, why)] -- everything PostgreSQL 17 would refuse, both layers.

    The two layers answer the same question about different statements: one
    never reaches the catalogs because the grammar stopped it. Scored together
    because a step is not asked to keep them apart -- `SELECT DATABASE()` and
    `REPLACE INTO t ...` are both SQL this server will not run.
    """
    out = [(s, "grammar: %s" % msg) for s, _sql, msg in parse_failures(stmts)]
    for s, kind, name in catalog_failures(stmts, funcs, colls):
        out.append((s, "no such %s: %s" % (kind, name)))
    return out


# ----------------------------------------------------------------- scoring ---

FIXTURE = "/tests/fixtures/sql-baseline.json"

# Trap 5 again. A tree with almost no PHP in it has almost no statements, and
# every "this site no longer offends" check below would pass on an empty /app.
#
# Two floors, because the file count and the statement count fail differently.
# A tree can keep every filename and empty the files -- 908 files walked, no
# SQL in any of them -- and the file floor sees nothing wrong. Neither floor is
# a deletion check: the instruction says in as many words that a file in scope
# "is ported, or it is not kept", so removing an unported vendored tree is an
# answer and not an evasion, and what stops that from being a *cheap* answer is
# `behavior:requests` on the pages that tree serves, not a count here.
MIN_FILE_FRACTION = 0.5
MIN_STATEMENT_FRACTION = 0.5

MAX_SHOWN = 8


def site(stmt, rel=None):
    """What identifies an offending statement across a rewrite.

    Not the SQL: the SQL is the thing the step is supposed to change, so
    keying on it would make every check pass the moment a word moved. Not
    (kind, detail) either -- `mysqlgone` can use that because a driver site
    names a fixed thing, while a dialect failure is a parser message and those
    are not a vocabulary. What survives a port is where the statement lives:
    the method that builds it. `dialect/no-new-failures` is the other half of
    this choice -- see `evaluate`.
    """
    path = stmt["file"]
    return ((rel(path) if rel else path), stmt.get("class") or "",
            stmt.get("function") or "")


def _rel(roots):
    roots = [os.path.abspath(r).rstrip("/") for r in roots]

    def rel(path):
        for r in roots:
            if path == r or path.startswith(r + "/"):
                return path[len(r) + 1:]
        return path
    return rel


def counts(offenders, rel=None):
    """{(file, class, function): how many statements offend there}."""
    out = {}
    for s, _why in offenders:
        k = site(s, rel)
        out[k] = out.get(k, 0) + 1
    return out


def where(offenders, k, rel=None):
    """Human-readable locations and reasons for one site's offences."""
    return ["%s:%s (%s)" % (s["file"], s["line"], " ".join(why.split())[:120])
            for s, why in offenders if site(s, rel) == k]


# ----------------------------------------------------------------- fixture ---

def load(path=FIXTURE):
    with open(path, "rb") as fh:
        return json.loads(fh.read().decode("utf-8"))


def record(roots, conn, tools=None):
    """AUTHORING: the baseline, recorded from the previous step's tree."""
    rel = _rel(roots)
    data = extract(roots, tools)
    funcs, colls = catalog_names(conn)
    bad = failures(data["statements"], funcs, colls)
    offenders = sorted(({"file": rel(s["file"]), "line": s["line"],
                         "class": s.get("class"),
                         "function": s.get("function"),
                         "why": " ".join(why.split())[:200]}
                        for s, why in bad),
                       key=lambda o: (o["file"], o["line"], o["why"]))
    return {"files": data["files"], "strings": data["strings"],
            "unparsable": data["unparsable"],
            "statements": len(data["statements"]),
            "fragments": len(data["fragments"]),
            "offenders": offenders}


def _slug(k):
    text = "%s::%s::%s" % k
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "x"


def _baseline_counts(want):
    out = {}
    for o in want["offenders"]:
        k = (o["file"], o.get("class") or "", o.get("function") or "")
        out[k] = out.get(k, 0) + 1
    return out


FIXED_IDS = ("dialect/parseable", "dialect/files-scanned",
             "dialect/statements-scanned", "dialect/catalog-readable",
             "dialect/no-new-failures")


def check_ids(fixture=FIXTURE):
    """The denominator, without looking at any tree."""
    want = load(fixture) if isinstance(fixture, str) else fixture
    ids = list(FIXED_IDS)
    base = _baseline_counts(want)
    for k in sorted(base):
        for i in range(1, base[k] + 1):
            ids.append("dialect/ported/%s#%d" % (_slug(k), i))
    return ids


# ---------------------------------------------------------------- evaluate ---

def evaluate(roots, conn, fixture=FIXTURE, tools=None, family="migrated:dialect"):
    """The dialect family for one candidate tree.

    `conn` is a connection to the database the candidate delivered, because
    the third layer reads that database's catalogs and not a reference
    server's -- see `catalog_names`. A tree with no reachable PostgreSQL has
    not answered the question this family asks, so the family fails whole
    rather than scoring the two layers it could still reach.
    """
    c = gate.Checks(family)
    try:
        want = load(fixture) if isinstance(fixture, str) else fixture
    except (OSError, ValueError) as e:
        return gate.crashed(family, "no recorded dialect baseline: %s" % e, 4)

    ids = check_ids(want)

    # The instrument before the finding (trap 6): a pglast that cannot parse
    # its own fixtures would report the candidate's whole tree as MySQL and
    # score a correct migration zero. That is the scorer's fault, not the
    # candidate's, and it is said in those words.
    try:
        funcs, colls = catalog_names(conn)
    except Exception as e:
        c.add("dialect/catalog-readable", False,
              "could not read the delivered database's catalogs: %s"
              % " ".join(str(e).split())[:200])
        c.fail_all([i for i in ids if i != "dialect/catalog-readable"],
                   "the delivered database's catalogs could not be read, so"
                   " no statement could be checked against them")
        return c
    c.add("dialect/catalog-readable", True)
    try:
        selftest(funcs, colls, tools)
    except Exception as e:
        return gate.crashed(family, "the scorer's dialect instrument failed"
                            " its own selftest (%s) and no statement in the"
                            " tree was judged"
                            % " ".join(str(e).split())[:300], len(ids))

    try:
        data = extract(roots, tools)
    except Exception as e:
        return gate.crashed(family, "cannot read the tree: %s"
                            % " ".join(str(e).split())[:200], len(ids))

    c.add("dialect/parseable", not data["unparsable"],
          "" if not data["unparsable"]
          else "%d file(s) the PHP parser could not read: %s"
          % (len(data["unparsable"]),
             ", ".join(u["file"] for u in data["unparsable"][:MAX_SHOWN])))

    ffloor = int(want["files"] * MIN_FILE_FRACTION)
    files_ok = data["files"] >= ffloor
    fshort = ("walked %d .php files; the baseline tree has %d, so anything"
              " under %d is not this application" % (data["files"],
                                                     want["files"], ffloor))
    c.add("dialect/files-scanned", files_ok, "" if files_ok else fshort)

    sfloor = int(want["statements"] * MIN_STATEMENT_FRACTION)
    stmts_ok = len(data["statements"]) >= sfloor
    sshort = ("folded %d driver-bound statement(s); the baseline tree has %d,"
              " so anything under %d is not this application"
              % (len(data["statements"]), want["statements"], sfloor))
    c.add("dialect/statements-scanned", stmts_ok, "" if stmts_ok else sshort)

    if not (files_ok and stmts_ok):
        why = fshort if not files_ok else sshort
        c.fail_all([i for i in ids if i not in
                    ("dialect/catalog-readable", "dialect/parseable",
                     "dialect/files-scanned", "dialect/statements-scanned")],
                   why)
        return c

    rel = _rel(roots)
    bad = failures(data["statements"], funcs, colls)
    have = counts(bad, rel)
    base = _baseline_counts(want)

    # One check per offending statement the baseline had, keyed by its site.
    # Four bad statements in `BaseNestedSet::Delete` are four checks, and
    # porting three of them earns three.
    for k in sorted(base):
        left = have.get(k, 0)
        for i in range(1, base[k] + 1):
            cid = "dialect/ported/%s#%d" % (_slug(k), i)
            done = i > left
            c.add(cid, done,
                  "" if done else "%d statement(s) here PostgreSQL 17 will"
                  " not run: %s" % (left, "; ".join(where(bad, k,
                                                          rel)[:MAX_SHOWN])))

    # The other half of keying by site. A statement moved to a method that
    # never offended would otherwise empty its old site and land somewhere
    # with no check on it, passing twice for one unported construct.
    new = sorted(k for k in have if k not in base)
    c.add("dialect/no-new-failures", not new,
          "" if not new else "%d statement(s) PostgreSQL 17 will not run, at"
          " %d site(s) the baseline had none: %s"
          % (sum(have[k] for k in new), len(new),
             "; ".join(where(bad, k, rel)[0] for k in new[:MAX_SHOWN])))

    c.note("%d .php files, %d folded string(s), %d statement(s), %d fragment(s)"
           % (data["files"], data["strings"], len(data["statements"]),
              len(data["fragments"])))
    c.note("%d statement(s) PostgreSQL 17 will not run, of %d in the baseline"
           % (len(bad), len(want["offenders"])))
    return c


# --------------------------------------------------------------- selftest ---

class SelfTestError(Exception):
    pass


def selftest(funcs, colls, tools=None):
    """Prove the instrument against a known answer before it is used.

    `selftest/clean.php` is PostgreSQL through every sink shape the extractor
    knows, plus prose and unreachable MySQL that must stay quiet.
    `selftest/dirty.php` is one MySQL construct per method: class `Syntax` for
    what PostgreSQL's grammar rejects, class `Catalog` for what it accepts and
    the catalogs do not. The expectation is the fixture's own class names, so
    adding a case to the fixture extends the check with it.

    Raises SelfTestError naming every method that came out wrong. Returns a
    summary dict on success.
    """
    tools = tools or HERE
    r = extract([os.path.join(tools, "selftest")])
    if r["unparsable"]:
        raise SelfTestError("the extractor could not read its own fixtures: %r"
                            % r["unparsable"])

    def methods(rows):
        return set((s["class"], s["function"]) for s in rows)

    syntax = methods(s for s, _, _ in parse_failures(r["statements"]))
    catalog = methods(s for s, _, _ in catalog_failures(r["statements"],
                                                        funcs, colls))
    have = {}
    for s in r["statements"]:
        have.setdefault(s["class"], set()).add((s["class"], s["function"]))

    counts = dict((c, len(v)) for c, v in have.items())
    floor = {"Syntax": 15, "Catalog": 8, "Clean": 5}
    wrong = []
    for cls, least in floor.items():
        if counts.get(cls, 0) < least:
            wrong.append("fixture class %s has %d methods, expected at least %d"
                         % (cls, counts.get(cls, 0), least))

    for m in sorted(have.get("Syntax", ())):
        if m not in syntax:
            wrong.append("%s::%s is MySQL and the grammar layer missed it"
                         % m)
    for m in sorted(have.get("Catalog", ())):
        if m in syntax:
            wrong.append("%s::%s should reach the catalog layer, not the"
                         " grammar layer" % m)
        if m not in catalog:
            wrong.append("%s::%s names something PostgreSQL does not have and"
                         " the catalog layer missed it" % m)
    for m in sorted(have.get("Clean", ())):
        if m in syntax:
            wrong.append("%s::%s is PostgreSQL and the grammar layer"
                         " reported it" % m)
        if m in catalog:
            wrong.append("%s::%s is PostgreSQL and the catalog layer"
                         " reported it" % m)
    if wrong:
        raise SelfTestError("; ".join(wrong))
    return {"statements": len(r["statements"]), "methods": counts,
            "syntax_reported": len(syntax), "catalog_reported": len(catalog)}
