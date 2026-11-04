"""One pass over the corpus: drive the application, then read the database.

Run inside the container that serves the application, never from outside it.
The application records REMOTE_ADDR in jf_stats on every request, so a run
driven from the host would write 172.17.0.1 where a run driven from inside
writes 127.0.0.1, and the two database snapshots would not compare.

`collect()` is the single code path that produces both the recorded expectation
(against MySQL 5.7) and the observation being scored (against PostgreSQL 17).
"""

import json

from . import corpus
from . import dbstate
from . import runner


# Arguments that belong to the corpus pass rather than to the connection.
_PASSTHROUGH = ("base", "host", "scenarios", "strict", "logmark")


def open_mysql(host="127.0.0.1", port=3306, user="webgoat", password="webgoat",
               database="webgoatphp", unix_socket=None):
    import pymysql
    # `SET NAMES binary` stops the server transcoding anything on the way out,
    # and use_unicode=False stops the driver decoding it on the way in, so every
    # value arrives as the bytes the column actually holds. The column's own
    # declared charset is what decodes it, in dbstate.MySQLState.
    kw = dict(user=user, password=password, database=database,
              charset="utf8mb4", use_unicode=False,
              init_command="SET NAMES binary",
              # Bounded rather than left to the driver's default, so a server
              # that is listening and not answering costs a named failure
              # inside the step's budget instead of the kernel's TCP timeout.
              # Connect only: a read timeout would cut off a slow but honest
              # query, and nothing here is trying to bound those.
              connect_timeout=15,
              cursorclass=pymysql.cursors.Cursor)
    if unix_socket:
        kw["unix_socket"] = unix_socket
    else:
        kw["host"] = host
        kw["port"] = port
    return pymysql.connect(**kw)


def open_postgres(host="127.0.0.1", port=5432, user="webgoat", password="webgoat",
                  database="webgoatphp"):
    import psycopg2
    return psycopg2.connect(host=host, port=port, user=user, password=password,
                            dbname=database, connect_timeout=15)


def collect(state, base="http://127.0.0.1", host="localhost", scenarios=None,
            strict=True, logmark=None):
    """One pass: the database before, every scored response, the database after.

    The database is read twice on purpose. `tables_before` is the migrated
    dataset itself, untouched -- the strongest single statement of whether the
    move preserved what was in it, and the only one the corpus cannot weaken,
    since the last scenario drops and recreates four lesson tables.
    `tables_after` is what the corpus wrote. A migration has to get both right.

    `strict` defaults to recording behaviour -- a transport error is an error.
    A scorer passes False, so a deployment that stops answering is graded on
    what it did answer instead of taking the whole family down with it. See
    `runner.Recorder`.
    """
    before = runner.db_snapshot(state)
    state.refresh()
    rec = runner.Recorder(base=base, host=host, strict=strict, logmark=logmark)
    raw = rec.run(scenarios)
    requests = rec.finalize(raw)
    state.refresh()
    after = runner.db_snapshot(state, rec.session_ids)
    return {"requests": requests,
            "tables_before": before, "tables_after": after,
            "session_ids_seen": len(rec.session_ids),
            "log_windows": rec.windows,
            "aborted": rec.aborted,
            "transport_errors": rec.transport_errors}


def collect_mysql(**kw):
    conn = open_mysql(**{k: v for k, v in kw.items()
                         if k not in _PASSTHROUGH})
    try:
        state = dbstate.MySQLState(conn, kw.get("database", "webgoatphp"))
        return collect(state, base=kw.get("base", "http://127.0.0.1"),
                       host=kw.get("host", "localhost"),
                       scenarios=kw.get("scenarios"),
                       strict=kw.get("strict", True),
                       logmark=kw.get("logmark"))
    finally:
        conn.close()


def collect_postgres(**kw):
    conn = open_postgres(**{k: v for k, v in kw.items()
                            if k not in _PASSTHROUGH})
    try:
        state = dbstate.PostgresState(conn)
        return collect(state, base=kw.get("base", "http://127.0.0.1"),
                       host=kw.get("host", "localhost"),
                       scenarios=kw.get("scenarios"),
                       strict=kw.get("strict", True),
                       logmark=kw.get("logmark"))
    finally:
        conn.close()


def dumps(result):
    return json.dumps(result, sort_keys=True, indent=1, ensure_ascii=False)


# ------------------------------------------------------------- comparison ---

# The fields a recorded response is compared on. One tuple, because
# `compare`, `request_scores` and `behavior.apply_overrides` all have to mean
# the same four: an override that carried three of them would leave the fourth
# scored against the old recording.
REQUEST_FIELDS = ("status", "location", "sha256", "length")


def compare(expected, actual):
    """[(kind, id, expected, actual)] -- empty when the two agree exactly."""
    problems = []

    exp_r = {r["id"]: r for r in expected["requests"]}
    act_r = {r["id"]: r for r in actual["requests"]}
    for rid in [r["id"] for r in expected["requests"]]:
        e = exp_r[rid]
        a = act_r.get(rid)
        if a is None:
            problems.append(("request-missing", rid, e, None))
            continue
        for field in REQUEST_FIELDS:
            if e[field] != a[field]:
                problems.append(("request-" + field, rid, e[field], a[field]))
                break
    for rid in act_r:
        if rid not in exp_r:
            problems.append(("request-unexpected", rid, None, act_r[rid]))

    for when in ("before", "after"):
        key = "tables_" + when
        for t, why, e, a in dbstate.diff(expected[key], actual[key]):
            problems.append(("%s-table-%s" % (when, why.replace(" ", "-")),
                             t, e, a))
    return problems


def table_scores(expected, actual):
    """{"before/<table>": bool} -- one pass/fail per table per snapshot."""
    out = {}
    for when in ("before", "after"):
        key = "tables_" + when
        for t, e in expected[key].items():
            out["%s/%s" % (when, t)] = (actual[key].get(t) == e)
    return out


def request_scores(expected, actual):
    """{id: bool} -- one pass/fail per scored request."""
    act = {r["id"]: r for r in actual["requests"]}
    out = {}
    for e in expected["requests"]:
        a = act.get(e["id"])
        out[e["id"]] = bool(a) and all(e[f] == a[f] for f in REQUEST_FIELDS)
    return out


# A flipped id is one whose *body* a step is allowed to change and whose new
# value the task may not impose. It is not a licence to stop answering. The
# envelope -- the status and any redirect -- is still compared against the
# recording, and only the body is required to differ. Without the split,
# "differs from the recording" is precisely what a stack trace does, so a
# deployment that answered 500 to every request would pass every flip in the
# step for free; the step-04 floor run is where that was measured.
REQUEST_ENVELOPE = ("status", "location")
REQUEST_BODY = ("sha256", "length")

# Not an assert: `python -O` strips those, and the scorer is free to be run
# that way. A partition that has silently stopped being one would score every
# request against a subset of its fields.
if set(REQUEST_ENVELOPE) | set(REQUEST_BODY) != set(REQUEST_FIELDS):
    raise RuntimeError(
        "envelope+body must partition REQUEST_FIELDS: %r + %r != %r"
        % (REQUEST_ENVELOPE, REQUEST_BODY, REQUEST_FIELDS))

# Flipped ids whose recorded answer is ALSO a correct one, so the id holds
# either as a flip or by reproducing the recording exactly. There is one. The
# lesson's `name LIKE 'S%'` runs over a column step 02 made case-insensitive,
# and step 02 accepts two ways of doing that: a non-deterministic collation,
# under which PostgreSQL refuses the pattern match and the page has to differ,
# and `citext`, whose own LIKE operator folds case, so the match runs and the
# page is MySQL's. Requiring a difference would fail the second schema for a
# choice the task allowed it, and requiring sameness would fail the first.
FLIP_OR_SAME = frozenset(["lesson-numeric-sqli-injection#05"])


def flip_reason(e, a, anchor=None):
    """Why this id does not hold as a flip, or None when it does.

    The conditions and their order are `request_flip_scores`'s -- this is its
    loop body -- and the order is also the order a reader wants them reported
    in: an id that both kept the recording's body and answered 500 is reported
    as the 500, because the envelope is the thing to fix first.
    """
    if not a:
        return "flip: the deployment did not answer this request"
    moved = [f for f in REQUEST_ENVELOPE if e[f] != a[f]]
    if moved:
        return "flip: the envelope moved, " + ", ".join(
            "%s expected=%r actual=%r" % (f, e[f], a[f]) for f in moved)
    if not any(e[f] != a[f] for f in REQUEST_BODY):
        return "flip: the body is the recording's, unchanged"
    # A reading this scorer did not take fails closed rather than passing on
    # the envelope, and says which of the two it was.
    surface = a.get("error_surface", None)
    if surface is None:
        return "flip: the record carries no error-surface reading"
    if surface:
        return "flip: the body carries an error surface, %s" % (surface,)
    # Same rule for the ends. An end the step did not record is not compared;
    # an end it did record and this scorer has no digest for is a failure,
    # which is what the `got != spec[...]` below gives when the rung is
    # missing from the record.
    for end, rungs in (("head", "heads"), ("tail", "tails")):
        spec = (anchor or {}).get(end)
        if not spec:
            continue
        got = (a.get(rungs) or {}).get(str(spec["bytes"]))
        if got != spec["sha256"]:
            return ("flip: the %s anchor over %d bytes is %s, not the"
                    " recording's %s" % (end, spec["bytes"],
                                         (got or "not recorded")[:12],
                                         spec["sha256"][:12]))
    return None


def request_flip_scores(expected, actual, anchors=None, reasons=None):
    """{id: bool} under the flipped test: same envelope, different body.

    `anchors` is the third condition and the one that makes a flip a test
    rather than a hole. A flipped id is allowed to change its body, and a
    deployment that stopped rendering the page changes its body too -- the
    envelope split above catches the deployment that answers 500, and catches
    nothing at all about a 200 carrying a warning where the lesson used to be.
    So a step may record, per flipped id, the digests of the two ends of the
    body that are the *same* in the recording and in its own golden tree: the
    lesson's own chrome, which the step does not change and which a page that
    is no longer the lesson does not have.

    {id: {"head": {"bytes": n, "sha256": s}, "tail": {...}}}, with `head` and
    `tail` both optional and at least one required, and each carrying the
    length it was taken over -- a rung of `runner.ANCHOR_LADDER`, which the
    scored record digests every end at. How much of an end is anchored is a
    property of the id and not a policy: on step 04's four flips the three
    whose statement was malformed on both servers agree for 26 kB before the
    error text and 4 kB after it, while the one whose statement used to
    *succeed* takes the application's error path and agrees for only 915 bytes
    at the front and 3.7 kB at the back, so it is anchored on 768 at the front
    and 2,048 at the back. Recording a longer head there would mean recording a
    digest of the golden tree's own new bytes, which is the one thing an anchor
    must not be. An id with no entry at all is scored on the envelope and the
    error surface alone.

    `reasons`, when given, is filled with {id: why} for every id that does not
    hold -- `behaviorgate` reports it, because the ordinary per-id detail is a
    diff against the recording and a flip fails for the *opposite* reason.

    The error surface is the other half, and it is the half that reads the
    middle. Anchors bound a flipped body at its ends; a page that rendered its
    chrome, printed a stack trace where the lesson goes, and rendered its
    footer satisfies both ends and is still not the lesson. So a flipped body
    may carry none of `httpdrv.ERROR_SURFACE` -- PHP's display template and
    jframework's handler, both of which this deployment has switched off, and
    neither of which is the driver's own message, which the step's instruction
    expressly allows the page to print.
    """
    act = {r["id"]: r for r in actual["requests"]}
    anchors = anchors or {}
    out = {}
    for e in expected["requests"]:
        rid = e["id"]
        a = act.get(rid)
        why = flip_reason(e, a, anchors.get(rid))
        if why is not None and rid in FLIP_OR_SAME and a \
                and all(e[f] == a[f] for f in REQUEST_FIELDS):
            why = None
        out[rid] = why is None
        if reasons is not None and why is not None:
            reasons[rid] = why
    return out


def summary():
    n_sc, n_req, n_adv = corpus.counts()
    return {"scenarios": n_sc, "requests": n_req, "adversarial_requests": n_adv}
