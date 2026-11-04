"""Normalisation and structural projection of application responses.

Comparing raw HTML across thirteen Rails versions compares the framework's tag
builder, not the application.  Every response is therefore projected onto a
structure that carries what the application decides -- which text, which links,
which form fields, which rows -- and drops what the framework decides -- tag
spelling, attribute order, asset digests, CSRF values, the hidden `utf8` field,
nonces, and the security headers that `load_defaults` owns.

Volatile values that the application *does* decide (auth tokens, timestamps,
database-generated ids that reach a URL) are replaced by labels that preserve
identity: the same value always gets the same label, different values always
get different labels, so "the link in the email is the token in the row" stays
a scored fact while the bytes do not.

Asset *wiring* is deliberately not projected.  The layout references
`vendor/modernizr`, which does not exist anywhere in the repository, and the
stylesheet chain runs through compass; which `<script>`/`<link>` tags survive
Sprockets -> Propshaft is a packaging decision the migration is allowed to
make.  Asset URLs the application emits as data -- the `<Play>` bodies of the
TwiML the phone call hears, and `<img>` sources -- are projected, because those
are what the application does rather than how it is built.
"""

from __future__ import annotations

import csv
import io
import json
import re
from html.parser import HTMLParser

# --------------------------------------------------------------------------
# volatile-value labelling
# --------------------------------------------------------------------------

# Ordered most specific first: the first pattern that matches a region wins.
_TIME_PATTERNS = [
    # 2016-01-19T12:00:03.123Z / +00:00 / -05:00
    r"\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})",
    # 2016-01-19 12:00:03 UTC / -0500
    r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} (?:UTC|[+-]\d{4})",
    # Mon, 19 Jan 2016 12:00:03 +0000  (RFC 2822, mail headers)
    r"[A-Z][a-z]{2}, \d{1,2} [A-Z][a-z]{2} \d{4} \d{2}:\d{2}:\d{2} [+-]\d{4}",
    # January 19, 2016 12:00 / 19 Jan 2016
    r"[A-Z][a-z]+ \d{1,2}, \d{4}(?: \d{2}:\d{2})?",
    # bare date, e.g. Time::DATE_FORMATS[:year_month_date]
    r"\d{4}-\d{2}-\d{2}",
    # bare clock
    r"\d{2}:\d{2}:\d{2}",
    # unix epoch in the 2001-2033 range (StatusController#check emits one)
    r"\b1[0-9]{9}\b",
]
_TIME_RE = re.compile("|".join("(?:%s)" % p for p in _TIME_PATTERNS))

# The origin libfaketime is pinned to, as a unix epoch (2016-01-19 12:00:00Z).
FROZEN_ORIGIN = 1453204800
# Anything the run itself produced lands inside this window; the repository's
# own seed data (2013-2014) and anything a template hardcodes land outside it.
NOW_WINDOW = 24 * 3600

# The clock is offset, not stopped.  It cannot be stopped: libfaketime 0.9.10
# rewrites every select()/poll() timeout by the speed factor, so `x0` makes
# unicorn's own event loop raise EINVAL on every iteration and the application
# never serves a request.  What still has to go is the *second* a value landed
# in, because on a shared box that is decided by how busy the machine was.
#
# So a timestamp inside the window is labelled `<NOW>` rather than `<TS:k>`:
# "the application answered with the current time" is the behaviour, and which
# second it was is not.  Two recordings then agree, and -- the reason this is
# not the same as dropping the field -- a migration that emits a *stale* or
# absent timestamp still loses the point, because its value falls outside the
# window and comes back as an indexed label instead.
_ISO_RE = re.compile(r"^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2}):(\d{2}))?")
_MONTHS = {m: i + 1 for i, m in enumerate(
    ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"])}
_RFC2822_RE = re.compile(
    r"^[A-Z][a-z]{2}, (\d{1,2}) ([A-Z][a-z]{2}) (\d{4}) "
    r"(\d{2}):(\d{2}):(\d{2})")


def _epoch_of(text):
    """Best-effort epoch for a matched timestamp, or None if it has no date."""
    if re.fullmatch(r"1[0-9]{9}", text):
        return int(text)
    match = _RFC2822_RE.match(text)
    if match:
        day, mon, year, hh, mm, ss = match.groups()
        month = _MONTHS.get(mon)
        if month is None:
            return None
        return _to_epoch(int(year), month, int(day), int(hh), int(mm), int(ss))
    match = _ISO_RE.match(text)
    if match:
        year, month, day, hh, mm, ss = match.groups()
        return _to_epoch(int(year), int(month), int(day),
                         int(hh or 0), int(mm or 0), int(ss or 0))
    return None  # a bare clock or a prose date carries no instant


def _to_epoch(year, month, day, hh, mm, ss):
    import calendar
    try:
        return calendar.timegm((year, month, day, hh, mm, ss, 0, 0, 0))
    except (ValueError, OverflowError):
        return None


def _is_now(text):
    epoch = _epoch_of(text)
    return epoch is not None and 0 <= epoch - FROZEN_ORIGIN < NOW_WINDOW

# Sprockets/Propshaft fingerprints: -<32 hex> (sprockets 3/4) or -<8..40 hex>.
_DIGEST_RE = re.compile(r"-([0-9a-f]{8,64})(\.[A-Za-z0-9]+)(?=$|[?#\"'\s)])")

# SecureRandom.urlsafe_base64(nil, false) -> 22 chars of [A-Za-z0-9_-].  This
# is only a safety net: the driver aliases every token it sees in a `token=`
# parameter before the response is projected, so a token normally never
# reaches this pattern at all.
#
# Both guards earn their keep.  Without the `token=` prefix the pattern also
# matches "CityVoice_email_header", the 22-character image name the weekly
# digest links to.  Without the mixed-case requirement it also matches
# "1-eli-s-mile-high-club" -- Location#to_param output happens to be exactly
# 22 characters wide for the repository's own first fixture -- and labelling
# slugs would make the whole locations surface unscoreable.  `parameterize`
# downcases, so no slug carries an upper-case letter.
_TOKEN_RE = re.compile(
    r"(?<=token=)"
    r"(?=[A-Za-z0-9_-]{22}(?![A-Za-z0-9_-]))"
    r"(?=[A-Za-z0-9_-]*[a-z])"
    r"(?=[A-Za-z0-9_-]*[A-Z])"
    r"[A-Za-z0-9_-]{22}")

# Rails logs its own timings into stdout -- "Rendered weekly_activity.html.erb
# (91.2ms)", "Completed 200 OK in 15ms (Views: 10.2ms | ActiveRecord: 1.1ms)".
# A duration is not a behaviour, and on a shared machine it is never twice the
# same number.
_MS_RE = re.compile(r"\b\d+(?:\.\d+)?ms\b")

# Rails 6.0 rewrote both of the lines the weekly-digest rake task prints.
# Action View's log subscriber turned "(4.2ms)" into "(Duration: 4.2ms |
# Allocations: 68652)", and Action Mailer's turned "Sent mail to
# resident@example.com (4.2ms)" into "Delivered mail <message id> (4.2ms)".
# The wording of both lines is kept -- it is the release's doing and the
# overlay for that step records it -- but the two values the release added
# are no more a behaviour than the duration beside them.
#
# An allocation count is a property of the interpreter and of every gem on
# the render path, so it moves under a step that touches neither the template
# nor the mailer.  A Mail message id is `<hex time>_<pid><random>@<hostname>`,
# and the hostname is the container's, so it is never twice the same string.
_ALLOC_RE = re.compile(r"(?<=Allocations: )\d+")
_MSGID_RE = re.compile(r"(?<=Delivered mail )\S+@\S+")

# Migrations report seconds, not milliseconds: "-> 0.0271s" under each
# statement and "migrated (0.0313s)" at the end of each migration.  The
# fractional part is the only thing that varies, but it varies on every run.
# Anchored on the decimal point so a bare integer followed by an `s` -- "1s",
# and more to the point a word like "lets" -- is not a duration.
_SEC_RE = re.compile(r"\b\d+\.\d+s\b")

_WS_RE = re.compile(r"\s+")

# --------------------------------------------------------------------------
# database-server noise in a task's own output
# --------------------------------------------------------------------------
#
# A driver's error text is the environment talking, not the application.  The
# baseline is recorded against PostgreSQL 9.4 because Rails 4.0's adapter
# cannot connect to anything newer, and every
# verifier runs PostgreSQL 16; the SQL beside it is ActiveRecord's, and its
# wording changes with Rails.  What is behaviour -- that the database refused
# the row, and the class of refusal -- is kept; the server's prose and the
# generated statement are not.
#
# `ERROR:` is required in the pattern so that `ActiveRecord::RecordInvalid:
# Validation failed: Name can't be blank`, which is the application's own
# message, is left exactly as it is.

_DB_ERR_RE = re.compile(
    r"^(\s*)(.*?)((?:PG|PGError)::[A-Za-z_]\w*):\s+ERROR:.*$")
_DB_SQL_RE = re.compile(
    r"^\s*:?\s*(?:SELECT|INSERT|UPDATE|DELETE|CREATE|ALTER|DROP)\b.*$",
    re.IGNORECASE)


def scrub_db_noise(line):
    """Collapse a database server's own words, keeping the error's class."""
    match = _DB_ERR_RE.match(line)
    if match:
        return "%s%s%s: <DBERROR>" % match.groups()
    if _DB_SQL_RE.match(line):
        return ": <SQL>"
    return line


# --------------------------------------------------------------------------
# rake db:* output, projected structurally
# --------------------------------------------------------------------------
#
# The migration tasks print a banner per migration and a line per DDL
# statement, and the *framing* of those lines is Rails' own and changes with
# Rails: 4.0 writes "==  CreateCalls: migrating ===...", 5.0 onwards writes
# "== 20140401145954 CreateCalls: migrating ===...", and 4.0 additionally
# announces "Migrating to CreateCalls (20140401145954)" beforehand.  Scoring
# that text verbatim would make every version bump fail for the framing rather
# than for the schema.
#
# What has to survive a migration is the event stream: which migrations ran, in
# which direction, and which DDL operations each one performed, in order.  That
# is what these projections keep.  The `=` padding, the announce lines, the
# per-statement timings and the position of the version number are framing and
# are deliberately dropped.

_MIG_BANNER = re.compile(
    r"^=+\s+(?:\d{14}\s+)?(\w+):\s+(migrating|migrated|reverting|reverted)\b")
_MIG_OP = re.compile(r"^--\s+(.+?)\s*$")
_MIG_STATUS_ROW = re.compile(r"^\s*(up|down)\s+(\d{14}|\*+)\s+(.*?)\s*$")
_MIG_VERSION = re.compile(r"^Current version:\s*(\d+)\s*$")
_MIG_NO_TABLE = re.compile(r"^Schema migrations table does not exist", re.I)


def project_migration(lines):
    """`db:migrate` / `db:rollback` output as an ordered event stream."""
    events = []
    for line in lines:
        banner = _MIG_BANNER.match(line)
        if banner:
            events.append([banner.group(2), banner.group(1)])
            continue
        op = _MIG_OP.match(line)
        if op:
            events.append(["op", op.group(1)])
    return {"events": events}


def project_migrate_status(lines):
    """`db:migrate:status` as (status, version, name) rows."""
    rows = []
    note = False
    for line in lines:
        row = _MIG_STATUS_ROW.match(line)
        if row:
            rows.append([row.group(1), row.group(2), row.group(3)])
        elif _MIG_NO_TABLE.match(line.strip()):
            note = True
    return {"rows": rows, "no_table": note}


def project_db_version(lines):
    """`db:version` as the version alone."""
    for line in lines:
        match = _MIG_VERSION.match(line.strip())
        if match:
            return {"version": match.group(1)}
    return {"version": None}


class Labeller:
    """Assigns stable labels to volatile values, first-appearance ordered.

    A label is shared across every response in a run, so a token minted in a
    confirmation email and the same token echoed by the unsubscribe page get
    the same label and the relationship between them stays scored.
    """

    def __init__(self):
        self._counters = {}
        self._labels = {}
        self.aliases = {}  # real value -> label, seeded by the driver

    def alias(self, value, label):
        """Pin a value the driver already knows about to a chosen label."""
        if value:
            self.aliases[str(value)] = label

    def seed_hosts(self, origins):
        """Register the origins the *harness* chose, so they scrub to nothing.

        Only these disappear.  A host CityVoice hardcodes -- the weekly digest
        header image lives on raw.github.com -- is application content, and a
        migration that drops it should lose the point, so host stripping has
        to be a whitelist rather than a pattern over every absolute URL.
        """
        for origin in origins:
            origin = origin.rstrip("/")
            if not origin:
                continue
            self.alias(origin, "")
            _, sep, rest = origin.partition("//")
            if sep:
                self.alias("//" + rest, "")

    def label_for(self, kind, value):
        key = (kind, value)
        if key not in self._labels:
            n = self._counters.get(kind, 0)
            self._counters[kind] = n + 1
            self._labels[key] = "<%s:%d>" % (kind, n)
        return self._labels[key]

    # -- text scrubbing ----------------------------------------------------

    def _time_label(self, match):
        text = match.group(0)
        return "<NOW>" if _is_now(text) else self.label_for("TS", text)

    def scrub(self, text):
        if text is None:
            return None
        text = str(text)
        for real, label in sorted(self.aliases.items(), key=lambda kv: -len(kv[0])):
            text = text.replace(real, label)
        text = _MSGID_RE.sub("<MID>", text)
        text = _TIME_RE.sub(self._time_label, text)
        text = _DIGEST_RE.sub(lambda m: m.group(2), text)
        text = _MS_RE.sub("<MS>", text)
        text = _ALLOC_RE.sub("<ALLOC>", text)
        text = _SEC_RE.sub("<S>", text)
        text = _TOKEN_RE.sub(lambda m: self.label_for("TOK", m.group(0)), text)
        return text


# --------------------------------------------------------------------------
# URL normalisation
# --------------------------------------------------------------------------

def norm_url(url, labeller):
    """Normalise a URL: the harness's own origin goes, everything else stays.

    Stripping is done by `Labeller.seed_hosts`, which the driver seeds with the
    addresses it happens to have bound.  A URL pointing anywhere else survives
    intact and stays scored.
    """
    if url is None:
        return None
    url = labeller.scrub(url.strip())
    return url if url else "/"


# --------------------------------------------------------------------------
# HTML projection
# --------------------------------------------------------------------------

# Elements whose *content* is framework plumbing rather than application text.
_SKIP_CONTENT = {"script", "style", "head"}

# Hidden form fields the framework owns: `utf8` disappears at Rails 6's
# default_enforce_utf8 = false, `authenticity_token` carries a fresh random
# value on every render, `_method` and `commit` are helper spelling.
_FRAMEWORK_FIELDS = {"utf8", "_method", "authenticity_token", "commit"}

_VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link",
         "meta", "param", "source", "track", "wbr"}


class _Node:
    """A tag with its children and its own text kept in document order.

    `items` interleaves ("t", str) and ("n", _Node) so that text extraction
    reproduces reading order; keeping text in a separate list would emit
    "Hello again world" for "<p>Hello <b>world</b> again</p>".
    """

    __slots__ = ("tag", "attrs", "items", "parent")

    def __init__(self, tag, attrs=None, parent=None):
        self.tag = tag
        self.attrs = dict(attrs or {})
        self.items = []
        self.parent = parent

    @property
    def children(self):
        return [v for kind, v in self.items if kind == "n"]


class _Builder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = _Node("#root")
        self.cur = self.root

    def _open(self, tag, attrs, descend):
        node = _Node(tag, attrs, self.cur)
        self.cur.items.append(("n", node))
        if descend:
            self.cur = node

    def handle_starttag(self, tag, attrs):
        self._open(tag, attrs, tag not in _VOID)

    def handle_startendtag(self, tag, attrs):
        self._open(tag, attrs, False)

    def handle_endtag(self, tag):
        if tag in _VOID:
            return
        node = self.cur
        while node is not self.root and node.tag != tag:
            node = node.parent
        if node is not self.root:
            self.cur = node.parent

    def handle_data(self, data):
        self.cur.items.append(("t", data))


def _walk(node):
    yield node
    for child in node.children:
        yield from _walk(child)


def _flat_text(node):
    """Reading-order text of a subtree, with script/style content dropped."""
    parts = []

    def rec(n):
        if n.tag in _SKIP_CONTENT:
            return
        for kind, value in n.items:
            if kind == "t":
                parts.append(value)
            else:
                # An element boundary separates words the way a browser lays
                # them out; whitespace is collapsed immediately afterwards.
                parts.append(" ")
                rec(value)
                parts.append(" ")

    rec(node)
    return _WS_RE.sub(" ", "".join(parts)).strip()


def project_html(body, labeller):
    b = _Builder()
    b.feed(body)
    b.close()
    root = b.root

    title = None
    links = []
    forms = []
    images = []
    audio = []
    headings = []
    tables = []
    body_class = None

    for node in _walk(root):
        tag = node.tag
        attrs = node.attrs
        if tag == "title":
            title = labeller.scrub(_flat_text(node))
        elif tag == "body":
            body_class = labeller.scrub(attrs.get("class", ""))
        elif tag == "a":
            links.append([norm_url(attrs.get("href"), labeller),
                          labeller.scrub(_flat_text(node))])
        elif tag == "img":
            images.append(norm_url(attrs.get("src"), labeller))
        elif tag in ("audio", "source"):
            src = attrs.get("src")
            if src:
                audio.append(norm_url(src, labeller))
        elif tag in ("h1", "h2", "h3", "h4"):
            headings.append([tag, labeller.scrub(_flat_text(node))])
        elif tag == "form":
            fields = []
            for inner in _walk(node):
                if inner.tag not in ("input", "select", "textarea", "button"):
                    continue
                name = inner.attrs.get("name")
                if name in _FRAMEWORK_FIELDS:
                    continue
                fields.append([
                    name,
                    inner.attrs.get("type"),
                    labeller.scrub(inner.attrs.get("value", "")),
                ])
            forms.append({
                "action": norm_url(node.attrs.get("action"), labeller),
                "method": (node.attrs.get("method") or "get").lower(),
                "fields": sorted(fields, key=lambda f: (str(f[0]), str(f[1]))),
            })
        elif tag == "table":
            rows = []
            for tr in _walk(node):
                if tr.tag != "tr":
                    continue
                rows.append([labeller.scrub(_flat_text(cell))
                             for cell in tr.children if cell.tag in ("td", "th")])
            tables.append(rows)

    # Elements the application positions itself (the map, the vote meters) are
    # carried as the set of data-* attributes it sets, not as markup.
    data_attrs = []
    for node in _walk(root):
        pairs = [(k, labeller.scrub(v))
                 for k, v in node.attrs.items()
                 if k.startswith("data-") and not k.startswith("data-turbo")]
        if pairs:
            data_attrs.append([node.tag] + sorted(pairs))

    return {
        "kind": "html",
        "title": title,
        "body_class": body_class,
        "text": labeller.scrub(_flat_text(root)),
        "headings": headings,
        "links": links,
        "forms": forms,
        "images": images,
        "audio": audio,
        "tables": tables,
        "data": data_attrs,
    }


# --------------------------------------------------------------------------
# XML (TwiML) projection
# --------------------------------------------------------------------------

def project_xml(body, labeller):
    """TwiML as an ordered verb list.

    Twilio's own gem changes its serialiser between major versions (attribute
    order, self-closing style, the XML declaration); the verbs, their order and
    their payloads are what the phone call actually does.
    """
    import xml.etree.ElementTree as ET

    try:
        root = ET.fromstring(body.strip())
    except ET.ParseError as exc:
        return {"kind": "xml", "parse_error": str(exc),
                "raw": labeller.scrub(body.strip()[:400])}

    def rec(el):
        return {
            "tag": el.tag,
            "attrs": {k: labeller.scrub(v) for k, v in sorted(el.attrib.items())},
            "text": labeller.scrub((el.text or "").strip()) or None,
            "children": [rec(c) for c in el],
        }

    return {"kind": "xml", "root": rec(root)}


# --------------------------------------------------------------------------
# JSON / CSV projections
# --------------------------------------------------------------------------

def project_json(body, labeller):
    try:
        data = json.loads(body)
    except ValueError as exc:
        return {"kind": "json", "parse_error": str(exc),
                "raw": labeller.scrub(body[:400])}

    def rec(v):
        if isinstance(v, dict):
            return {k: rec(v[k]) for k in sorted(v)}
        if isinstance(v, list):
            return [rec(x) for x in v]
        if isinstance(v, str):
            return labeller.scrub(v)
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            # Epoch seconds reach JSON as a number, not a string.
            scrubbed = labeller.scrub(str(v))
            return scrubbed if scrubbed != str(v) else v
        return v

    return {"kind": "json", "data": rec(data)}


def project_csv(body, labeller):
    rows = [[labeller.scrub(cell) for cell in row]
            for row in csv.reader(io.StringIO(body))]
    return {"kind": "csv", "rows": rows}


def project_text(body, labeller):
    return {"kind": "text", "text": labeller.scrub(body.strip())}


def project_binary(raw, labeller=None):
    """Bytes the application serves rather than composes.

    An mp3 or an image has no structure worth projecting and decoding it as
    text produces a couple of hundred kilobytes of replacement characters, so
    it is identified by its length and digest instead.
    """
    import hashlib
    return {"kind": "binary", "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest()}


# --------------------------------------------------------------------------
# response projection
# --------------------------------------------------------------------------

# Headers the application decides.  Everything else -- Date, ETag, X-Runtime,
# Set-Cookie, and the X-Frame-Options/X-Content-Type-Options family that
# `load_defaults` owns -- is framework or clock and is scored by the gates
# instead.
_KEEP_HEADERS = ("content-type", "location", "content-disposition",
                 "www-authenticate")


# Everything else is served rather than composed, and is digested instead.
_TEXTUAL = ("text/", "application/json", "application/xml",
            "application/javascript", "application/xhtml",
            "application/csv", "application/octet-stream")


def project_response(status, headers, body, labeller, want=None, raw=None):
    hdrs = {}
    for name in _KEEP_HEADERS:
        value = headers.get(name)
        if value is None:
            continue
        if name == "location":
            hdrs[name] = norm_url(value, labeller)
        elif name == "content-type":
            # "text/html; charset=utf-8" -> media type plus charset, with the
            # optional whitespace and parameter order settled.
            parts = [p.strip().lower() for p in value.split(";")]
            hdrs[name] = "; ".join([parts[0]] + sorted(parts[1:]))
        else:
            hdrs[name] = labeller.scrub(value)

    ctype = (headers.get("content-type") or "").lower()
    if want is None:
        if "json" in ctype:
            want = "json"
        elif "xml" in ctype:
            want = "xml"
        elif "csv" in ctype:
            want = "csv"
        elif "html" in ctype:
            want = "html"
        elif raw is not None and not any(ctype.startswith(t) for t in _TEXTUAL):
            want = "binary"
        else:
            want = "text"

    if want == "binary":
        return {"status": status, "headers": hdrs,
                "body": project_binary(raw if raw is not None else b"")}

    # An error page is HTML whatever the request asked for.
    if want in ("json", "csv", "xml") and status >= 400 and "html" in ctype:
        want = "html"

    projector = {
        "html": project_html,
        "xml": project_xml,
        "json": project_json,
        "csv": project_csv,
        "text": project_text,
    }[want]
    return {"status": status, "headers": hdrs, "body": projector(body, labeller)}
