# Step 2 — Python 3.11: report every attempt, not just the last

## 1. Implement this milestone

`/app` holds the **sublime-elasticsearch-client** plugin as the previous step
left it: a Python 3.10 codebase with the Python 2 compatibility layer gone.
Make it a Python 3.11 codebase. `python3.11` is the interpreter the tree must
import and run under.

Three places in the tree try the same operation more than once, and all three
throw away everything but the last failure:

* **the Elasticsearch transport's request retry loop** —
  `elasticsearch/transport.py`, `Transport.perform_request`. It catches a
  failure per attempt, decides whether to retry, and on the last attempt
  re-raises whatever it happens to be holding. The earlier attempts' failures
  are gone by then, and a caller that was told "connection refused" has no way
  to learn that the two attempts before it timed out instead.
* **host sniffing** — `elasticsearch/transport.py`, `Transport.sniff_hosts`.
  The loop over the connections swallows each failure with a bare `pass` and
  then reports a single `Unable to sniff hosts.` that says nothing about any of
  them.
* **the analytics consumer's upload retry** — `analytics/consumer.py`,
  `Consumer.request`. Same shape, by recursion rather than by a loop.

Make each of the three report **all** the failures it collected, together, as
an exception group — the `ExceptionGroup` PEP 654 added in 3.11, or the
`BaseExceptionGroup` it derives from where that is what the site needs (see
below). Specifically:

1. **One member per attempt, in attempt order.** The members are the exceptions
   those attempts raised, unchanged: same types, same messages, same `args`,
   same `__cause__`. A group carrying the last failure repeated once per
   attempt, or a group holding a summary of what happened, is not this.
2. **Always a group, even of one.** A site that made a single attempt — a
   request with `max_retries=0` — still reports a group, with one member in
   it. A caller of the retry path must not have to handle two shapes
   depending on how many attempts there were; `asyncio.TaskGroup` wraps a lone
   failure for the same reason. How many attempts a site makes is a property
   of the loop as it already stands, so read the loop rather than assume:
   sniffing, for one, walks the connection pool and then the seed connections,
   which is already more than one attempt for a client built with a single
   host.

   The group belongs to the retry path and only to it. `perform_request`
   decides, failure by failure, whether that failure is one to try again; the
   group is what it reports when the answer was yes and it then ran out of
   attempts — including when it was allowed exactly one. When the answer is
   no, nothing was collected and the loop never ran a second time: a status
   the client is not configured to retry, a timeout with `retry_on_timeout`
   off, a malformed query, a missing document. Those leave the function
   exactly as they do today, on their own and unwrapped: wherever a 404
   raises `NotFoundError` now it still raises `NotFoundError`, and a 400 still
   raises `RequestError`. "Even of one" is about a retry that got one attempt,
   not about every exception that leaves the function.
3. **The message does not move.** Each site already reports some text and it is
   the text the group carries: for the two retry loops, the `str()` of the last
   attempt's failure, which is exactly the exception they re-raise today; for
   sniffing, `Unable to sniff hosts.`
4. **A loop that succeeds still raises nothing.** Collecting failures is not
   reporting them. A request that fails twice and then answers, a sniff where
   one connection out of two responds, an upload that is refused and then
   accepted — all of those return normally, exactly as they do now, and nothing
   observes that anything went wrong.
5. **The attempts themselves do not change.** Each site makes the same number
   of tries against the same hosts in the same order, marks connections dead
   when it marked them dead before, logs what it logged before, and puts the
   same requests on the wire.

**What the callers see.** Two of these failures are caught a level up, and
those call sites must keep behaving exactly as they behave today:

* `Elasticsearch.ping()` answers `False` when the request fails. It must still
  answer `False`, not propagate.
* `elasticsearch.helpers.streaming_bulk` (and `bulk` on top of it) reports a
  failed chunk's error information per action and honours `raise_on_exception`
  and `raise_on_error`. All of that is unchanged, field for field: the per
  action dict still carries the same `error` message text, the same `status`,
  and under `exception` the same single failure object it files there today —
  the one the chunk ended on, not the group — and the same decision follows
  about what to raise. What does change is the raise itself: when the helper
  propagates instead of absorbing, what comes out is the whole group rather
  than one exception picked out of it.
* `analytics.Consumer.upload()` absorbs a failed upload and needs no change of
  its own to go on doing it: it still returns `False`, still calls the
  `on_error` callback exactly once with the failure and the batch, still
  writes its `error uploading: …` log record, and still tells the queue the
  task is done. What is different is what it is handed. The failure it catches
  is now the group, so the group is what reaches `on_error`, and the log
  record carries the group's own `str()` — the message it already reported
  plus CPython's `(N sub-exceptions)` suffix. Both of those follow from the
  group and are correct: nothing here is unwrapped to reproduce the 3.10
  wording.

**One of the three sites needs the wider group.** `Consumer.request` catches
its failures with a bare `except:`, so it retries — and then re-raises — things
that do not derive from `Exception`, such as a `KeyboardInterrupt` in a worker
being shut down. That distinction is observable and must survive: what the site
raises when its members are ordinary exceptions has to stay catchable by
`except Exception` (which is how `upload()` absorbs a failed upload), and what
it raises when a member is not an ordinary exception must **not** be — an
interrupted worker still travels past `upload()`'s handler exactly as it does
today. The other two sites only ever hold `TransportError`s and do not have
this problem.

**Four things that are deliberately *not* part of this milestone:**

* The vendored `urllib3`'s own retry machinery — `urllib3/util/retry.py`,
  `MaxRetryError`, and the mirrored copy under `requests/packages/` — does not
  aggregate anything today and must not start. It re-raises the last failure,
  and the elasticsearch client reaches it from the transport loop above,
  which is where the aggregation belongs. Leave it alone.
* `sniff_hosts` has a second `raise`, for the case where the connections
  answered but the node list yielded no usable host. Nothing failed there and
  there is nothing to collect: it stays the single `TransportError` it is
  today, with its present message.
* `perform_request` likewise has a second `raise`, the one it reaches when it
  decides a failure is not worth retrying. One attempt was made and nothing
  was collected: it stays the single exception it is today, with its present
  type and its present message.
* Every other `try`/`except` in the tree. This step is the three sites named
  above and nothing else, and no other modernisation the code might invite is
  part of it.

**Everything else about the tree's observable behaviour stays exactly as it
is.** Return values and their types, dict ordering, the type and the exact
message text of every other exception raised, what is written to `stdout`,
`stderr` and the logging handlers, the warnings issued, the HTTP requests the
client puts on the wire and what it makes of the responses that come back.

**Where CPython 3.11 itself changed an answer, the 3.11 answer is the right
one.** The interpreter reworded some of its own error messages in 3.11 and
changed the iteration order of a standard-library container or two; where a
difference like that shows through, it is correct and expected. Do not add a
shim, a sort, or a compatibility branch to reproduce what 3.10 used to say.

The previous step's requirements still hold: nothing may reintroduce `six`, a
`__future__` import, a `sys.version_info` branch, a Python-2 builtin or
iteration spelling, a `collections` ABC alias, `fractions.gcd`, or a deleted
compatibility module — and, as before, a branch parked in a comment or in a
triple-quoted string has not been removed, and a `u'…'` prefix must not appear.
Port the vendored libraries in place — do not upgrade
them, re-vendor them from upstream, or replace one with a dependency installed
from an index. Apart from the files the previous step removed, every file the
tree has at the start is still there at the end.

There is no build step and nothing to start: the tree is imported as a library
under `python3.11`, with `/app/lib` on `sys.path` the way `ElasticsearchClient.py`
puts it there, and each library the plugin loads from it — `dateutil`,
`requests`, `urllib3`, `analytics`, `elasticsearch` and
`elasticsearch_connections` — imports without raising. A tree whose
libraries do not import under `python3.11` has not reached this milestone.

## 2. Read these local references

**Normative semantics:** the tree in `/app` itself. There is no separate
specification to consult and none is needed — the behaviour this migration must
preserve is defined entirely by the code already in the workspace, and the
target is the public Python language, whose exception-group semantics are
specified by PEP 654 and documented in "What's New In Python 3.11".

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
