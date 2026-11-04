"""Step 01's gate: the source server rejects invalid values instead of
silently repairing them, and the application is correct under that.

The deployment ships with `sql_mode = NO_ENGINE_SUBSTITUTION`, which is MySQL
5.5's default kept through two upgrades. Under it the server repairs whatever
the application gives it: a NULL bound to a `NOT NULL` column becomes `''`, an
omitted column with no `DEFAULT` becomes `''`/`0`, an empty string bound to an
`int` becomes `0`, text outside a `latin1` column's repertoire becomes `?`, and
a value longer than the column is truncated. `NOTES-authoring.md` §19 has the
measurement: five statements in four files depend on that, and the application
sees none of it, because PDO is `ERRMODE_SILENT` and no mysqli return value is
ever checked.

PostgreSQL has no permissive mode, so all five are hard failures at the
cutover. This step moves the repair from the server into the application while
the recorded corpus is still there to say whether anything changed.

What this module scores splits into three families, and which of them pay is
the whole of the step's calibration.

*The mode itself* (`migrated:strict-mode`, earned) -- the running server,
started by the verifier from the candidate's own `/etc/mysql/my.cnf`, is
strict, and the file on disk is what put it there.

*The repairs* (`migrated:repairs`, earned) -- the six readings that were
*measured* to change when the mode goes on and nothing else does: the session
bookkeeping UPDATE, the signup's two rows and their values, the contest
challenge submitted with an empty `points`, and the two names outside
`latin1`'s repertoire. Each drives the application over HTTP and reads what
reached the column, against what the permissive server stored for the very
same input.

*Everything else* (`migrated:no-shortcut`, guard) -- the rule-7 must-flip
probes `M1`-`M3`, the readings the mode does not disturb (`S6`, `S8`, `S10`),
and the shortcuts `G11`-`G14`. None of it can be earned and all of it can be
broken, so per `gate.combine` it multiplies rather than adds.

The split was measured, not chosen. A first cut scored all twenty checks in one
flat family, and a container running strict mode with the application untouched
scored 13/20: thirteen of the questions were already true before the step began.
Nothing was wrong with the checks; a mean over questions the candidate did not
answer was paying for arriving. The ones that actually moved are the earned
families, and the repairs' floor is now 0/6. (Nineteen checks, not twenty: an
XSS2 probe was withdrawn for the reason given under `ENCODING_CASES`.)

Three of those guards compare against the recording rather than against zero,
because the untouched deployment is not clean and saying otherwise would fail a
correct answer for something it did not do: the tree names `sql_mode` six times
in mysqldump headers under `install/_db/` and `SQL/`, and the corpus's last
scenario resets four lessons, each of which recreates its table with no
`CHARSET` clause and so brings it back as `utf8` rather than the `latin1` the
dump gave it. Both facts are in the recording; only what a candidate *adds* to
them is scored.

Both `observe()` and the checks live here because the recording and the
checking have to be the same code, and `check_all()` takes two `observe()`
results and nothing else: `authoring/probe_step01.py` calls `observe()` against the
untouched tree to produce `fixtures-steps/01-strict-mode/strict-probes.json`,
and the verifier calls `observe()` again and hands both to `check()`.
"""

import os
import re
import subprocess

from . import collect, deploy, gate, ready, sqlgate

# Every value this module sends carries this prefix, so a row it reads is a row
# it wrote and never a seeded one.
MARK = "wgs1"

# Every over-length value ends with this. Whether the value survived is then a
# question about its tail rather than about its length, which is the only form
# the question can take here: `jf_stats.Query` holds what mod_rewrite left in
# `REDIRECT_QUERY_STRING`, not the bytes the client put in the URL, so the
# recorded length is an artefact of the rewrite rules and the sent length is
# not comparable to it. A tail is comparable to itself.
TAIL = "-end-" + MARK
EMAIL_TAIL = "@example.com"

# Column limits, from `schema-baseline.json`. Each over-length value is sent at
# roughly 20% past its column so that nothing about the result depends on an
# off-by-one.
LIMIT_QUERY = 1024        # jf_stats.Query   varchar(1024)
LIMIT_UA = 1024           # jf_stats.UserAgent varchar(1024)
LIMIT_EMAIL = 255         # jf_xuser.Email   varchar(255)

CONTEST_ADMIN = ("contest.admin", "Admin!234")
GUEST = ("guest", "guest")
PASSWORD = "Wgs1!Pass1"

MY_CNF = "/etc/mysql/my.cnf"
DATABASE = "webgoatphp"
SOCKET = "/var/run/mysqld/mysqld.sock"

# Where `deploy.start_mysql` puts the general query log. Named from there, not
# repeated, so the writer and the reader cannot drift apart.
GENERAL_LOG = deploy.MYSQL_GENERAL_LOG

SIGNUP_USER = MARK + "-signup"
SIGNUP_EMAIL = SIGNUP_USER + EMAIL_TAIL
OVERLONG_EMAIL_MARK = MARK + "-e"

# What `httpdrv.App` sends when nothing overrides it. Named here because the
# ordinary-traffic count below is "rows this pass logged that were not one of
# the over-length probes", and the probes are told apart by their markers.
DEFAULT_UA = "wg-fixture/1"

# `TRADITIONAL` is in here because it is not a flag but a spelling: MySQL
# expands it to both strict flags and five more. A gate that only knew the two
# flags would read `sql_mode = TRADITIONAL` -- a correct, idiomatic way to do
# this step -- as a relaxation, and fail a candidate for the manual's own
# recommended setting. `ANSI` is deliberately not here: it expands to quoting
# and grouping rules and leaves the server as permissive as it found it.
STRICT_FLAGS = ("STRICT_TRANS_TABLES", "STRICT_ALL_TABLES", "TRADITIONAL")

# The four values whose storage depends on the column's repertoire, one per
# thing a `latin1` column over a `utf8` connection can do to text.
#
# They go into `contest_challenges.NameToDisplay` -- `latin1 varchar(100)`,
# written straight from `$_POST['name']` by
# `app/control/mode/contest/ajax/challenge.php` -- and not into the XSS2
# lesson, for a measured reason. The corpus's last scenario resets four
# lessons, and each lesson's `reset()` recreates its table with no `CHARSET`
# clause, so it comes back as the *database* default, `utf8`.
# `lesson_XSS2_messages.message` is therefore `latin1` before the corpus runs
# and `utf8` after it, and a probe on it would be measuring which order the
# families ran in. `contest_challenges` is created by the dump, never recreated
# at runtime, and stays `latin1`.
#
# An earlier cut of this gate also probed the XSS2 lesson directly, driving the
# reset first so that the reading was deterministic whichever order things ran
# in. It was removed, and the reason is worth keeping: after the reset that
# column is `utf8 text`, and what permissive MySQL does there with a byte
# sequence that is not valid UTF-8 is store the value *truncated at the first
# bad byte*. Scoring a repair against that would demand the candidate reproduce
# a silent truncation in the same step whose `M1`-`M3` say a silent truncation
# is the wrong answer. The ground it covered is covered twice over anyway:
# `S9-raw-bytes` asks the same question of a `latin1` column, where the
# recorded answer is a `?` per bad byte, and five of the nine recorded
# responses this step is scored on are the XSS2 lesson's own.
#
# The blind run of build-process item 7 found the same asymmetry from the
# server, and found that the instruction had flattened it into one rule for
# every column. The instruction now says it plainly; what is scored is
# unchanged, and §19.11-19.12 say why adding the utf8 case would have made
# this step contradict its own `M1`-`M3`.
#
# What each one stores is recorded, not predicted -- `NOTES-authoring.md` §19.5
# has the bytes.
ENCODING_CASES = (
    ("cp1252-range", "a", u"latin-1 range: caf\u00e9 na\u00efve \u00dfra\u00dfe"),
    ("cp1252-only", "b", u"cp1252 only: \u20ac \u2013 \u201c \u201d \u0152 \u2122"),
    ("outside-latin1", "c", u"outside latin-1: \u65e5\u672c\u8a9e \u0394\u03b5\u03bb\u03c4\u03b1"),
    ("raw-bytes", "d", b"raw bytes: \xc3 \xa9 \x81 \xfe \xff"),
)

# ------------------------------------------------------------ environment ---

class Env(object):
    """A live deployment: one database connection and a way to make sessions."""

    def __init__(self, conn, base="http://127.0.0.1", host="localhost"):
        self.conn = conn
        self.base = base
        self.host = host

    def app(self):
        from . import httpdrv
        return httpdrv.App(self.base, self.host)

    def rows(self, sql, args=()):
        """Read committed rows.

        The commit first is not decoration: InnoDB defaults to REPEATABLE READ,
        so a connection that has already read a table goes on returning the
        rows it saw first however much the application writes in between.
        """
        self.conn.commit()
        cur = self.conn.cursor()
        cur.execute(sql, args)
        out = cur.fetchall()
        cur.close()
        return out

    def scalar(self, sql, args=()):
        rows = self.rows(sql, args)
        return rows[0][0] if rows else None


def _s(v):
    """A column value as a JSON-safe string, byte for byte.

    The scorer's connection is `SET NAMES binary`, so text arrives as bytes.
    latin-1 is the one decode that maps every byte to exactly one code point,
    so this is a lossless rename, not an interpretation.
    """
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).decode("latin-1")
    return v


def _hex(v):
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).hex()
    if isinstance(v, str):
        return v.encode("utf-8").hex()
    return v


def _i(v):
    return None if v is None else int(v)


def is_strict(sql_mode):
    value = (sql_mode or "").upper()
    return any(flag in value for flag in STRICT_FLAGS)


def server_sql_mode(env):
    return _s(env.scalar("SELECT @@GLOBAL.sql_mode"))


# ------------------------------------------------------------ observation --

def _max_ids(env):
    return {
        "jf_stats": _i(env.scalar("SELECT IFNULL(MAX(ID),0) FROM jf_stats")),
        "jf_users": _i(env.scalar("SELECT IFNULL(MAX(ID),0) FROM jf_users")),
        "jf_xuser": _i(env.scalar("SELECT IFNULL(MAX(ID),0) FROM jf_xuser")),
        "contest_challenges": _i(env.scalar(
            "SELECT IFNULL(MAX(ID),0) FROM contest_challenges")),
    }
    # `lesson_XSS2_messages` is deliberately absent. The corpus's last
    # scenario resets that lesson, which drops the table and takes its
    # AUTO_INCREMENT back to 1, so a high-water mark taken before a verifier
    # run would not survive one. Nothing here reads it: the encoding probes
    # live in `contest_challenges`, and what the XSS2 lesson does to text is
    # already five of the nine recorded responses this step is scored on.


def _drive(env):
    """Every request this gate makes, in one pass. Returns what the client saw.

    Ordering matters in one place only: the session probe has to own its own
    browsing session, because AccessCount counts the requests made on it.
    """
    seen = {}

    # -- S3: the session bookkeeping UPDATE -------------------------------
    app = env.app()
    first = app.get("/main")
    sid_before = app.session_id()
    app.login(*GUEST)
    sid_after = app.session_id()
    hits = [app.get("/main"), app.get("/about"), app.get("/main")]
    seen["session"] = {
        "first_status": first["status"],
        "session_id_changed": sid_before != sid_after,
        "requests_on_session": 1 + 2 + len(hits),
        "statuses": [r["status"] for r in hits],
    }
    seen["_session_id"] = sid_after

    # -- S4/S5/S6: signup writes jf_users and jf_xuser ---------------------
    app = env.app()
    signup = app.post("/mode/contest/user/signup",
                      {"Username": SIGNUP_USER, "Password": PASSWORD,
                       "Confirm": PASSWORD, "Email": SIGNUP_EMAIL})
    login = env.app().post("/user/login", {"Username": SIGNUP_USER,
                                           "Password": PASSWORD})
    seen["signup"] = {"status": signup["status"],
                      "login_status": login["status"],
                      "login_location": login["location"]}

    # -- S7: the contest insert built straight out of $_POST ---------------
    admin = env.app()
    admin.login(*CONTEST_ADMIN)
    challenge = admin.post("/mode/contest/ajax/challenge",
                           {"challenge": MARK + "-pts", "name": MARK + " pts",
                            "points": "", "flag": ""})
    seen["contest"] = {"status": challenge["status"],
                       "body_head": challenge["body"][:120].decode("latin-1")}

    # -- S8/S9: what reaches a latin1 column over a utf8 connection --------
    # `points` and `flag` are valid here on purpose. They are S7's subject, and
    # an INSERT that strict mode rejects for its integer would fail these four
    # for a reason that has nothing to do with the text they carry.
    seen["encoding"] = {}
    for case_id, letter, value in ENCODING_CASES:
        r = admin.post("/mode/contest/ajax/challenge",
                       {"challenge": "%s-%s" % (MARK, letter), "name": value,
                        "points": "10", "flag": "%s-%s-flag" % (MARK, letter)})
        seen["encoding"][case_id] = {"status": r["status"],
                                     "sent_hex": _hex(value)}

    # -- M1/M3: over-length values into jf_stats ---------------------------
    plain = env.app()
    over_q = MARK + "-q" + "z" * (LIMIT_QUERY + 200) + TAIL
    r = plain.get("/about?q=" + over_q)
    seen["overlong_query"] = {"status": r["status"], "sent_len": len(over_q)}

    over_ua = MARK + "-u " + "U" * (LIMIT_UA + 200) + TAIL
    r = plain.get("/about", headers={"User-Agent": over_ua})
    seen["overlong_useragent"] = {"status": r["status"],
                                  "sent_len": len(over_ua)}

    # -- M2: an over-length address into jf_xuser --------------------------
    # The address ends in the domain rather than in TAIL, because the tail has
    # to be something an address may plausibly end with: a candidate that
    # rejects the value for not looking like an address is making a defensible
    # choice and M2 does not care which way it goes, only that nothing
    # shortened lands.
    over_email = OVERLONG_EMAIL_MARK + "e" * (LIMIT_EMAIL + 40) + EMAIL_TAIL
    r = env.app().post("/mode/contest/user/signup",
                       {"Username": MARK + "-overlong", "Password": PASSWORD,
                        "Confirm": PASSWORD, "Email": over_email})
    seen["overlong_email"] = {"status": r["status"],
                              "sent_len": len(over_email)}

    # -- S10: an ordinary request still gets logged ------------------------
    seen["plain_get"] = {"status": env.app().get("/about")["status"]}
    return seen


def _prefix(value, n):
    return _s(value)[:n] if value is not None else ""


def _intact(value, tail):
    """One stored over-length value: how long it is, and whether it is whole.

    `intact` false is the truncation signature, and it is a statement about the
    tail rather than about the length so that it holds whatever the transport
    did to the front of the value.
    """
    text = _s(value) or ""
    return {"len": len(text), "intact": text.endswith(tail)}


def _read(env, before):
    """What the requests left behind: only rows this pass created."""
    out = {}

    stats = env.rows(
        "SELECT ID, Query, UserAgent FROM jf_stats WHERE ID > %s ORDER BY ID",
        (before["jf_stats"],))
    out["stats_rows"] = len(stats)
    q_mark, u_mark = MARK + "-q", MARK + "-u "
    # The query string is matched by containment, not by prefix: what reaches
    # the column is `REDIRECT_QUERY_STRING` with its first element removed,
    # so the marker is near the front but not at it.
    over_q = [r for r in stats if q_mark in _s(r[1])[:64]]
    over_u = [r for r in stats if _prefix(r[2], len(u_mark)) == u_mark]
    out["overlong_query_rows"] = [_intact(r[1], TAIL) for r in over_q]
    out["overlong_useragent_rows"] = [_intact(r[2], TAIL) for r in over_u]
    # Ordinary traffic: this pass's rows with neither probe's marker on them.
    # The over-long *query* request sends the default User-Agent, so counting
    # on the agent alone would fold a row that is under test in M1 into a check
    # about untouched traffic -- and then S10 would be demanding the candidate
    # keep logging a value M1 forbids it to store.
    out["stats_ordinary"] = sum(
        1 for r in stats
        if _s(r[2]) == DEFAULT_UA and q_mark not in _s(r[1])[:64])

    users = env.rows(
        "SELECT ID, Username FROM jf_users WHERE ID > %s ORDER BY ID",
        (before["jf_users"],))
    out["users_rows"] = [_s(r[1]) for r in users]

    xuser = env.rows(
        "SELECT ID, Email, TemporaryResetPassword, TemporaryResetPasswordTimeout,"
        " LastLoginTimestamp, FailedLoginAttempts, LockTimeout, Activated,"
        " CreateTimestamp, PasswordChangeTimestamp"
        " FROM jf_xuser WHERE ID > %s ORDER BY ID", (before["jf_xuser"],))
    out["xuser_rows"] = [{
        "email": _s(r[1]), "temporary_reset_password": _s(r[2]),
        "temporary_reset_password_timeout": _i(r[3]),
        "last_login_timestamp": _i(r[4]), "failed_login_attempts": _i(r[5]),
        "lock_timeout": _i(r[6]), "activated": _i(r[7]),
        "create_timestamp": _i(r[8]), "password_change_timestamp": _i(r[9]),
    } for r in xuser]

    # Keyed by ChallengeName, which is ASCII and is the one field of the five
    # that no probe here is trying to bend. A challenge the INSERT never made
    # is simply absent, which is what every check on it then reports.
    chal = env.rows(
        "SELECT ID, ChallengeName, NameToDisplay, Points, CorrectFlag"
        " FROM contest_challenges WHERE ID > %s ORDER BY ID",
        (before["contest_challenges"],))
    out["challenge_rows"] = {_s(r[1]): {
        "name_hex": _hex(r[2]), "name_len": len(_s(r[2]) or ""),
        "points": _i(r[3]), "correct_flag": _s(r[4])} for r in chal}
    return out


def _session_row(env, session_id):
    rows = env.rows(
        "SELECT AccessCount, LastAccess, CurrentRequest IS NULL, CurrentRequest"
        " FROM jf_session WHERE SessionID=%s", (session_id,))
    if not rows:
        return {"row": False}
    r = rows[0]
    return {"row": True, "access_count": _i(r[0]), "last_access": _i(r[1]),
            "current_request_null": bool(r[2]),
            "current_request_len": len(_s(r[3])) if r[3] is not None else None}


def observe(env, schema=None, database=DATABASE, roots=("/app",),
            config_path=MY_CNF, socket_path=SOCKET, tools=None,
            log_path=GENERAL_LOG, docroot=ready.DOCROOT):
    """Everything this gate knows about a deployment, in one structure.

    The recorder and the verifier call exactly this, and `check()` takes two of
    these and nothing else. The static evidence -- the config file, the tree,
    the live catalogue, the socket -- is gathered here rather than in `check()`
    so that the candidate's tree and the recording are described by the same
    code and compared field for field, the way the probes are.
    """
    before = _max_ids(env)
    seen = _drive(env)
    session_id = seen.pop("_session_id")
    out = {"sql_mode": server_sql_mode(env), "seen": seen,
           "stored": _read(env, before)}
    out["stored"]["session"] = _session_row(env, session_id)
    out["config_sql_mode"] = list(config_sql_mode(config_path))
    mentions, scan = sql_mode_mentions(roots, tools)
    out["sql_mode_mentions"] = [list(h) for h in mentions]
    # Which of those files the application actually loads. Measured after the
    # probes have been driven and the rows read, because it serves four more
    # requests of its own and they must not land inside anything's snapshot.
    loaded, loaded_note = loaded_files(docroot=docroot)
    out["sql_mode_loaded"] = sorted(loaded) if loaded is not None else None
    out["sql_mode_scan"] = list(scan) + [loaded_note]
    out["logged_sql_mode"] = general_log_settings(log_path)
    try:
        out["init_connect"] = _s(env.scalar("SELECT @@GLOBAL.init_connect"))
    except Exception:
        out["init_connect"] = None
    out["schema_differences"] = schema_differences(
        schema["tables"], live_schema(env, database)) if schema else None
    out["root_over_socket"] = list(root_over_socket(database, socket_path))
    return out


# ------------------------------------------------------- static evidence ---

# `SET sql_mode`, `SET SESSION sql_mode`, `SET @@sql_mode`, or a `sql_mode=`
# inside a DSN or an option array. Any of them per connection would undo the
# server's setting for the application only, which is the one way to pass every
# other check here while leaving the deployment exactly as permissive as it was.
SQL_MODE_RE = re.compile(
    r"(?is)(?:\bSET\b[^;'\"]{0,40}?\bsql_mode\b|@@\s*(?:GLOBAL|SESSION)?\s*\.?\s*sql_mode"
    r"|\bsql_mode\s*=)")

SOURCE_SUFFIXES = (".php", ".inc", ".ini", ".cnf", ".sql", ".sh")

# What is still read as lines. `.php` is not, because a line is the wrong unit
# for PHP in both directions: `$conn->query("SET SESSION " . "sql_mode" .
# " = ''")` is one statement written over three lines and no line of it names
# sql_mode, while `# sql_mode lives in my.cnf` is a comment that names it and
# runs nothing. The rest are configuration files and SQL dumps, where a line is
# the unit the file itself uses and there is no syntax to read instead.
LINE_SUFFIXES = tuple(s for s in SOURCE_SUFFIXES if s != ".php")

# Only version control is skipped. A relaxation hidden under `vendor/` or
# `tests/` would still be a relaxation the application runs, so those are read
# like everything else.
SKIP_DIRS = {".git", ".svn", ".hg"}


def _line_mentions(path):
    """[(path, line number, line)] for one file, read as lines."""
    hits = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for n, line in enumerate(fh, 1):
                if SQL_MODE_RE.search(line):
                    hits.append((path, n, line.strip()[:200]))
    except OSError:
        pass
    return hits


def _folded(text):
    """One folded string expression, as a line of report text.

    The extractor marks every part it could not evaluate with `sqlgate.HOLE`;
    `${}` is the same thing in something the reader of a report can see.
    """
    return " ".join(text.replace(sqlgate.HOLE, "${}").split())[:200]


def sql_mode_mentions(roots, tools=None):
    """([(path, line, text)], [note]) -- every place the tree names sql_mode.

    PHP is read as syntax and everything else as lines. `sqlextract.php` folds
    each string expression -- a literal, an interpolation, or a whole `.` chain
    -- into the one text it becomes, so a relaxation split across pieces
    arrives here whole and is matched like any other. Every folded string is
    matched whatever its sink: an option array's `MYSQL_ATTR_INIT_COMMAND`
    value is handed to no call site this module could name, and relaxes every
    connection the driver opens. The pieces of one variable are then joined and
    matched again -- see `_php_mentions`.

    A file the parser cannot read is not a silent skip. It falls back to the
    line scan, which is the weaker reading, and the notes say so -- a tree that
    has stopped parsing is a tree this gate has stopped reading properly, and
    that belongs in the report rather than in nobody's hands.
    """
    line_files, php_files = [], []
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in filenames:
                path = os.path.join(dirpath, name)
                if name.endswith(".php"):
                    php_files.append(path)
                elif name.endswith(LINE_SUFFIXES):
                    line_files.append(path)

    notes, folded, as_lines = [], [], []
    try:
        data = sqlgate.run_extractor(roots, tools)
    except Exception as exc:
        as_lines = sorted(php_files)
        notes.append("the PHP parser did not run (%s); %d .php file(s) were"
                     " read as lines instead"
                     % (str(exc).strip()[:200] or exc.__class__.__name__,
                        len(as_lines)))
    else:
        folded = data["strings"]
        as_lines = sorted({f["file"] for f in data["unparsable"]})
        notes.append("%d .php file(s) parsed, %d string expression(s) folded"
                     % (data["files"], len(folded)))
        if as_lines:
            notes.append("the PHP parser could not read %d file(s), which were"
                         " read as lines instead: %s"
                         % (len(as_lines), ", ".join(as_lines[:10])))

    hits = []
    for path in line_files:
        hits.extend(_line_mentions(path))
    for path in as_lines:
        hits.extend(_line_mentions(path))
    hits.extend(_php_mentions(folded))
    return hits, notes


def _php_mentions(folded):
    """[(file, line, text)] for the folded strings that name sql_mode.

    Two readings of the same list. Every string is matched on its own, whatever
    its sink. Then the pieces the extractor attributed to one variable in one
    function are joined in line order and matched again, because

        $q = "SET SESSION sql_";
        $q .= "mode = ''";
        $conn->query($q);

    spells the relaxation in the tree with neither half naming it. Joining
    invents nothing: the extractor has already decided those pieces are `$q`,
    and `$q` is already what the driver is handed. A chain whose pieces matched
    on their own is left out, so nothing is reported twice.
    """
    hits, chains = [], {}
    for s in sorted(folded, key=lambda s: (s["file"], s["line"])):
        matched = bool(SQL_MODE_RE.search(s["text"]))
        if matched:
            hits.append((s["file"], s["line"], _folded(s["text"])))
        if (s["sink"] or "").startswith("var:"):
            key = (s["file"], str(s["class"]), str(s["function"]), s["sink"])
            chain = chains.setdefault(key, [])
            chain.append((s, matched))
    for key in sorted(chains):
        parts = chains[key]
        if len(parts) < 2 or any(m for _s, m in parts):
            continue
        text = "".join(s["text"] for s, _m in parts)
        if SQL_MODE_RE.search(text):
            hits.append((key[0], parts[0][0]["line"], _folded(text)))
    return hits


# `!include <file>` and `!includedir <dir>`, which mysqld acts on where it
# meets them.
INCLUDE_RE = re.compile(r"(?i)^!(include|includedir)\s+(.+?)\s*$")

SETTING_RE = re.compile(r"(?i)^sql[-_]mode\s*=\s*(.*)$")

# An option file is a sequence of groups, and mysqld reads only some of them.
# Scoping matters in both directions. A `sql_mode` under `[client]` or
# `[mysqld_safe]` -- both of which this deployment's my.cnf already has, below
# the server group -- does not reach the server at all, so crediting it would
# pass a tree whose server is still permissive; and because this reader keeps
# the *last* match it finds, an unscoped read would also let a stray
# `[client] sql_mode = ` further down the file silently undo a correct
# `[mysqld]` setting above it and fail a candidate who did the work. mysqld
# reads `[mysqld]`, `[server]`, and the version-suffixed `[mysqld-5.7]`; a
# setting before any group header is in no group and is read by nobody.
SECTION_RE = re.compile(r"^\[\s*([^\]]+?)\s*\]$")
SERVER_SECTION_RE = re.compile(r"(?i)^(mysqld|server|mysqld-\d+\.\d+)$")

# mysqld stops following includes at ten levels (`my_default.c`), so this stops
# there too -- and a Python recursion error inside `observe()` would fail the
# whole family rather than this one reading.
MAX_INCLUDE_DEPTH = 10


# ------------------------------------------------- what the application runs --

# The paths the front controller is asked for when the loaded set is measured.
# Any one of them loads the framework, the configuration and the database
# adapter, which is where a relaxation has to live to reach the connections the
# application uses; the rest are here so that a few controllers are loaded too.
# A path that routes nowhere still loads everything before the routing.
LOADED_PATHS = ("/about", "/login", "/lessons", "/")

# `auto_prepend_file` is the one hook that runs before the front controller and
# can register a shutdown handler, so the invocation stays otherwise identical
# to `ready.app_ready()`'s. The handler runs after `exit()` and after a fatal
# error, which is what makes this a measurement rather than a happy path.
LOADED_PROBE = """<?php
register_shutdown_function(function () {
    $out = getenv('WG_LOADED_OUT');
    if ($out) { @file_put_contents($out, json_encode(get_included_files())); }
});
"""


def loaded_files(docroot=ready.DOCROOT, paths=LOADED_PATHS, prefix=None,
                 timeout=60.0):
    """(set of absolute paths, note) -- the files the application itself loads.

    The step forbids the *application* putting its connections back out of
    strict mode. That is a statement about the code the application runs, and
    the tree is not the same thing as the code the application runs: a
    candidate who writes a scratch harness to check their own repairs against
    a permissive server has added a file that names sql_mode and changed
    nothing about the deployment. Reading the tree whole cannot tell the two
    apart, so this asks PHP, which keeps the list.

    None -- not an empty set -- when the question could not be put: no front
    controller, or not one run of it produced a list. The caller reads None as
    "no measurement" and falls back to the whole tree, which is the stricter
    reading and the safe direction for an instrument that has gone blind.

    Runs as the account that serves the application, like every other thing
    here that executes candidate code.
    """
    import json as _json
    import shutil
    import tempfile

    if not os.path.isfile(os.path.join(docroot, "jframework.php")):
        return None, "no front controller at %s/jframework.php" % docroot
    if prefix is None:
        prefix = deploy.as_user(deploy.APP_USER, [])

    tmp = tempfile.mkdtemp(prefix="wg-loaded-", dir="/tmp")
    try:
        os.chmod(tmp, 0o777)
        shim = os.path.join(tmp, "prepend.php")
        with open(shim, "w", encoding="utf-8") as fh:
            fh.write(LOADED_PROBE)
        os.chmod(shim, 0o644)

        seen, answered = set(), 0
        for n, path in enumerate(paths):
            out = os.path.join(tmp, "loaded-%d.json" % n)
            cmd = (list(prefix) + ready.frozen_prefix()
                   + ["env", "WG_LOADED_OUT=" + out,
                      "php", "-d", "auto_prepend_file=" + shim,
                      "jframework.php", path])
            try:
                subprocess.run(cmd, cwd=docroot, capture_output=True,
                               timeout=timeout)
            except (OSError, subprocess.TimeoutExpired):
                continue
            try:
                with open(out, "r", encoding="utf-8") as fh:
                    got = _json.load(fh)
            except (OSError, ValueError):
                continue
            answered += 1
            for item in got:
                if isinstance(item, str):
                    seen.add(os.path.realpath(item))
        seen.discard(os.path.realpath(shim))
        if not answered:
            return None, ("the application loaded nothing this could read:"
                          " %d front-controller run(s), none answered"
                          % len(paths))
        return seen, ("%d of %d front-controller run(s) answered; the"
                      " application loads %d file(s)"
                      % (answered, len(paths), len(seen)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def config_sql_mode(path=MY_CNF):
    """(the last sql_mode the configuration sets, the lines that set it).

    The configuration is not one file. mysqld reads `!include <file>` and
    `!includedir <dir>` at the point they appear, relative to the directory of
    the file that names them, and `!includedir` takes that directory's `*.cnf`
    in name order -- which is how a Debian server ends up configured out of
    `/etc/mysql/conf.d/` with almost nothing in `my.cnf` itself. Reading
    `my.cnf` alone would answer "nothing sets sql_mode" for a server that is
    correctly and permanently strict, and fail `S2` on a candidate who did the
    work in the idiomatic place.

    The order is mysqld's, so the answer is still the *last* setting read. A
    target that is not there is ignored, the way mysqld ignores it, and a file
    already read is not read again, so an include cycle ends rather than hangs.
    """
    if not os.path.isfile(path):
        return None, ["%s does not exist" % path]
    found = {"value": None, "lines": [], "seen": set()}
    _read_config(path, found, 0)
    return found["value"], found["lines"]


def _read_config(path, found, depth):
    """One option file, and everything it includes, folded into `found`."""
    if depth > MAX_INCLUDE_DEPTH or not os.path.isfile(path):
        return
    try:
        real = os.path.realpath(path)
    except OSError:
        return
    if real in found["seen"]:
        return
    found["seen"].add(real)
    here = os.path.dirname(os.path.abspath(path))
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            lines = list(enumerate(fh, 1))
    except OSError:
        return
    # Per file: an included file's groups are its own, and a group header in
    # it does not continue into the file that included it or back out of it.
    section = None
    for n, line in lines:
        bare = line.split("#", 1)[0].strip()
        head = SECTION_RE.match(bare)
        if head:
            section = head.group(1)
            continue
        inc = INCLUDE_RE.match(bare)
        if inc:
            target = inc.group(2).strip().strip("'\"")
            if not os.path.isabs(target):
                target = os.path.join(here, target)
            if inc.group(1).lower() == "include":
                _read_config(target, found, depth + 1)
                continue
            try:
                names = sorted(os.listdir(target))
            except OSError:
                names = []
            for name in names:
                if name.endswith(".cnf"):
                    _read_config(os.path.join(target, name), found, depth + 1)
            continue
        m = SETTING_RE.match(bare)
        if m and section is not None and SERVER_SECTION_RE.match(section):
            found["value"] = m.group(1).strip().strip("'\"")
            found["lines"].append("%s:%d [%s] %s" % (path, n, section, bare))


# ------------------------------------------------------- runtime evidence ---
#
# Everything above reads the tree. None of it can read a statement the tree
# does not contain: `$this->q("SET SESSION sql_" . $k . "=" . $v)` is assembled
# while the request is being served and no file holds it. That one statement
# would leave the config file strict, the server's global mode strict, and
# every repair probe answered by a connection that is not -- the whole step
# scored for work that was not done.
#
# So the server is asked what it was actually sent. `deploy.start_mysql` starts
# mysqld with the general query log on, and the log records every statement of
# every connection after the application has finished assembling it, so how it
# was assembled stops mattering. By the time this gate runs the corpus has
# already been served through the application's own driver -- `plan.py` runs
# the two behaviour families before the three `migrated:` ones -- so the log
# holds what the deployment really does, not what it would do if asked.
#
# This is evidence the recording does not have: it was made before the log
# existed. So it is kept under its own key, read from the observation only, and
# an absent log is the absence of evidence rather than evidence of absence. A
# candidate is not marked down for a file the scorer did not manage to write.

# One entry per line, `<time>\t<connection id> <Command>\t<argument>`, and a
# statement that spans lines continues on the ones after it with no header.
LOG_ENTRY_RE = re.compile(r"^[^\t]*\t\s*\d+\s+([A-Za-z][A-Za-z ]*?)\s*\t(.*)$")

# The commands whose argument is a statement. `Connect` carries credentials and
# `Quit` carries nothing; neither can set a mode.
LOG_COMMANDS = ("Query", "Prepare", "Execute")

# An assignment to the mode, in every spelling: `SET sql_mode=`,
# `SET SESSION sql_mode =`, `SET @@GLOBAL.sql_mode :=`. What comes before the
# name is not enumerated, because it is the value that gets judged.
#
# It stops at the `=`. Matching the whitespace after it as well would be run
# against the masked copy, where a quoted value is a run of spaces, and the
# match would swallow the value whole and report every setting as empty.
ASSIGN_RE = re.compile(r"(?i)\bsql_mode\b\s*:?=")

# Values that name no mode because they inherit one. `@@GLOBAL.sql_mode` is
# whatever the server was configured with and `DEFAULT` is the same thing
# spelled shorter; both leave a strict server strict.
INHERITS_RE = re.compile(
    r"(?i)@@\s*(?:(?:GLOBAL|SESSION|LOCAL)\s*\.\s*)?sql_mode|^\s*DEFAULT\s*$")

# How many distinct setting statements are kept. The log holds every statement
# of a 413-request corpus; a report wants the evidence, not the transcript.
LOG_KEEP = 20


def _mask_quotes(sql):
    """`sql` with every quoted run blanked out, one space per character.

    An offset in the mask is an offset in the original, so something found in
    the mask can be read back out of the statement itself. Quoted text is
    masked because `INSERT INTO jf_stats VALUES ('sql_mode=1')` stores a string
    and sets nothing -- and a gate that could not tell those apart would fail a
    candidate for the data the step told them to preserve.
    """
    out = list(sql)
    i, n, quote = 0, len(sql), ""
    while i < n:
        c = sql[i]
        if not quote:
            if c in "'\"`":
                quote, out[i] = c, " "
            i += 1
            continue
        if c == "\\" and quote != "`" and i + 1 < n:
            out[i] = out[i + 1] = " "
            i += 2
            continue
        out[i] = " "
        if c == quote:
            quote = ""
        i += 1
    return "".join(out)


def _normalize_statement(sql):
    """`sql` as the server parses it, for finding an assignment in.

    Comments outside quotes are removed -- except that a `/*!NNNNN ... */`
    versioned comment is code to MySQL, so its body stays and only its
    markers go -- and backtick quoting is dropped, because `` `sql_mode` `` is
    the same identifier as `sql_mode`. (Round-3 migration review,
    2026-10-05: `SET SESSION sql_mode/**/=''` was read as no assignment at
    all, in the statement log and in `init_connect`, and so as no relaxation.)
    """
    mask = _mask_quotes(sql)
    out, i, n = [], 0, len(sql)
    while i < n:
        if mask.startswith("/*!", i):
            j = i + 3
            while j < n and sql[j].isdigit():
                j += 1
            out.append(" ")
            i = j
            continue
        if mask.startswith("/*", i):
            j = mask.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
            continue
        if mask.startswith("*/", i):
            out.append(" ")
            i += 2
            continue
        if ((mask.startswith("--", i) and (i + 2 >= n or mask[i + 2].isspace()))
                or mask[i] == "#"):
            j = mask.find("\n", i)
            i = n if j < 0 else j
            out.append(" ")
            continue
        if sql[i] == "`":
            out.append(" ")
            i += 1
            continue
        out.append(sql[i])
        i += 1
    return "".join(out)


def sql_mode_values(statement):
    """Every value `statement` assigns to sql_mode, in the order it does.

    The statement is not required to begin with `SET`. The assignment is found
    wherever it sits, so a second statement after a `;` in one `multi_query()`,
    an assignment third in a list of them, and a `/*!40101 SET SQL_MODE=... */`
    that only MySQL executes are all read the same way. The value runs to the
    next comma or semicolon outside quotes and parentheses, so
    `CONCAT(@@sql_mode, ',STRICT_ALL_TABLES')` is one value and not two.
    The statement is read as `_normalize_statement` leaves it.
    """
    statement = _normalize_statement(statement)
    mask = _mask_quotes(statement)
    values = []
    for m in ASSIGN_RE.finditer(mask):
        start = m.end()
        while start < len(statement) and statement[start].isspace():
            start += 1
        depth, end = 0, len(statement)
        for j in range(start, len(mask)):
            c = mask[j]
            if c in "([":
                depth += 1
            elif c in ")]":
                if depth == 0:
                    end = j
                    break
                depth -= 1
            elif c in ",;" and depth == 0:
                end = j
                break
        values.append(statement[start:end].strip())
    return values


def general_log_settings(path=GENERAL_LOG):
    """The statements the server logged that assign sql_mode, or None.

    None means there is nothing to read -- no log file, or one that would not
    open -- and the caller treats that as no evidence rather than as a finding.
    """
    if not os.path.isfile(path):
        return None
    found = []

    def keep(command, argument):
        if command not in LOG_COMMANDS or "sql_mode" not in argument.lower():
            return
        if not sql_mode_values(argument):
            return
        text = " ".join(argument.split())[:400]
        if text not in found and len(found) < LOG_KEEP:
            found.append(text)

    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            command, argument = "", ""
            for line in fh:
                m = LOG_ENTRY_RE.match(line)
                if m:
                    keep(command, argument)
                    command, argument = m.group(1).strip(), m.group(2)
                else:
                    argument += "\n" + line.rstrip("\n")
            keep(command, argument)
    except OSError:
        return None
    return found


# A value keeps a strict server strict only in the shapes that cannot take a
# flag away: the inherited mode on its own, a literal list that names a strict
# flag, or a CONCAT of those two kinds of operand (which can only add flags).
# Anything else -- REPLACE(@@sql_mode, 'STRICT_TRANS_TABLES', ''), a user
# variable, a subquery, a literal whose only strict flag sits in a comment --
# is judged as relaxing, because the flag name appearing somewhere inside the
# expression says nothing about whether it survives evaluation. (Round-1
# migration review, 2026-10-05: both of those spellings used to pass G11.)
_VALUE_INHERITS_RE = re.compile(
    r"(?is)^\s*(?:@@\s*(?:(?:GLOBAL|SESSION|LOCAL)\s*\.\s*)?sql_mode|DEFAULT)\s*$")
_VALUE_LITERAL_RE = re.compile(r"""(?s)^\s*(?:'([^'\\]*)'|"([^"\\]*)")\s*$""")
_VALUE_BARE_RE = re.compile(r"^\s*[A-Za-z_][A-Za-z_,]*\s*$")
_VALUE_CONCAT_RE = re.compile(r"(?is)^\s*CONCAT\s*\((.*)\)\s*$")


def _strip_sql_comments(value):
    """`value` with every comment outside quotes removed."""
    mask = _mask_quotes(value)
    out, i, n = [], 0, len(value)
    while i < n:
        if mask.startswith("/*", i):
            j = mask.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
            continue
        if mask.startswith("--", i) or mask[i] == "#":
            j = mask.find("\n", i)
            i = n if j < 0 else j
            out.append(" ")
            continue
        out.append(value[i])
        i += 1
    return "".join(out)


def _split_top_level(text):
    """`text` split at the commas that sit outside quotes and parentheses."""
    mask = _mask_quotes(text)
    parts, depth, start = [], 0, 0
    for j, c in enumerate(mask):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(text[start:j])
            start = j + 1
    parts.append(text[start:])
    return parts


def keeps_strict(value):
    """True when assigning `value` to sql_mode leaves a strict server strict."""
    v = _strip_sql_comments(value or "").strip()
    # The close of a `/*!40101 ... */` version comment the value sat inside.
    v = re.sub(r"\*/\s*$", "", v).strip()
    if _VALUE_INHERITS_RE.match(v):
        return True
    m = _VALUE_LITERAL_RE.match(v)
    if m:
        return is_strict(m.group(1) if m.group(1) is not None else m.group(2))
    if _VALUE_BARE_RE.match(v):
        return is_strict(v)
    m = _VALUE_CONCAT_RE.match(v)
    if m:
        parts = _split_top_level(m.group(1))
        inherit = [p for p in parts if _VALUE_INHERITS_RE.match(p)]
        literal = [p for p in parts if _VALUE_LITERAL_RE.match(p)]
        if len(inherit) + len(literal) != len(parts):
            return False
        if inherit:
            return True
        return is_strict(",".join(_VALUE_LITERAL_RE.match(p).group(1) or
                                  _VALUE_LITERAL_RE.match(p).group(2) or ""
                                  for p in literal))
    return False


def relaxing_statements(logged):
    """The logged statements that set sql_mode to something not strict."""
    out = []
    for statement in logged or ():
        for value in sql_mode_values(statement):
            if keeps_strict(value):
                continue
            out.append(statement)
            break
    return out


def relaxing_init_connect(init_connect):
    """The server's `init_connect`, if it can take a connection out of strict.

    `init_connect` runs for every connection the application's own account
    opens, never appears in the statement log, and lives in the server's
    configuration rather than in `/app`, so neither of the other two readings
    sees it. It relaxes when it assigns sql_mode a value `keeps_strict` does
    not accept, or when it calls a routine, whose body this cannot read.
    """
    text = init_connect or ""
    if not text.strip():
        return None
    if re.search(r"(?i)\bCALL\b", _mask_quotes(text)):
        return text
    for value in sql_mode_values(text):
        if not keeps_strict(value):
            return text
    return None


def root_over_socket(database, socket_path="/var/run/mysqld/mysqld.sock"):
    """(ok, detail). Step 01's instruction says this keeps working."""
    try:
        conn = collect.open_mysql(user="root", password="",
                                  database=database, unix_socket=socket_path)
    except Exception as exc:
        return False, "root over %s: %s" % (socket_path, exc)
    try:
        conn.close()
    except Exception:
        pass
    return True, "root over %s" % socket_path


# -------------------------------------------------------------- schema ---

SCHEMA_KEEP = ("column_type", "length", "charset", "collation", "type")


def live_schema(env, database):
    """{table: {"engine", "collation", "columns": [{...}]}} from the server."""
    tables = {}
    for name, engine, collation in env.rows(
            "SELECT table_name, engine, table_collation"
            " FROM information_schema.tables"
            " WHERE table_schema=%s AND table_type='BASE TABLE'", (database,)):
        tables[_s(name)] = {"engine": _s(engine), "collation": _s(collation),
                            "columns": {}}
    for row in env.rows(
            "SELECT table_name, column_name, column_type, character_set_name,"
            " collation_name, character_maximum_length, data_type"
            " FROM information_schema.columns WHERE table_schema=%s",
            (database,)):
        table = _s(row[0])
        if table in tables:
            tables[table]["columns"][_s(row[1])] = {
                "column_type": _s(row[2]), "charset": _s(row[3]),
                "collation": _s(row[4]), "length": _i(row[5]),
                "type": _s(row[6])}
    return tables


def schema_differences(expected, actual):
    """{"tables": [str], "columns": [str]} -- every declared shape that moved.

    Nullability and defaults are deliberately not compared: giving a column
    the `DEFAULT` the schema never had is one of the legitimate ways to repair
    an INSERT that omits it, and this step is not the one that decides what the
    PostgreSQL schema looks like.
    """
    tables, columns = [], []
    for table in sorted(expected):
        have = actual.get(table)
        if have is None:
            tables.append("%s: table missing" % table)
            continue
        want_engine = expected[table].get("engine")
        if want_engine and have["engine"] != want_engine:
            tables.append("%s: engine %s -> %s"
                          % (table, want_engine, have["engine"]))
        want_coll = expected[table].get("collation")
        if want_coll and have["collation"] != want_coll:
            tables.append("%s: table collation %s -> %s"
                          % (table, want_coll, have["collation"]))
        for col in expected[table]["columns"]:
            name = col["name"]
            got = have["columns"].get(name)
            if got is None:
                columns.append("%s.%s: column missing" % (table, name))
                continue
            for key in SCHEMA_KEEP:
                if col.get(key) != got.get(key):
                    columns.append("%s.%s: %s %r -> %r"
                                   % (table, name, key, col.get(key),
                                      got.get(key)))
    extra = sorted(set(actual) - set(expected))
    if extra:
        tables.append("tables the recording never had: %s" % ", ".join(extra))
    return {"tables": tables, "columns": columns}


# --------------------------------------------------------------- checking ---

# What jf_session's row is compared on: the bookkeeping the UPDATE does, and
# both readings of CurrentRequest -- whether it came out NULL, and how long it
# is.
#
# The permissive server invents `''` for that column, because `jf::$Request` is
# never assigned (`_japp/model/lib/session.php`) and the UPDATE therefore binds
# a NULL into a column that forbids one. So the deployment's own answer is "not
# NULL, length zero", the recording carries exactly that
# (`current_request_null: false`, `current_request_len: 0`), and "every value
# the server invents today must still end up in that column, with the same
# value" is a question this comparison can now ask. Keeping only the three
# bookkeeping fields recorded the column and then never read it: an application
# that filled it with the request it was serving changed what the deployment
# stores and scored the same as one that did not.
#
# The contents past the length stay out, because at length zero there are no
# contents to disagree about, and jf_session is excluded from the corpus's
# comparable tables, so nothing else asks either.
SESSION_KEEP = ("row", "access_count", "last_access",
                "current_request_null", "current_request_len")

XUSER_KEEP = ("email", "temporary_reset_password",
              "temporary_reset_password_timeout", "last_login_timestamp",
              "failed_login_attempts", "lock_timeout", "activated",
              "create_timestamp", "password_change_timestamp")

# ChallengeName is the key, so it is not in the comparison.
CHALLENGE_KEEP = ("name_hex", "name_len", "points", "correct_flag")

# S8 is the two values `latin1` can hold, S9 the two it cannot; the split is in
# the ids because the instruction and the report are read by someone asking
# which kind of text moved.
ENCODING_CHECK_ID = {"cp1252-range": "S8-cp1252-range",
                     "cp1252-only": "S8-cp1252-only",
                     "outside-latin1": "S9-outside-latin1",
                     "raw-bytes": "S9-raw-bytes"}


def _safe(checks, check_id, fn):
    """One check, whose own failure to evaluate is its own failure.

    Without this a single missing key takes the whole family to `gate.crashed`
    and the other eighteen checks stop being evidence.
    """
    try:
        ok, detail = fn()
    except Exception as exc:
        ok, detail = False, "could not be evaluated: %r" % (exc,)
    checks.add(check_id, bool(ok), detail)


def _eq(want, got, label):
    return want == got, ("" if want == got
                         else "%s: recorded %r, found %r" % (label, want, got))


def _subset(keys, row):
    return None if row is None else {k: row.get(k) for k in keys}


def _shortened(rows):
    """The rows holding a value that lost its tail on the way in."""
    return [r for r in rows if not r["intact"]]


def _flip_check(recorded, observed, key, label):
    """A must-flip probe: the recording truncated, the candidate must not.

    The recording is checked first. A probe whose baseline shows no truncation
    is measuring nothing, and saying so is more use than a green tick.

    What passes: no row at all (the write was rejected, or the request was), or
    a row whose value still carries its tail (it was stored whole, or it was
    replaced by something that is not a silently shortened version of it).
    What fails: a row that begins with what was sent and does not end with it.
    """
    sent = observed["seen"][key]["sent_len"]
    rec_sent = recorded["seen"][key]["sent_len"]
    if sent != rec_sent:
        return False, ("instrument: sent %d characters, the recording sent %d"
                       % (sent, rec_sent))
    if not _shortened(recorded["stored"][key + "_rows"]):
        return False, ("instrument: the recording stores no shortened %s, so"
                       " this probe measures nothing" % label)
    got = _shortened(observed["stored"][key + "_rows"])
    if got:
        return False, ("%d characters were sent and a row holding %s of them"
                       " was written: the value was still silently shortened"
                       % (sent, ", ".join(str(r["len"]) for r in got)))
    return True, ""


def no_relaxation(recorded, observed):
    """(ok, detail): the tree does not turn the mode back off.

    Two readings, and either one fails it. The server's own statement log is
    read, so a relaxation the tree never spells -- assembled while a request
    was served, out of pieces that are each innocent -- is caught by what
    actually ran; and the tree is read as syntax, so a relaxation spread across
    a concatenation or parked in an option array is one string here and is
    compared like any other, whether or not the corpus happened to run it.
    Neither reading can be traded for the other.

    What the static reading is *scoped to* is the part that had to be measured
    rather than assumed. The step forbids the application putting its own
    connections back out of strict mode. A file the application never loads
    puts no connection anywhere: a scratch harness that opens its own
    connection to its own scratch database, sets the mode permissive on it, and
    compares what that server stores against what the candidate's repairs
    produce is the candidate *measuring* the thing this step is about, and
    failing them for it is failing the work for the instrument that proves it.
    Measured on the step-1 blind run, where exactly that file took a tree with
    404/404 responses, 50/50 table snapshots and every repair landed from 1.000
    to 0.067 (`NOTES-authoring.md` §19.12).

    So the tree is read whole, and the hits are then split by whether the
    application loads the file -- which `loaded_files()` asks PHP rather than
    inferring. A hit in a file the application loads fails this. A hit in a
    file it does not is named in the detail and costs nothing, because the step
    never asked the candidate to keep the tree tidy and a requirement that is
    not stated is not scored.

    The narrowing is conditional on there being runtime evidence to back it.
    With no statement log, or no measurement of what the application loads, the
    tree is read whole again and the detail says so: an instrument that has
    gone blind should be the stricter one, not the more forgiving one.

    Line numbers are dropped before the comparison, because inserting a line
    above a file's dump header moves a mention without changing it.

    This is scored with the mode itself rather than among the guards, and the
    repairs are preconditioned on it, because a `SET SESSION sql_mode = ''` in
    the database driver does not merely lose a point: it hands every other
    check in the step back to the permissive server, which passes them all by
    reproducing the recording it made. A deduction for that would leave a
    one-line defeat of the whole step scoring above 0.9.
    """
    def mentions(obs):
        return [(path, "%s %s" % (path, text))
                for path, _n, text in obs["sql_mode_mentions"]]
    before = {key for _path, key in mentions(recorded)}
    added = [(path, key) for path, key in mentions(observed)
             if key not in before]
    def _relaxes(text):
        values = sql_mode_values(text)
        if values:
            return not all(keeps_strict(v) for v in values)
        bare = _normalize_statement(text)
        if (re.match(r"(?is)^\s*(?:SELECT|SHOW)\b", bare)
                and not re.search(r"(?i)\bSET\b", _mask_quotes(bare))):
            # A read of the mode -- `SELECT @@SESSION.sql_mode` in a check the
            # application makes of its own connection -- assigns nothing and
            # relaxes nothing. (Round-2 review, 2026-10-05: this used to fall
            # through to the line below and fail G11 for a pure read.) Only an
            # explicit read is exempt: a bare `@@SESSION.sql_mode` fragment is
            # how a `SET` built in another string names its target, so it is
            # judged like any mention whose value could not be read. (Round 3,
            # 2026-10-05: the exemption had been "no SET in this string".)
            return False
        return not is_strict(text)
    # The mention's own text, without the path the key carries in front of it.
    bad = [(path, key) for path, key in added if _relaxes(key[len(path) + 1:])]

    # The server's own per-connection hook, read off the running server.
    ic = relaxing_init_connect(observed.get("init_connect"))
    if ic:
        return False, ("the server's init_connect takes every application"
                       " connection back out of strict mode: %s" % ic[:200])

    # The log belongs to the observation alone: the recording predates it, so
    # there is nothing to subtract and nothing to compare against.
    logged = observed.get("logged_sql_mode")
    if logged is not None:
        ran = relaxing_statements(logged)
        if ran:
            return False, ("the server was sent a statement that sets sql_mode"
                           " to a value that is not strict: %s"
                           % "; ".join(s[:200] for s in ran[:5]))
        runtime = "%d logged setting(s), all strict" % len(logged)
    else:
        runtime = "no statement log"

    loaded = observed.get("sql_mode_loaded")
    if logged is None or loaded is None:
        blocking, stray = [key for _p, key in bad], []
        scope = "the whole tree (%s)" % (
            "no statement log" if logged is None
            else "the application's own file list could not be read")
    else:
        run = {os.path.realpath(path) for path in loaded}
        blocking = [key for path, key in bad if os.path.realpath(path) in run]
        stray = [key for path, key in bad if os.path.realpath(path) not in run]
        scope = "%d file(s) the application loads" % len(run)

    if blocking:
        return False, ("sql_mode is set where the recording did not set it,"
                       " and not to a strict value, in code the application"
                       " runs: %s" % "; ".join(blocking[:10]))

    detail = "%d new mention(s), none relaxing in %s; %s" % (
        len(added), scope, runtime)
    if stray:
        detail += ("; %d relaxing mention(s) in file(s) the application does"
                   " not load, which this does not score: %s"
                   % (len(stray), "; ".join(stray[:5])))
    scan = "; ".join(observed.get("sql_mode_scan") or ())
    return True, detail + (("; " + scan) if scan else "")


def _challenge(recorded, observed, name, label):
    """One `contest_challenges` row, compared field by field, as a check fn."""
    def fn():
        return _eq(
            _subset(CHALLENGE_KEEP,
                    recorded["stored"]["challenge_rows"].get(name)),
            _subset(CHALLENGE_KEEP,
                    observed["stored"]["challenge_rows"].get(name)),
            "the %s contest_challenges row" % label)
    return fn


def _added(recorded, observed, key):
    """What `observed` reports under `key` that `recorded` did not.

    Three checks work this way -- the sql_mode mentions in the tree, the
    schema's table shapes, the schema's column shapes -- and they work this way
    for the same reason. The untouched tree is not clean: it names sql_mode six
    times in mysqldump headers, and the corpus's own lesson resets recreate
    four tables in the database's default charset. Demanding an empty list
    would fail a correct answer for something it did not do. The recording is
    therefore the baseline, and what these ask is the only question worth
    asking: did the candidate add anything?
    """
    before = set(recorded.get(key) or [])
    return [x for x in (observed.get(key) or []) if x not in before]


# ---------------------------------------------------------------- families --
#
# Three families, and which is which was decided before the tree was looked at
# (`gate.combine`).
#
# `migrated:strict-mode` and `migrated:repairs` are *earned*: every check in
# them is false on the deployment the candidate is handed and true on the one
# the step asks for. The repairs are exactly the six readings that were
# measured to change when the mode goes on and nothing else does.
#
# `migrated:no-shortcut` is a *guard*: every check in it is already true before
# the step begins, so passing it is not evidence of work and it must not pay.
# It holds the must-flip probes, the readings strict mode does not disturb, and
# the schema and configuration the step must not reshape. The must-flip probes
# sit here rather than with the repairs for a measured reason: under strict and
# nothing else they already flip, so what they demonstrate is the mode being
# on, which `migrated:strict-mode` is already paid for. Scoring them twice
# would pay a one-line edit for the work it did not do. What they are still
# needed for is the wrong answer they are the only thing that catches -- an
# application that "fixes" strict mode by shortening values in PHP -- and a
# guard catches it just as well, at the cost of the multiplier.
MODE_FAMILY = "migrated:strict-mode"
REPAIR_FAMILY = "migrated:repairs"
GUARD_FAMILY = "migrated:no-shortcut"

MODE_IDS = ("S1-server-strict", "S2-config-strict",
             "G11-no-relaxation")

REPAIR_IDS = ("S3-session-update", "S4-signup-rows", "S5-xuser-values",
              "S7-contest-points", "S9-outside-latin1", "S9-raw-bytes")

GUARD_IDS = ("M1-overlong-query", "M2-overlong-email", "M3-overlong-useragent",
             "S6-signup-login", "S8-cp1252-range", "S8-cp1252-only",
             "S10-stats-logged", "G12-column-shapes", "G13-table-set",
             "G14-root-socket")

# Why the two families below are preconditioned on the mode being on, and why
# that is not bookkeeping: they ask whether the application supplies what the
# server used to invent for it. On a server that still invents it they are all
# true without the application having changed at all -- the recording
# reproduces itself, which is what an untouched tree does by definition. There
# is no question there to answer until the server stops answering it, and per
# `gate.py` a check that cannot be evaluated is a failure, never a skip.
PRECONDITION = ("the server still repairs invalid values silently"
                " (sql_mode=%s), so whether the application supplies them"
                " itself is not yet a question this deployment can answer")


def effective_mode(recorded, observed):
    """(ok, why): is the application actually running under strict mode?

    Two ways it is not, and the second is the one worth measuring: the server
    was never put into it, or the tree puts its own connections back out of it.
    Either way the repairs below are unanswerable, because the permissive
    server passes them by reproducing the recording it made.
    """
    if not is_strict(observed.get("sql_mode")):
        return False, PRECONDITION % observed.get("sql_mode")
    ok, detail = no_relaxation(recorded, observed)
    if not ok:
        return False, ("the tree takes its own connections back out of strict"
                       " mode, so the server is still repairing what the"
                       " application sends it -- %s" % detail)
    return True, ""


def check_mode(recorded, observed):
    """The deliverable: the server is strict, and it is strict on disk."""
    checks = gate.Checks(MODE_FAMILY)

    _safe(checks, "S1-server-strict", lambda: (
        is_strict(observed["sql_mode"]),
        "the running server reports sql_mode=%s, which still repairs invalid"
        " values silently" % observed["sql_mode"]))

    def _config():
        value, lines = observed["config_sql_mode"]
        if value is None:
            return False, ("no sql_mode is set in %s, so the mode the server"
                           " came up in is not the one on disk: %s"
                           % (MY_CNF, "; ".join(lines) or "no setting"))
        return is_strict(value), "%s sets sql_mode=%s" % (MY_CNF, value)
    _safe(checks, "S2-config-strict", _config)

    # G11's detail is kept as a note whether it passed or failed, because it
    # is the only place a reader can see that the two readings behind it were
    # alive. "0 new mention(s), all strict; no statement log" is a pass, and it
    # is also a runtime check that read nothing -- an instrument that has
    # quietly stopped asking, which is exactly the shape trap 6 is about. A
    # failure puts the same text in `failed`; the duplication is worth it.
    detail = []

    def _relaxation():
        ok, why = no_relaxation(recorded, observed)
        detail.append(why)
        return ok, why
    _safe(checks, "G11-no-relaxation", _relaxation)

    checks.note("sql_mode=%s" % observed.get("sql_mode"))
    for why in detail:
        checks.note(why)
    return checks


def check_repairs(recorded, observed):
    """The six readings the mode breaks, and the application must restore.

    Each one drives the application over HTTP and then reads what reached the
    column, against what the permissive server stored for the very same input.
    That recording is the whole specification: the repair reproduces `''`, `0`
    and `?` exactly, including the `?`. Repairing the text itself is step 8's
    work and doing it here would break the corpus.
    """
    checks = gate.Checks(REPAIR_FAMILY)
    ok, why = effective_mode(recorded, observed)
    if not ok:
        checks.fail_all(REPAIR_IDS, why)
        return checks

    def _session():
        # The NULL is asked first so that it is what the report says. It is now
        # one of the compared fields too, and a field-by-field diff would name
        # it as `current_request_null: False -> True`, which is the same
        # finding in a form nobody reads twice.
        if observed["stored"]["session"].get("current_request_null"):
            return False, "jf_session.CurrentRequest was left NULL"
        want = _subset(SESSION_KEEP, recorded["stored"]["session"])
        got = _subset(SESSION_KEEP, observed["stored"]["session"])
        ok, detail = _eq(want, got, "jf_session")
        if not ok:
            return ok, ("the session bookkeeping UPDATE did not run as it"
                        " does today -- %s" % detail)
        return True, ""
    _safe(checks, "S3-session-update", _session)

    def _signup_rows():
        if SIGNUP_USER not in observed["stored"]["users_rows"]:
            return False, ("signup created no jf_users row for %r"
                           % SIGNUP_USER)
        hits = [r for r in observed["stored"]["xuser_rows"]
                if r["email"] == SIGNUP_EMAIL]
        return (len(hits) == 1,
                "signup created %d jf_xuser rows for %r, the recording created"
                " 1" % (len(hits), SIGNUP_EMAIL))
    _safe(checks, "S4-signup-rows", _signup_rows)

    def _xuser_values():
        def one(obs):
            hits = [r for r in obs["stored"]["xuser_rows"]
                    if r["email"] == SIGNUP_EMAIL]
            return _subset(XUSER_KEEP, hits[0]) if hits else None
        return _eq(one(recorded), one(observed), "the signup's jf_xuser row")
    _safe(checks, "S5-xuser-values", _xuser_values)

    _safe(checks, "S7-contest-points",
          _challenge(recorded, observed, MARK + "-pts", "empty-points"))

    for case_id, letter, _value in ENCODING_CASES:
        if ENCODING_CHECK_ID[case_id] not in REPAIR_IDS:
            continue
        _safe(checks, ENCODING_CHECK_ID[case_id],
              _challenge(recorded, observed, "%s-%s" % (MARK, letter),
                         case_id))

    checks.note("what the probes stored: %s" % gate.dumps(observed["stored"]))
    return checks


def check_guards(recorded, observed):
    """What was already true, and a wrong answer breaks.

    Nothing here can be earned. `M1`-`M3` are the rule-7 must-flip probes and
    catch an application that shortens values in PHP; `S6`, `S8` and `S10` are
    the readings the mode does not disturb, so a repair that breaks them broke
    something it was not asked to touch; `G11`-`G14` are the shortcuts -- a new
    `sql_mode` anywhere in the tree, a widened or re-charactered column, a
    changed table set, a database root that can no longer be reached over the
    socket the next step needs. Widening a column or moving it to `utf8mb4`
    would buy `M1` and `M3` by doing step 2's and step 8's work early, and it
    is the schema gate at those steps, not this one, that is allowed to accept
    that.
    """
    checks = gate.Checks(GUARD_FAMILY)
    if not is_strict(observed.get("sql_mode")):
        checks.fail_all(GUARD_IDS, PRECONDITION % observed.get("sql_mode"))
        return checks

    _safe(checks, "M1-overlong-query",
          lambda: _flip_check(recorded, observed, "overlong_query",
                              "query string"))

    def _m2():
        def shortened(obs):
            return [r["email"] for r in obs["stored"]["xuser_rows"]
                    if r["email"].startswith(OVERLONG_EMAIL_MARK)
                    and not r["email"].endswith(EMAIL_TAIL)]
        if not shortened(recorded):
            return False, ("instrument: the recording stores no shortened"
                           " address, so this probe measures nothing")
        got = shortened(observed)
        if got:
            return False, ("a %d-character address was sent and a"
                           " %d-character one was written"
                           % (observed["seen"]["overlong_email"]["sent_len"],
                              len(got[0])))
        return True, ""
    _safe(checks, "M2-overlong-email", _m2)

    _safe(checks, "M3-overlong-useragent",
          lambda: _flip_check(recorded, observed, "overlong_useragent",
                              "User-Agent"))

    def _login():
        keys = ("login_status", "login_location")
        return _eq(_subset(keys, recorded["seen"]["signup"]),
                   _subset(keys, observed["seen"]["signup"]),
                   "logging in as the account signup created")
    _safe(checks, "S6-signup-login", _login)

    for case_id, letter, _value in ENCODING_CASES:
        if ENCODING_CHECK_ID[case_id] not in GUARD_IDS:
            continue
        _safe(checks, ENCODING_CHECK_ID[case_id],
              _challenge(recorded, observed, "%s-%s" % (MARK, letter),
                         case_id))

    _safe(checks, "S10-stats-logged", lambda: _eq(
        recorded["stored"]["stats_ordinary"],
        observed["stored"]["stats_ordinary"],
        "jf_stats rows logged for the pass's ordinary requests"))

    def _schema(which):
        if observed.get("schema_differences") is None:
            return False, "the live catalogue could not be read"
        rec = (recorded.get("schema_differences") or {})
        added = _added(rec, observed["schema_differences"], which)
        return not added, "; ".join(added[:10])
    _safe(checks, "G12-column-shapes", lambda: _schema("columns"))
    _safe(checks, "G13-table-set", lambda: _schema("tables"))

    _safe(checks, "G14-root-socket",
          lambda: tuple(observed["root_over_socket"]))
    return checks


def check_all(recorded, observed):
    """Every family, in report order. What the plan and the rehearsal call."""
    return [check_mode(recorded, observed),
            check_repairs(recorded, observed),
            check_guards(recorded, observed)]


# ------------------------------------------------------------------ plan ---

RECORDING = "/tests/fixtures/strict-probes.json"
SCHEMA = "/tests/fixtures/schema-baseline.json"

# One observation per verifier run, cached on the context. Driving the probes
# twice would not merely double the wall-clock: the second pass would read a
# deployment the first had already signed up to and posted to, and every count
# in it would be wrong.
SHARED_KEY = "strict:observation"


def _load(path):
    import json
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def observation(ctx, recording=RECORDING, schema=SCHEMA, database=DATABASE):
    """((recorded, observed), error) -- exactly one of them is None."""
    if SHARED_KEY not in ctx.shared:
        try:
            env = Env(ctx.mysql(database))
            pair = (_load(recording),
                    observe(env, schema=_load(schema), database=database,
                            roots=tuple(ctx.roots), tools=ctx.tools))
            ctx.shared[SHARED_KEY] = (pair, None)
        except Exception:
            import traceback
            ctx.shared[SHARED_KEY] = (None, traceback.format_exc(limit=8))
    return ctx.shared[SHARED_KEY]


def _family(ctx, builder, family, ids, **kw):
    pair, error = observation(ctx, **kw)
    if error is not None:
        checks = gate.Checks(family)
        checks.fail_all(ids, "the probes could not be driven")
        checks.error = error
        return checks
    return builder(*pair)


def mode_family(ctx, **kw):
    return _family(ctx, check_mode, MODE_FAMILY, MODE_IDS, **kw)


def repair_family(ctx, **kw):
    return _family(ctx, check_repairs, REPAIR_FAMILY, REPAIR_IDS, **kw)


def guard_family(ctx, **kw):
    return _family(ctx, check_guards, GUARD_FAMILY, GUARD_IDS, **kw)


def precondition(ctx, **kw):
    """(ok, detail) -- the step's deliverable, as the behaviour families see it.

    The server the verifier started from the candidate's own config file either
    rejects what it cannot store or it does not, and the tree either leaves it
    that way or puts its own connections back. Everything the recording asks
    about is unanswerable until both hold. Shares the one observation, so
    asking costs nothing.
    """
    pair, error = observation(ctx, **kw)
    if error is not None:
        return False, "the deployment could not be probed"
    return effective_mode(*pair)
