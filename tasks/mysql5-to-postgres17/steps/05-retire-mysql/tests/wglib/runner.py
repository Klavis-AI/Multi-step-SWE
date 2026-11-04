"""Runs the corpus and reduces each response to the record that is compared.

The same code produces the expectation (against MySQL, while recording) and the
observation (against whatever the agent built, while scoring). Nothing here is
allowed to know which of the two it is doing.
"""

import hashlib
import socket
import time
import urllib.error

from . import corpus
from . import dbstate
from . import httpdrv

# A deployment that answers slowly is a deployment that fails, not a scorer
# that waits. Two bounds, because they fail differently:
#
#   * REQUEST_TIMEOUT bounds one response. The recorded corpus was measured on
#     a MySQL deployment where the slowest request is well under a second, so
#     twenty is not a performance threshold -- it is the line between "slow"
#     and "never".
#   * CORPUS_BUDGET bounds the whole pass. Without it the worst case is 413
#     requests each burning the per-request timeout, which is hours, and hours
#     is a Harbor kill: the step vanishes from the run rather than scoring what
#     it earned (trap 3). `installgate` already fail-closes the SQL it did not
#     get to apply; this is the same rule for HTTP, and the requests the budget
#     did not reach are simply absent from the records, which `collect` already
#     scores as failures.
REQUEST_TIMEOUT = 20
CORPUS_BUDGET = 900

# How much of each end of a normalised body is digested separately, beside the
# digest of the whole. Only the flipped ids use it (`collect.request_flip_scores`)
# and only they need to: a flipped id is one whose body a step is allowed to
# change, and without an anchor "allowed to change" is indistinguishable from
# "stopped rendering". The page those four ids answer with is 33 KB of lesson
# chrome around a few hundred bytes of the server's own words, so the two ends
# of it are the part the step does not touch, and they are recorded as such.
#
# A ladder rather than one length, because how much of an end two trees agree
# on is a property of the id. One length means an id that agrees on less than
# it gets no anchor at that end at all: step 04's
# `lesson-numeric-sqli-injection#05` agrees on 915 leading bytes, one anchor
# needed 2,048, and the id was left anchored on its tail alone -- a page whose
# whole front had been replaced by an error would have passed. With the ladder
# the step records the largest rung that fits inside the agreement, 768 bytes
# there, and the front of the page is a test again. 256 is the smallest rung on
# purpose: below that an anchor is a digest of `<!DOCTYPE html>` and a title,
# which every page in the application shares.
#
# Computed for every record rather than for the four, because the recorder is
# not allowed to know which ids a step flips -- it produces the same reading
# whether it is recording the expectation or scoring a candidate, and a
# recorder that took the flip list as an argument would be two instruments.
ANCHOR_BYTES = 2048
ANCHOR_LADDER = (256, 512, 768, 1024, 1536, ANCHOR_BYTES)


def request_id(scenario, index):
    return "%s#%02d" % (scenario.name, index)


def body_reading(body):
    """Every reading taken of one normalised body.

    Its own function so that there is exactly one of it: `authoring/
    rehearse_gates.py flips` builds records out of real captured bodies and
    scores them through `collect.request_flip_scores`, and a rehearsal that
    computed its own digests would be rehearsing itself rather than the
    instrument.
    """
    return {
        "sha256": hashlib.sha256(body).hexdigest(),
        "length": len(body),
        "heads": {str(n): hashlib.sha256(body[:n]).hexdigest()
                  for n in ANCHOR_LADDER if n <= len(body)},
        "tails": {str(n): hashlib.sha256(body[-n:]).hexdigest()
                  for n in ANCHOR_LADDER if n <= len(body)},
        # Not a digest: the list of error surfaces this body carries, which is
        # short, fixed, and says nothing about the candidate's own bytes.
        # `collect.request_flip_scores` is the only reader.
        "error_surface": httpdrv.error_surface(body),
    }


class Recorder(object):
    """Drives the corpus and collects one record per request.

    `strict` is the difference between recording and scoring. While recording
    the expectation there is no such thing as a request that failed to happen:
    a transport error means the fixture would be wrong, so it is raised. While
    scoring, the same error is the candidate's deployment not answering, and it
    is recorded as a response that cannot match anything.

    `logmark` is an optional callable returning an opaque position in the
    server log (see `pglog.mark`). When given, `windows` ends up holding one
    (scenario, start, end) per scenario, which is what lets a statement the
    server logged be attributed to the scenario that caused it.
    """

    def __init__(self, base="http://127.0.0.1", host="localhost",
                 timeout=REQUEST_TIMEOUT, budget=CORPUS_BUDGET, strict=True,
                 logmark=None):
        self.base = base
        self.host = host
        self.timeout = timeout
        self.budget = budget
        self.strict = strict
        self.logmark = logmark
        self.session_ids = set()
        self.windows = []           # [(scenario name, start, end)]
        self.aborted = None         # why the pass stopped early, or None
        self.transport_errors = []  # [(request id, what went wrong)]

    def _note_session(self, app):
        sid = app.session_id()
        if sid:
            self.session_ids.add(sid)

    def _request(self, app, rid, req):
        """One request, or a record that cannot match anything.

        One retry, and only for a connection-level error -- the server was not
        there to accept the socket. A timeout is not retried: it means the
        request was accepted and not answered, and doing it twice doubles the
        wait for a deployment that is already over budget.
        """
        for attempt in (1, 2):
            try:
                return app.request(req.method, req.path, data=req.data)
            except (urllib.error.URLError, OSError) as e:
                reason = getattr(e, "reason", e)
                timed_out = isinstance(reason, socket.timeout)
                if self.strict:
                    raise
                if attempt == 1 and not timed_out:
                    time.sleep(0.5)
                    continue
                why = "%s: %s" % (type(reason).__name__,
                                  " ".join(str(reason).split())[:120])
                self.transport_errors.append((rid, why))
                return {"status": 0, "location": None,
                        "body": ("<no response: %s>" % why).encode()}

    def run(self, scenarios=None):
        """[{id, status, location, sha256, length, adversarial}] in order."""
        out = []
        deadline = time.monotonic() + self.budget if self.budget else None
        for sc in (scenarios if scenarios is not None else corpus.SCENARIOS):
            if deadline is not None and time.monotonic() > deadline:
                self.aborted = (
                    "the corpus budget of %ds ran out before %r; %d request(s)"
                    " were not sent and are scored as unanswered"
                    % (self.budget, sc.name,
                       sum(len(s.reqs) for s in _remaining(scenarios, sc))))
                break
            start = self.logmark() if self.logmark else None
            app = httpdrv.App(self.base, self.host, self.timeout)
            for i, req in enumerate(sc.reqs):
                rid = request_id(sc, i)
                self._note_session(app)
                resp = self._request(app, rid, req)
                self._note_session(app)
                out.append({
                    "id": rid,
                    "method": req.method,
                    "path": req.path,
                    "status": resp["status"],
                    "location": resp["location"],
                    "body": resp["body"],
                    "adversarial": sc.adversarial,
                })
            if self.logmark:
                self.windows.append((sc.name, start, self.logmark()))
        return out

    def finalize(self, records):
        """Normalise the collected bodies once every session id is known.

        A session id is only substituted out after the whole run, because the id
        a request hands out can still appear in a response recorded earlier in
        the same scenario -- the login form embeds the id the server has just
        issued.
        """
        sids = sorted(self.session_ids, key=len, reverse=True)
        final = []
        for r in records:
            norm = httpdrv.normalize(
                {"status": r["status"], "location": r["location"], "body": r["body"]},
                sids)
            body = norm["body"]
            rec = {
                "id": r["id"],
                "method": r["method"],
                "path": r["path"],
                "adversarial": r["adversarial"],
                "status": norm["status"],
                "location": norm["location"],
            }
            rec.update(body_reading(body))
            final.append(rec)
        return final


def _remaining(scenarios, first):
    """The scenarios from `first` onward, for the abort message."""
    seq = list(scenarios if scenarios is not None else corpus.SCENARIOS)
    try:
        return seq[seq.index(first):]
    except ValueError:
        return []


# ------------------------------------------------------------ db snapshot ---

def _mask(state, table, rows):
    """Replace values that are random by construction with their length.

    corpus.masked_columns() and corpus.random_cells() name their columns the
    way the MySQL recording spells them -- `ID`, `Password`, `Name`. A migrated
    PostgreSQL schema is free to fold those names, and the faithful conversion
    of an unquoted MySQL identifier does fold them, so an exact-name lookup
    finds nothing on that side and every mask silently stops applying: the
    random salts of the accounts the corpus created would be compared against
    each other and `tables_after` could never match. The column is located by
    its folded name, which is what both sides agree on.
    """
    names = [c[0] if isinstance(c, tuple) else c for c in state.columns(table)]
    at_of = {}
    for i, n in enumerate(names):
        at_of.setdefault(n.lower(), i)

    # Cells that carry a random token instead of data. These are rewritten in
    # every row of the table, not only in rows the corpus created, because the
    # token is the cell's whole identity -- see corpus.random_cells().
    tokens = []
    for col, pattern, replacement in corpus.random_cells().get(table, ()):
        if col.lower() in at_of:
            tokens.append((at_of[col.lower()], pattern, replacement))

    rule = corpus.masked_columns().get(table)
    id_at = None
    if rule is not None:
        id_col, threshold, cols = rule
        if id_col.lower() in at_of:
            id_at = at_of[id_col.lower()]
            at = [at_of[c.lower()] for c in cols if c.lower() in at_of]

    if id_at is None and not tokens:
        for r in rows:
            yield r
        return

    for r in rows:
        for j, pattern, replacement in tokens:
            if r[j] is not None and pattern.match(_text(r[j])):
                r = list(r)
                r[j] = replacement
        if id_at is not None:
            try:
                created_by_corpus = int(r[id_at]) > threshold
            except (TypeError, ValueError):
                created_by_corpus = False
            if created_by_corpus:
                r = list(r)
                for j in at:
                    r[j] = "<MASKED len=%d>" % (len(r[j]) if r[j] is not None else -1)
        yield r


def _text(v):
    """A str to match a pattern against, whatever the driver handed back."""
    if isinstance(v, bytes):
        return v.decode("utf-8", "replace")
    return v if isinstance(v, str) else str(v)


def db_snapshot(state, session_ids=()):
    """{table: {digest, rows}} with masking and session-id substitution applied."""
    out = {}
    sids = sorted(session_ids, key=len, reverse=True)
    for t in state.tables():
        digest, n = dbstate.table_digest(_mask(state, t, state.rows(t)), sids)
        out[t] = {"digest": digest, "rows": n}
    return out
