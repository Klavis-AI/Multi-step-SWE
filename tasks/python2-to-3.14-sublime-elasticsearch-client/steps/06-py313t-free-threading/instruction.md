# Step 6 — Free-threaded Python 3.13: the races the GIL was hiding

## 1. Implement this milestone

`/app` holds the **sublime-elasticsearch-client** plugin as the previous step
left it: a Python 3.13 codebase whose client core carries a complete type
surface. Make it a codebase that is correct with the global interpreter lock
switched off. `python3.13t` — the free-threaded build PEP 703 added, run with
`PYTHON_GIL=0` — is the interpreter the tree must import and run under.

Nothing in this tree's source becomes invalid on that build. There is no
removed module, no changed syntax, no renamed API; every file that imports
today imports there. What changes is an assumption the code has been allowed
to make since it was written. `self.n += 1` is a read, an add and a write, and
on a GIL build the interpreter usually — not always, but usually — gets all
three done before it lets another thread run. Take the lock away and "usually"
goes with it: two threads read the same value, both add one, and the counter
advances by one instead of two. The same holds for `if x not in d: d[x] = ...`,
for `lst.remove(x)` paired with a count of what was removed, and for every
other sequence in this tree that reads shared state and then writes back a
value derived from what it read.

This step is that audit, for five attributes in three libraries, and nothing
else.

### The interpreter, for orientation

`python3.13t` is the free-threaded build, and `PYTHON_GIL=0` is already in the
environment it runs in, so `sys._is_gil_enabled()` is false before this tree
imports anything. That is the setting, not one of the requirements below: the
variable is read once at interpreter start-up and nothing a library does
afterwards moves it. Every module still has to import there, which they all
do today: the free-threaded build rejects no syntax the ordinary one takes.

### The connection selector

`RoundRobinSelector.select` in `lib/elasticsearch/connection_pool.py` advances
`self.rr` and then reduces it modulo the number of connections it was handed.
Two read-modify-writes, back to back, on one attribute shared by every thread
that asks the pool for a connection.

With one selector, a fixed list of connections and many threads each calling
`select` many times:

1. **Every call returns, and returns a member of the list it was given.**
   Nothing raises. The interleaving that costs this method its `IndexError`
   is the one where a thread's `%=` lands after another thread's `+=`, which
   leaves `self.rr` larger than the list is long and indexes past the end —
   from a method whose body contains no `raise`.
2. **The count is exact.** N threads making M selections each make exactly
   N×M selections, not fewer.
3. **The round robin is still round.** When the number of connections divides
   the number of selections, each connection is chosen exactly the same
   number of times. Skew here is the lost update: two callers that read the
   same `self.rr` send two requests to the same node and skip another.
4. **One thread sees exactly what it sees today.** Successive calls walk the
   list in order from where the last call left off, `rr` is still the
   attribute they walk it with, still starts at `-1`, and is still readable
   and writable from outside. Between calls it still holds what it holds
   today: the index of the connection that was just returned, already
   reduced — after any selection, `0 <= rr < len(connections)` — rather than
   a running total of how many selections have been made, or a value private
   to the thread that made them.

### The pool's record of what is dead

`ConnectionPool` in the same module keeps two pieces of state about which
connections are usable: the live list in `self.connections`, and
`self.dead_count`, which records how many times in a row each connection has
failed. `mark_dead` writes both; `mark_live` and `resurrect` each write one.
They are one invariant, not three: a connection is live, or it is counted
dead, and no thread should ever be able to observe it in neither or in both.

With many threads marking connections dead at once:

1. **Every removal is accounted for.** N threads making M `mark_dead` calls
   each, on connections that are all present in the live list, remove exactly
   N×M entries from it.
2. **`dead_count` sums to exactly that.** This count is not a statistic. It is
   the exponent in `dead_timeout * 2 ** min(dead_count - 1, timeout_cutoff)`,
   so every increment lost here halves the time a failing node is held out of
   rotation: a node
   that has failed five times is retried as though it had failed twice, which
   reads from outside as a flaky cluster rather than as a lost update.
3. **The early return stays.** `mark_dead` counts nothing when the connection
   was already gone from the live list — `remove` raises, the method returns,
   and no failure is recorded. That is existing behaviour and other threads
   reaching that branch is the normal case, not an error.
4. **`mark_live` and `resurrect` join the same invariant.** Deleting a
   connection's fail count and appending it back to the live list are writes
   to the same two pieces of state, and a scheme that orders `mark_dead`
   against itself but not against those two has ordered half of it.
5. **`self.connections` stays one list, mutated in place.** `mark_dead`
   removes from it and `resurrect` appends to it; the attribute is not
   rebound, replaced with a copy, or swapped for a different container. Code
   outside the class reads it, and `get_connection` takes a slice of it.
6. **`DummyConnectionPool` still works.** It inherits from `ConnectionPool`
   and deliberately does not call its `__init__`, so it has only the three
   attributes it sets itself. Its `connections` is still the one-element
   tuple it is today, and its `mark_dead`, `mark_live` and `resurrect` are
   still the same single no-op function object bound to three names. Anything
   the pool now needs in order to work has to be arranged so that this
   subclass, which builds none of it, keeps answering as it does.

### The digest `nc` counter

`HTTPDigestAuth.build_digest_header` in `lib/requests/auth.py` decides whether
the challenge nonce it was given is the one it saw last, advances
`self.nonce_count`, and formats the result into the header as `nc=%08x`. That
field exists for exactly one reason: so a server can tell two requests apart
and refuse a replayed `Authorization` header. Two live requests sharing an
`nc` is not a wrong statistic, it is the replay window the field was added to
close.

With one auth object, one challenge, and many threads building headers at
once:

1. **Every header is well formed and parses.** Nothing raises, and every
   returned header carries an `nc` of eight hexadecimal digits.
2. **No two headers share an `nc`.** N threads building M headers each produce
   N×M distinct values, not N runs of the same M values.
3. **Per-thread state is the wrong fix.** Giving each thread its own counter
   makes each of them count from one, so the twelve headers in flight at any
   moment are twelve copies of the same `nc`. It removes the lost update and
   leaves the defect the lost update was a symptom of.
4. **`nonce_count` and `last_nonce` stay plain attributes** with the values
   and types they have today: readable and writable from outside, integer and
   string, initialised to `0` and `''`. Callers set them directly.
5. **One thread sees exactly what it sees today.** A fresh nonce starts the
   count at one and the header says `nc=00000001`; the next call with the same
   nonce says `nc=00000002`; the MD5, MD5-SESS and SHA branches answer as they
   answer now; the method still returns `None` for a `qop` it does not
   implement, while an algorithm it has no hash for still fails exactly as it
   fails today, with the same exception and message; and `cnonce`, `opaque`,
   `algorithm` and the quoting of every field are untouched.
6. **`num_401_calls` is not part of this step.** `handle_401` tests it and
   then increments it, which is the same shape as the defect above, and it is
   deliberately excluded: the section that would have to be atomic spans
   `r.connection.send` — a network round trip — and the call it wraps is
   `build_digest_header`. Serialising it either holds a lock across the
   network or deadlocks against the header builder, and the challenge-response
   flow the earlier steps preserve is what notices. Leave it as it is.

### The urllib3 pool counters

`HTTPConnectionPool` in `lib/urllib3/connectionpool.py` keeps two counters:
`num_connections`, incremented in `_new_conn` — in the HTTP pool and again in
the HTTPS pool that subclasses it — and `num_requests`, incremented in
`_make_request`. They are the numbers an operator reads to decide whether a
pool is leaking sockets, and a lost update makes a leaking pool look healthy.

1. **Both counts are exact.** N threads opening M connections each leave
   `num_connections` at exactly N×M; the same holds for `num_requests` across
   `_make_request`.
2. **Both counters stay plain integer attributes** on the pool, under their
   present names, readable from outside, starting at zero.
3. **`_new_conn` still builds and returns the same connection object**, with
   the same host, port, timeout and connection keywords, and still does not
   connect it.

### Both copies, always

`requests` vendors its own copy of urllib3 under
`lib/requests/packages/urllib3/`. It is byte-identical to `lib/urllib3/` and
each copy is imported by a different half of this tree. Everything above
applies to both: two `HTTPConnectionPool` classes, two `_new_conn` methods
each, two `_make_request` methods. Fixing one copy and leaving the other is
the most likely way to half-finish this step.

### How much to serialise

The requirement is that these sequences be **atomic**, not that the library be
**serial**. A section of code that cannot be entered by two threads at once is
a section of code that cannot be run by two threads at once, and on a build
whose entire purpose is that twelve threads make progress at the same time,
turning a race into a queue is a different bug rather than a fix.

How the locks are arranged is left to you: one per object, one per attribute,
or a single module-level lock shared by all five satisfy the requirements
above equally. The coarsest of
those is the one the paragraph you have just read is asking you not to reach
for first. Two rules are worth following anyway, and the second of them is
the difference between a counter that is right and a log line that is:

* **Hold nothing across a call that can block.** A network round trip, a DNS
  lookup, a `Queue.put` on a bounded queue, a `Queue.get`, or anything that
  waits on another thread. `mark_dead` ends by putting the failed connection
  on `self.dead`, and `self.dead` is a `PriorityQueue` bounded at the number
  of connections the pool was built with. Whatever makes the removal and the
  count atomic should not still be in force by the time that `put` runs.
* **Keep the section to the statements that read and write the shared
  attribute.** Formatting a log message, computing a hash, building a
  connection object and raising an exception are not part of it -- though the
  *value* a message is about is. `_new_conn` logs the ordinal of the
  connection it is opening, and it gets that ordinal by reading back the
  counter it has just incremented; read the attribute again once the section
  has been left and two connections announce themselves as the same one. Take
  the number inside the section and format the message outside it.

### Deliberately *not* part of this milestone

* **Making the whole library thread-safe.** The five attributes named above —
  `rr`, `dead_count` (with the live list it is paired with), `nonce_count`,
  `num_connections` and `num_requests` — are the whole of this step. The
  tree has other mutable state, and a reader looking for it will find more;
  none of the rest is part of this milestone, and `num_401_calls` is excluded
  by name and for a reason. Auditing further is not asked for, and the two rules
  above say why it can cost.
* **Adding a dependency.** The standard library is what is available. There is
  no concurrency framework, no atomics package and no lock-free container to
  install, and nothing outside the standard library may be imported for this.
* **Renaming or retyping any of the five attributes.** They keep their names,
  their types and their meanings; code outside this tree reads and writes
  them, and callers set them directly.
* **Upgrading the vendored libraries.** Port them in place. Do not pull a
  newer urllib3, requests, elasticsearch-py, dateutil or analytics from an
  index, do not re-vendor one from upstream, and do not replace a vendored
  package with a dependency. Apart from the files this step and the previous
  ones remove, every file the tree has at the start is still there at the end.
* **Changing what a single thread sees.** Every contract above has a
  single-threaded clause, and everything the tree does in a single thread
  stays exactly as it is today.

**Everything else about the tree's observable behaviour stays exactly as it
is.** Return values and their types, dict ordering, the type and the exact
message text of every exception raised, what is written to `stdout`, `stderr`
and the logging handlers, the warnings issued, the HTTP requests the client
puts on the wire and what it makes of the responses that come back.

**Where CPython's free-threaded build itself answers differently, its answer
is the right one.** It reports its own build in `sys.version` and
`sysconfig.get_config_var('Py_GIL_DISABLED')`, and where a difference like
that shows through it is correct and expected. Do not add a shim or a branch
to reproduce what the GIL build says.

The previous steps' requirements still hold: nothing may reintroduce `six`, a
`__future__` import other than `annotations`, a `sys.version_info` branch, a
Python-2 builtin or iteration spelling, a `collections` ABC alias,
`fractions.gcd`, a deleted compatibility module, `imp`, `distutils`,
`asynchat`, `asyncore`, `smtpd` or any of the twenty modules 3.13 retired; no
`ssl.wrap_socket`, `match_hostname`, `CertificateError` or `PROTOCOL_SSLv*` by
any spelling, and the vendored `ssl_match_hostname` backport stays deleted; no
`datetime.utcnow()` or `utcfromtimestamp()`; the three retry sites still
report every attempt as an exception group; the six deprecated definitions
still carry `@warnings.deprecated` and nothing inside the tree still calls
them; the type surface stays complete and true, as restated below; every
module still compiles with no `SyntaxWarning` and no `DeprecationWarning` from
the compiler; and, as before, a branch parked in a comment or in a
triple-quoted string has not been removed, and a `u'…'` prefix must not
appear.

**The type surface, restated.** The *client core* is
`lib/elasticsearch_connections.py`, `lib/elasticsearch/*.py`,
`lib/elasticsearch/client/utils.py`, `lib/elasticsearch/connection/*.py` and
`lib/elasticsearch/helpers/*.py` (one directory level each, so the other
`client/` API tables are outside it). Together with the plugin's own
`ElasticsearchClient.py`, `events.py`, `commands/*.py` and `panel/*.py` it is
the repository's own surface. Across that surface every public function and
method the tree has (module or class scope, name not starting with `_`, plus
`__init__`, `__enter__`, `__exit__`, `__iter__`, `__next__` and `__call__`)
is still there and fully annotated — every parameter but a leading
`self`/`cls`, and the return of everything but `__init__`; every module there
that carries an annotation also carries `from __future__ import annotations`;
no annotation, `typing.cast(...)` target or type variable bound is or
contains `typing.Any`; no `# type: ignore`, `# noqa`, `# mypy:` or
`# pragma: no cover` comment appears and nothing is marked
`typing.no_type_check`; `typing.TYPE_CHECKING` (or a constant standing in for
it) appears only as the whole condition of an `if` with no `else` or `elif`
whose block only declares — imports, class and function definitions,
assignments — since a checker never reads the branch that constant rules out
nor what follows a block of it that leaves; a parameter's annotation
names the type of a default the source spells out; and a return annotation is
true of the body it sits on — `-> None`
over a body that returns a value, a body with `yield` annotated as anything
but an iterator, a concrete non-optional type over a body that never returns
a value (an annotation that mentions `None` or is `object`, a body whose only
exit is `raise`, and a stub body are exempt from that one), and a type that
cannot be true of a value a `return` spells out (a
literal, a list/dict/set/tuple display or comprehension, an f-string, a
comparison or `not`) are all wrong, and `-> object` over such a value counts
as wrong too: it is true of everything and so says nothing about it. A
`return` of a call or a name is not read this way. The client core passes a strict static type check
run from `lib/` as the import root, with the vendored packages and the other
`client/` API tables neither checked nor followed into.

Anything added to the typed core by this step is part of that core's type
surface and is read on the same terms as the rest of it.

There is no build step and nothing to start: the tree is imported as a library
under `python3.13t` with `PYTHON_GIL=0` in the environment, with `/app/lib`
on `sys.path` the way `ElasticsearchClient.py` puts it there, and each
library the plugin loads from it — `dateutil`, `requests`, `urllib3`,
`analytics`, `elasticsearch` and `elasticsearch_connections` — imports
without raising. A tree whose libraries do not import under `python3.13t` has
not reached this milestone.

## 2. Read these local references

**Normative semantics:** the tree in `/app` itself. There is no separate
specification to consult and none is needed — the behaviour this migration
must preserve is defined entirely by the code already in the workspace, and
the target is the public Python language, whose free-threaded build is
specified by PEP 703 and described in "What's New In Python 3.13" and in the
"Python support for free threading" HOWTO.

**Additional files:** `/app/README.md` describes what the plugin does and how
its pieces fit together; `/app/timer.sh` reports your remaining budget.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests and documents. Do not create a prose answer, a summary of what you
  would have done, or an alternate entry point in place of the edits
  themselves.
- Do not cheat by using online solutions or hints
  specific to this task, and do not search for, download, copy, wrap or invoke
  existing solutions or answers of any kind.
- Leave the workspace in a state a fresh session can pick up: the tree as it
  stands is the whole of the deliverable.
