# -*- coding: utf-8 -*-
"""Shared mutable state under concurrent access — step 06's own bar.

Every other case module drives the tree from one thread. These drive it from
twelve, and they are the only cases in the suite whose *expected* answer is an
invariant rather than a value copied off the 3.8 recording: a round robin over
four connections hands out exactly a quarter each, a counter incremented 3,612
times reads 3,612, and 3,612 digest headers carry 3,612 distinct nonce counts.
Nothing here asks the tree to be fast or to scale. It asks it not to lose an
update.

**Why these are recordable at all.** The constructs under test
(``self.rr += 1``, ``dead_count[c] = dead_count.get(c, 0) + 1``,
``self.nonce_count += 1``) are unguarded read-modify-writes, so on paper they
race on any interpreter. In practice the GIL serialises the bytecode and they do
not, and that is measured rather than assumed — a probe at this exact shape
(12 threads, 301 iterations) reports:

    3.8.20   GIL      0/60 trials raced      (the recording interpreter)
    3.13.15  GIL      0/60 trials raced      (rungs 04 and 05)
    3.13.15t GIL=1    0/20 trials raced
    3.13.15t GIL=0   10/10 trials raced      (IndexError, ~30% updates lost)

So these cases are stable enough to record, they pass on rungs 01-05 where a
correct migration leaves them alone, and at rung 06 — where the verifier sets
``PYTHON_GIL=0`` — the unguarded predecessor fails every one of them. That is
the "predecessor passes family 1 completely and fails family 2 completely"
contract, obtained without a single line of the suite being special-cased per
rung.

**Why every case here silences logging.** ``ConnectionPool.mark_dead`` and
``urllib3``'s pool both log per call, and ``runcase`` captures those records
into the observation. Twelve threads emit 3,612 records in an order no two runs
agree on, so a case that kept them would be unrecordable for a reason that has
nothing to do with the state it is scoring. The counters are the subject; the
log stream is not. Single-threaded cases elsewhere in the suite still score the
log stream, and the trace logger remains the sharpest view of the str/bytes
boundary there.
"""
import re
import threading

from ..registry import case

#: Measured shape (see the module docstring). Twelve workers against the seven
#: vCPUs the task pins is deliberate oversubscription: a worker that owns a
#: core for its whole run never interleaves with anything, while one that is
#: preempted is preempted somewhere, and the interesting somewhere is between
#: the read and the write. ~300 iterations is where the GIL-off failure is
#: total while the GIL-on trial budget is still clean.
#:
#: 301 rather than 300, and the odd number is the whole point. ``THREADS *
#: ITERS`` is 3,612, which is divisible by ``CONNS``, so one shared cursor
#: still hands out an exactly even 903 per connection -- but ``ITERS`` itself
#: is not, so twelve *private* cursors hand out 76 of one and 75 of the other
#: three each and the totals come out [912, 900, 900, 900]. At 300 they would
#: come out [900, 900, 900, 900]: twelve identical private robins over a
#: divisor of their own length are globally indistinguishable from one shared
#: one, and a selector that gave each thread its own cursor and published the
#: result onto ``self.rr`` would collect this rung's whole selector case while
#: removing the state its callers read. `rr_after` alone does not close that
#: -- the private robins also leave `rr` at an index, just the wrong one. The
#: odd count closes it in the distribution as well, which is the half a
#: candidate cannot inspect the cursor to satisfy.
THREADS = 12
ITERS = 301
CONNS = 4


class _Quiet(object):
    """Suppress logging for the duration of a case body.

    Restored on the way out even though each case is its own process: a global
    left flipped is the kind of thing that is harmless until the day something
    else runs after it.
    """
    def __enter__(self):
        import logging
        logging.disable(logging.CRITICAL)
        return self

    def __exit__(self, *exc):
        import logging
        logging.disable(logging.NOTSET)
        return False


def _hammer(work, threads=THREADS):
    """Run ``work(i)`` on ``threads`` threads released simultaneously.

    The barrier matters: without it the first worker can finish before the last
    one starts, and a race that needs two threads inside the same three
    bytecodes never gets the chance to happen. Exceptions are collected rather
    than raised, because "did this raise" is itself part of what is scored --
    the round-robin selector is documented never to raise and does, under free
    threading, from an index it computed two statements earlier.
    """
    start = threading.Barrier(threads)
    errors, lock = [], threading.Lock()

    def worker(i):
        start.wait()
        try:
            work(i)
        except BaseException as exc:                       # noqa: BLE001
            with lock:
                errors.append(type(exc).__name__)

    ts = [threading.Thread(target=worker, args=(i,)) for i in range(threads)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    # Sorted and de-duplicated: twelve threads hitting the same defect produce
    # between one and twelve copies of the same name, and which it is on a
    # given run is exactly the nondeterminism these cases must not carry.
    return sorted(set(errors))


# ------------------------------------------- elasticsearch connection pool ---

@case("freethread.rr-selector-fair", tags=("adversarial", "freethread"))
def _(ctx):
    """``RoundRobinSelector.select`` is ``self.rr += 1; self.rr %= len(...)``.

    Two unguarded read-modify-writes back to back. A lost update skews the
    distribution; an interleaving between the increment and the modulo indexes
    the list out of range, which is how a selector that cannot raise raises.
    """
    from elasticsearch.connection_pool import RoundRobinSelector

    conns = ["c%d" % i for i in range(CONNS)]
    sel = RoundRobinSelector(dict((c, {}) for c in conns))
    tally = dict((c, 0) for c in conns)
    lock = threading.Lock()

    def work(_i):
        mine = dict((c, 0) for c in conns)
        for _ in range(ITERS):
            mine[sel.select(conns)] += 1
        with lock:
            for c, v in mine.items():
                tally[c] += v

    with _Quiet():
        errors = _hammer(work)
    total = THREADS * ITERS
    return {"raised": errors,
            "selections": sum(tally.values()),
            "expected_selections": total,
            # The values, not the mapping: which physical connection is "first"
            # depends on dict ordering the tree is entitled to change.
            "distribution": sorted(tally.values()),
            "expected_each": total // CONNS,
            # Where the cursor ends up, which a flat distribution does not
            # pin down. `rr` starts at -1 and every call leaves it at the
            # *reduced* index it just returned, so after `total` calls it is
            # `(total - 1) % CONNS` -- and it is that on any tree that still
            # advances one shared cursor per selection and stores the
            # remainder, which is what the pinned `self.rr %= len(...)` does
            # and what the instruction states it still does.
            #
            # Three replacements hand out a perfectly even distribution with
            # no lost update at all and still fail here, which is the point of
            # reading the attribute: an `itertools.cycle` closed over by
            # `select` leaves `rr` at -1; a cursor kept in a `threading.local`
            # leaves it wherever the last thread to write it left it, or at -1
            # if it never writes it at all; and a shared cursor that increments
            # under a lock but indexes with `%` without storing the remainder
            # leaves it at `total - 1`. Each is correct-looking output from an
            # object that no longer holds the state its callers read and write
            # directly -- and every earlier rung's suite does read and write it
            # directly.
            "rr_after": sel.rr,
            "expected_rr_after": (total - 1) % CONNS}


@case("freethread.pool-dead-count-exact", tags=("adversarial", "freethread"))
def _(ctx):
    """``dead_count[c] = dead_count.get(c, 0) + 1`` in ``mark_dead``.

    The textbook lost update, and not a cosmetic one: this count is the
    exponent in ``dead_timeout * 2 ** (dead_count - 1)``, so an update lost here
    silently shortens the timeout a failing node is held out for -- the failure
    is a node being retried too early, which looks like a flaky cluster rather
    than like a bug.
    """
    import queue

    from elasticsearch.connection_pool import ConnectionPool

    class C(object):
        def __init__(self, i):
            self.i = i

        def __lt__(self, other):          # PriorityQueue ties compare the item
            return self.i < other.i

    conn = C(0)
    pool = ConnectionPool([(conn, {})], randomize_hosts=False)
    # The pool sizes its dead queue at len(connections), so the second
    # mark_dead on a one-host pool blocks forever. The subject here is the
    # counter, and a case that deadlocks on an unrelated bound scores nothing.
    pool.dead = queue.PriorityQueue(0)
    # One occurrence per call, pre-loaded.
    #
    # mark_dead counts only on the branch where ``self.connections.remove()``
    # succeeded -- a connection that is already gone returns early and
    # increments nothing. The obvious probe (re-add it if missing, then mark it
    # dead) therefore races against *itself*: two workers can observe the
    # connection present, one removes it, and the other takes the early return.
    # That cost 13 of the 3,600 increments the case made at its first shape on
    # the first guarded run, which read
    # exactly like a surviving defect in the tree and was not one. Pre-loading
    # the list means every call finds its connection and takes the counting
    # branch, so the only unguarded read-modify-write left in the case is the
    # one being scored.
    total = THREADS * ITERS
    pool.connections = [conn] * total

    def work(_i):
        for _ in range(ITERS):
            pool.mark_dead(conn, now=0)

    with _Quiet():
        errors = _hammer(work)
    return {"raised": errors,
            "counted": sum(pool.dead_count.values()),
            "expected": total,
            # Every occurrence consumed: a lost remove would leave the list
            # non-empty and is a different defect from a lost increment.
            "live_remaining": len(pool.connections)}


# ------------------------------------------------------ requests auth ---

@case("freethread.digest-nonce-unique", tags=("adversarial", "freethread"))
def _(ctx):
    """``HTTPDigestAuth.build_digest_header``'s ``nc`` counter.

    ``if nonce == self.last_nonce: self.nonce_count += 1``, and the result goes
    on the wire as ``nc=%08x``. That field exists so a server can refuse a
    replayed Authorization header, so a lost update here is not a wrong
    statistic -- it is two live requests sharing an ``nc``, which is precisely
    the replay window the field was added to close. Scored from outside, by
    parsing the header the caller gets back.
    """
    from requests.auth import HTTPDigestAuth

    auth = HTTPDigestAuth("u", "p")
    auth.chal = {"realm": "r", "nonce": "n", "algorithm": "MD5", "qop": "auth"}
    auth.last_nonce = "n"
    auth.nonce_count = 0
    pat = re.compile(r"nc=([0-9a-f]{8})")
    seen, lock = [], threading.Lock()

    def work(_i):
        mine = []
        for _ in range(ITERS):
            m = pat.search(auth.build_digest_header("GET", "http://x/y"))
            mine.append(m.group(1) if m else "?")
        with lock:
            seen.extend(mine)

    with _Quiet():
        errors = _hammer(work)
    total = THREADS * ITERS
    return {"raised": errors,
            "headers": len(seen),
            # The count of distinct nc values, not the values: a correct
            # implementation hands out 1..N and the *set* is what matters.
            "distinct_nc": len(set(seen)),
            "expected_distinct": total,
            "unparsed": seen.count("?")}


# ----------------------------------------------------------- urllib3 pool ---

class _Stop(Exception):
    """Ends ``_make_request`` one statement after the counter it is driving.

    ``self.num_requests += 1`` is the first statement of that method and
    everything after it wants a socket. A stub connection whose ``request``
    raises gets the increment and nothing else, which keeps the case off the
    network and keeps its observation to the counter.
    """


class _StubConn(object):
    timeout = None
    sock = None

    def request(self, *args, **kwargs):
        raise _Stop()


def _count_new_conn(pool):
    def work(_i):
        for _ in range(ITERS):
            pool._new_conn()
    return work


def _count_make_request(pool):
    def work(_i):
        for _ in range(ITERS):
            try:
                pool._make_request(_StubConn(), "GET", "/")
            except _Stop:
                pass
    return work


@case("freethread.urllib3-num-connections", tags=("adversarial", "freethread"))
def _(ctx):
    """``num_connections`` and ``num_requests``, in both copies of urllib3.

    The pool's own accounting of how many sockets it has opened and how many
    requests it has put through them -- the numbers an operator reads to
    decide whether a pool is leaking. Driven through ``_new_conn`` and
    ``_make_request`` rather than by touching the attributes, so this
    exercises the same statements a real request does. ``_new_conn`` builds
    the connection object without connecting it and ``_make_request`` is
    stopped at its second statement, so nothing here reaches the network.

    Both copies, because ``requests`` vendors its own urllib3 and every call
    this plugin makes through ``requests`` goes to the vendored one. The two
    are byte-identical in the pinned tree; a fix applied to whichever copy a
    test happens to import is half a fix, and driving ``lib/urllib3`` alone
    would score the half ``requests`` does not use.

    What this does *not* score is the ordinal ``_new_conn`` logs -- the value
    it reads back out of the counter it has just incremented. A tree that
    guards the increment alone, and reads the attribute again in the log call
    the way the pinned source does, keeps the count exact and still hands two
    concurrent calls the same ordinal; the golden holds the read inside the
    section, but this case does not ask for it. It cannot: the read-back is a
    second statement, the GIL is free to switch between two statements, and on
    the recording interpreter one run in three loses an ordinal.
    An expectation the oracle cannot reproduce is not an expectation.

    Both schemes, because ``HTTPSConnectionPool`` does not inherit
    ``_new_conn`` -- it overrides it with a second copy of the same
    increment-and-read, for the same counter, four lines longer. Guarding the
    two ``HTTPConnectionPool`` copies and leaving the two HTTPS overrides
    racing is again half a fix, and it is the half that carries every request
    this plugin makes to a cluster behind TLS, which after step 03 is the only
    kind it is willing to make. ``_make_request`` is the other way round: each
    copy has one of it, shared by that copy's HTTP and HTTPS classes, and it is
    driven on the HTTP pools only, because the HTTPS pools validate the
    connection before they use it and a stub has no socket to validate.
    """
    from urllib3.connectionpool import (
        HTTPConnectionPool, HTTPSConnectionPool)
    from requests.packages.urllib3.connectionpool import (
        HTTPConnectionPool as VendoredHTTPConnectionPool,
        HTTPSConnectionPool as VendoredHTTPSConnectionPool)

    out = {"expected": THREADS * ITERS}
    for label, classes in (
            ("urllib3", (HTTPConnectionPool, HTTPSConnectionPool)),
            ("requests.packages.urllib3", (VendoredHTTPConnectionPool,
                                           VendoredHTTPSConnectionPool))):
        errors, schemes = [], {}
        for scheme, cls in zip(("http", "https"), classes):
            pool = cls("localhost", maxsize=1)
            with _Quiet():
                errors += _hammer(_count_new_conn(pool))
                schemes[scheme] = {"num_connections": pool.num_connections}
                if scheme == "http":
                    errors += _hammer(_count_make_request(pool))
                    schemes[scheme]["num_requests"] = pool.num_requests
        out[label] = {"raised": sorted(set(errors)),
                      "http": schemes["http"],
                      "https": schemes["https"]}
    return out
