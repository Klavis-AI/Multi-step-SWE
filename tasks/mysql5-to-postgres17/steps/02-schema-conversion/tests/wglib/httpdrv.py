"""Drives the application over HTTP and normalises what comes back.

Only the standard library is used: the verifier must not depend on a package
index being reachable while grading.

Normalisation removes three things and nothing else:

  * the PHP session id, wherever it appears (cookie, body, redirect target) --
    it varies between two identical runs of the *same* build;
  * the lesson source panes, where a lesson page echoes its own `index.php`
    back to the client -- they vary with the file's bytes rather than with
    anything the deployment did. See `SOURCE_PANES` for the measurement that
    made this necessary;
  * the order of the rows in a response that is a bare JSON array -- the
    application emits one such response and pins no order on it, so the order
    is the query planner's rather than the application's. See
    `canonical_json_rows` for the measurement that made this necessary.

Response *dates* need no normalisation: the application server runs on a frozen
clock, so `Date:` is constant. Anything else that moves between two runs is a
genuine instability and is dropped from the corpus rather than normalised away
(see record.py).
"""

import gzip
import http.cookiejar
import io
import json
import urllib.error
import urllib.parse
import urllib.request

SESSION_COOKIE = "JFSESSIONID"


class App(object):
    """One browsing session against the application."""

    def __init__(self, base="http://127.0.0.1", host="localhost", timeout=30):
        self.base = base.rstrip("/")
        self.host = host
        self.timeout = timeout
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self.jar),
            _NoRedirect())

    # -- session ----------------------------------------------------------
    def session_id(self):
        for c in self.jar:
            if c.name == SESSION_COOKIE:
                return c.value
        return None

    def reset(self):
        self.jar.clear()

    # -- requests ---------------------------------------------------------
    def request(self, method, path, data=None, headers=None):
        """Returns {"status", "location", "body"} with body as raw bytes."""
        url = self.base + path
        body = None
        hdrs = {"Host": self.host, "User-Agent": "wg-fixture/1",
                "Accept-Encoding": "identity"}
        if data is not None:
            if isinstance(data, (bytes, bytearray)):
                body = bytes(data)
            else:
                body = _urlencode_bytes(data)
            hdrs["Content-Type"] = "application/x-www-form-urlencoded"
        hdrs.update(headers or {})
        req = urllib.request.Request(url, data=body, headers=hdrs, method=method)
        try:
            with self.opener.open(req, timeout=self.timeout) as r:
                raw = r.read()
                status = r.status
                loc = r.headers.get("Location")
                enc = (r.headers.get("Content-Encoding") or "").lower()
        except urllib.error.HTTPError as e:
            raw = e.read()
            status = e.code
            loc = e.headers.get("Location")
            enc = (e.headers.get("Content-Encoding") or "").lower()
        if enc == "gzip":
            raw = gzip.GzipFile(fileobj=io.BytesIO(raw)).read()
        return {"status": status, "location": loc, "body": raw}

    def get(self, path, **kw):
        return self.request("GET", path, **kw)

    def post(self, path, data, **kw):
        return self.request("POST", path, data=data, **kw)

    def login(self, username, password):
        self.get("/user/login")
        return self.post("/user/login", {"Username": username, "Password": password})


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    """Redirects are observations, not something to follow silently."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None

    http_error_301 = http_error_302 = http_error_303 = http_error_307 = \
        lambda self, req, fp, code, msg, headers: None


def _urlencode_bytes(data):
    """Form-encode, accepting bytes values so raw byte sequences can be sent."""
    parts = []
    for k, v in (data.items() if hasattr(data, "items") else data):
        if isinstance(v, str):
            v = v.encode("utf-8", "surrogateescape")
        elif not isinstance(v, (bytes, bytearray)):
            v = str(v).encode("ascii")
        parts.append("%s=%s" % (urllib.parse.quote_plus(k),
                                urllib.parse.quote_from_bytes(bytes(v), safe="")))
    return "&".join(parts).encode("ascii")


# --------------------------------------------------------------- normalise --

# Every lesson page echoes its own source file back to the client.
# `app/control/mode/{single,workshop}/challenges/__catch.php` reads
# `<lesson>/index.php` with `file_get_contents` and renders it html-escaped
# into the hidden `complete-scode` pane, and the single-mode controller renders
# a window of the same file into the `editor` pane as well.
#
# Those bytes are the file's, not the deployment's behaviour, and hashing them
# would turn the recording into a checksum of source files. That is not a
# theoretical objection, it is measured: `challenges/single/XSS2`,
# `challenges/single/NumericSQLInjection` and
# `challenges/single/BusinessLayerAccessControl` each build their table with
# `CREATE TABLE ... (id int auto_increment primary key, ...)`, which no
# PostgreSQL server will accept, so a correct migration *must* edit all three.
# With the panes hashed, editing one shifts every response that renders it --
# one comment added to the XSS2 lesson moved 21 of the 413 recorded responses,
# all by the same constant, none of them for a behavioural reason, and 104 of
# the 413 carry a pane at all. The task would have been unpassable from its
# PostgreSQL step onwards.
#
# So each pane is replaced by a fixed marker, the way the session id is. The
# surrounding markers stay in the body, so a page that loses its source pane
# altogether still differs; and what the lesson *rendered* -- the message list,
# the hints, the completion state, the form -- is outside both panes and is
# compared exactly as it stands.
#
# `authoring/rehearse_gates.py normalize` holds this to both halves of that
# claim, against real captured pages: a source-only edit must vanish, a
# rendered change and a removed pane must not.
SOURCE_PANES = (
    (b'<div id="editor" style="height: 260px;">',
     b'<form id="scode-form" method="POST">'),
    (b'<div class="hidden" id="complete-scode">',
     b'<!-- Options'),
)
SOURCE_MARKER = b"<LESSON-SOURCE>"


def hide_lesson_source(body):
    """Replace each echoed lesson-source pane with `SOURCE_MARKER`.

    A response that does not carry a pane comes back untouched, so this is a
    no-op on the 380-odd corpus responses that are not lesson pages.
    """
    for start, end in SOURCE_PANES:
        i = body.find(start)
        if i < 0:
            continue
        j = body.find(end, i + len(start))
        if j < 0:
            continue
        body = body[:i + len(start)] + SOURCE_MARKER + body[j:]
    return body


# One route in the application answers with a bare JSON array of database rows:
# `GET /mode/workshop/user/get`, which `app/model/workshop/users.php::getAll()`
# fills from
#
#     SELECT Username, ID from jf_users inner join jf_rbac_userroles
#     on ID = UserID where RoleID = ?
#
# and hands straight to `json_encode`. There is no `ORDER BY`, so the order the
# rows arrive in is the one the plan happens to produce, and nothing in the
# application promises it.
#
# That is measured rather than assumed, twice, on the untouched tree the agent
# is handed:
#
#   * The seed image ships `mysql.innodb_table_stats` reading `n_rows = 0` for
#     both tables -- the counts were written while the tables were empty and
#     `innodb_stats_auto_recalc` needs a 10% change to fire, which the corpus's
#     thirteen new users out of 432 do not reach. With zeroed statistics the
#     optimiser drives from `jf_users` through the covering `Username` index and
#     the rows come out in username order. A single `ANALYZE TABLE` -- no other
#     change at all, same tree, same data, same 143 rows -- moves the plan to
#     `jf_rbac_userroles` PRIMARY and the rows come out in id order. One of the
#     413 recorded responses changes, and it changes to exactly the bytes a real
#     blind run produced.
#   * Turning every `optimizer_switch` flag off moves it again: all *nine*
#     recordings of this route change, and no other response in the corpus does.
#
# So the order is a property of the query planner's state, which routine
# maintenance perturbs and which a migration to another engine does not preserve
# at all. Hashing it would score a candidate on their statistics table, and
# would make these nine checks unpassable from the step that moves the
# application onto PostgreSQL onwards.
#
# The rows themselves are behaviour and stay exactly as they are: a canonical
# form is one row per line, each row re-serialised with its keys sorted, the
# lines sorted. Adding, removing or altering any row still changes the record;
# only the order stops mattering. Duplicate rows survive, because the lines are
# sorted rather than deduplicated.
#
# The double recording rule 7 asks for could not have caught this. Both passes
# run against a freshly started container built from the same image, so both
# carry the same zeroed statistics and agree perfectly. Instability between two
# runs of the same build is not the same property as dependence on state the
# candidate is free to change, and only the second one was at stake here.
#
# `authoring/rehearse_gates.py normalize` holds this to both halves of the
# claim: a permutation of the rows must vanish, and a changed, added or removed
# row must not.
JSON_ROWS_MARKER = b"<JSON-ROWS>\n"


def canonical_json_rows(body):
    """Rewrite a bare JSON array body into a canonical row order.

    Anything that is not a JSON array comes back untouched -- the JSON objects
    the status routes answer with, every HTML page, every redirect body. The
    array is re-serialised from the decoded values rather than by sorting the
    original text, so two bodies that differ only in key order or in whitespace
    reduce to the same record, and two that differ in a value do not.
    """
    if body.lstrip()[:1] != b"[":
        return body
    try:
        rows = json.loads(body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        # Not JSON after all, or not valid UTF-8. Either is itself a difference
        # worth keeping, so the body is left exactly as it arrived.
        return body
    if not isinstance(rows, list):
        return body
    lines = sorted(json.dumps(r, sort_keys=True, separators=(",", ":"))
                   for r in rows)
    return JSON_ROWS_MARKER + "\n".join(lines).encode("utf-8")


# ------------------------------------------------------------ error surface --

# What PHP and jframework print when something went wrong, and this deployment
# does not. Both are switched off in the tree the candidate is handed: php.ini
# has `display_errors = Off`, and the environment build sets
# `jf\ErrorHandler::$Enabled=false` in app/config/application.php. So a body
# carrying any of these is a body whose deployment turned one of them back on
# -- which is what "the rest of the site's error handling is unchanged" in
# step 04's instruction forbids, and what makes a 200 with a stack trace where
# the lesson used to be a failure rather than an allowed difference.
#
# Every literal was generated rather than remembered: `probe*.sh` in
# `authoring/fixtures/error-surfaces/` inserted one statement into the middle
# of app/view/default/about.php, asked the running deployment for the page, and
# kept what came back -- twelve pages, every one of them HTTP 200. The eight
# literals below are what those pages carry and what the good pages beside them
# (the clean /about, and both sides of all four of step 04's flipped ids) do
# not. See authoring/derive_error_markers.py, which is what rewrites this
# block, and `error-surface-markers.json`, which records the derivation.
#
# Not on the list, deliberately: `SQLSTATE[...]`, the driver's own message.
# instruction.md says the lesson prints the database's error message and that
# the message is PostgreSQL's now, so a candidate who surfaces PDO's string is
# doing what the step asked.
ERROR_SURFACE = (
    b'#0 /app/',
    b'<b>Deprecated</b>:',
    b'<b>Fatal error</b>:',
    b'<b>Notice</b>:',
    b'<b>Parse error</b>:',
    b'<b>Warning</b>:',
    b'Stack trace:',
    b'encountered an error:',
)


def error_surface(body):
    """Which of those a body carries, as a sorted list of strings.

    Read off the *normalised* body, after the echoed lesson source has been
    replaced by its marker: a lesson whose own source file contains the words
    is not a lesson that failed.
    """
    return [m.decode("ascii") for m in ERROR_SURFACE if m in body]


def normalize(resp, session_ids=()):
    """Raw response -> comparable bytes, plus the status and redirect target.

    Three substitutions, and no others. The session id, because it is random
    per run; the lesson source panes, because they are a file echoed back
    rather than anything the deployment did (see `SOURCE_PANES`); and the row
    order of a bare JSON array, because the one route that answers with one
    pins no order on it (see `canonical_json_rows`).

    AutoForm and AutoList generate random element ids with no distinguishing
    prefix -- bare runs of 2-10 lowercase letters -- which no regex can replace
    without also rewriting ordinary page text. Rather than normalise them, the
    two routes that use them (`/jform`, `/user/captcha`) are simply left out of
    the corpus; neither reads or writes application data, so nothing about the
    migration is lost.
    """
    body = hide_lesson_source(resp["body"])
    for sid in session_ids:
        if sid:
            body = body.replace(sid.encode("ascii"), b"<SESSION>")
    # Last, so the rows are sorted on the bytes that are finally compared: a
    # session id inside a row would otherwise order the lines at random.
    body = canonical_json_rows(body)
    loc = resp["location"]
    if loc:
        for sid in session_ids:
            if sid:
                loc = loc.replace(sid, "<SESSION>")
    return {"status": resp["status"], "location": loc, "body": body}
