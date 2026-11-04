# -*- coding: utf-8 -*-
"""Golden solution for rung 06, as a list of byte-anchored edits.

    python3 work/solve06.py <tree>

Applied on top of rung 05's tree. Same shape as ``work/solve01.py`` through
``work/solve05.py``: every edit anchors on the exact bytes it expects, counts
them, and refuses to continue when a count is wrong or a replacement would be
a no-op (INSTRUCTIONS trap 7).

One requirement, and it is the smallest diff of any rung here -- four objects,
one ``threading.Lock`` each, eight statements moved inside a ``with``. What
makes it a rung is that nothing in the tree *looks* wrong. Every one of these
sites is correct under a global interpreter lock, because a bytecode boundary
is where the GIL lets another thread in and ``INPLACE_ADD`` on an ``int``
attribute is three bytecodes that the GIL does not happen to split often
enough to notice. ``python3.13t`` with ``PYTHON_GIL=0`` removes the accident,
not the code:

  ``RoundRobinSelector.select``   ``self.rr += 1`` then ``self.rr %= len(...)``.
                                  A lost update skews the round robin; an
                                  interleaving *between* the two statements
                                  indexes past the end of the list, which is
                                  how a method containing no ``raise`` raises
                                  ``IndexError``.

  ``ConnectionPool.mark_dead``    ``dead_count.get(c, 0) + 1`` then
                                  ``dead_count[c] = ...``. The lost update is
                                  not a cosmetic statistic: that count is the
                                  exponent in
                                  ``dead_timeout * 2 ** (dead_count - 1)``, so
                                  losing it puts a node that has failed five
                                  times back in rotation on a two-failure
                                  timeout.

  ``HTTPDigestAuth``              ``nonce_count`` and the ``nc=`` it is
                                  formatted into. ``nc`` exists so a server can
                                  refuse a replayed Authorization header; two
                                  threads sharing one is the replay window the
                                  field was added to close.

  ``HTTPConnectionPool``          ``num_connections`` in both ``_new_conn``
                                  methods and ``num_requests`` in
                                  ``_make_request`` -- the numbers an operator
                                  reads to decide whether a pool is leaking.

Three decisions in here are not "add a lock", and each is a place a plausible
solver goes wrong (they are called out in the instruction for that reason):

  the queue stays out    ``mark_dead`` ends with ``self.dead.put(...)`` and
                         ``self.dead`` is a ``PriorityQueue(len(live))`` --
                         bounded. A ``put`` that blocks inside the pool's lock
                         blocks every thread that could drain the queue, so the
                         lock covers the ``remove`` and the counter and stops
                         there.

  not ``threading.local``  which is what upstream requests eventually reached
                         for, and which is wrong here: per-thread counters hand
                         out ``nc=00000001`` from twelve threads at once.

  ``num_401_calls``      is a check-then-act too (``< 2`` then ``+= 1``) and is
                         deliberately *not* locked. Its critical section would
                         have to span ``connection.send`` -- a network round
                         trip -- and the callee is ``build_digest_header``,
                         which takes the auth object's lock. A
                         ``threading.Lock`` there is a deadlock, and the
                         inherited ``requests.auth-digest-flow`` case is what
                         notices.

Nothing here is OPTIONAL. Rung 05 left no latitude that touches these eight
statements: they arrive at this rung byte-identical from the pin, through five
rungs that had no reason to go near them, and the rung-05 blind agent's tree
has them verbatim as well (NOTES section 11.13's file-set diff).
"""
from __future__ import print_function

import ast
import os
import sys


# (relpath, old, new, count): ``old`` must appear exactly ``count`` times.
EDITS = []

#: The two urllib3 copies, as at rung 04: byte-identical in the pinned tree,
#: each imported by a different half of this codebase. Only the top-level one
#: is driven by a case, and editing only the one a case drives is how a tree
#: ends up half migrated.
U3 = ("lib/urllib3", "lib/requests/packages/urllib3")


def edit(rel, old, new, count=1):
    EDITS.append((rel, old, new, count))


def edit3(rel, old, new, count=1):
    for root in U3:
        EDITS.append((root + "/" + rel, old, new, count))


# --------------------------------------------------------------------------
# 1. elasticsearch/connection_pool.py -- the selector and the pool.
#
# This module is one of the seventeen in `gates.TYPED_LIB`, so everything
# added here is read by `types-check-clean` under `mypy --strict`. A
# `threading.Lock()` bound in `__init__` needs no annotation for that: mypy
# infers the attribute from the assignment, and the name is private, so
# `public-surface-census` does not see it either.
# --------------------------------------------------------------------------

edit("lib/elasticsearch/connection_pool.py",
     b"import logging\nfrom queue import PriorityQueue, Empty\n",
     b"import logging\nimport threading\nfrom queue import PriorityQueue, Empty\n")

edit("lib/elasticsearch/connection_pool.py",
     b"""        super(RoundRobinSelector, self).__init__(opts)
        self.rr = -1

    def select(self, connections: Sequence[Connection]) -> Connection:
        self.rr += 1
        self.rr %= len(connections)
        return connections[self.rr]
""",
     b"""        super(RoundRobinSelector, self).__init__(opts)
        self.rr = -1
        #: `rr += 1` and `rr %= len(connections)` are two read-modify-writes
        #: on one attribute, and the second is the one that keeps the index
        #: in range. Interleave them and the selector either skews -- two
        #: callers reading the same `rr` -- or indexes past the end of the
        #: list it was handed.
        self._lock = threading.Lock()

    def select(self, connections: Sequence[Connection]) -> Connection:
        with self._lock:
            self.rr += 1
            self.rr %= len(connections)
            return connections[self.rr]
""")

edit("lib/elasticsearch/connection_pool.py",
     b"""        self.dead_count: dict[Connection, int] = {}
""",
     b"""        self.dead_count: dict[Connection, int] = {}
        #: Guards pool membership: `mark_dead`'s `remove` and its `dead_count`
        #: bump, `mark_live`'s `del`, `resurrect`'s `append`. One lock for all
        #: four because they maintain one invariant -- a connection is in the
        #: live list, or it is counted in `dead_count`, and no thread should
        #: ever see it in neither or in both.
        #:
        #: `self.dead.put` is deliberately left outside it; see `mark_dead`.
        self._lock = threading.Lock()
""")

edit("lib/elasticsearch/connection_pool.py",
     b"""        now = now if now else time.time()
        try:
            # `connections` is a list in this class; see `__init__`.
            cast("list[Connection]", self.connections).remove(
                connection)
        except ValueError:
            # connection not alive or another thread marked it already, ignore
            return
        else:
            dead_count = self.dead_count.get(connection, 0) + 1
            self.dead_count[connection] = dead_count
            timeout = self.dead_timeout * 2 ** min(dead_count - 1, self.timeout_cutoff)
            self.dead.put((now + timeout, connection))
            logger.warning(
                'Connection %r has failed for %i times in a row, putting on %i second timeout.',
                connection, dead_count, timeout
            )
""",
     b"""        now = now if now else time.time()
        with self._lock:
            try:
                # `connections` is a list in this class; see `__init__`.
                cast("list[Connection]", self.connections).remove(
                    connection)
            except ValueError:
                # connection not alive or another thread marked it already, ignore
                return
            # `get` then `[...] =` is the textbook lost update, and what it
            # loses here is the exponent in the timeout computed just below.
            dead_count = self.dead_count.get(connection, 0) + 1
            self.dead_count[connection] = dead_count
        # Outside the lock on purpose. `self.dead` is bounded at the number of
        # connections the pool was built with, so this `put` can block, and a
        # blocking call made while holding `_lock` would stop `resurrect` --
        # the only thing that drains the queue -- from ever running.
        timeout = self.dead_timeout * 2 ** min(dead_count - 1, self.timeout_cutoff)
        self.dead.put((now + timeout, connection))
        logger.warning(
            'Connection %r has failed for %i times in a row, putting on %i second timeout.',
            connection, dead_count, timeout
        )
""")

edit("lib/elasticsearch/connection_pool.py",
     b"""        try:
            del self.dead_count[connection]
        except KeyError:
            # race condition, safe to ignore
            pass
""",
     b"""        with self._lock:
            try:
                del self.dead_count[connection]
            except KeyError:
                # race condition, safe to ignore
                pass
""")

edit("lib/elasticsearch/connection_pool.py",
     b"""        # either we were forced or the connection is elligible to be retried
        cast("list[Connection]", self.connections).append(connection)
""",
     b"""        # either we were forced or the connection is elligible to be retried
        with self._lock:
            cast("list[Connection]", self.connections).append(connection)
""")


# --------------------------------------------------------------------------
# 2. requests/auth.py -- the digest `nc` counter.
# --------------------------------------------------------------------------

edit("lib/requests/auth.py",
     b"import os\nimport re\nimport time\nimport hashlib\n",
     b"import os\nimport re\nimport time\nimport hashlib\nimport threading\n")

edit("lib/requests/auth.py",
     b"""        self.pos = None
        self.num_401_calls = 1
""",
     b"""        self.pos = None
        self.num_401_calls = 1
        # Guards the `nc` counter in `build_digest_header` and nothing else.
        # Not a `threading.local()`: a per-thread counter hands out
        # `nc=00000001` from every thread at once, and `nc` is the one field
        # in the header whose whole job is to be different every time.
        #
        # `num_401_calls` below is a check-then-act as well, and is left
        # alone: its critical section would have to span
        # `r.connection.send`, and the call it wraps takes this lock.
        self._lock = threading.Lock()
""")

edit("lib/requests/auth.py",
     b"""        if nonce == self.last_nonce:
            self.nonce_count += 1
        else:
            self.nonce_count = 1
        ncvalue = '%08x' % self.nonce_count
        s = str(self.nonce_count).encode('utf-8')
""",
     b"""        with self._lock:
            if nonce == self.last_nonce:
                self.nonce_count += 1
            else:
                self.nonce_count = 1
                # Published here and not only at the bottom of the method:
                # two threads meeting a fresh nonce would otherwise both read
                # the stale `last_nonce`, both reset the counter, and both
                # send `nc=00000001`.
                self.last_nonce = nonce
            ncvalue = '%08x' % self.nonce_count
            s = str(self.nonce_count).encode('utf-8')
""")


# --------------------------------------------------------------------------
# 3. urllib3 -- the two pool counters, in both copies.
# --------------------------------------------------------------------------

edit3("connectionpool.py",
      b"import errno\nimport logging\nimport sys\nimport warnings\n",
      b"import errno\nimport logging\nimport sys\nimport threading\nimport warnings\n")

edit3("connectionpool.py",
      b"""        # These are mostly for testing and debugging purposes.
        self.num_connections = 0
        self.num_requests = 0
""",
      b"""        # These are mostly for testing and debugging purposes.
        self.num_connections = 0
        self.num_requests = 0
        # For those two counters and nothing else. The pool of connections
        # itself was always thread-safe -- it is a `LifoQueue` -- which is
        # exactly why these two plain integers next to it never looked wrong.
        self._counter_lock = threading.Lock()
""")

edit3("connectionpool.py",
      b"""        self.num_connections += 1
        log.info("Starting new HTTP connection (%d): %s" %
                 (self.num_connections, self.host))
""",
      b"""        with self._counter_lock:
            self.num_connections += 1
            num = self.num_connections
        log.info("Starting new HTTP connection (%d): %s" %
                 (num, self.host))
""")

edit3("connectionpool.py",
      b"""        self.num_connections += 1
        log.info("Starting new HTTPS connection (%d): %s"
                 % (self.num_connections, self.host))
""",
      b"""        with self._counter_lock:
            self.num_connections += 1
            num = self.num_connections
        log.info("Starting new HTTPS connection (%d): %s"
                 % (num, self.host))
""")

edit3("connectionpool.py",
      b"""        self.num_requests += 1
""",
      b"""        with self._counter_lock:
            self.num_requests += 1
""")


# --------------------------------------------------------------------------
# Final scan.
# --------------------------------------------------------------------------

#: (relpath, attribute) pairs whose every in-place mutation must sit inside a
#: `with` statement by the time this rung is done. Deliberately the five
#: attributes this rung is about and not "every `+=` in the tree": a scan
#: wider than the requirement would fail a correct solver, and a scan narrower
#: than the anchors above would not be a check at all.
GUARDED = (
    ("lib/elasticsearch/connection_pool.py", "rr"),
    ("lib/elasticsearch/connection_pool.py", "dead_count"),
    ("lib/requests/auth.py", "nonce_count"),
    ("lib/urllib3/connectionpool.py", "num_connections"),
    ("lib/urllib3/connectionpool.py", "num_requests"),
    ("lib/requests/packages/urllib3/connectionpool.py", "num_connections"),
    ("lib/requests/packages/urllib3/connectionpool.py", "num_requests"),
)

#: `nonce_count` is read-modify-written in `build_digest_header` and nowhere
#: else; `num_401_calls` is the one counter this rung leaves unlocked, so it
#: is absent from GUARDED above rather than excepted here.


def _self_attr(node):
    """``self.x`` -> ``"x"``, anything else -> None."""
    if (isinstance(node, ast.Attribute)
            and isinstance(node.value, ast.Name)
            and node.value.id == "self"):
        return node.attr
    return None


def _mutations_of(tree, attr):
    """Every statement that read-modify-writes ``self.<attr>``.

    Three shapes, which is all this tree uses: ``self.a += 1``,
    ``self.a = <expr mentioning self.a>``, and ``self.a[k] = <expr>`` or
    ``del self.a[k]`` for the one attribute that is a dict. Returns the AST
    statements, for the caller to locate.
    """
    out = []
    for node in ast.walk(tree):
        if isinstance(node, ast.AugAssign):
            if _self_attr(node.target) == attr:
                out.append(node)
        elif isinstance(node, ast.Assign):
            for t in node.targets:
                if _self_attr(t) == attr:
                    # A bare rebind is only a mutation if it reads the old
                    # value; `self.rr = -1` in `__init__` is not.
                    if any(_self_attr(n) == attr
                           for n in ast.walk(node.value)):
                        out.append(node)
                elif (isinstance(t, ast.Subscript)
                        and _self_attr(t.value) == attr):
                    out.append(node)
        elif isinstance(node, ast.Delete):
            for t in node.targets:
                if (isinstance(t, ast.Subscript)
                        and _self_attr(t.value) == attr):
                    out.append(node)
    return out


def _locked_spans(tree):
    """Line ranges covered by a ``with`` whose context is ``self.<lock>``.

    The attribute has to be one this module added -- a leading underscore and
    a name ending in ``lock`` -- so that wrapping a counter in, say,
    ``with self.pool.mutex`` (which exists, and which is the queue's lock, not
    the counter's) does not read as guarded.
    """
    spans = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.With):
            continue
        for item in node.items:
            ctx = item.context_expr
            name = _self_attr(ctx)
            if name and name.startswith("_") and name.endswith("lock"):
                spans.append((node.body[0].lineno, node.end_lineno))
                break
    return spans


def _final_scan(tree):
    """Every read-modify-write this rung owns is inside one of its locks.

    The byte anchors above are exact, which makes them precise and makes them
    blind: an anchor that is missing is a loud failure, but a site the anchors
    happen not to cover is silent. It was silent once, at rung 03, and cost
    that oracle 0.058333 with every one of its anchors matching. So the
    requirement is restated here as a property of the finished tree and
    checked against the tree, not against the diff.

    Parsed from **bytes**: one module in this tree is not UTF-8 and
    ``ast.parse`` honours the PEP 263 declaration.
    """
    bad = []
    for rel, attr in GUARDED:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        try:
            parsed = ast.parse(src)
        except SyntaxError as exc:
            bad.append("%s: does not parse: %s" % (rel, exc))
            continue
        if not any(isinstance(n, ast.Import)
                   and any(a.name == "threading" for a in n.names)
                   for n in ast.walk(parsed)):
            bad.append("%s: no `import threading`" % rel)
        spans = _locked_spans(parsed)
        muts = _mutations_of(parsed, attr)
        if not muts:
            bad.append("%s: found no mutation of self.%s to check -- the "
                       "scan has gone stale" % (rel, attr))
        for node in muts:
            if not any(lo <= node.lineno <= hi for lo, hi in spans):
                bad.append("%s:%d: self.%s mutated outside any lock"
                           % (rel, node.lineno, attr))
    if bad:
        raise SystemExit("solve06: final scan failed:\n  "
                         + "\n  ".join(bad))


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: solve06.py <tree>")
    tree = os.path.abspath(argv[1])

    applied = 0
    for rel, old, new, count in EDITS:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        n = src.count(old)
        if n != count:
            raise SystemExit("%s: anchor appears %d times, expected %d:\n%r"
                             % (rel, n, count, old[:160]))
        out = src.replace(old, new)
        if out == src:
            raise SystemExit("%s: edit changed nothing" % rel)
        with open(path, "wb") as fh:
            fh.write(out)
        applied += 1

    _final_scan(tree)
    print("solve06: %d edits, every owned read-modify-write locked" % applied)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
