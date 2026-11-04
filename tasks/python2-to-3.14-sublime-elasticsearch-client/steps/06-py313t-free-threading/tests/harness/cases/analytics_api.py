# -*- coding: utf-8 -*-
"""The vendored analytics-python 1.1.0 (Segment), driven without the network.

``analytics/request.py`` hardcodes ``https://api.segment.io/v1/batch``: there is
no host or base-url argument anywhere in the package, so the uploader cannot be
pointed at the loopback fixture through the library's own surface. Rather than
invent a driver the repo does not have, these cases use the two extension
points it *does* expose -- ``Client(send=False)``, which builds the whole
message pipeline but never starts the consumer thread, and subclassing
``Consumer`` to override ``request``, which is the documented way to change how
a batch is delivered. Everything below the uploader (message construction,
timezone guessing, cleaning, the queue) is driven end to end.

Three separate rungs of the ladder land in this package:

* ``Client._enqueue`` calls ``self.log.warn``. The alias has been deprecated
  since 3.3 and is still present at 3.14.7 (measured: it exists on every
  interpreter in the ladder and forwards to ``warning``), so the queue-full
  path keeps working and the log record is identical either way. What the
  alias costs is a ``DeprecationWarning`` raised at its caller's frame, which
  is inside the tree -- and that is what ``analytics.queue-full-logging-alias``
  in ``deprecations.py`` observes. This case observes the branch's contract,
  which the rename must not disturb.
* ``Client._enqueue`` and ``request.post`` call ``datetime.utcnow()``, which is
  deprecated from 3.12.
* ``Consumer.upload`` ends with ``return success`` inside a ``finally`` block,
  which is a ``SyntaxWarning`` under PEP 765 at 3.14 -- and silently swallows
  any exception raised by the cleanup loop above it.
"""
import datetime

from ..registry import case

TS = datetime.datetime(2015, 6, 1, 12, 30, 45)


def _client(**kw):
    from analytics.client import Client
    kw.setdefault("write_key", "test-key")
    kw.setdefault("send", False)
    return Client(**kw)


# ------------------------------------------------------------- utils ----

@case("analytics.is-naive-and-guess-timezone")
def _(ctx):
    from analytics.utils import guess_timezone, is_naive
    from dateutil.tz import tzoffset, tzutc
    old = datetime.datetime(2001, 1, 1, 0, 0, 0)
    aware = TS.replace(tzinfo=tzoffset("JST", 9 * 3600))
    return {
        "naive": [is_naive(TS), is_naive(aware),
                  is_naive(TS.replace(tzinfo=tzutc()))],
        # A datetime far in the past is more than five seconds from now on any
        # clock, so guess_timezone must choose UTC; the branch that chooses the
        # local zone is covered by the case below.
        "old_guessed": guess_timezone(old).isoformat(),
        "aware_unchanged": guess_timezone(aware).isoformat(),
    }


@case("analytics.is-naive-tzinfo-without-offset", tags=("adversarial",))
def _(ctx):
    """A ``tzinfo`` attached but declining to say what the offset is.

    ``datetime`` permits ``utcoffset()`` to return ``None``, meaning "attached
    but not actually locating this moment", and ``is_naive`` says so with
    ``dt.tzinfo is None or dt.tzinfo.utcoffset(dt) is None``. Dropping the
    second half is the obvious simplification while converting this package's
    naive ``utcnow`` handling, and nothing else in the suite reaches it:
    deleting that clause leaves every other case passing.
    This is the input that makes the clause observable, through
    ``guess_timezone`` as well, which is what the package actually calls.
    """
    from analytics.utils import guess_timezone, is_naive

    class Undecided(datetime.tzinfo):
        def utcoffset(self, dt):
            return None

        def dst(self, dt):
            return None

        def tzname(self, dt):
            return "UNDECIDED"

    attached = TS.replace(tzinfo=Undecided())
    guessed = guess_timezone(attached)
    return {"is_naive": is_naive(attached),
            "had_tzinfo": attached.tzinfo is not None,
            "utcoffset_is_none": attached.utcoffset() is None,
            # guess_timezone treats it as naive and replaces the tzinfo, so the
            # offset it lands on is the observable consequence.
            "guessed_offset": guessed.utcoffset(),
            "guessed_tzname": guessed.tzname()}


@case("analytics.guess-timezone-now-is-local", tags=("adversarial",))
def _(ctx):
    # The five-second window in guess_timezone is the one piece of this package
    # that reads the wall clock to make a decision. Only the *decision* is
    # observed -- the timestamp itself would differ every run.
    from analytics.utils import guess_timezone
    from dateutil.tz import tzlocal, tzutc
    now = guess_timezone(datetime.datetime.now())
    return {"is_local": isinstance(now.tzinfo, tzlocal),
            "is_utc": isinstance(now.tzinfo, tzutc),
            "offset": now.utcoffset()}


@case("analytics.total-seconds")
def _(ctx):
    from analytics.utils import total_seconds
    return [total_seconds(datetime.timedelta(**kw)) for kw in
            ({"seconds": 1}, {"microseconds": 1}, {"days": 1},
             {"days": -1}, {"seconds": -1, "microseconds": -500000},
             {"days": 1, "seconds": 2, "microseconds": 3})]


@case("analytics.clean")
def _(ctx):
    from analytics.utils import clean
    return clean({
        "str": u"日本語",
        "bytes": u"日本語".encode("utf-8"),
        "int": 1, "float": 1.5, "bool": True, "none": None,
        "dt": TS,
        "list": [1, u"ü", None],
        "tuple": (1, 2),
        "set": {1},
        "nested": {"a": {"b": [1, {"c": u"深"}]}},
    })


@case("analytics.clean-unserialisable", tags=("adversarial",))
def _(ctx):
    # _coerce_unicode calls .decode on whatever is left, and its AttributeError
    # handler does ":".join(exception) -- joining an *exception object*, which
    # itself raises on Python 3. Pinning the current behaviour keeps a
    # migration from quietly changing which error escapes.
    from analytics.utils import clean
    out = []
    for value in (object(), 1 + 2j, lambda: None, b"\xff\xfe"):
        try:
            out.append(repr(clean(value)))
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


# ------------------------------------------------------------ client ----

@case("analytics.client-requires-string-write-key", tags=("adversarial",))
def _(ctx):
    from analytics.client import Client
    out = []
    for key in (None, 1, [], u"鍵"):
        try:
            Client(key, send=False)
            out.append("accepted:%r" % (key,))
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


@case("analytics.track-message")
def _(ctx):
    c = _client()
    ok, msg = c.track(user_id="u1", event=u"検索", timestamp=TS,
                      properties={"query": u"日本語", "n": 3})
    return {"ok": ok, "msg": msg, "qsize": c.queue.qsize()}


@case("analytics.identify-group-alias-page-screen")
def _(ctx):
    c = _client()
    out = []
    out.append(c.identify(user_id="u1", traits={"name": u"名前"},
                          timestamp=TS)[1])
    out.append(c.group(user_id="u1", group_id="g1", traits={"a": 1},
                       timestamp=TS)[1])
    out.append(c.alias(previous_id="p1", user_id="u1", timestamp=TS)[1])
    out.append(c.page(user_id="u1", name=u"ページ", category="c",
                      properties={"url": u"http://例.jp"}, timestamp=TS)[1])
    out.append(c.screen(anonymous_id="a1", name=u"画面", timestamp=TS)[1])
    return {"messages": out, "qsize": c.queue.qsize()}


@case("analytics.enqueue-fills-defaults")
def _(ctx):
    # No timestamp: _enqueue reaches for datetime.utcnow(). The value is
    # normalised away, but the *shape* it produces -- an aware ISO string with
    # a +00:00 offset and a uuid4 messageId -- is the contract.
    c = _client()
    ok, msg = c.track(user_id="u1", event="e")
    return {"ok": ok, "msg": msg,
            "ts_is_aware": msg["timestamp"].endswith("+00:00")}


@case("analytics.message-validation-rejects", tags=("adversarial",))
def _(ctx):
    c = _client()
    out = []
    for call, kw in (("track", {"user_id": None, "event": "e"}),
                     ("track", {"user_id": "u", "event": None}),
                     ("track", {"user_id": "u", "event": "e",
                                "properties": "notadict"}),
                     ("identify", {"user_id": "u", "traits": "notadict"}),
                     ("alias", {"previous_id": None, "user_id": "u"}),
                     ("group", {"user_id": "u", "group_id": None}),
                     ("page", {"user_id": "u", "name": 1}),
                     ("track", {"user_id": "u", "event": "e",
                                "timestamp": "not-a-datetime"}),
                     ("track", {"user_id": "u", "event": "e",
                                "integrations": "nope"})):
        try:
            getattr(c, call)(**kw)
            out.append("accepted:%s" % call)
        except Exception as e:
            out.append("%s: %s" % (type(e).__name__, e))
    return out


@case("analytics.numeric-user-id-allowed")
def _(ctx):
    c = _client()
    return [c.track(user_id=42, event="e", timestamp=TS)[1]["userId"],
            c.track(user_id=4.5, event="e", timestamp=TS)[1]["userId"]]


@case("analytics.queue-full-warns", tags=("adversarial",))
def _(ctx):
    # _enqueue's full-queue branch calls self.log.warn, the alias deprecated
    # since 3.3. The return value is the contract; the log record the runner
    # captures is what proves the branch was taken. Both survive the rename to
    # `warning` unchanged -- the alias forwards -- so this case pins what must
    # NOT move when rung 04 renames it, and the warning the alias raises is
    # scored next door in `analytics.queue-full-logging-alias`.
    c = _client(max_queue_size=1)
    first = c.track(user_id="u", event="one", timestamp=TS)[0]
    second = c.track(user_id="u", event="two", timestamp=TS)
    return {"first": first, "second_ok": second[0],
            "second_event": second[1]["event"], "qsize": c.queue.qsize()}


@case("analytics.send-false-does-not-start-consumer")
def _(ctx):
    c = _client()
    return {"is_alive": c.consumer.is_alive(), "daemon": c.consumer.daemon,
            "send": c.send, "retries": c.consumer.retries,
            "upload_size": c.consumer.upload_size}


@case("analytics.debug-sets-log-level")
def _(ctx):
    import logging
    c = _client(debug=True)
    return {"level": logging.getLogger("segment").level,
            "is_debug": logging.getLogger("segment").level == logging.DEBUG,
            "client_debug": c.debug}


# ---------------------------------------------------------- consumer ----

def _recording_consumer():
    """A Consumer that records batches instead of posting them.

    ``request`` is the method analytics-python documents as the delivery hook;
    overriding it is a supported extension, not a harness-only backdoor.
    """
    from analytics.consumer import Consumer

    class Recording(Consumer):
        def __init__(self, *a, **kw):
            self.fail = kw.pop("fail", False)
            Consumer.__init__(self, *a, **kw)
            self.sent = []

        def request(self, batch, attempt=0):
            self.sent.append(batch)
            if self.fail:
                raise RuntimeError("upload refused")
    return Recording


@case("analytics.consumer-uploads-batch")
def _(ctx):
    try:
        import queue
    except ImportError:  # pragma: no cover - py2 spelling, kept for parity
        import Queue as queue
    c = _recording_consumer()(queue.Queue(), "k", upload_size=2)
    for i in range(3):
        c.queue.put({"i": i})
    return {"first": c.upload(), "second": c.upload(),
            "sent": c.sent, "unfinished": c.queue.unfinished_tasks}


@case("analytics.consumer-empty-queue-returns-false")
def _(ctx):
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    c = _recording_consumer()(queue.Queue(), "k")
    # next_item blocks for five seconds on an empty queue by design; one
    # sentinel item makes the batch non-empty and keeps the case bounded.
    c.queue.put({"i": 0})
    first = c.upload()
    # Driven through upload() rather than through the batch-builder it calls:
    # that builder is spelled ``next``, one of the py2 names the first rung
    # retires, and an inherited case naming it directly would be unpassable
    # from there on. upload() reaches the same empty-queue path and
    # reports it as False.
    return {"first": first, "sent": c.sent, "upload_on_empty": c.upload()}


@case("analytics.consumer-on-error-callback", tags=("adversarial",))
def _(ctx):
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    seen = []
    c = _recording_consumer()(queue.Queue(), "k",
                              on_error=lambda e, b: seen.append(
                                  (type(e).__name__, str(e), len(b))),
                              fail=True)
    c.queue.put({"i": 0})
    return {"upload": c.upload(), "on_error": seen,
            "unfinished": c.queue.unfinished_tasks}


@case("analytics.consumer-finally-return-swallows", tags=("adversarial",))
def _(ctx):
    # ``Consumer.upload`` ends with ``return success`` inside ``finally`` --
    # the construct PEP 765 turns into a SyntaxWarning at 3.14. Two probes,
    # because the two halves of the construct behave differently and only one
    # of them is what PEP 765 is about:
    #
    # ``swallowed``  is the PEP 765 observation. ``except Exception`` does not
    #   catch a BaseException, so one raised by ``request()`` is in flight when
    #   the ``finally`` runs -- and the ``return`` there discards it. The call
    #   reports a plain False and the caller never learns the upload died. The
    #   natural rewrite (move the ``return`` past the ``finally``) makes it
    #   propagate, so this probe is a *deliberate* flip at the rung that
    #   removes the construct, and the step states which of the two it wants.
    #
    # ``propagated`` is the half that must **not** move: an exception raised by
    #   the ``finally`` body itself replaces whatever was in flight and reaches
    #   the caller, ``return`` or no ``return``. Pinned so that "fixing" PEP 765
    #   cannot quietly start swallowing the cleanup's own failures instead.
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue

    class Abandoned(BaseException):
        pass

    class Refusing(_recording_consumer()):
        def request(self, batch, attempt=0):
            self.sent.append(batch)
            raise Abandoned("worker abandoned")

    class BrokenQueue(queue.Queue):
        def task_done(self):
            raise RuntimeError("task_done exploded")

    out = {}
    c = Refusing(queue.Queue(), "k")
    c.queue.put({"i": 0})
    try:
        out["swallowed"] = {"upload": c.upload(), "escaped": None}
    except BaseException as e:
        out["swallowed"] = {"upload": None,
                            "escaped": "%s: %s" % (type(e).__name__, e)}
    out["swallowed"]["sent"] = c.sent
    # The queue still has to be told: a swallowed failure that also left
    # ``unfinished_tasks`` high would hang a join() forever, and that is a
    # different defect from the one above.
    out["swallowed"]["unfinished"] = c.queue.unfinished_tasks

    c = _recording_consumer()(BrokenQueue(), "k")
    c.queue.put({"i": 0})
    try:
        out["propagated"] = {"upload": c.upload(), "escaped": None}
    except BaseException as e:
        out["propagated"] = {"upload": None,
                             "escaped": "%s: %s" % (type(e).__name__, e)}
    out["propagated"]["sent"] = c.sent
    return out


@case("analytics.consumer-pause-stops-run")
def _(ctx):
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    c = _recording_consumer()(queue.Queue(), "k")
    c.queue.put({"i": 0})
    c.upload()
    c.pause()
    return {"running": c.running, "sent": c.sent}


@case("analytics.consumer-retries-then-raises", tags=("adversarial",))
def _(ctx):
    # Consumer.request recurses up to self.retries times before giving up.
    # The recursion depth is the observation; the network is never touched.
    from analytics.consumer import Consumer
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue

    attempts = []

    class Counting(Consumer):
        def request(self, batch, attempt=0):
            attempts.append(attempt)
            try:
                raise RuntimeError("always fails")
            except Exception:
                if attempt > self.retries:
                    raise
                self.request(batch, attempt + 1)

    c = Counting(queue.Queue(), "k")
    c.queue.put({"i": 0})
    result = c.upload()
    return {"upload": result, "attempts": attempts}


def _offline_session(fail_first=None,
                     reason="stub session: the network is not reachable",
                     exc=None):
    """Stand in for ``analytics.request._session``.

    ``request.post`` hardcodes ``https://api.segment.io/v1/batch`` and takes
    its session from a module global, so replacing that global is the only way
    to reach ``Consumer.request``'s own retry loop without a network. The
    cases above override ``request`` itself instead and therefore never run
    that loop at all -- they pin what ``upload`` does with a failure, not what
    the loop did before handing one over.

    ``fail_first`` bounds how many posts are refused; later ones answer 200 the
    way the endpoint would. None refuses every post. ``exc`` is the class a
    refused post raises, defaulting to the ConnectionError a real offline
    session raises.
    """
    import requests

    class Answered(object):
        status_code = 200

    class Offline(object):
        def __init__(self):
            self.posts = []

        def post(self, url, **kw):
            self.posts.append(url)
            if fail_first is None or len(self.posts) <= fail_first:
                raise (exc or requests.exceptions.ConnectionError)(reason)
            return Answered()

    import analytics.request
    session = Offline()
    analytics.request._session = session
    return session


@case("analytics.consumer-real-retry-aggregates", tags=("adversarial",))
def _(ctx):
    # The tree's own retry loop, driven end to end: four retries on top of the
    # first attempt is five posts, and the loop reports one failure for them.
    # Returning the exception rather than letting it escape keeps the attempt
    # count in the same observation as whatever the loop kept.
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    from analytics.consumer import Consumer

    session = _offline_session()
    c = Consumer(queue.Queue(), "k")
    try:
        c.request([{"i": 0}])
    except BaseException as e:
        return {"attempts": len(session.posts), "retries": c.retries,
                "raised": e}
    return {"attempts": len(session.posts), "raised": None}


@case("analytics.consumer-real-retry-zero-retries", tags=("adversarial",))
def _(ctx):
    # retries down to 0. The loop still makes the one retry its comparison
    # allows -- `if attempt > self.retries` is only true from the second
    # attempt on -- so this is two posts, and what the loop reports has to
    # follow the attempts it actually made rather than a fixed number.
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    from analytics.consumer import Consumer

    session = _offline_session()
    c = Consumer(queue.Queue(), "k")
    c.retries = 0
    try:
        c.request([{"i": 0}])
    except BaseException as e:
        return {"attempts": len(session.posts), "retries": c.retries,
                "raised": e}
    return {"attempts": len(session.posts), "raised": None}


@case("analytics.consumer-real-retry-recovers", tags=("adversarial",))
def _(ctx):
    # The contrast: two refusals and then an answer. The loop returns normally,
    # upload() reports True and nothing is raised -- so a tree that collected
    # the failures and reported them regardless of how the loop ended would
    # fail here and nowhere else.
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    from analytics.consumer import Consumer

    session = _offline_session(fail_first=2)
    seen = []
    c = Consumer(queue.Queue(), "k",
                 on_error=lambda e, b: seen.append((e, len(b))))
    c.queue.put({"i": 0})
    return {"upload": c.upload(), "attempts": len(session.posts),
            "on_error": seen, "unfinished": c.queue.unfinished_tasks}


@case("analytics.consumer-real-retry-base-exception", tags=("adversarial",))
def _(ctx):
    # The loop is written with a bare ``except:``, so it catches what does not
    # derive from Exception as well -- a worker being torn down retries and is
    # then re-raised like anything else. Which except clause catches what comes
    # out is recorded beside it: an upload that failed and a worker that was
    # interrupted must not become the same thing to a caller.
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    from analytics.consumer import Consumer

    session = _offline_session(exc=KeyboardInterrupt)
    c = Consumer(queue.Queue(), "k")
    try:
        c.request([{"i": 0}])
    except Exception as e:
        return {"attempts": len(session.posts), "caught_as": "Exception",
                "raised": e}
    except BaseException as e:
        return {"attempts": len(session.posts), "caught_as": "BaseException",
                "raised": e}
    return {"attempts": len(session.posts), "caught_as": None, "raised": None}


@case("analytics.consumer-real-retry-on-error", tags=("adversarial",))
def _(ctx):
    # Same loop, reached the way the consumer thread reaches it. upload() has
    # to keep absorbing the loop's failure: False to the caller, one on_error
    # callback, the queue told, and an 'error uploading' record carrying the
    # failure's str -- which the runner captures whether or not it changes.
    try:
        import queue
    except ImportError:  # pragma: no cover
        import Queue as queue
    from analytics.consumer import Consumer

    session = _offline_session()
    seen = []
    c = Consumer(queue.Queue(), "k",
                 on_error=lambda e, b: seen.append((e, len(b))))
    c.queue.put({"i": 0})
    result = c.upload()
    return {"upload": result, "attempts": len(session.posts),
            "on_error": seen, "unfinished": c.queue.unfinished_tasks}


# ----------------------------------------------------------- request ----

@case("analytics.datetime-serializer")
def _(ctx):
    import json
    from analytics.request import DatetimeSerializer
    from dateutil.tz import tzutc
    body = {"t": TS, "aware": TS.replace(tzinfo=tzutc()), "s": u"日本語",
            "n": 1}
    return {"dumped": json.dumps(body, cls=DatetimeSerializer,
                                 sort_keys=True),
            "unserialisable": _dump_fails(json, DatetimeSerializer)}


def _dump_fails(json, cls):
    try:
        return json.dumps({"o": object()}, cls=cls)
    except Exception as e:
        return "%s: %s" % (type(e).__name__, e)


@case("analytics.api-error")
def _(ctx):
    from analytics.request import APIError
    e = APIError(400, "bad_request", u"不正なリクエスト")
    return {"str": str(e), "status": e.status, "code": e.code,
            "message": e.message,
            "mro": [c.__name__ for c in type(e).__mro__]}


@case("analytics.module-proxy-uses-default-client")
def _(ctx):
    # The module-level façade lazily builds a Client from module globals. It is
    # the only entry point ElasticsearchClient.py itself calls.
    import analytics
    analytics.write_key = "module-key"
    analytics.send = False
    analytics.track("u1", u"イベント", {"a": 1}, timestamp=TS)
    c = analytics.default_client
    return {"version": analytics.__version__,
            "write_key": c.write_key, "send": c.send,
            "qsize": c.queue.qsize(),
            "queued_type": c.queue.get()["type"]}


@case("analytics.module-proxy-requires-write-key", tags=("adversarial",))
def _(ctx):
    import analytics
    analytics.write_key = None
    analytics.default_client = None
    try:
        analytics.track("u1", "e", timestamp=TS)
        return {"raised": None}
    except Exception as e:
        return {"raised": type(e).__name__, "str": str(e)}
